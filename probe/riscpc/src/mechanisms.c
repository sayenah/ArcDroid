/* ArcDroid authored mechanisms: expectations documented in expectations.md.
 * GPL-2.0-only. No firmware. */
#include "probe.h"
#include "cp15.h"
#include "iomd.h"
#include "sound.h"
#include "vidc20.h"
#include "keyboard.h"
#include "arcdroid_core.h"
#include "ide_probe.h"
#include <inttypes.h>
static unsigned checks,failures;
#define CHECK(name,expr) do {checks++;if(!(expr)){fprintf(stderr,"FAIL %s line=%d\n",name,__LINE__);failures++;}}while(0)
static void reg(unsigned n,uint32_t v) {cp15_write((n==8&&machine.cpu_model!=CPUModel_SA110?5:n)<<16,v);}
static uint32_t fsr(void) {return cp15_read(5<<16);}
static uint32_t far(void) {return cp15_read(6<<16);}
static void fault(const char *label,uint32_t addr,int write,uint32_t status) {
 arm.event=0;probe_translate(addr,write,0);
 CHECK(label,(arm.event&0x40)&&far()==addr&&fsr()==status);
 printf("fault %s far=%08x fsr=%02x event=%02x\n",label,far(),fsr(),arm.event);arm.event=0;
}
static void mmu_tests(void) {
 probe_machine_reset();
 reg(2,0x10000000);reg(3,1<<(3*2));
 ram00[0x100000/4]=0; /* data distinct from descriptor table */
 /* VA 0x001xxxxx maps PA 0x101xxxxx, domain 3, AP2. */
 ram00[1]=0x10100000|(3<<5)|(2<<10)|2;
 reg(1,0x31);memmode=0;
 CHECK("first-read",probe_translate(0x100120,0,0)==0x10100120);
 int misses=tlbs;CHECK("cached-read",probe_translate(0x100128,0,0)==0x10100128&&tlbs==misses);
 fault("cached-user-write",0x10012c,1,0x3d);
 memmode=1;CHECK("super-write",probe_translate(0x10012c,1,0)==0x1010012c);
 mem_write32(0x10012c,0x12345678);memmode=0;mem_write32(0x10012c,0xffffffff);
 CHECK("pointer-write-permissions",(arm.event&0x40)&&ram00[0x10012c/4]==0x12345678);arm.event=0;
 reg(3,3<<(3*2));CHECK("manager",probe_translate(0x10012c,1,0)==0x1010012c);
 reg(3,0);fault("domain-change",0x100120,0,0x39);
 reg(3,1<<(3*2));memmode=1;
 ram00[1]=0x10100000|(3<<5)|(1<<10)|2;reg(8,0);
 CHECK("super-only-fill",mem_read32(0x10012c)==0x12345678);
 CHECK("user-transfer",mem_user_read32(0x10012c)==0&&(arm.event&0x40)&&fsr()==0x3d&&far()==0x10012c);arm.event=0;
 updatemode(0x10);fault("privilege-change",0x100120,0,0x3d);
 updatemode(0x13);
 /* Coarse table at 0x10004000; small-page APs = 3,2,1,0. */
 ram00[2]=0x10004000|(3<<5)|1;
 ram00[0x4000/4]=0x10100000|(3<<4)|(2<<6)|(1<<8)|2;
 reg(8,0);memmode=0;
 CHECK("small-fill",probe_translate(0x200000,1,0)==0x10100000);
 fault("small-ap2-write",0x200400,1,0x3f);
 CHECK("small-ap2-read",probe_translate(0x200400,0,0)==0x10100400);
 fault("small-ap1-read",0x200800,0,0x3f);fault("small-ap0-read",0x200c00,0,0x3f);
 memmode=1;CHECK("small-super",probe_translate(0x200800,1,0)==0x10100800);
 fault("small-super-ap0",0x200c00,0,0x3f);
 reg(1,0x131);CHECK("system-ap0-read",probe_translate(0x200c00,0,0)==0x10100c00);fault("system-ap0-write",0x200c00,1,0x3f);
 reg(1,0x231);memmode=0;CHECK("rom-ap0-user-read",probe_translate(0x200c00,0,0)==0x10100c00);
 fault("rom-ap0-write",0x200c00,1,0x3f);reg(1,0x31);
 /* Descriptor writes are NOT TLB invalidation; explicit purge is required. */
 ram00[1]=0x10200000|(3<<5)|(3<<10)|2;reg(8,0);
 CHECK("explicit-invalidate",probe_translate(0x100120,1,0)==0x10200120);
 fault("translation-section",0x300000,0,0x05);
 ram00[3]=0x10004400|(3<<5)|1;reg(8,0);fault("translation-page",0x300000,0,0x37);
 reg(3,2<<(3*2));fault("reserved-domain-page",0x200000,0,0x3b);
}
static void more_mmu_tests(void) {
 probe_machine_reset();reg(2,0x10000000);reg(3,1u|(1u<<6));reg(1,0x31);
 ram00[4]=0x10005000|(3<<5)|1;
 for(unsigned i=0;i<16;i++)ram00[0x5000/4+i]=0x10100000|(3<<4)|(2<<6)|(1<<8)|1;
 memmode=0;CHECK("large-ap3",probe_translate(0x400123,1,0)==0x10100123);
 CHECK("large-ap2-read",probe_translate(0x404123,0,0)==0x10104123);
 fault("large-ap2-write",0x404123,1,0x3f);fault("large-ap1-user",0x408123,0,0x3f);
 memmode=1;CHECK("large-ap1-super",probe_translate(0x408123,1,0)==0x10108123);fault("large-ap0-super",0x40c123,0,0x3f);
 ram00[0x8000/4+4]=0x10200000|(3<<10)|2;reg(2,0x10008000);
 CHECK("ttbr-invalidate",probe_translate(0x400123,1,0)==0x10200123);
 reg(2,0x10000000);reg(3,1u|(1u<<6));reg(1,0x31);
 ram00[0]=0x00000c02;ram00[256]=0x10000c02;ram00[2]=0x10100000|(3<<5)|(2<<10)|2;reg(8,0);
 memmode=0;CHECK("guest-cache-fill",probe_translate(0x200120,0,0)==0x10100120);
 ram00[0x10120/4]=0;ram00[0x10000/4]=0xe5801000;ram00[0x10004/4]=0xeafffffe;
 rom[4]=0xe3a04001;rom[5]=0xeafffffe;
 updatemode(0x10);cpsr=16;arm.reg[16]=0xd0;arm.reg[15]=0x10010008;arm.reg[0]=0x200120;arm.reg[1]=0xfeedface;
 probe_batch_instructions=1;arm_exec();
 CHECK("guest-data-abort",arm.mode==0x17&&PC==0x10&&arm.reg[14]==0x10010008&&arm.spsr[7]==0xd0&&far()==0x200120&&fsr()==0x3d);
 CHECK("guest-store-refused",ram00[0x100120/4]==0);
 printf("guest abort mode=%02x pc=%08x lr=%08x spsr=%08x far=%08x fsr=%02x\n",arm.mode,PC,arm.reg[14],arm.spsr[7],far(),fsr());
 /* Cache first subpage instruction fetch, deny second subpage of same 4KiB page. */
 probe_machine_reset();reg(2,0x10000000);reg(3,1u);reg(1,0x31);
 ram00[0]=0xc02;ram00[256]=0x10004001;
 ram00[0x4000/4]=0x10010000|(3<<4)|(1<<6)|(3<<8)|(3<<10)|2;
 ram00[0x10000/4]=0xe1a00000;ram00[0x10400/4]=0xe3a05077;
 updatemode(0x10);arm.reg[16]=0xd0;cpsr=16;arm.reg[15]=0x10000008;
 probe_batch_instructions=1;arm_exec();CHECK("prefetch-cached-first",PC==0x10000004);
 reg(6,0x11223344);reg(5,0x55);uint32_t oldfar=far(),oldfsr=fsr();
 arm.reg[15]=0x10000408;arm_exec();
 CHECK("prefetch-subpage-fault",arm.mode==0x17&&PC==0x0c&&arm.reg[5]==0&&far()==oldfar&&fsr()==oldfsr);
}

