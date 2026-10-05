/* ArcDroid deterministic trial machine. GPL-2.0-only. */
#include "probe.h"
#include "cp15.h"
#include "iomd.h"
#include "cmos.h"
#include "vidc20.h"
#include "keyboard.h"
#include "i8042.h"
#include "ide.h"
#include "fdc.h"
#include "disc.h"
#include "media.h"
#include <stdarg.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif
Machine machine;
FILE *probe_boot_log;
FILE *probe_mode_log;
Config config;
int drawscre, quited;
int probe_can_escape, probe_failed, probe_idle;
jmp_buf probe_escape;
char probe_error[256];
uint64_t probe_now;
unsigned probe_mips;
static uint64_t cpu_fraction, next_keyboard, next_mouse, next_fdc, next_ide, next_poll;
static int old_k, old_m, old_f, old_i;
static unsigned poll_period;
uint64_t probe_wall_ns(void) {
#ifdef _WIN32
 LARGE_INTEGER c,f; QueryPerformanceCounter(&c);QueryPerformanceFrequency(&f);
 return (uint64_t)(c.QuadPart/f.QuadPart)*1000000000 + (uint64_t)(c.QuadPart%f.QuadPart)*1000000000/f.QuadPart;
#else
 struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);return (uint64_t)ts.tv_sec*1000000000+ts.tv_nsec;
#endif
}
uint64_t probe_hash(uint64_t h,const void *data,size_t n) {
 const uint8_t *p=data;for(size_t i=0;i<n;i++) {h^=p[i];h*=UINT64_C(1099511628211);}return h;
}
void fatal(const char *fmt,...) {
 va_list ap;va_start(ap,fmt);vsnprintf(probe_error,sizeof(probe_error),fmt,ap);va_end(ap);
 probe_failed=1;
 if(probe_can_escape) longjmp(probe_escape,1);
 /* Only command-line mechanism tests call without a libretro boundary. */
 fprintf(stderr,"probe mechanism fatal: %s\n",probe_error);abort();
}
void error(const char *fmt,...) { (void)fmt;fatal("Upstream error"); }
void rpclog(const char *fmt,...) { (void)fmt; /* Never log guest names or strings. */ }
uint64_t rpcemu_nsec_timer_ticks(void) {return probe_now;}
void rpcemu_idle(void) {probe_idle=1;}
void rpcemu_idle_process_events(void) {}
void rpcemu_move_host_mouse(uint16_t x,uint16_t y) {(void)x;(void)y;}
void rpcemu_video_update(const uint32_t *p,int x,int y,int yl,int yh,int d,int hx,int hy) {(void)p;(void)x;(void)y;(void)yl;(void)yh;(void)d;(void)hx;(void)hy;}
void vidcstartthread(void) {}
void vidcendthread(void) {}
void vidcwakeupthread(void) {vidcthread();}
int vidctrymutex(void) {return 1;}
void vidcreleasemutex(void) {}
const char *rpcemu_get_datadir(void) {return "";}
static void timer_sync(int current,int *old,uint64_t *due,uint64_t unit) {
 if(current!=*old) {*due=current>0?probe_now+(uint64_t)current*unit:UINT64_MAX;*old=current;}
}
void probe_machine_reset(void) {
 probe_now=probe_instructions=cpu_fraction=0;probe_idle=probe_failed=0;probe_error[0]=0;
 probe_mode_reset();
 mem_reset(config.mem_size,config.vram_size);memset(vram,0,8*1024*1024);
 cp15_reset(machine.cpu_model);memset(&arm,0,sizeof(arm));arm_reset(machine.cpu_model);
 keyboard_reset();iomd_reset(machine.iomd_type);reseti2c(machine.i2c_devices);cmos_init();cmos_reset();
 superio_reset(machine.super_type);i8042_reset();fdc_reset();resetide();probe_media_reset();
 closevideo();initvideo();probe_sound_reset();
 next_keyboard=next_mouse=next_fdc=next_ide=UINT64_MAX;next_poll=32000;
 old_k=old_m=old_f=old_i=0;poll_period=32;
}
void probe_machine_init(Model m,unsigned mips) {
 machine.model=m;machine.cpu_model=m==Model_RPCSA110?CPUModel_SA110:m==Model_RPCARM610?CPUModel_ARM610:m==Model_A7000?CPUModel_ARM7500:CPUModel_ARM710;
 machine.iomd_type=m==Model_A7000?IOMDType_ARM7500:IOMDType_IOMD;
 machine.super_type=SuperIOType_FDC37C665GT;machine.i2c_devices=I2C_PCF8583;
 memset(&config,0,sizeof(config));config.mem_size=64;config.vram_size=m==Model_A7000?0:2;config.soundenabled=1;config.cpu_idle=1;
 probe_mips=mips;mem_init();arm_init();probe_machine_reset();
}
void probe_machine_close(void) {
 keyboard_reset();fdc_reset();resetide();
 next_keyboard=next_mouse=next_fdc=next_ide=UINT64_MAX;
 closevideo();free(rom);free(vram);free(ram00);free(ram01);free(ram1);
 rom=vram=ram00=ram01=ram1=NULL;
}
/* CPU/device delivery boundary: at most 1us of instructions, no host state.
 * Fractional instruction credit survives event splits; stable coincident order:
 * timers, keyboard, mouse, FDC, IDE, media, sound, then video raster.
 * Pending IRQs wake at entry; IRQs raised by any device wake at that edge. */
