/* ArcDroid R13 authored scanout mechanisms. GPL-2.0-only.
 * Expected colours/coordinates/times are declared in expectations.md. */
#include "probe.h"
#include "vidc20.h"
#include "iomd.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,failures;
#define CHECK(name,x) do {checks++;if(!(x)){failures++;fprintf(stderr,"FAIL %s line %d\n",name,__LINE__);}}while(0)
/* Synthetic ARM guest LDR/LDR/STR; every device write goes through CPU bus. */
static void guest_write(uint32_t address,uint32_t value)
{
 uint32_t code[]={0xe59f0008,0xe59f1008,0xe5801000,0xeafffffe,address,value};
 memcpy(rom,code,sizeof(code));arm.reg[15]=0x0c00000b;probe_batch_instructions=3;arm_exec();
}
static void v(uint32_t value){guest_write(0x03400000,value);}
static uint32_t base;
static void setup(unsigned depth)
{
 probe_machine_reset();base=machine.model==Model_A7000?0x10000000:0x02000000;
 v(0xe0000000|(depth<<5));v(0xd0000000|(machine.model==Model_A7000?0x203:0));
 v(0x80000000|760);v(0x90000000|38);v(0x83000000);v(0x84000020);
 v(0x93000002);v(0x9400000a);v(0x96000000);v(0x97000000);
 v(0x10000000);for(unsigned i=0;i<256;i++)v(i*0x010101);
 guest_write(0x030001d8,base);guest_write(0x030001dc,base);
 guest_write(0x030001d4,base+0x100000);guest_write(0x030001e0,0x20);
 for(unsigned i=0;i<8192;i+=4)mem_write32(base+i,0x01010101);
 /* No guest instruction during advances; bus writes above have already run. */
 probe_idle=1;
}
static void advance(uint64_t ns){probe_idle=1;probe_advance(ns);}
static void finish(void){while(!probe_video_frames)advance(probe_video_due);}
static uint32_t pixel(unsigned x,unsigned y)
{
 unsigned w,h;const uint32_t *p=probe_video_pixels(&w,&h);
 CHECK("scan-geometry",w==32&&h==8);return p[y*(probe_video_pitch()/4)+x];
}
static void split_tests(void)
{
 setup(3);v(0x10000001);v(0x000000ff);probe_video_start();
 advance(1120000);CHECK("split-before-frame",probe_video_frames==0);
 v(0x10000001);v(0x00ff0000);finish();
 for(unsigned y=0;y<8;y++)CHECK("palette-split",pixel(0,y)==(y<4?0xffff0000:0xff0000ff));
 CHECK("raster-period",probe_now==1280000&&probe_video_last_period==1280000);
 printf("raster palette time=%"PRIu64" upper=%08x lower=%08x hash=%016"PRIx64"\n",probe_now,pixel(0,0),pixel(0,7),probe_video_hash());
 setup(3);guest_write(0x030001e0,0);v(0x400000ff);probe_video_start();advance(1120000);v(0x4000ff00);finish();
 for(unsigned y=0;y<8;y++)CHECK("border-split",pixel(0,y)==(y<4?0xffff0000:0xff00ff00));
 setup(3);for(unsigned i=0;i<4096;i+=4)mem_write32(base+4096+i,0x02020202);
 probe_video_start();advance(1120000);guest_write(0x030001d0,base+4096);finish();
 for(unsigned y=0;y<8;y++)CHECK("current-pointer-split",pixel(0,y)==(y<4?0xff010101:0xff020202));
 /* Change the end/start ring while current is on the old buffer. Two
  * quads remain in the last old line; wrap then begins the new buffer. */
 setup(3);for(unsigned i=0;i<4096;i+=4)mem_write32(base+4096+i,0x02020202);
 probe_video_start();advance(1120000);
 guest_write(0x030001d8,base+4096);
 guest_write(0x030001d4,base+160-(machine.model==Model_A7000?16:2048));
 finish();CHECK("ring-last-old-line",pixel(0,4)==0xff010101);
 CHECK("ring-start-next-line",pixel(0,5)==0xff020202&&pixel(0,7)==0xff020202);
 /* INIT written during active lines must wait for blank; frame 2 uses it. */
 setup(3);for(unsigned i=0;i<4096;i+=4)mem_write32(base+4096+i,0x02020202);
 probe_video_start();advance(1120000);guest_write(0x030001dc,base+4096);finish();
 CHECK("init-deferred",pixel(0,7)==0xff010101);
 uint64_t frame=probe_video_frames;while(probe_video_frames==frame)advance(probe_video_due);
 CHECK("init-next-frame",pixel(0,0)==0xff020202&&pixel(0,7)==0xff020202);
 /* Cursor palette changes and position changes sampled at the same line. */
 setup(3);for(unsigned i=0;i<64;i+=4)mem_write32(base+0x10000+i,0x55555555);
 guest_write(0x030001c4,base+0x10000);v(0x96000002);v(0x9700000a);v(0x86000000);v(0x500000ff);
 probe_video_start();advance(1120000);v(0x5000ff00);v(0x86000004);finish();
 CHECK("cursor-old",pixel(0,0)==0xffff0000);CHECK("cursor-new",pixel(4,7)==0xff00ff00&&pixel(0,7)==0xff010101);
 /* Current mode depth changes: same packed bytes interpreted per line. */
 setup(3);probe_video_start();advance(1120000);v(0xe0000040);finish();
 CHECK("depth-old",pixel(1,0)==0xff010101);CHECK("depth-new",pixel(1,7)==0xff000000);
}
static void timing_tests(void)
{
 setup(3);probe_video_start();advance(1120000);
 uint64_t pending=probe_video_due;v(0x80000000|1528);
 CHECK("timing-inflight-edge",probe_video_due==pending&&pending==1152000);
 advance(pending);CHECK("timing-next-edge",probe_video_due==1216000);
 finish();CHECK("timing-changed-frame",probe_now==1408000);
 setup(3);v(0x400000ff);probe_video_start();advance(1120000);
 guest_write(0x030001e0,0);finish();
 CHECK("enable-before",pixel(0,3)==0xff010101);
 CHECK("disable-after",pixel(0,4)==0xffff0000);
 setup(3);probe_video_start();advance(1120000);v(0x83000004);finish();
 CHECK("mode-horizontal-before",pixel(0,3)==0xff010101);
 CHECK("mode-horizontal-after",pixel(0,4)==0xff000000&&pixel(4,4)==0xff010101);
 /* Delaying vertical start makes the next physical raster border; its
  * colour is the current one, not the frame's initialization colour. */
 setup(3);probe_video_start();advance(1120000);
 v(0x4000ff00);v(0x93000007);finish();
 uint32_t blank=pixel(0,4),resume=pixel(0,5);
 CHECK("mode-vertical-start-border",blank==0xff00ff00);
 CHECK("mode-vertical-start-resume",resume==0xff010101);
 printf("raster vertical-start border=%08x resume=%08x\n",blank,resume);
 setup(3);probe_video_start();advance(1120000);v(0x93000008);
 advance(1152000);CHECK("mode-vertical-start-flyback",mem_read32(0x03000000)&0x80);
 guest_write(0x03000014,IOMD_IRQA_FLYBACK);
 advance(1184000);CHECK("blank-no-irq-reassert",!(iomd.irqa.status&IOMD_IRQA_FLYBACK));
 CHECK("blank-flyback-level",mem_read32(0x03000000)&0x80);
 advance(1216000);CHECK("display-flyback-level",!(mem_read32(0x03000000)&0x80));
 printf("raster flyback time=%"PRIu64" level=%02x irq=%02x\n",probe_now,mem_read32(0x03000000)&0x80,iomd.irqa.status&IOMD_IRQA_FLYBACK);finish();
 setup(3);probe_video_start();advance(1120000);v(0x9400000c);finish();
 unsigned w,h;probe_video_pixels(&w,&h);
 CHECK("mode-vertical-grow",w==32&&h==10&&probe_now==1344000);
}
static void equivalent_tests(void)
{
 unsigned depths[]={0,1,2,3,4,6};
 for(unsigned k=0;k<6;k++) {
  setup(depths[k]);for(unsigned i=0;i<8192;i+=4)mem_write32(base+i,0x12345678u+i);
  drawscr();unsigned w,h;const uint32_t *p=probe_video_pixels(&w,&h);
  uint32_t expected[32*8];for(unsigned y=0;y<8;y++)memcpy(expected+y*32,(const uint8_t *)p+y*probe_video_pitch(),32*4);
  probe_video_start();finish();p=probe_video_pixels(&w,&h);
  int same=1;for(unsigned y=0;y<8;y++)if(memcmp(expected+y*32,(const uint8_t *)p+y*probe_video_pitch(),32*4))same=0;
  CHECK("unchanged-vs-legacy",same);
  printf("raster equivalent depth=%u same=%d hash=%016"PRIx64"\n",depths[k],same,probe_video_hash());
 }
}
static void counter_rom(void)
{
 /* Reset-vector SVC26 flags mask IRQ/FIQ; the peripheral IRQ still wakes
  * idle, as in the scheduler's existing timer path. Count normal stores. */
 uint32_t code[]={0xe3a00201,0xe3a01000,0xe2811001,0xe5801000,0xeafffffc};
 memcpy(rom,code,sizeof(code));arm.reg[15]=0x0c00000b;
 mem_write32(0x10000000,0);
}
static void idle_wake_tests(void)
{
 setup(3);guest_write(0x03000018,IOMD_IRQA_FLYBACK);probe_video_start();counter_rom();
 probe_idle=1;probe_advance(1000);
 CHECK("pending-irq-first-batch",mem_read32(0x10000000)==33);
 setup(3);guest_write(0x03000018,IOMD_IRQA_FLYBACK);probe_video_start();
 guest_write(0x03000014,IOMD_IRQA_FLYBACK);counter_rom();
 probe_idle=1;probe_advance(1279999);
 CHECK("idle-before-video-irq",mem_read32(0x10000000)==0);
 probe_advance(1280000);CHECK("video-irq-wakes-at-edge",!probe_idle);
 probe_advance(1281000);uint32_t count=mem_read32(0x10000000);
 CHECK("video-irq-next-batch",count==33);
 printf("raster idle-wake time=%"PRIu64" count=%u\n",probe_now,count);
}
int main(void)
{
 Model models[]={Model_RPCARM610,Model_RPCARM710,Model_RPCSA110,Model_A7000};
 for(unsigned i=0;i<4;i++) {printf("model=%u\n",i);probe_machine_init(models[i],100);split_tests();timing_tests();equivalent_tests();idle_wake_tests();probe_machine_close();}
 fprintf(stderr,"raster checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
