/* ArcDroid authored guest controller exercises. GPL-2.0-only. */
#include "probe.h"
#include "media.h"
#include "fdc.h"
#include "iomd.h"
#include <inttypes.h>
#define DOR UINT32_C(0x03010fc8)
#define FIFO UINT32_C(0x03010fd4)
#define MSR UINT32_C(0x03010fd0)
#define CCR UINT32_C(0x03010fdc)
#define FIQ_PORT UINT32_C(0x03000030)
#define DMA UINT32_C(0x03012000)
#define TC UINT32_C(0x0302a000)
#define BUFFER UINT32_C(0x10040000)
#define RESULT UINT32_C(0x10050000)
#define DONE UINT32_C(0x1005fff0)
static unsigned at,checks,failures;
#define CHECK(label,condition) do {checks++;if(!(condition)){fprintf(stderr,"FAIL %s line=%u\n",label,__LINE__);failures++;}}while(0)
static void word(uint32_t x) {rom[at++]=x;}
static void load(unsigned r,uint32_t x) {word(0xe59f0000|(r<<12));word(0xea000000);word(x);}
static void store(uint32_t addr,uint32_t value) {load(0,addr);load(1,value);word(0xe5801000);}
static void branch(unsigned cond,unsigned target) {word((cond<<28)|0x0a000000|((target-at-2)&0xffffff));}
static void wait_result(void) {
 load(0,MSR);unsigned loop=at;word(0xe5901000);word(0xe3110040);branch(0,loop);
}
static void program(unsigned cmd,unsigned c,unsigned h,unsigned r,unsigned n,unsigned eot,unsigned rate,unsigned bytes,int writing) {
 memset(rom,0,ROMSIZE);at=0;store(DOR,0x1c);store(CCR,rate);
 load(4,5000);unsigned delay=at;word(0xe2544001);branch(1,delay); /* >=100us at 100M */
 store(FIFO,8);wait_result();load(0,FIFO);word(0xe5d01000);word(0xe5d01000);
 store(FIFO,cmd);
 if((cmd&31)==10)store(FIFO,h<<2);
 else {unsigned p[]={h<<2,c,h,r,n,eot,0x1b,0x80};for(unsigned i=0;i<8;i++)store(FIFO,p[i]);}
 load(4,bytes);unsigned jump_result=0;
 if(bytes) {
  load(0,FIQ_PORT);load(2,BUFFER);load(5,DMA);load(6,TC);load(7,MSR);load(1,0x55);
  unsigned poll=at;
  word(0xe5978000);word(0xe3180040);jump_result=at;word(0); /* result before data (RNF/WP/CRC) */
  word(0xe5908000);word(0xe3180001);branch(0,poll);word(0xe3540001);
  if(writing) {word(0x05c61000);word(0x15c51000);word(0xe2811001);}
  else {word(0x05d61000);word(0x15d51000);word(0xe4c21001);}
  word(0xe2544001);branch(1,poll);
 }
 unsigned result=at;if(jump_result)rom[jump_result]=0x1a000000|((result-jump_result-2)&0xffffff);
 wait_result();load(0,FIFO);load(2,RESULT);
 for(unsigned i=0;i<7;i++) {word(0xe5d01000);word(0xe4c21001);}
 load(0,DONE);word(0xe3a01001);word(0xe5801000);word(0xe5804004);unsigned end=at;branch(14,end);
}
static void fixture(ArcJfdImage *im,unsigned flags,unsigned crc) {
 memset(im,0,sizeof(*im));im->extended=true;im->flags=flags;im->track_count=160;im->sector_count=800;im->decoded_size=800*1024;
 im->decoded=malloc(im->decoded_size);im->sectors=calloc(800,sizeof(*im->sectors));if(!im->decoded||!im->sectors)abort();
 for(unsigned t=0;t<160;t++) {im->tracks[t].first_sector=t*5;im->tracks[t].sector_count=5;
  for(unsigned id=0;id<5;id++) {unsigned k=t*5+id;im->sectors[k]=(ArcJfdSector){.time_us=id*ARC_JFD_PDD5_SPACING_US,.data_offset=k*1024,.size=1024,.id=id,.density=2,.flags=k?0:crc};for(unsigned b=0;b<1024;b++)im->decoded[k*1024+b]=(b+16*k)&255;}
 }
 probe_media_test_attach(im);
}
static uint8_t *buffer(void) {return (uint8_t *)ram00+0x40000;}
static uint8_t *result(void) {return (uint8_t *)ram00+0x50000;}
static int done(void) {return ram00[0x5fff0/4]!=0;}
static unsigned remaining(void) {return ram00[0x5fff4/4];}
static void advance(uint64_t ns) {
 while(probe_now<ns) {uint64_t step=probe_now+20000000;if(step>ns)step=ns;probe_audio_count=0;probe_advance(step);}
}
static void trace(const char *tag) {uint8_t *p=result();printf("fdc %s time=%"PRIu64" pc=%08x done=%d remaining=%u results=",tag,probe_now,PC,done(),remaining());for(unsigned i=0;i<7;i++)printf("%02x",p[i]);printf(" dma=%02x writes=%"PRIu64"\n",iomd.fiq.status&IOMD_FIQ_FLOPPY_DMA_REQUEST,probe_media_writes);}
static void normal_tests(void) {
 ArcJfdImage im;
 fixture(&im,2,0);probe_machine_reset();program(0x4a,0,0,0,3,0,2,0,0);advance(210000000);
 CHECK("read-id",done()&&result()[0]==0&&result()[1]==0&&result()[2]==0&&result()[3]==0&&result()[4]==0&&result()[5]==1&&result()[6]==3);trace("id");
 probe_machine_reset();program(0x46,0,0,0,3,0,2,1024,0);advance(250000000);
 CHECK("read-complete",done()&&remaining()==0&&result()[0]==0&&result()[1]==0&&result()[2]==0&&result()[6]==3);
 unsigned mismatches=0;for(unsigned i=0;i<1024;i++)if(buffer()[i]!=(i&255))mismatches++;
 CHECK("read-bytes",mismatches==0);CHECK("read-no-stale-DMA",!(iomd.fiq.status&1));trace("read");
 probe_machine_reset();program(0x46,0,0,0,3,1,2,2048,0);advance(300000000);
 CHECK("multi-complete",done()&&remaining()==0&&result()[1]==0&&result()[2]==0);
 mismatches=0;for(unsigned i=0;i<2048;i++)if(buffer()[i]!=((i+(i/1024)*16)&255))mismatches++;
 CHECK("multi-bytes",mismatches==0);trace("multi");
 probe_machine_reset();program(0x45,0,0,0,3,0,2,1024,1);advance(250000000);
 CHECK("write-complete",done()&&remaining()==0&&result()[1]==0&&im.overlay->data[0]!=NULL);
 mismatches=0;if(im.overlay->data[0])for(unsigned i=0;i<1024;i++)if(im.overlay->data[0][i]!=((0x55+i)&255))mismatches++;
 CHECK("write-bytes",im.overlay->data[0]&&mismatches==0);CHECK("base-unchanged",im.decoded[0]==0&&im.decoded[1]==1);trace("write");
 probe_media_close();arc_jfd_free(&im);
}
static void failure_test(const char *tag,unsigned cmd,unsigned c,unsigned n,unsigned rate,unsigned flags,unsigned crc,unsigned bytes,int writing,unsigned st1,unsigned st2,unsigned transferred) {
 ArcJfdImage im;fixture(&im,flags,crc);probe_machine_reset();program(cmd,c,0,0,n,0,rate,bytes,writing);advance(450000000);
 CHECK(tag,done()&&result()[1]==st1&&result()[2]==st2&&result()[6]==n&&remaining()==bytes-transferred);
 CHECK("failed-write-no-commit",!im.overlay->data[0]);CHECK("failure-no-stale-DMA",!(iomd.fiq.status&1));trace(tag);
 probe_media_close();arc_jfd_free(&im);
}
static void cancellation_tests(void) {
 ArcJfdImage im;fixture(&im,2,0);probe_machine_reset();program(0x46,0,0,0,3,0,2,1024,0);advance(201000000);
 CHECK("read-in-flight",!done()&&buffer()[1]==1);probe_media_eject(true);advance(450000000);
 CHECK("eject-completion",done()&&remaining()>0&&result()[1]==5&&!(iomd.fiq.status&1));trace("eject");
 probe_media_eject(false);probe_machine_reset();program(0x45,0,0,0,3,0,2,1024,1);advance(201000000);fdc_reset();advance(450000000);
 CHECK("reset-cancels-write",!im.overlay->data[0]&&!(iomd.fiq.status&1)&&fdccallback==0);trace("reset");
 probe_media_close();arc_jfd_free(&im);
}
int main(void) {
 probe_machine_init(Model_RPCSA110,100);normal_tests();
 failure_test("wrong-size",0x46,0,2,2,2,0,1024,0,4,0,0);
 failure_test("wrong-cylinder",0x46,1,3,2,2,0,1024,0,4,0x10,0);
 failure_test("wrong-density",0x46,0,3,0,2,0,1024,0,5,0,0);
 failure_test("header-CRC",0x46,0,3,2,2,ARC_JFD_ID_CRC_BAD,1024,0,0x20,0,0);
 failure_test("data-CRC",0x46,0,3,2,2,ARC_JFD_DATA_CRC_BAD,1024,0,0x20,0x20,1024);
 failure_test("WP",0x45,0,3,2,3,0,1024,1,2,0,0);
 failure_test("deleted-write",0x49,0,3,2,2,0,1024,1,2,0,0);
 failure_test("short-write",0x45,0,3,2,2,0,32,1,0x10,0,32);
 cancellation_tests();probe_machine_close();printf("controller checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
