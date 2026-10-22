"""Native tests of actual LED register writes, transitions, retries and ToF mapping."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

HARDWARE = r'''
#ifndef MAIN_H
#define MAIN_H
#include <stdint.h>
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef struct { unsigned unused; } I2C_HandleTypeDef;
extern I2C_HandleTypeDef hi2c2;
#define I2C_MEMADD_SIZE_8BIT 1
uint32_t HAL_GetTick(void);
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t mask);
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *,uint16_t,uint16_t,uint16_t,uint8_t *,uint16_t,uint32_t);
#endif
'''

HARNESS = r'''
#include "LEDController.h"
#include "status_led.h"
#include "tof_control.h"
#include "boot_diagnostics.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

I2C_HandleTypeDef hi2c2;
static uint32_t tick,mask;
static unsigned writes,fail_at,reenter;
static struct {uint16_t address,size;uint8_t data[36];} wire[16];
uint32_t HAL_GetTick(void){return tick;}
uint32_t __get_PRIMASK(void){return mask;}
void __disable_irq(void){mask=1;}
void __set_PRIMASK(uint32_t value){mask=value;}
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *i,uint16_t addr,uint16_t reg,
                                  uint16_t mode,uint8_t *data,uint16_t size,uint32_t timeout){
 assert(i==&hi2c2 && addr==0x5a && mode==I2C_MEMADD_SIZE_8BIT && timeout==10);
 assert(!mask); /* Transfers must leave the tick interrupt enabled. */
 assert(writes<16 && size<=36);
 wire[writes].address=reg;wire[writes].size=size;memcpy(wire[writes].data,data,size);++writes;
 if(reenter){
  reenter=0;
  assert(LEDController_SetAnimation(&led_controller,LED_ANIMATION_APP_FAULT)==HAL_BUSY);
 }
 return fail_at==writes?HAL_TIMEOUT:HAL_OK;
}
static void clear_writes(void){writes=0;memset(wire,0,sizeof(wire));}
static void initialize(void){
 clear_writes();fail_at=0;assert(LEDController_Init(&led_controller)==HAL_OK);clear_writes();
}
static void check_transaction(void){
 static const unsigned addresses[]={0x11,0x14,0,0x1c,0x0f,0x10};
 static const unsigned sizes[]={1,3,13,36,1,1};
 assert(writes==6);
 for(unsigned i=0;i<6;++i){assert(wire[i].address==addresses[i] && wire[i].size==sizes[i]);}
 assert(wire[0].data[0]==0xaa && wire[4].data[0]==0x55 && wire[5].data[0]==0xff);
 assert(wire[2].data[0]==3 && wire[2].data[1]==0 && wire[2].data[3]==0);
 assert(wire[2].data[4]==0x77 && wire[2].data[5]==0x24);
 for(unsigned i=27;i<36;++i)assert(!wire[3].data[i]); /* Unused pattern stays clear. */
 for(unsigned i=0;i<3;++i){
  if(wire[2].data[2]&(1U<<i)){
   assert(wire[3].data[9*i+1]>=1 && wire[3].data[9*i+1]<=15);
   assert(((wire[2].data[12]>>(2*i))&3)==3); /* Engine repeats forever. */
  }
 }
}
static void patterns(void){
 initialize();
 for(unsigned animation=0;animation<LED_ANIMATION_COUNT;++animation){
  initialize();assert(LEDController_SetAnimation(&led_controller,animation)==HAL_OK);check_transaction();
  uint8_t expected[36],config[13];memcpy(expected,wire[3].data,36);memcpy(config,wire[2].data,13);
  clear_writes();
  for(unsigned i=0;i<100;++i)assert(LEDController_SetAnimation(&led_controller,animation)==HAL_OK);
  assert(writes==0); /* Frame publication must not restart autonomous patterns. */
  initialize();assert(LEDController_SetAnimation(&led_controller,LED_ANIMATION_APP_FAULT)==HAL_OK);
  if(animation==LED_ANIMATION_APP_FAULT)assert(LEDController_SetAnimation(&led_controller,LED_ANIMATION_OFF)==HAL_OK);
  clear_writes();assert(LEDController_SetAnimation(&led_controller,animation)==HAL_OK);
  assert(!memcmp(expected,wire[3].data,36) && !memcmp(config,wire[2].data,13));
 }
 /* Wire values for the documented blink, breathing and double-pulse rhythms. */
 initialize();LEDController_SetAnimation(&led_controller,LED_ANIMATION_FSBL_STARTED);
 assert(wire[2].data[2]==3 && wire[3].data[7]==5 && wire[3].data[8]==5);
 clear_writes();LEDController_SetAnimation(&led_controller,LED_ANIMATION_FSBL_LOADING);
 assert(wire[3].data[7]==2 && wire[3].data[8]==2);
 clear_writes();LEDController_SetAnimation(&led_controller,LED_ANIMATION_APP_RANGING);
 assert(wire[2].data[2]==2 && wire[3].data[9+7]==0x2a && wire[3].data[9+8]==0x2a);
 clear_writes();LEDController_SetAnimation(&led_controller,LED_ANIMATION_APP_INIT_FINISHED);
 assert(wire[2].data[2]==6 && wire[3].data[9]==0x0b);
 assert(wire[3].data[9+3]>0 && wire[3].data[9+5]>0 && wire[3].data[9+4]==0);
 puts("All presets, deterministic transitions, infinite playback and unchanged-state caching");
}
static void failures(void){
 for(unsigned failure=1;failure<=6;++failure){
  initialize();fail_at=failure;
  assert(LEDController_SetAnimation(&led_controller,LED_ANIMATION_APP_RANGING)==HAL_TIMEOUT);
  assert(writes==failure && !led_controller.animation_valid && !led_controller.busy);
  clear_writes();fail_at=0;
  assert(LEDController_SetAnimation(&led_controller,LED_ANIMATION_APP_RANGING)==HAL_TIMEOUT && !writes);
  tick+=500;
  assert(LEDController_SetAnimation(&led_controller,LED_ANIMATION_APP_RANGING)==HAL_OK);
  check_transaction();
 }
 initialize();clear_writes();reenter=1;
 assert(LEDController_SetAnimation(&led_controller,LED_ANIMATION_APP_IDLE)==HAL_OK);
 assert(led_controller.animation==LED_ANIMATION_APP_IDLE);check_transaction();
 initialize();tick=UINT32_MAX-100;fail_at=1;
 assert(LEDController_SetAnimation(&led_controller,LED_ANIMATION_APP_RANGING)==HAL_TIMEOUT);
 clear_writes();fail_at=0;tick=20;
 assert(LEDController_SetAnimation(&led_controller,LED_ANIMATION_APP_RANGING)==HAL_TIMEOUT && !writes);
 tick=400;assert(LEDController_SetAnimation(&led_controller,LED_ANIMATION_APP_RANGING)==HAL_OK);
 initialize();clear_writes();
 assert(LEDController_SetAnimation(NULL,0)==HAL_ERROR);
 assert(LEDController_SetAnimation(&led_controller,LED_ANIMATION_COUNT)==HAL_ERROR);
 assert(LEDController_SetAnimation(&led_controller,(LED_Animation_t)-1)==HAL_ERROR);
 LEDController blank={0};assert(LEDController_SetAnimation(&blank,LED_ANIMATION_APP_FAULT)==HAL_ERROR);
 assert(!writes && LEDController_Init(NULL)==HAL_ERROR);
 puts("Every failed transaction step, retry throttling, timer wrap, concurrent writer and invalid inputs");
}
static void lifecycle(void){
 static const unsigned states[]={TOF_STREAMING,TOF_STOPPED,TOF_OFF,TOF_INITIALIZING,TOF_STOPPED,TOF_STREAMING,TOF_FAULT,99};
 static const LED_Animation_t animations[]={LED_ANIMATION_APP_RANGING,LED_ANIMATION_APP_IDLE,
 LED_ANIMATION_APP_POWER_DOWN,LED_ANIMATION_APP_RESETTING,LED_ANIMATION_APP_IDLE,
 LED_ANIMATION_APP_RANGING,LED_ANIMATION_APP_FAULT,LED_ANIMATION_APP_FAULT};
 initialize();
 for(unsigned i=0;i<sizeof(states)/sizeof(states[0]);++i){
  clear_writes();status_led_tof(states[i]);assert(led_controller.animation==animations[i]);
  if(i && animations[i]==animations[i-1])assert(!writes);else check_transaction();
 }
 puts("Actual ToF state-to-pattern mappings, stop/start/reset/power-down/fault recovery");
}
/* LP5815 behavioral model: PT counts only slopes; pauses surround the finite
 * slope loop. Engine repeat wraps the entire pattern. Infinite PT never reaches
 * the end pause. Sample the resulting waveform, not just register equality. */
static unsigned sample(const uint8_t *p,unsigned time){
 static const unsigned ms[]={0,50,100,150,200,250,300,350,400,450,500,1000,2000,4000,6000,8000};
 unsigned duration[4]={ms[p[7]&15],ms[p[7]>>4],ms[p[8]&15],ms[p[8]>>4]};
 unsigned body=duration[0]+duration[1]+duration[2]+duration[3];
 unsigned before=ms[p[0]>>4],after=ms[p[0]&15],repeats=p[1]&15;
 assert(body);
 if(repeats!=15)time%=before+body*repeats+after;
 if(time<before)return p[2];
 time-=before;
 if(repeats!=15 && time>=body*repeats)return p[6];
 time%=body;
 for(unsigned i=0;i<4;++i){
  if(time<duration[i]){
   int start=p[2+i],end=p[3+i];
   return (unsigned)(start+(end-start)*(int)time/(int)duration[i]);
  }
  time-=duration[i];
 }
 assert(0);return 0;
}
static unsigned pulse_count(const uint8_t *p,unsigned duration){
 unsigned count=0,previous=0;
 for(unsigned t=0;t<duration;t+=50){
  unsigned bright=sample(p,t)>90;
  if(bright && !previous)++count;
  previous=bright;
 }
 return count;
}
static void diagnostics(void){
 static const LED_DiagnosticColor_t colors[]={LED_DIAGNOSTIC_RED,LED_DIAGNOSTIC_AMBER,
 LED_DIAGNOSTIC_BLUE,LED_DIAGNOSTIC_MAGENTA};
 for(unsigned c=0;c<4;++c)for(unsigned n=1;n<=14;++n){
  initialize();assert(LEDController_SetFaultCode(&led_controller,colors[c],n)==HAL_OK);
  check_transaction();assert(wire[2].data[2]==colors[c] && !led_controller.animation_valid);
  for(unsigned ch=0;ch<3;++ch)if(colors[c]&(1U<<ch)){
   const uint8_t *p=wire[3].data+9*ch;
   assert(p[1]==n && p[0]==12 && p[7]==4 && p[8]==4 && !p[6]);
   /* Amber's green is dimmer; use red or blue for pulse threshold checks. */
   if(ch!=1){
    assert(pulse_count(p,2*(400*n+2000))==2*n);
    for(unsigned t=400*n;t<400*n+2000;t+=50)assert(sample(p,t)==0);
   }
  }
  clear_writes();assert(LEDController_SetAnimation(&led_controller,LED_ANIMATION_APP_IDLE)==HAL_OK);
  assert(writes==6); /* Diagnostic writes cannot poison the normal preset cache. */
 }
 initialize();LEDController_SetAnimation(&led_controller,LED_ANIMATION_FSBL_ERROR);
 assert(wire[3].data[1]==1 && pulse_count(wire[3].data,2800)==4);
 for(unsigned t=400;t<1400;t+=50)assert(sample(wire[3].data,t)==0);
 clear_writes();LEDController_SetAnimation(&led_controller,LED_ANIMATION_UNCAUGHT_EXCEPTION);
 assert(pulse_count(wire[3].data,1000)==5); /* Legacy fatal remains continuous 5 Hz. */
 for(unsigned failure=1;failure<=6;++failure){
  initialize();fail_at=failure;
  assert(LEDController_SetFaultCode(&led_controller,LED_DIAGNOSTIC_RED,7)==HAL_TIMEOUT);
  assert(writes==failure && !led_controller.busy && !led_controller.animation_valid);
 }
 initialize();clear_writes();reenter=1;
 assert(LEDController_SetFaultCode(&led_controller,LED_DIAGNOSTIC_AMBER,3)==HAL_OK);
 clear_writes();
 assert(LEDController_SetFaultCode(NULL,LED_DIAGNOSTIC_RED,1)==HAL_ERROR);
 assert(LEDController_SetFaultCode(&led_controller,LED_DIAGNOSTIC_RED,0)==HAL_ERROR);
 assert(LEDController_SetFaultCode(&led_controller,LED_DIAGNOSTIC_RED,15)==HAL_ERROR);
 assert(LEDController_SetFaultCode(&led_controller,2,1)==HAL_ERROR && !writes);
 static const app_boot_stage_t stages[]={APP_BOOT_HAL,APP_BOOT_CLOCKS,APP_BOOT_GPIO,
 APP_BOOT_I2C,APP_BOOT_DMA,APP_BOOT_DCMIPP,APP_BOOT_ETHERNET,APP_BOOT_UCPD,
 APP_BOOT_USB,APP_BOOT_SPI,APP_BOOT_I3C,APP_BOOT_XSPI,APP_BOOT_ISOLATION,APP_BOOT_PD_PREINIT,
 APP_BOOT_PARAMETERS,APP_BOOT_KERNEL,APP_BOOT_RUNNING,APP_BOOT_TX_POOL,APP_BOOT_TX_INIT,
 APP_BOOT_NX_POOL,APP_BOOT_NX_INIT,APP_BOOT_UX_POOL,APP_BOOT_UX_INIT,APP_BOOT_PD_POOL,APP_BOOT_PD_INIT};
 for(unsigned i=0;i<sizeof(stages)/sizeof(stages[0]);++i){
  initialize();app_boot_stage=stages[i];app_boot_report_failure();
  assert(app_boot_failed_stage==stages[i]);
  unsigned color=(i<14)?LED_DIAGNOSTIC_RED:(i<17)?LED_DIAGNOSTIC_BLUE:LED_DIAGNOSTIC_MAGENTA;
  unsigned channel=(color==LED_DIAGNOSTIC_BLUE)?2:0;
  assert(wire[2].data[2]==color && wire[3].data[9*channel+1]==(stages[i]&15));
 }
 puts("Counted fault waveforms, real dark pauses, I2C failures, cache recovery and every startup stage");
}
int main(int argc,char **argv){
 assert(argc==2);
 if(!strcmp(argv[1],"patterns"))patterns();
 else if(!strcmp(argv[1],"failures"))failures();
 else if(!strcmp(argv[1],"diagnostics"))diagnostics();
 else lifecycle();
 return 0;
}
'''


@unittest.skipUnless(shutil.which('cc'), 'native C compiler required')
class LedControllerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        temp = Path(cls.temp.name)
        (temp / 'main.h').write_text(HARDWARE)
        (temp / 'i2c.h').write_text('#include "main.h"\n')
        (temp / 'harness.c').write_text(HARNESS)
        cls.executable = temp / 'led'
        result = subprocess.run(
            ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-I', str(temp),
             '-I', str(ROOT / 'common/led'), '-I', str(ROOT / 'Appli/Libs/control'),
             str(ROOT / 'common/led/LEDController.c'), str(ROOT / 'Appli/Libs/control/status_led.c'),
             str(ROOT / 'Appli/Libs/control/boot_diagnostics.c'),
             str(temp / 'harness.c'), '-o', str(cls.executable)], capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stderr)

    def test_patterns_and_transitions(self):
        subprocess.run([str(self.executable), 'patterns'], check=True)

    def test_i2c_failures_and_concurrent_writer(self):
        subprocess.run([str(self.executable), 'failures'], check=True)

    def test_sensor_lifecycle_mapping(self):
        subprocess.run([str(self.executable), 'lifecycle'], check=True)

    def test_fault_bursts_pause_timing_and_startup_codes(self):
        subprocess.run([str(self.executable), 'diagnostics'], check=True)


if __name__ == '__main__':
    unittest.main()
