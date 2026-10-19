#!/usr/bin/env python3
"""Upload a v2.3 STM32 application image through USB CDC or TCP port 50003."""
import argparse
import hashlib
import json
from pathlib import Path
import socket
import struct
import time
import zlib

HEADER = struct.Struct('<4sIIHH')
STATUS, BEGIN, DATA, FINISH, ABORT, RESET, CLEAR = range(1, 8)
RESULTS = ('ok', 'bad request', 'busy', 'flash I/O error', 'invalid image',
           'hash/CRC mismatch', 'unexpected offset/length', 'not ready', 'trial boot failed')
FLAGS = {1: 'upload_in_progress', 2: 'image_uploaded', 4: 'image_verified',
         8: 'boot_confirmed', 16: 'fallback_warning', 32: 'upload_failed',
         64: 'no_valid_image', 128: 'metadata_initialized_or_recovered', 256: 'upload_aborted'}
NONE = 0xffffffff


class UploadCancelled(Exception):
    """The caller cancelled an upload before its image was committed."""


## @brief Validate the signed STM32 image layout, vectors, and checksum, then return its SHA-256.
def validate_image(data):
    if not 0x408 <= len(data) <= 0x80000 or len(data) % 4:
        raise ValueError('Image must be an aligned signed application fitting 512 KiB SRAM')
    word = lambda offset: struct.unpack_from('<I', data, offset)[0]
    if word(0) != 0x324d5453 or word(0x68) != 0x00020300 or word(0x6c) + 0x240 != len(data):
        raise ValueError('Use VL53L9_eval_Appli-signed.bin (STM32 header v2.3 with --align)')
    sp, entry = struct.unpack_from('<II', data, 0x400)
    if (not 0x34080000 < sp <= 0x34200000 or sp % 8 or not entry & 1 or
            not 0x34000400 <= (entry & ~1) < 0x34000000 + len(data) or word(0x70) != entry):
        raise ValueError('Image vectors/entry point do not match the SRAM application layout')
    if (sum(data[0x240:]) & NONE) != word(0x64):
        raise ValueError('STM32 payload checksum is invalid')
    return hashlib.sha256(data).digest()


## @brief Decode a DFU reply payload and expand its persistent status flags.
def decode_status(payload):
    if len(payload) != 48:
        raise ValueError('Invalid DFU status length')
    keys = ('result', 'flags', 'running', 'confirmed', 'pending', 'received', 'total',
            'last_error', 'generation', 'version_a', 'version_b', 'chunk_size')
    result = dict(zip(keys, struct.unpack('<12I', payload)))
    result['flag_names'] = [name for bit, name in FLAGS.items() if result['flags'] & bit]
    return result


class Client:
    ## @brief Open the selected TCP or USB endpoint and initialize DFU reply parsing.
    def __init__(self, *, tcp=None, usb=None, port=50003, timeout=30):
        self.timeout = timeout
        self.request_id = 0
        self.buffer = bytearray()
        if tcp:
            self.link = socket.create_connection((tcp, port), timeout=timeout)
            self.link.settimeout(.25)
            self.tcp = True
        else:
            import serial
            self.link = serial.Serial(usb, 115200, timeout=.25, write_timeout=timeout)
            self.link.dtr = True
            self.tcp = False
            time.sleep(.2)
            self.link.reset_input_buffer()

    ## @brief Close the update transport connection.
    def close(self):
        self.link.close()

    ## @brief Send a DFU command and wait for its matching acknowledgement while skipping telemetry.
    def request(self, operation, payload=b''):
        self.request_id = (self.request_id + 1) & NONE or 1
        packet = HEADER.pack(b'DFUC', self.request_id, len(payload), 1, operation) + payload
        if self.tcp:
            self.link.sendall(packet)
        elif self.link.write(packet) != len(packet):
            raise OSError('Short USB command write')
        deadline = time.monotonic() + self.timeout
        # Known outgoing frame magics; never scan inside an accepted payload.
        magics = (b'DFUS', b'TOFS', b'PARM', b'IMUF', b'PROF', b'VL9F')
        while time.monotonic() < deadline:
            while len(self.buffer) >= HEADER.size:
                magic, request_id, size, version, op = HEADER.unpack_from(self.buffer)
                if magic not in magics or size > 65536:
                    del self.buffer[0]
                    continue
                if len(self.buffer) < HEADER.size + size:
                    break
                body = bytes(self.buffer[HEADER.size:HEADER.size + size])
                del self.buffer[:HEADER.size + size]
                if magic == b'DFUS' and request_id == self.request_id and op == operation and version == 1:
                    status = decode_status(body)
                    if status['result']:
                        code = status['result']
                        raise RuntimeError(f'DFU operation {operation}: {RESULTS[code] if code < len(RESULTS) else code}; {status}')
                    return status
            try:
                if self.tcp:
                    chunk = self.link.recv(4096)
                else:
                    # ACKs are only 64 bytes. Waiting for a 4096-byte serial
                    # read would add the read timeout to every chunk upload.
                    waiting = self.link.in_waiting
                    chunk = self.link.read(min(4096, max(1, waiting)))
            except socket.timeout:
                continue
            if not chunk and self.tcp:
                raise ConnectionError('DFU connection closed')
            self.buffer.extend(chunk)
        raise TimeoutError('DFU acknowledgement timed out; reconnect and inspect status before retrying')