static void cpu_tests(void) {
 probe_machine_reset();probe_authored_rom();probe_batch_instructions=100;arm_exec();
 CHECK("ram-store",ram00[0]==arm.reg[1]&&arm.reg[1]==33);
 printf("cpu rom pc=%08x r1=%08x ram=%08x instructions=%"PRIu64"\n",PC,arm.reg[1],ram00[0],probe_instructions);
 probe_machine_reset();
 reg(1,0x30);updatemode(0x13);arm.reg[16]=0xd3;cpsr=16;
 ram00[0]=0xe3a02007;ram00[1]=0xeafffffe;arm.reg[15]=0x10000008;probe_batch_instructions=2;arm_exec();
 CHECK("ram-exec",arm.reg[2]==7);
 mem_write32(0x10000000,0xe3a02009);arm.reg[15]=0x10000008;arm_exec();CHECK("self-modifying",arm.reg[2]==9);
 printf("cpu ram pc=%08x r2=%08x\n",PC,arm.reg[2]);
}
static void scheduler_tests(void) {
 probe_machine_reset();probe_authored_rom();
 iomd_write(0x40,10);iomd_write(0x44,0);iomd_write(0x48,0);
 probe_advance(4500);CHECK("timer-before",!(iomd.irqa.status&IOMD_IRQA_TIMER_0));
 probe_advance(5000);CHECK("timer-deadline",iomd.irqa.status&IOMD_IRQA_TIMER_0);
 CHECK("instruction-credit",probe_instructions==500);
 iomd.irqa.status=0;iomd.irqa.mask=IOMD_IRQA_TIMER_0;updateirqs();probe_idle=1;
 uint64_t old=probe_instructions;probe_advance(10000);
 CHECK("idle",probe_instructions==old&&probe_now==10000&&!probe_idle);
 printf("scheduler time=%"PRIu64" instructions=%"PRIu64" counter=%d irq=%02x\n",probe_now,probe_instructions,iomd.t0.counter,arm.event);
}
static void video_tests(void) {
 const unsigned widths[]={320,640,800},heights[]={256,480,600};
 for(unsigned mode=0;mode<3;mode++)for(unsigned rate=50;rate<=60;rate+=10) {
  probe_machine_reset();probe_authored_rom();unsigned total=rate==50?600:500;
  writevidc20(0xe0000060);writevidc20(0xd0000000|(machine.model==Model_A7000?0x203:0));writevidc20(0x80000000|792);writevidc20(0x90000000|(total-2));
  writevidc20(0x83000000);writevidc20(0x84000000|widths[mode]);writevidc20(0x93000000);writevidc20(0x94000000|heights[mode]);
  writevidc20(0x10000000);for(unsigned i=0;i<256;i++)writevidc20(i*0x010101);
  uint32_t base=machine.model==Model_A7000?0x10000000:0x02000000;
  iomd.vidcr=0x20;iomd.vidinit=base;iomd.vidstart=base;iomd.vidend=base+0x1ff800;
  mem_write32(base,0x03020100);drawscr();unsigned w,h;const uint32_t *pixels=probe_video_pixels(&w,&h);
  CHECK("geometry",w==widths[mode]&&h==heights[mode]);CHECK("palette",pixels[0]==0xff000000&&pixels[3]==0xff030303);
  uint64_t expected=rate==50?20000000:16666666;CHECK("mode-period",probe_video_period()==expected);
  mem_write32(base,0x04040404);drawscr();pixels=probe_video_pixels(&w,&h);CHECK("DMA-dirty",pixels[0]==0xff040404);
  printf("video %ux%u %uHz period=%"PRIu64" hash=%016"PRIx64"\n",w,h,rate,probe_video_period(),probe_hash(UINT64_C(1469598103934665603),pixels,(size_t)w*h*4));
 }
 /* Published TV envelope: 512 pixels * 312 lines / 8 MHz = 19.968 ms.
  * ARM7500's 32 MHz reference uses FSYN 3/4 to obtain the same 24 MHz. */
 writevidc20(0xe0000068);writevidc20(0xd0000000|(machine.model==Model_A7000?0x203:0));
 writevidc20(0x800001f8);writevidc20(0x90000136);
 writevidc20(0x93000000);writevidc20(0x94000100);
 CHECK("TV-published-line-total",probe_video_period()==19968000);
 CHECK("TV-published-blank-lines",probe_video_blank_ns()==3584000);
}
/* Video memory/pitch boundaries are independent of ordinary mode fixtures. */
static void video_boundary_tests(void) {
 probe_machine_reset();probe_authored_rom();
 writevidc20(0xe0000060);writevidc20(0xd0000000|(machine.model==Model_A7000?0x203:0));
 writevidc20(0x83000000);writevidc20(0x84000142);writevidc20(0x93000000);writevidc20(0x94000101);
 writevidc20(0x10000000);for(unsigned i=0;i<256;i++)writevidc20(i*0x010101);
 uint32_t base=machine.model==Model_A7000?0x14c00000:0x02000000;
 iomd.vidcr=0x20;iomd.vidstart=iomd.vidinit=base;iomd.vidend=base+0x100000;
 mem_write32(base,0x03020100);drawscr();unsigned w,h;const uint32_t *p=probe_video_pixels(&w,&h);
 CHECK("odd-pitch",w==322&&h==257&&probe_video_pitch()==352*4);
 CHECK("bank-1-high-offset",p[0]==0xff000000&&p[3]==0xff030303);
 mem_write32(base,0x04040404);drawscr();p=probe_video_pixels(&w,&h);
 CHECK("bank-1-dirty",p[0]==0xff040404);
 printf("video boundary %ux%u pitch=%zu hash=%016"PRIx64"\n",w,h,probe_video_pitch(),probe_video_hash());
}
static void audio_tests(void) {
 probe_machine_reset();probe_authored_rom();
 for(unsigned i=0;i<1024;i++)ram00[i]=0x1234fedc;
 soundaddr[0]=soundaddr[2]=0x10000000;soundaddr[1]=soundaddr[3]=0x10000ff0;
 sound_samplefreq_change(48000);iomd_write(0x190,0xa0);
 probe_audio_count=0;probe_advance(10000000);
 CHECK("stereo-count",probe_audio_count==481);
 CHECK("stereo-values",probe_audio[0]==(int16_t)0xfedc&&probe_audio[1]==0x1234);
 CHECK("DMA-interrupt",iomd.irqdma.status&IOMD_IRQDMA_SOUND_0);
 sound_samplefreq_change(24000);probe_audio_count=0;probe_advance(20000000);CHECK("rate-change-output",probe_audio_count==480);
 CHECK("bounded-audio",probe_underruns==0&&probe_overruns==0);
 printf("audio samples=%u rate=%u hash=%016"PRIx64"\n",probe_audio_count,probe_sound_rate,probe_hash(UINT64_C(1469598103934665603),probe_audio,(size_t)probe_audio_count*4));
}
/* Distinct source samples make resampling/rate-transition errors observable. */
static void patterned_audio_test(void) {
 probe_machine_reset();probe_authored_rom();probe_idle=1;
 for(unsigned i=0;i<1024;i++)ram00[i]=((1000+i)<<16)|(uint16_t)(-1000+(int)i);
 soundaddr[0]=soundaddr[2]=0x10000000;soundaddr[1]=soundaddr[3]=0x10000ff0;
 sound_samplefreq_change(48000);iomd_write(0x190,0xa0);probe_advance(10000000);
 CHECK("pattern-count",probe_audio_count==481);
 CHECK("pattern-start",probe_audio[0]==-1000&&probe_audio[1]==1000);
 CHECK("pattern-before-change",probe_audio[960]==-520&&probe_audio[961]==1480);
 sound_samplefreq_change(24000);probe_audio_count=0;probe_advance(20000000);
 CHECK("pattern-change-count",probe_audio_count==480);
 CHECK("pattern-change-start",probe_audio[0]==-519&&probe_audio[1]==1481);
 CHECK("pattern-change-end",probe_audio[958]==-280&&probe_audio[959]==1720);
 printf("audio pattern first=%d/%d last=%d/%d samples=%u hash=%016"PRIx64"\n",probe_audio[0],probe_audio[1],probe_audio[958],probe_audio[959],probe_audio_count,probe_hash(UINT64_C(1469598103934665603),probe_audio,probe_audio_count*4));
}
static void input_tests(void) {
 probe_machine_reset();probe_authored_rom();mouse_mouse_move_relative(10,7);mouse_mouse_press(1);mouse_mouse_release(1);
 CHECK("quadrature-no-repeated-delta",iomd.mousex==10&&iomd.mousey==(uint16_t)-7);
 uint8_t sc[2]={0x29,0};keyboard_key_press(sc);probe_advance(50000);CHECK("PS2-keydown",keyboard_data_read()==0x29);
 keyboard_key_release(sc);probe_advance(100000);CHECK("PS2-break",keyboard_data_read()==0xf0);probe_advance(150000);CHECK("PS2-keyup",keyboard_data_read()==0x29);
 probe_machine_reset();CHECK("held-reset",mouse_buttons_get()==0);
}
static void contention_test(void) {
 probe_machine_reset();probe_workload_rom();probe_advance(25000000);
 CHECK("guest-DMA-counter",probe_instructions==2500000&&arm.reg[3]==416481&&arm.reg[4]==((416481*4)&4092));
 CHECK("guest-sound-write",ram00[0x20000/4]==415745);
 uint32_t *screen=machine.model==Model_A7000?ram00:vram;
 CHECK("guest-video-write",screen[0]==415745);
 CHECK("guest-DMA-audio",probe_audio_count==1201&&probe_audio[probe_audio_count*2-2]!=0);
 drawscr();unsigned w,h;const uint32_t *pixels=probe_video_pixels(&w,&h);
 CHECK("guest-DMA-pixel",w==320&&h==256&&pixels[0]==(0xff000000|((415745&255)*0x010101)));
 printf("contention counter=%u screen=%08x audio=%016"PRIx64"\n",arm.reg[3],screen[0],probe_hash(UINT64_C(1469598103934665603),probe_audio,(size_t)probe_audio_count*4));
}
static void cold_input_advance(uint64_t target) {
 while(probe_now<target){uint64_t stop=probe_now+10000000;if(stop>target)stop=target;probe_audio_count=0;probe_advance(stop);}
}
static void cold_input_tests(void) {
 probe_cmos_configure("none","none",1);probe_machine_reset();probe_authored_rom();
 uint8_t cmos[256];probe_cmos_snapshot(cmos);unsigned sum=0;for(unsigned i=0;i<256;i++)sum+=cmos[i];
 CHECK("factory-truly-empty",sum==0);
 if(machine.model!=Model_A7000) {
  CHECK("board-reset-gate-cold",mem_read8(0x03310000)==0x70);
  mem_write8(0x03310000,0xff);CHECK("board-reset-gate-byte",mem_read8(0x03310000)==0xf0);
  mem_write32(0x03310000,0);CHECK("board-reset-gate-word",mem_read32(0x03310000)==0x70);
  mouse_mouse_press(1);mem_write8(0x03310000,0x80);CHECK("board-reset-gate-inputs",mem_read8(0x03310000)==0xb0);
  probe_machine_reset();probe_authored_rom();CHECK("board-reset-gate-reset",mem_read8(0x03310000)==0x70);
 }
 /* Any ordinary held switch, not a Delete-specific test or intervention. */
 uint8_t sc[2]={0x29,0};keyboard_key_press(sc);keyboard_data_write(0xff);
 probe_advance(40000);CHECK("keyboard-reset-ack",keyboard_data_read()==0xfa);
 probe_advance(5040000);CHECK("keyboard-reset-por",keyboard_data_read()==0xaa);
 probe_advance(5090000);CHECK("keyboard-reset-held-make",keyboard_data_read()==0x29);
 keyboard_data_write(0xf3);probe_advance(5100000);CHECK("typematic-command-ack",keyboard_data_read()==0xfa);
 keyboard_data_write(0x7f);probe_advance(5110000);CHECK("typematic-parameter-ack",keyboard_data_read()==0xfa);
 CHECK("typematic-delay",probe_keyboard_due==1005100000);
 cold_input_advance(1005100000);CHECK("typematic-no-early-make",!(keyboard_status_read()&0x20));
 probe_advance(1005150000);CHECK("typematic-virtual-make",keyboard_data_read()==0x29);
 CHECK("typematic-period",probe_keyboard_due==1505100000);
 keyboard_key_release(sc);CHECK("typematic-release",probe_keyboard_due==UINT64_MAX);
 probe_machine_reset();CHECK("typematic-machine-reset",probe_keyboard_due==UINT64_MAX);
 probe_cmos_snapshot(cmos);sum=0;for(unsigned i=0;i<256;i++)sum+=cmos[i];CHECK("factory-reset-empty",sum==0);
 printf("cold CMOS empty; keyboard ACK/POR/held make; F3 7f delay=1000ms period=500ms release/reset\n");
 probe_cmos_configure("none","none",0);
 /* R10: hashed authored baseline; reset must preserve guest drive counts. */
 memset(cmos,0,sizeof(cmos));cmos[199]=0x41;
 const char *path="mechanism-cmos-owned.bin";char digest[41];
 arc_sha1_hex(cmos,sizeof(cmos),digest);
 FILE *f=fopen(path,"wb");if(!f||fwrite(cmos,1,sizeof(cmos),f)!=sizeof(cmos)||fclose(f))fatal("Authored CMOS fixture write");
 int saved_ide=probe_ide_enabled;probe_ide_enabled=0;
 probe_cmos_configure(path,digest,0);probe_machine_reset();probe_cmos_snapshot(cmos);
 CHECK("cold-reset-preserves-guest-drive-counts",cmos[199]==0x41);
 probe_machine_reset();probe_cmos_snapshot(cmos);
 CHECK("repeated-reset-preserves-guest-drive-counts",cmos[199]==0x41);
 if(remove(path))fatal("Authored CMOS fixture cleanup");
 probe_ide_enabled=saved_ide;probe_cmos_configure("none","none",0);
}
static void mode_observer_test(void) {
 probe_machine_reset();reg(2,0x10000000);reg(3,1);reg(1,0x31);
 ram00[2]=0x10100c02;
 const uint32_t selector[]={1,640,480,3,60,UINT32_MAX};
 memcpy(ram00+0x100000/4,selector,sizeof(selector));
 arm.reg[0]=0;arm.reg[1]=0x200000;arm.reg[2]=123;arm.event=0x40;
 uint32_t regs[16];memcpy(regs,arm.reg,sizeof(regs));
 uint32_t saved_far=far(),saved_fsr=fsr();int saved_tlbs=tlbs;
 probe_mode_log=tmpfile();CHECK("mode-observer-output",probe_mode_log!=NULL);
 if(!probe_mode_log)return;
 probe_mode_reset();probe_observe_swi(0x20065);
 arm.reg[1]=0x300000;probe_observe_swi(0x65);arm.reg[1]=regs[1];
 CHECK("mode-observer-passive",!memcmp(regs,arm.reg,sizeof(regs))&&arm.event==0x40&&tlbs==saved_tlbs&&far()==saved_far&&fsr()==saved_fsr);
 rewind(probe_mode_log);char line[256];
 CHECK("mode-observer-selector",fgets(line,sizeof(line),probe_mode_log)&&strstr(line,",6,00000001,00000280,000001e0,00000003,0000003c,ffffffff"));
 CHECK("mode-observer-invalid",fgets(line,sizeof(line),probe_mode_log)&&strstr(line,",0\n"));
 if(mem_vrammask) {
  /* A guest may keep its translation table in fitted VRAM. */
  CHECK("mode-observer-seek",fseek(probe_mode_log,0,SEEK_END)==0);
  reg(2,0x02004000);vram[0x4000/4+2]=0x10100c02;
  probe_observe_swi(0x65);rewind(probe_mode_log);
  CHECK("mode-observer-vram-table",fgets(line,sizeof(line),probe_mode_log)&&fgets(line,sizeof(line),probe_mode_log)&&fgets(line,sizeof(line),probe_mode_log)&&strstr(line,",6,00000001,00000280,000001e0,00000003,0000003c,ffffffff"));
 }
 fclose(probe_mode_log);probe_mode_log=NULL;
 printf("mode observer valid/invalid selector, no guest/TLB/fault side effects\n");
}
static void ps2_mouse_test(void) {
 probe_machine_close();probe_machine_init(Model_A7000,35);probe_authored_rom();
 mouse_data_write(0xf4);probe_advance(20000);CHECK("mouse-enable-ack",mouse_data_read()==0xfa);
 mouse_mouse_move_relative(10,7);probe_advance(40000);CHECK("mouse-packet-status",mouse_data_read()==0x28);
 probe_advance(60000);CHECK("mouse-packet-x",mouse_data_read()==10);
 probe_advance(80000);CHECK("mouse-packet-y",mouse_data_read()==(uint8_t)-7);
 mouse_mouse_press(1);probe_advance(100000);CHECK("mouse-button-packet",mouse_data_read()==9);
 probe_advance(120000);CHECK("mouse-button-zero-x",mouse_data_read()==0);
 probe_advance(140000);CHECK("mouse-button-zero-y",mouse_data_read()==0);
 probe_machine_reset();CHECK("PS2-reset",mouse_buttons_get()==0&&mouse_status_read()==0);
 printf("mouse PS2 make/move/buttons/reset checked\n");
}
int main(void) {
 const Model models[]={Model_RPCARM610,Model_RPCARM710,Model_RPCSA110,Model_A7000};
 for(unsigned i=0;i<4;i++) {
  printf("model=%u\n",i);probe_machine_init(models[i],100);
  mmu_tests();more_mmu_tests();cpu_tests();scheduler_tests();video_tests();video_boundary_tests();audio_tests();patterned_audio_test();input_tests();contention_test();
  mode_observer_test();cold_input_tests();if(i==3)ps2_mouse_test();probe_machine_close();
 }
 printf("checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
