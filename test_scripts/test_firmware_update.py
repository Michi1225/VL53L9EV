"""Host tests for the actual portable C updater and its wire protocol."""
import hashlib
import importlib.util
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('firmware_update_client', ROOT / 'scripts/firmware_update.py')
client = importlib.util.module_from_spec(spec)
spec.loader.exec_module(client)

HARNESS = r'''
#include "fw_update.h"
#include "fw_wire.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint8_t flash[FW_META_B+FW_ERASE_SIZE];
static uint8_t image_a[2048],image_b[2048];
static int write_budget=-1,erase_budget=-1;
static unsigned reads,writes,erases;
/** @brief Encode a little-endian word in the update fixture. */
static void put(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(n>>(8*i));}
/** @brief Decode a little-endian word from the update fixture. */
static uint32_t get(const uint8_t *p){return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
/** @brief Read a checked range from the simulated NOR flash. */
static int read_flash(void *ctx,uint32_t a,void *p,uint32_t n)
{(void)ctx;assert(a<=sizeof(flash)&&n<=sizeof(flash)-a);++reads;memcpy(p,flash+a,n);return 0;}
/** @brief Check that the simulated write fits an allowed image or metadata range. */
static bool allowed(uint32_t a,uint32_t n){
 const uint32_t b[]={FW_SLOT_A,FW_SLOT_B,FW_META_A,FW_META_B};
 const uint32_t s[]={FW_MAX_IMAGE,FW_MAX_IMAGE,FW_RECORD_SIZE,FW_RECORD_SIZE};
 for(unsigned i=0;i<4;++i)if(a>=b[i]&&a-b[i]<s[i]&&n<=s[i]-(a-b[i]))return true;
 return false;
}
/** @brief Emulate NOR programming with bounds checks and injected partial writes. */
static int write_flash(void *ctx,uint32_t a,const void *p,uint32_t n){
 (void)ctx;assert(allowed(a,n));++writes;
 const uint8_t *data=p;
 for(uint32_t i=0;i<n;++i){
  if(write_budget==0)return -1;
  assert((flash[a+i]&data[i])==data[i]);flash[a+i]&=data[i];
  if(write_budget>0)--write_budget;
 }
 return 0;
}
/** @brief Emulate a sector erase with optional partial-erase failure. */
static int erase_flash(void *ctx,uint32_t a){
 (void)ctx;assert(a%FW_ERASE_SIZE==0 && allowed(a,1));++erases;
 if(erase_budget>=0){memset(flash+a,255,(size_t)erase_budget);return -1;}
 memset(flash+a,255,FW_ERASE_SIZE);return 0;
}
static fw_io_t io={NULL,read_flash,write_flash,erase_flash};
/** @brief Build a signed image fixture with valid vectors and payload checksum. */
static void fixture(uint8_t *p,uint8_t version){
 memset(p,version,2048);put(p,0x324d5453);put(p+0x68,0x20300);put(p+0x6c,2048-0x240);
 put(p+0x70,FW_RAM_BASE+0x409);put(p+0x400,0x34200000);put(p+0x404,FW_RAM_BASE+0x409);
 uint32_t sum=0;for(unsigned i=0x240;i<2048;++i)sum+=p[i];put(p+0x64,sum);
}
/** @brief Initialize erased flash with slot A and adopt its boot metadata. */
static fw_update_t initial(void){
 memset(flash,255,sizeof(flash));fixture(image_a,1);fixture(image_b,2);
 memcpy(flash+FW_SLOT_A,image_a,sizeof(image_a));write_budget=erase_budget=-1;
 fw_update_t f;assert(fw_open(&f,&io)==FW_OK);assert(f.record==-1);
 uint32_t slot;bool trial;assert(fw_boot_select(&f,&slot,&trial)==FW_OK);assert(slot==0&&!trial);
 return f;
}
/** @brief Begin an update and deliver all fixture chunks with valid CRCs. */
static void send_image(fw_update_t *f,const uint8_t *p){
 uint8_t hash[32];fw_sha256(p,2048,hash);assert(fw_begin(f,2048,hash,42)==FW_OK);
 for(uint32_t off=0;off<2048;off+=512)assert(fw_data(f,off,p+off,512,fw_crc32(p+off,512))==FW_OK);
}
/** @brief Reload update state from the simulated persistent metadata. */
static fw_update_t reopened(void){fw_update_t f;assert(fw_open(&f,&io)==FW_OK);return f;}
/** @brief Assert that boot selection returns the expected slot and trial state. */
static void boot_is(fw_update_t *f,uint32_t expected,bool expected_trial){
 uint32_t slot;bool trial;assert(fw_boot_select(f,&slot,&trial)==FW_OK);assert(slot==expected&&trial==expected_trial);
}
/** @brief Check SHA-256 and CRC-32 against known reference values. */
static void crypto(void){
 uint8_t digest[32];fw_sha256("abc",3,digest);
 const uint8_t expected[]={0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
 0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
 assert(!memcmp(digest,expected,32));assert(fw_crc32("123456789",9)==0xcbf43926);
 fw_sha256("",0,digest);assert(digest[0]==0xe3&&digest[31]==0x55);
}
/** @brief Exercise uploads, trial boots, and confirmation in both slot directions. */
static void lifecycle(void){
 fw_update_t f=initial();send_image(&f,image_b);assert(fw_finish(&f)==FW_OK);
 assert(f.meta.pending==1 && (f.meta.flags&(FW_UPLOADED|FW_VERIFIED))==(FW_UPLOADED|FW_VERIFIED));
 assert(!memcmp(flash+FW_SLOT_A,image_a,2048));
 f=reopened();boot_is(&f,1,true);assert(f.meta.confirmed==0&&f.meta.trial==1);
 assert(fw_confirm(&f,0)==FW_NOT_READY);assert(fw_confirm(&f,1)==FW_OK);
 f=reopened();boot_is(&f,1,false);assert(f.meta.pending==FW_NONE&&(f.meta.flags&FW_CONFIRMED));
 assert(f.meta.image[1].version==42);
 send_image(&f,image_a);assert(fw_finish(&f)==FW_OK);f=reopened();boot_is(&f,0,true);
 assert(fw_confirm(&f,0)==FW_OK);f=reopened();boot_is(&f,0,false);
 puts("A/B upload, verification, trial and confirmation");
}
/** @brief Verify fallback after failed trials and corrupted pending or confirmed images. */
static void rollback(void){
 fw_update_t f=initial();send_image(&f,image_b);assert(fw_finish(&f)==FW_OK);f=reopened();boot_is(&f,1,true);
 /* No confirmation: watchdog reset, hard fault, or power cycle during trial. */
 f=reopened();boot_is(&f,0,false);assert(f.meta.flags&FW_FALLBACK);assert(f.meta.last_error==FW_TRIAL_FAILED);
 assert(!f.meta.image[1].size&&f.meta.pending==FW_NONE);
 assert(fw_clear_warnings(&f)==FW_OK);assert(!(f.meta.flags&FW_WARNINGS));
 send_image(&f,image_b);assert(fw_finish(&f)==FW_OK);flash[FW_SLOT_B+1600]^=1;
 f=reopened();boot_is(&f,0,false);assert(f.meta.flags&FW_FALLBACK);
 /* Confirm B, then damage it: the intact previous A remains bootable. */
 send_image(&f,image_b);assert(fw_finish(&f)==FW_OK);f=reopened();boot_is(&f,1,true);assert(fw_confirm(&f,1)==FW_OK);
 flash[FW_SLOT_B+1600]^=1;f=reopened();boot_is(&f,0,false);assert(f.meta.flags&FW_FALLBACK);
 puts("Unconfirmed, pending-corrupt and confirmed-corrupt image fallback");
}
/** @brief Verify rejection of invalid sizes, offsets, hashes, vectors, and incomplete uploads. */
static void bad_uploads(void){
 fw_update_t f=initial();uint8_t hash[32];fw_sha256(image_b,2048,hash);
 assert(fw_begin(&f,UINT32_MAX,hash,0)==FW_BAD_REQUEST);assert(fw_begin(&f,FW_MAX_IMAGE+4,hash,0)==FW_BAD_REQUEST);
 assert(fw_begin(&f,1024,hash,0)==FW_BAD_REQUEST);assert(fw_begin(&f,2048,hash,0)==FW_OK);
 unsigned n=writes;assert(fw_data(&f,4,image_b,512,fw_crc32(image_b,512))==FW_BAD_OFFSET);
 assert(fw_data(&f,0,image_b,512,0)==FW_BAD_HASH);assert(writes==n);
 assert(fw_data(&f,0,image_b,512,fw_crc32(image_b,512))==FW_OK);
 assert(fw_data(&f,0,image_b,512,fw_crc32(image_b,512))==FW_BAD_OFFSET);
 assert(fw_finish(&f)==FW_NOT_READY);assert(fw_begin(&f,2048,hash,0)==FW_BUSY);
 /* Incomplete upload survives reboot only as a failure flag, never pending. */
 f=reopened();boot_is(&f,0,false);assert((f.meta.flags&FW_UPLOAD_FAILED)&&!(f.meta.flags&FW_UPLOADING));
 send_image(&f,image_b);
 /* An additive-checksum collision must still fail SHA-256 validation. */
 ++flash[FW_SLOT_B+1600];--flash[FW_SLOT_B+1601];assert(fw_finish(&f)==FW_BAD_HASH);
 assert(f.meta.pending==FW_NONE&&(f.meta.flags&FW_UPLOAD_FAILED));boot_is(&f,0,false);
 send_image(&f,image_b);assert(fw_abort(&f)==FW_OK);assert(f.meta.flags&FW_UPLOAD_ABORTED);boot_is(&f,0,false);
 /* Correct SHA/checksum with invalid vectors cannot become pending. */
 put(image_b+0x400,0x90000000);uint32_t sum=0;for(unsigned i=0x240;i<2048;++i)sum+=image_b[i];put(image_b+0x64,sum);
 send_image(&f,image_b);assert(fw_finish(&f)==FW_BAD_IMAGE);boot_is(&f,0,false);
 assert(!memcmp(flash+FW_SLOT_A,image_a,2048));
 puts("Bounds, offsets, CRC/SHA, vector validation, interrupted and aborted uploads");
}
/** @brief Verify metadata recovery across partial writes, partial erases, and generation rollover. */
static void torn_metadata(void){
 fw_update_t base=initial();assert(fw_clear_warnings(&base)==FW_OK);
 uint8_t saved[2][FW_RECORD_SIZE];memcpy(saved[0],flash+FW_META_A,FW_RECORD_SIZE);memcpy(saved[1],flash+FW_META_B,FW_RECORD_SIZE);
 for(int cut=0;cut<(int)FW_RECORD_SIZE;++cut){
  memcpy(flash+FW_META_A,saved[0],FW_RECORD_SIZE);memcpy(flash+FW_META_B,saved[1],FW_RECORD_SIZE);
  fw_update_t f=base;fw_metadata_t next=f.meta;next.flags|=FW_FALLBACK;
  write_budget=cut;assert(fw_store(&f,&next)==FW_IO_ERROR);write_budget=-1;
  f=reopened();assert(f.meta.generation==base.meta.generation);assert(!(f.meta.flags&FW_FALLBACK));boot_is(&f,0,false);
 }
 for(int cut=0;cut<=(int)FW_RECORD_SIZE;++cut){
  memcpy(flash+FW_META_A,saved[0],FW_RECORD_SIZE);memcpy(flash+FW_META_B,saved[1],FW_RECORD_SIZE);
  fw_update_t f=base;fw_metadata_t next=f.meta;next.flags|=FW_FALLBACK;
  erase_budget=cut;assert(fw_store(&f,&next)==FW_IO_ERROR);erase_budget=-1;
  f=reopened();assert(f.meta.generation==base.meta.generation);boot_is(&f,0,false);
 }
 /* Generation wrap: the zero-generation record follows UINT32_MAX. */
 memcpy(flash+FW_META_A,saved[0],FW_RECORD_SIZE);memcpy(flash+FW_META_B,saved[1],FW_RECORD_SIZE);
 put(flash+FW_META_A+12,UINT32_MAX);put(flash+FW_META_A+116,fw_crc32(flash+FW_META_A,116));
 put(flash+FW_META_B+12,0);put(flash+FW_META_B+116,fw_crc32(flash+FW_META_B,116));
 fw_update_t f=reopened();assert(f.record==1&&f.meta.generation==0);
 puts("249 torn metadata writes/erases and generation rollover");
}

/** @brief Verify safe boot behavior after interrupted image writes and metadata commits. */
static void failed_programs_and_commits(void){
 /* Partial page programming never changes the confirmed image. */
 fw_update_t f=initial();uint8_t hash[32];fw_sha256(image_b,2048,hash);
 assert(fw_begin(&f,2048,hash,5)==FW_OK);write_budget=200;
 assert(fw_data(&f,0,image_b,512,fw_crc32(image_b,512))==FW_IO_ERROR);
 write_budget=-1;f=reopened();boot_is(&f,0,false);
 assert(!memcmp(flash+FW_SLOT_A,image_a,2048));
 /* A fully written file with a torn FINISH commit is still never pending. */
 for(int cut=0;cut<124;cut+=11){
  f=initial();send_image(&f,image_b);write_budget=cut;assert(fw_finish(&f)==FW_IO_ERROR);
  write_budget=-1;f=reopened();boot_is(&f,0,false);assert(f.meta.pending==FW_NONE);
 }
 /* A failed trial-record write cannot authorize a candidate jump. */
 for(int cut=0;cut<124;cut+=11){
  f=initial();send_image(&f,image_b);assert(fw_finish(&f)==FW_OK);
  write_budget=cut;uint32_t slot;bool trial;assert(fw_boot_select(&f,&slot,&trial)==FW_IO_ERROR);
  write_budget=-1;f=reopened();boot_is(&f,1,true);
 }
 /* If confirmation tears, the previously committed trial record wins and
    the next boot must fall back, even though the candidate was running. */
 for(int cut=0;cut<124;cut+=11){
  f=initial();send_image(&f,image_b);assert(fw_finish(&f)==FW_OK);boot_is(&f,1,true);
  write_budget=cut;assert(fw_confirm(&f,1)==FW_IO_ERROR);
  write_budget=-1;f=reopened();boot_is(&f,0,false);assert(f.meta.flags&FW_FALLBACK);
 }
 /* Both images invalid: refuse to jump and persist the diagnostic. */
 f=initial();flash[FW_SLOT_A]^=1;uint32_t slot;bool trial;
 assert(fw_boot_select(&f,&slot,&trial)==FW_BAD_IMAGE);assert(f.meta.flags&FW_NO_VALID_IMAGE);
 puts("Partial image programs and torn FINISH/trial/confirmation commits");
}
static unsigned packets,control_packets;
/** @brief Count complete parsed frames and check their declared payload lengths. */
static void frame(void *ctx,const uint8_t *p,uint32_t size){
 (void)ctx;assert(size==16+get(p+8));++packets;if(!memcmp(p,"CTRL",4))++control_packets;
}
/** @brief Exercise fragmented, multiplexed, and invalid firmware update input. */
static void wire(void){
 fw_parser_t p={0};uint8_t input[64]={0};memcpy(input,"DFUC",4);put(input+4,1);put(input+8,12);input[12]=1;
 memcpy(input+16,"CTRLDFUCxxxx",12);
 for(unsigned i=0;i<28;++i)fw_wire_feed(&p,input+i,1,frame,NULL);
 assert(packets==1&&!control_packets&&p.used==0);
 memcpy(input,"CTRL",4);put(input+8,0);fw_wire_feed(&p,input,16,frame,NULL);assert(control_packets==1);
 memset(input,'x',64);fw_wire_feed(&p,input,64,frame,NULL);
 memcpy(input,"DFUC",4);put(input+4,2);put(input+8,0);input[12]=1;input[13]=0;
 fw_wire_feed(&p,input,7,frame,NULL);fw_wire_feed(&p,input+7,9,frame,NULL);assert(packets==3);
 /* Oversized headers cannot overrun the parser. */
 put(input+8,UINT32_MAX);for(unsigned i=0;i<100;++i)fw_wire_feed(&p,input,64,frame,NULL);
 puts("Fragmented/multiplexed framing, embedded magic and oversized input");
}
/** @brief Run the portable updater, persistence, cryptography, and framing checks. */
int main(int argc,char **argv){
 (void)argv;
 if(argc>1){static uint8_t data[FW_MAX_IMAGE];size_t n=fread(data,1,sizeof(data),stdin);
  uint8_t digest[32];fw_sha256(data,n,digest);assert(fwrite(digest,1,32,stdout)==32);return 0;}
 crypto();lifecycle();rollback();bad_uploads();torn_metadata();failed_programs_and_commits();wire();
 printf("Firmware update tests passed (%u reads, %u programs, %u erases)\n",reads,writes,erases);return 0;}
'''