## @brief Validate and upload an image in checked chunks, commit it, and optionally request a reset.
def upload(client, image, *, version=0, reset=True, progress=None, cancelled=None, log=print):
    data = image.read_bytes()
    digest = validate_image(data)
    if not isinstance(version, int) or not 0 <= version <= NONE:
        raise ValueError('Release identifier must fit uint32')
    manifest = image.with_suffix('.manifest.json')
    if manifest.exists() and json.loads(manifest.read_text()).get('options', {}).get('DEV_MODE'):
        raise ValueError('DEV_MODE images cannot be installed; rebuild without --dev')
    if cancelled and cancelled():
        raise UploadCancelled('Upload cancelled before programming')
    before = client.request(STATUS)
    if before['pending'] != NONE:
        raise ValueError('A verified image is already pending; reset to test it before starting another upload')
    if cancelled and cancelled():
        raise UploadCancelled('Upload cancelled before programming')
    status = client.request(BEGIN, struct.pack('<II32s', len(data), version, digest))
    try:
        chunk_size = min(status['chunk_size'], 512)
        if chunk_size < 4 or chunk_size % 4:
            raise ValueError('Invalid device chunk size')
        if progress:
            progress(0, len(data), status)
        for offset in range(0, len(data), chunk_size):
            if cancelled and cancelled():
                raise UploadCancelled('Upload cancelled; incomplete image discarded')
            chunk = data[offset:offset + chunk_size]
            status = client.request(DATA, struct.pack('<II', offset, zlib.crc32(chunk)) + chunk)
            if status['received'] != offset + len(chunk):
                raise ValueError('Device acknowledged an unexpected upload offset')
            if progress:
                progress(status['received'], len(data), status)
            if offset % (32 * 1024) == 0:
                log(f'Uploaded {status["received"]}/{len(data)} bytes')
        if cancelled and cancelled():
            raise UploadCancelled('Upload cancelled; incomplete image discarded')
        status = client.request(FINISH)
    except UploadCancelled:
        client.request(ABORT)
        raise
    if status['pending'] == NONE or status['flags'] & 6 != 6:
        raise ValueError('Device did not commit a verified pending image')
    if progress:
        progress(len(data), len(data), status)
    log(f'Verified slot {"AB"[status["pending"]]}, SHA-256 {digest.hex()}')
    if reset:
        client.request(RESET)
        log('Reset scheduled. Reconnect after boot and check status for confirmation or fallback_warning.')
    return status


## @brief Run the requested firmware upload, status query, or update maintenance action.
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    endpoint = parser.add_mutually_exclusive_group(required=True)
    endpoint.add_argument('--tcp', metavar='HOST')
    endpoint.add_argument('--usb', metavar='DEVICE')
    parser.add_argument('--port', type=int, default=50003)
    action = parser.add_mutually_exclusive_group()
    action.add_argument('--image', type=Path)
    action.add_argument('--abort', action='store_true')
    action.add_argument('--reset', action='store_true')
    action.add_argument('--clear-warnings', action='store_true')
    parser.add_argument('--version', type=lambda s: int(s, 0), default=0, help='uint32 release identifier')
    parser.add_argument('--no-reset', action='store_true')
    args = parser.parse_args()
    if not 0 <= args.version <= NONE:
        parser.error('--version must fit uint32')
    client = None
    try:
        client = Client(tcp=args.tcp, usb=args.usb, port=args.port)
        if args.image:
            status = upload(client, args.image, version=args.version, reset=not args.no_reset)
        else:
            operation = ABORT if args.abort else RESET if args.reset else CLEAR if args.clear_warnings else STATUS
            status = client.request(operation)
        print(json.dumps(status, indent=2))
    except (OSError, ValueError, RuntimeError, TimeoutError, ImportError) as error:
        parser.exit(1, f'Firmware update failed: {error}\n')
    finally:
        if client:
            client.close()


if __name__ == '__main__':
    main()
