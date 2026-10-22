"""Exercise the real FSBL handoff cleanup with ROM-like inherited register state."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(shutil.which('cc'), 'native C compiler required')
class FsblHandoffTests(unittest.TestCase):
    def test_caches_mpu_systick_and_pending_interrupts_are_quiesced(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            (temp / 'stm32n6xx.h').write_text(r'''
#include <stdint.h>
#include <assert.h>
#define __DCACHE_PRESENT 1U
#define __ICACHE_PRESENT 1U
#define __MPU_PRESENT 1U
#define SCB_CCR_DC_Msk (1U<<16)
#define SCB_CCR_IC_Msk (1U<<17)
#define SCB_ICSR_PENDSTCLR_Msk (1U<<25)
#define SCB_ICSR_PENDSVCLR_Msk (1U<<27)
#define ICB_ICTR_INTLINESNUM_Msk 15U
static struct {uint32_t CTRL,LOAD,VAL;} tick;
static struct {uint32_t ICER[16],ICPR[16];} nvic;
static struct {uint32_t ICSR,CCR,VTOR;} scb;
static struct {uint32_t ICTR;} icb;
#define SysTick (&tick)
#define NVIC (&nvic)
#define SCB (&scb)
#define ICB (&icb)
static unsigned masked,dcache_calls,icache_calls,mpu_calls,barriers;
static void __disable_irq(void){masked=1;}
static void SCB_DisableDCache(void){
 assert(masked && !tick.CTRL && !tick.LOAD && !tick.VAL);
 assert(!icache_calls && !mpu_calls);++dcache_calls;scb.CCR&=~SCB_CCR_DC_Msk;
}
static void SCB_DisableICache(void){
 assert(masked && !(scb.CCR&SCB_CCR_DC_Msk) && !mpu_calls);
 ++icache_calls;scb.CCR&=~SCB_CCR_IC_Msk;
}
static void ARM_MPU_Disable(void){assert(icache_calls);++mpu_calls;}
static void __DSB(void){++barriers;}
static void __ISB(void){++barriers;}
''')
            (temp / 'test.c').write_text(r'''
#include "fw_handoff.h"
#include <string.h>
int main(void){
 for(unsigned cached=0;cached<2;++cached){
  memset(&nvic,0,sizeof(nvic));
  tick.CTRL=7;tick.LOAD=64000;tick.VAL=17;
  scb.CCR=cached?(SCB_CCR_DC_Msk|SCB_CCR_IC_Msk):0;
  scb.VTOR=0x34180400;icb.ICTR=8;
  masked=dcache_calls=icache_calls=mpu_calls=barriers=0;
  fw_prepare_handoff(0x34000400);
  assert(masked && !tick.CTRL && !tick.LOAD && !tick.VAL);
  for(unsigned i=0;i<16;++i){
   assert(nvic.ICER[i]==(i<9?UINT32_MAX:0));
   assert(nvic.ICPR[i]==(i<9?UINT32_MAX:0));
  }
  assert(scb.ICSR==(SCB_ICSR_PENDSTCLR_Msk|SCB_ICSR_PENDSVCLR_Msk));
  assert(dcache_calls==cached && icache_calls==1 && mpu_calls==1);
  assert(!scb.CCR && scb.VTOR==0x34000400 && barriers==2);
 }
 return 0;
}
''')
            result = subprocess.run(
                ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-I', str(temp),
                 '-I', str(ROOT / 'common/firmware'), str(temp / 'test.c'), '-o', str(temp / 'test')],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            subprocess.run([str(temp / 'test')], check=True)


if __name__ == '__main__':
    unittest.main()