## @brief Build a checksum-valid STM32 application image for update tests.
def image_fixture():
    data = bytearray(b'\x01' * 2048)
    for offset, value in ((0, 0x324d5453), (0x68, 0x20300), (0x6c, len(data) - 0x240),
                          (0x70, 0x34000409), (0x400, 0x34200000), (0x404, 0x34000409)):
        struct.pack_into('<I', data, offset, value)
    struct.pack_into('<I', data, 0x64, sum(data[0x240:]))
    return bytes(data)


class FirmwareUpdateTests(unittest.TestCase):
    ## @brief Verify c update policy.
    def test_c_update_policy(self):
        compiler = shutil.which('cc')
        if not compiler:
            self.skipTest('host C compiler unavailable')
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'test.c'
            source.write_text(HARNESS)
            subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-Werror', '-g',
                            '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                            '-I', str(ROOT / 'common/firmware'), str(source),
                            str(ROOT / 'common/firmware/fw_update.c'),
                            str(ROOT / 'common/firmware/fw_wire.c'), '-o', str(root / 'test')], check=True)
            # The harness uses static storage; LSan cannot run under ptrace,
            # while ASan/UBSan still check bounds and undefined behavior.
            env = {**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0'}
            subprocess.run([str(root / 'test')], check=True, env=env)
            for size in (0, 1, 55, 56, 63, 64, 65, 127, 128, 512, 2048, 0x80000):
                data = bytes((i * 17 + i // 7) % 256 for i in range(size))
                digest = subprocess.run([str(root / 'test'), 'sha'], input=data,
                                        stdout=subprocess.PIPE, check=True, env=env).stdout
                self.assertEqual(digest, hashlib.sha256(data).digest(), size)

    ## @brief Verify python image validation.
    def test_python_image_validation(self):
        data = image_fixture()
        self.assertEqual(client.validate_image(data), hashlib.sha256(data).digest())
        for offset in (0, 0x68, 0x6c, 0x70, 0x400, 0x404, 1600):
            corrupt = bytearray(data)
            corrupt[offset] ^= 0x80
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                client.validate_image(corrupt)
        for size in (0, 1024, 0x80004):
            with self.assertRaises(ValueError):
                client.validate_image(bytes(size))

    ## @brief Verify client reads fragmented replies and ignores telemetry.
    def test_client_reads_fragmented_replies_and_ignores_telemetry(self):
        payload = struct.pack('<12I', 0, 8, 0, 0, client.NONE, 0, 0, 0, 4, 1, 0, 512)
        telemetry = client.HEADER.pack(b'PARM', 0, 8, 1, 0) + b'DFUSxxxx'
        wrong_id = client.HEADER.pack(b'DFUS', 99, 48, 1, client.STATUS) + payload
        wrong_op = client.HEADER.pack(b'DFUS', 1, 48, 1, client.BEGIN) + payload
        reply = client.HEADER.pack(b'DFUS', 1, 48, 1, client.STATUS) + payload
        stream = b'noise' + telemetry + wrong_id + wrong_op + reply
        class Link:
            ## @brief Initialize the simulated transport state used by this test.
            def __init__(self):
                self.position = 0
            ## @brief Submit outgoing bytes to the simulated transport.
            def sendall(self, packet):
                self.sent = packet
            ## @brief Return the next simulated TCP input or the configured timeout or disconnection.
            def recv(self, size):
                data = stream[self.position:self.position + min(3, size)]
                self.position += len(data)
                return data
            ## @brief Report the input currently available from the simulated serial transport.
            @property
            def in_waiting(self):
                return min(3, len(stream) - self.position)
            ## @brief Return the next bytes supplied by the simulated input stream.
            def read(self, size):
                assert size <= max(1, self.in_waiting), 'serial ACK reads must not wait for a large buffer'
                return self.recv(size)
            ## @brief Consume outgoing bytes and emulate the transport response used by the test.
            def write(self, data):
                self.sent = data
                return len(data)
        for tcp in (False, True):
            with self.subTest(tcp=tcp):
                instance = client.Client.__new__(client.Client)
                instance.link = Link()
                instance.buffer = bytearray()
                instance.request_id = 0
                instance.timeout = 1
                instance.tcp = tcp
                status = instance.request(client.STATUS)
                self.assertEqual(status['confirmed'], 0)
                self.assertEqual(instance.link.sent, client.HEADER.pack(b'DFUC', 1, 0, 1, client.STATUS))

    ## @brief Verify host upload.
    def test_host_upload(self):
        class FakeClient:
            ## @brief Initialize the simulated transport state used by this test.
            def __init__(self):
                self.data = bytearray()
                self.operations = []
            ## @brief Emulate a DFU request and return the corresponding update status.
            def request(self, op, payload=b''):
                self.operations.append(op)
                result = dict(pending=NONE, chunk_size=512, flags=0)
                if op == client.BEGIN:
                    self.size, self.version, self.digest = struct.unpack('<II32s', payload)
                elif op == client.DATA:
                    offset, crc = struct.unpack_from('<II', payload)
                    assert offset == len(self.data)
                    assert crc == zlib.crc32(payload[8:])
                    self.data.extend(payload[8:])
                    result['received'] = len(self.data)
                elif op == client.FINISH:
                    assert len(self.data) == self.size
                    assert hashlib.sha256(self.data).digest() == self.digest
                    result.update(pending=1, flags=6)
                return result
        NONE = client.NONE
        fake = FakeClient()
        with tempfile.TemporaryDirectory() as directory:
            image = Path(directory) / 'application-signed.bin'
            image.write_bytes(image_fixture())
            client.upload(fake, image, version=9)
        self.assertEqual(fake.version, 9)
        self.assertEqual(fake.data, image_fixture())
        self.assertEqual(fake.operations, [client.STATUS, client.BEGIN] + [client.DATA] * 4 + [client.FINISH, client.RESET])


if __name__ == '__main__':
    unittest.main()