void probe_advance(uint64_t target) {
 probe_sound_step();
 while(probe_now<target) {
  probe_keyboard_step();
  if(arm.event)probe_idle=0;
  if(poll_period!=probe_disc_period_us) {poll_period=probe_disc_period_us;next_poll=probe_now+(uint64_t)poll_period*1000;}
  timer_sync(kcallback,&old_k,&next_keyboard,2500);
  timer_sync(mcallback,&old_m,&next_mouse,1000);
  timer_sync(fdccallback,&old_f,&next_fdc,100);
  timer_sync(idecallback,&old_i,&next_ide,1000);
  uint64_t stop=probe_idle?target:probe_now+1000;
  uint64_t phase=probe_now%500;
  if(iomd.t0.in_latch) {uint64_t d=probe_now+(uint64_t)(iomd.t0.counter+1)*500-phase;if(d<stop)stop=d;}
  if(iomd.t1.in_latch) {uint64_t d=probe_now+(uint64_t)(iomd.t1.counter+1)*500-phase;if(d<stop)stop=d;}
  uint64_t ds[]={target,next_keyboard,next_mouse,next_fdc,next_ide,next_poll,probe_sound_due,probe_keyboard_due,probe_video_due};
  for(unsigned i=0;i<sizeof(ds)/sizeof(ds[0]);i++) if(ds[i]<stop)stop=ds[i];
  if(stop<probe_now)fatal("Past scheduler deadline");
  uint64_t credit=(stop-probe_now)*probe_mips+cpu_fraction;
  if(!probe_idle) {probe_batch_instructions=(int)(credit/1000);if(probe_batch_instructions)arm_exec();}
  cpu_fraction=credit%1000;probe_now=stop;
  gentimerirq(probe_now);
  if(arm.event)probe_idle=0;
  if(probe_now==next_keyboard) {kcallback=old_k=0;next_keyboard=UINT64_MAX;keyboard_callback_rpcemu();}
  if(probe_now==next_mouse) {mcallback=old_m=0;next_mouse=UINT64_MAX;mouse_ps2_callback();}
  if(probe_now==next_fdc) {fdccallback=old_f=0;next_fdc=UINT64_MAX;fdc_callback();}
  if(probe_now==next_ide) {idecallback=old_i=0;next_ide=UINT64_MAX;callbackide();}
  if(probe_now==next_poll) {disc_poll();next_poll+=(uint64_t)poll_period*1000;}
  probe_sound_step();
  if(probe_now==probe_video_due)probe_video_step();
  if(arm.event)probe_idle=0;
 }
 probe_sound_flush(target);
}
/* Authored ARMv3 ROM: RAM-visible counter and border, no copyrighted content. */
void probe_authored_rom(void) {
 memset(rom,0,ROMSIZE);
 uint32_t words[]={0xe3a00201,0xe3a01000,0xe2811001,0xe5801000,0xeafffffc};
 memcpy(rom,words,sizeof(words));
}
/* A second authored ROM workload exercises guest framebuffer and sound writes.
 * Literal loads/stores all go through normal CPU memory/device interfaces. */
static unsigned authored_at;
static void authored_write(uint32_t address,uint32_t value) {
 uint32_t w[]={0xe59f0008,0xe59f1008,0xe5801000,0xea000001,address,value};
 memcpy(rom+authored_at,w,sizeof(w));authored_at+=6;
}
static void authored_load(unsigned r,uint32_t value) {
 rom[authored_at++]=0xe59f0000|(r<<12);rom[authored_at++]=0xea000000;rom[authored_at++]=value;
}
void probe_workload_rom(void) {
 memset(rom,0,ROMSIZE);authored_at=0;
 uint32_t base=machine.model==Model_A7000?0x10000000:0x02000000;
 const uint32_t vidc[]={0xe0000060,0xd0000000|(machine.model==Model_A7000?0x203:0),0x80000318,0x900001f3,0x83000000,0x84000140,0x93000000,0x94000100,0x10000000};
 for(unsigned i=0;i<sizeof(vidc)/sizeof(vidc[0]);i++)authored_write(0x03400000,vidc[i]);
 for(unsigned i=0;i<256;i++)authored_write(0x03400000,i*0x010101);
 authored_write(0x030001d8,base);authored_write(0x030001dc,base);authored_write(0x030001d4,base+0x1ff800);authored_write(0x030001e0,0x20);
 authored_write(0x03400000,0xb0000002);authored_write(0x03400000,0xb1000002);
 authored_write(0x03000180,0x10020000);authored_write(0x03000184,0x10020ff0);authored_write(0x03000188,0x10020000);authored_write(0x0300018c,0x10020ff0);authored_write(0x03000190,0xa0);
 authored_load(0,base);authored_load(2,0x10020000);authored_load(3,0);authored_load(4,0);authored_load(5,0xffc);
 unsigned loop=authored_at;
 rom[authored_at++]=0xe2833001; /* ADD counter */
 rom[authored_at++]=0xe7803004; /* STR into displayed memory */
 rom[authored_at++]=0xe7823004; /* STR into sound DMA memory */
 rom[authored_at++]=0xe2844004; /* ADD offset */
 rom[authored_at++]=0xe0044005; /* AND offset with 4092 */
 rom[authored_at]=(0xea000000|((loop-authored_at-2)&0xffffff));
}

uint64_t probe_video_hash(void) {
 unsigned w,h;const uint32_t *p=probe_video_pixels(&w,&h);
 if(!p)return 0;uint64_t hash=UINT64_C(1469598103934665603);
 for(unsigned y=0;y<h;y++)hash=probe_hash(hash,(const uint8_t *)p+y*probe_video_pitch(),w*4);
 return hash;
}
