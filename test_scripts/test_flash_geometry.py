"""Native regression for the hybrid SEMPER erase range used by CubeProgrammer."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(shutil.which('cc'), 'native C compiler required')
class FlashGeometryTests(unittest.TestCase):
    def test_parameter_sectors_do_not_touch_fsbl_and_ranges_are_bounded(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            (temp / 'memory_wrapper.h').write_text('typedef enum {MEM_OK, MEM_FAIL} MEM_STATUS;\n')
            (temp / 'stm32_extmemloader_conf.h').write_text('''
#define STM32EXTLOADER_DEVICE_ADDR 0x90000000U
#define STM32EXTLOADER_DEVICE_SIZE 0x4000000U
#define STM32EXTLOADER_DEVICE_MEMORY_ID 0U
''')
            (temp / 'stm32_extmem.h').write_text('''
#include <stdint.h>
#define EXTMEM_OK 0
int EXTMEM_EraseSector(uint32_t, uint32_t, uint32_t);
''')
            (temp / 'test.c').write_text(r'''
#include "memory_wrapper.h"
#include <stdint.h>
#include <assert.h>
MEM_STATUS memory_sectorerase(uint32_t,uint32_t,uint32_t);
static uint32_t next, count, fail;
int EXTMEM_EraseSector(uint32_t id, uint32_t address, uint32_t size) {
 assert(id==0 && address==next);
 assert(size==(address<0x20000 ? 0x1000U : address<0x40000 ? 0x20000U : 0x40000U));
 next+=size; ++count; return fail ? 1 : 0;
}
int main(void) {
 assert(memory_sectorerase(0x90000000,0x90020000,0x40000)==MEM_OK);
 assert(count==33 && next==0x40000); /* FSBL starts here: never erase it. */
 count=next=0;
 assert(memory_sectorerase(0x90000000,0x9003ffff,0x40000)==MEM_OK);
 assert(count==33 && next==0x40000); /* inclusive last byte works too */
 count=0; next=0x40000;
 assert(memory_sectorerase(0x90040000,0x90080000,0x40000)==MEM_OK);
 assert(count==2 && next==0xc0000);
 count=0; next=0;
 assert(memory_sectorerase(0x90000000,0x93fc0000,0x40000)==MEM_OK);
 assert(count==288 && next==0x4000000);
 count=0;
 assert(memory_sectorerase(0x8fffffff,0x90000000,0x40000)==MEM_FAIL);
 assert(memory_sectorerase(0x90040000,0x90000000,0x40000)==MEM_FAIL);
 assert(memory_sectorerase(0x90000001,0x90001000,0x40000)==MEM_FAIL);
 assert(memory_sectorerase(0x90000000,0x94000000,0x40000)==MEM_FAIL);
 assert(count==0);
 next=0; fail=1;
 assert(memory_sectorerase(0x90000000,0x90020000,0x40000)==MEM_FAIL);
 assert(count==1);
 return 0;
}
''')
            result = subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                                     '-I', str(temp), str(ROOT / 'ExtMemLoader/Core/Src/flash_geometry.c'),
                                     str(temp / 'test.c'), '-o', str(temp / 'test')],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            subprocess.run([str(temp / 'test')], check=True)


if __name__ == '__main__':
    unittest.main()
