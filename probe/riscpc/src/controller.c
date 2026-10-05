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
static unsigned at,checks,failures,guest_drive;
#define CHECK(label,condition) do {checks++;if(!(condition)){fprintf(stderr,"FAIL %s line=%u\n",label,__LINE__);failures++;}}while(0)
static void word(uint32_t x) {rom[at++]=x;}
static void load(unsigned r,uint32_t x) {word(0xe59f0000|(r<<12));word(0xea000000);word(x);}
static void store(uint32_t addr,uint32_t value) {load(0,addr);load(1,value);word(0xe5801000);}
static void branch(unsigned cond,unsigned target) {word((cond<<28)|0x0a000000|((target-at-2)&0xffffff));}
static void wait_result(void) {
 load(0,MSR);unsigned loop=at;word(0xe5901000);word(0xe3110040);branch(0,loop);
}
static void program(unsigned cmd,unsigned c,unsigned h,unsigned r,unsigned n,unsigned eot,unsigned rate,unsigned bytes,int writing) {
 memset(rom,0,ROMSIZE);at=0;store(DOR,guest_drive?0x2d:0x1c);store(CCR,rate);
 load(4,5000);unsigned delay=at;word(0xe2544001);branch(1,delay); /* >=100us at 100M */
 store(FIFO,8);wait_result();load(0,FIFO);word(0xe5d01000);word(0xe5d01000);
 store(FIFO,cmd);
 if((cmd&31)==10)store(FIFO,(h<<2)|guest_drive);
 else {unsigned p[]={(h<<2)|guest_drive,c,h,r,n,eot,0x1b,0x80};for(unsigned i=0;i<8;i++)store(FIFO,p[i]);}
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
static void helper_tests(void) {
 guest_drive=1;probe_helper_enabled=1;
 probe_machine_reset();memset(rom,0,ROMSIZE);at=0;store(DOR,0x2d);unsigned loop=at;branch(14,loop);advance(201000000);
 CHECK("helper-index-without-command",(iomd.irqa.status&IOMD_IRQA_FLOPPY_INDEX)!=0);
 probe_machine_reset();program(0x4a,0,0,0,1,0,2,0,0);advance(210000000);
 CHECK("helper-read-id-CHRN",done()&&result()[0]==1&&result()[1]==0&&result()[2]==0&&result()[3]==0&&result()[4]==0&&result()[5]==1&&result()[6]==1);trace("helper-id");
 probe_machine_reset();program(0x46,0,0,2,1,2,2,256,0);advance(250000000);
 CHECK("helper-root-complete",done()&&remaining()==0&&result()[0]==1&&result()[1]==0&&result()[3]==1&&result()[4]==0&&result()[5]==1&&result()[6]==1);
 CHECK("helper-authored-old-root",buffer()[0]==1&&!memcmp(buffer()+1,"Hugo",4)&&(buffer()[5]&127)=='!'&&(buffer()[6]&127)=='B'&&!memcmp(buffer()+7,"oot",3)&&(buffer()[31]&127)=='M'&&(buffer()[32]&127)=='u'&&!memcmp(buffer()+33,"ltisync",7));trace("helper-root");
 probe_machine_reset();program(0x46,0,0,7,1,8,2,512,0);advance(250000000);
 CHECK("helper-normal-boot-data",done()&&remaining()==0&&result()[0]==1&&result()[1]==0&&strstr((char *)buffer(),"LoadModeFile ADFS::1.$.Multisync")!=NULL);
 CHECK("helper-multi-sector-boot-environment",result()[3]==1&&result()[4]==0&&result()[5]==1&&result()[6]==1&&strstr((char *)buffer(),"BASIC -quit ADFS::1.$.BootEnv"));trace("helper-boot");
 probe_machine_reset();program(0x46,0,0,7,1,15,2,512,0);advance(250000000);
 CHECK("helper-TC-before-EOT",done()&&remaining()==0&&result()[0]==1&&result()[1]==0&&result()[2]==0&&result()[3]==0&&result()[4]==0&&result()[5]==9&&result()[6]==1);trace("helper-TC-before-EOT");
 probe_machine_reset();program(0x46,0,0,16,1,16,2,256,0);advance(450000000);
 CHECK("helper-out-of-range-sector-not-ID",done()&&remaining()==256&&result()[1]!=0&&result()[0]==0x41);trace("helper-sector-16");
 probe_machine_reset();program(0x45,0,0,7,1,7,2,256,1);advance(250000000);
 CHECK("helper-write-refused",done()&&result()[1]==2&&remaining()==256&&!(iomd.fiq.status&1));trace("helper-WP");
 probe_machine_reset();program(0x46,0,0,7,1,7,2,256,0);advance(88000000);CHECK("helper-read-in-flight",!done());fdc_reset();advance(450000000);
 CHECK("helper-reset-cancels",!done()&&fdccallback==0&&!(iomd.fiq.status&1));
 probe_helper_enabled=0;guest_drive=0;
}

/* Run-8 authored two-image disk-control fixtures, generated in test cwd. */
static void le32(uint8_t *p,uint32_t x) {for(unsigned i=0;i<4;i++)p[i]=x>>(8*i);}
static void swap_fixtures(void) {
 const unsigned sector_table=316+160*4,data_table=sector_table+160*44;
 const unsigned length=data_table+800*1024;
 uint8_t *b=calloc(length,1);if(!b)abort();
 memcpy(b,"JFDI",4);le32(b+8,length);le32(b+24,316);
 le32(b+28,sector_table);le32(b+32,data_table);le32(b+304,2);
 for(unsigned t=0;t<160;t++) {
  le32(b+316+t*4,t*44);
  for(unsigned id=0;id<5;id++) {
   unsigned k=t*5+id;le32(b+sector_table+t*44+id*8,0xff020003|(id<<8));
   le32(b+sector_table+t*44+id*8+4,k*1024);
   for(unsigned i=0;i<1024;i++)b[data_table+k*1024+i]=(i+16*k)&255;
  }
  le32(b+sector_table+t*44+40,UINT32_MAX);
 }
 const char *names[]={"controller-authored-a.jfd","controller-authored-b.jfd"};
 for(unsigned n=0;n<2;n++) {
  if(n)for(unsigned i=data_table;i<length;i++)b[i]+=0x40;
  FILE *f=fopen(names[n],"wb");if(!f||fwrite(b,1,length,f)!=length||fclose(f))abort();
 }
 free(b);FILE *f=fopen("controller-authored.m3u","wb");
 if(!f||fprintf(f,"%s\n%s\n",names[0],names[1])<0||fclose(f))abort();
 probe_media_open_content("controller-authored.m3u",NULL);
}
static void guest_seek(unsigned d,unsigned c) {
 store(FIFO,15);store(FIFO,d);store(FIFO,c);
 load(4,5000);unsigned delay=at;word(0xe2544001);branch(1,delay);
 store(FIFO,8);wait_result();load(0,FIFO);word(0xe5d01000);word(0xe5d01000);
}
/* R9: independently expected unit identities, read by authored guest code. */
static void captured_move(unsigned d,unsigned c,int recalibrate,unsigned slot) {
 store(DOR,d?0x3d:0x3c);
 store(FIFO,recalibrate?7:15);store(FIFO,recalibrate?d:4|d);
 if(!recalibrate)store(FIFO,c);
 load(4,5000);unsigned delay=at;word(0xe2544001);branch(1,delay);
 store(FIFO,8);wait_result();load(0,FIFO);load(2,RESULT+slot);
 for(unsigned i=0;i<2;i++) {word(0xe5d01000);word(0xe4c21001);}
}
static void captured_drive_status(unsigned d,unsigned slot) {
 store(DOR,d?0x3d:0x3c);store(FIFO,4);store(FIFO,d);wait_result();
 load(0,FIFO);word(0xe5d01000);load(2,RESULT+slot);word(0xe5c21000);
}
static void seek_identity_tests(void) {
 ArcJfdImage im;probe_helper_enabled=1;fixture(&im,2,0);probe_machine_reset();
 memset(rom,0,ROMSIZE);at=0;store(DOR,0x3c);
 load(4,5000);unsigned delay=at;word(0xe2544001);branch(1,delay);
 store(FIFO,8);wait_result();load(0,FIFO);word(0xe5d01000);word(0xe5d01000);
 captured_move(1,5,0,0);captured_drive_status(0,8);captured_drive_status(1,9);
 captured_move(0,9,0,2);captured_move(1,0,1,4);
 captured_drive_status(0,10);captured_drive_status(1,11);captured_move(0,0,1,6);
 store(DONE,1);unsigned end=at;branch(14,end);advance(1000000);
 CHECK("unit-1-SEEK-identity-PCN",done()&&result()[0]==0x21&&result()[1]==5);
 CHECK("unit-0-SEEK-regression-PCN",done()&&result()[2]==0x20&&result()[3]==9);
 CHECK("unit-1-RECALIBRATE-identity-PCN",done()&&result()[4]==0x21&&result()[5]==0);
 CHECK("unit-0-RECALIBRATE-regression-PCN",done()&&result()[6]==0x20&&result()[7]==0);
 CHECK("seek-completion-H-is-zero",!(result()[0]&4)&&!(result()[2]&4)&&!(result()[4]&4)&&!(result()[6]&4));
 CHECK("unit-0-T0-survives-other-SEEK",done()&&(result()[8]&0x13)==0x10);
 CHECK("unit-1-T0-clears-at-cylinder-5",done()&&(result()[9]&0x13)==1);
 CHECK("unit-0-T0-stays-clear-after-other-RECALIBRATE",done()&&(result()[10]&0x13)==0);
 CHECK("unit-1-T0-after-RECALIBRATE",done()&&(result()[11]&0x13)==0x11);
 printf("fdc seek-identities time=%"PRIu64" done=%d results=",probe_now,done());
 for(unsigned i=0;i<12;i++)printf("%02x",result()[i]);printf("\n");
 probe_media_close();arc_jfd_free(&im);probe_helper_enabled=0;
}
static void read_dir(unsigned slot) {
 load(0,CCR);word(0xe5d01000);load(2,RESULT+slot);word(0xe5c21000);
}
static void change_program(unsigned stage) {
 memset(rom,0,ROMSIZE);at=0;
 /* Both motors on; selection must come from bits 1:0, not motor priority. */
 store(DOR,0x3c);load(4,5000);unsigned delay=at;word(0xe2544001);branch(1,delay);
 store(FIFO,8);wait_result();load(0,FIFO);word(0xe5d01000);word(0xe5d01000);
 read_dir(0);
 if(stage==0) {
  guest_seek(0,0);read_dir(1);guest_seek(0,1);read_dir(2);
  store(DOR,0x3d);read_dir(3);guest_seek(1,1);read_dir(4);
  store(DOR,0x3c);read_dir(5);
  store(DOR,0);store(DOR,0x3c);read_dir(6); /* FDC reset cannot reset drive latch. */
 } else {
  /* Stage 1 executes while empty; stage 2 follows insertion at physical C=2. */
  guest_seek(0,2);read_dir(1);
  if(stage==2) {guest_seek(0,3);read_dir(2);}
  store(DOR,0x3d);read_dir(3);
 }
 store(DONE,1);unsigned end=at;branch(14,end);
}
static void ownership_tests(void) {
 probe_helper_enabled=1;swap_fixtures();guest_drive=1;
 probe_machine_reset();program(0x4a,0,0,0,1,0,2,0,0);advance(20000000);
 uint8_t expected[7]={1,0,0,0,0,1,1};
 CHECK("ownership-reference-helper-ID",done()&&!memcmp(result(),expected,7));
 uint64_t reference_instructions=probe_instructions;uint32_t reference_pc=PC;
 ARMState reference_arm=arm;
 uint8_t reference_ram[32];memcpy(reference_ram,result(),32);
 probe_machine_reset();program(0x4a,0,0,0,1,0,2,0,0);advance(1000000);
 CHECK("helper-ID-pending-before-content-swap",!done()&&fdc_read(0x3f4)==0x10);
 unsigned motors=motoron;probe_media_swap(1);
 CHECK("unaffected-helper-not-cancelled",motoron==(int)motors&&fdc_read(0x3f4)==0x10&&!(iomd.irqb.status&IOMD_IRQB_FLOPPY)&&!(iomd.fiq.status&1));
 advance(20000000);
 CHECK("swap-helper-exact-result-deadline-state",done()&&!memcmp(result(),reference_ram,32)&&PC==reference_pc&&probe_instructions==reference_instructions&&!memcmp(&arm,&reference_arm,sizeof(arm)));trace("other-drive-swap-ID");
 probe_machine_reset();program(0x4a,0,0,0,1,0,2,0,0);advance(1000000);
 for(unsigned i=0;i<8;i++)probe_media_swap(i&1);
 advance(20000000);
 CHECK("repeated-swaps-leave-helper-ID-identical",done()&&!memcmp(result(),expected,7)&&PC==reference_pc&&probe_instructions==reference_instructions&&!memcmp(&arm,&reference_arm,sizeof(arm)));trace("repeated-swaps-ID");
 /* A command owned by an absent helper still has its original timeout. */
 probe_helper_enabled=0;probe_machine_reset();program(0x4a,0,0,0,1,0,2,0,0);advance(1000000);
 probe_media_swap(1);advance(20000000);CHECK("other-drive-empty-search-survives",!done());
 advance(450000000);CHECK("other-drive-empty-search-completes",done()&&result()[0]==0x41&&result()[1]==5);trace("other-drive-empty");
 probe_helper_enabled=1;guest_drive=0;probe_media_swap(0);
 probe_machine_reset();program(0x46,0,0,0,3,0,2,1024,0);advance(201000000);
 CHECK("own-read-strict-prefix",!done()&&buffer()[1]==1);
 uint8_t prefix[1024];memcpy(prefix,buffer(),1024);probe_media_swap(1);advance(450000000);
 CHECK("own-swap-abnormal-result",done()&&remaining()>0&&remaining()<1024&&result()[0]==0x40&&result()[1]==5);
 CHECK("own-swap-no-later-data",!memcmp(prefix,buffer(),1024)&&!(iomd.fiq.status&1)&&fdccallback==0);trace("own-drive-swap");
 /* Same replacement before issue is a fresh normal read, including new bytes. */
 probe_machine_reset();program(0x46,0,0,0,3,0,2,1024,0);advance(250000000);
 unsigned bad=0;for(unsigned i=0;i<1024;i++)bad+=buffer()[i]!=((i+0x40)&255);
 CHECK("early-swap-regression-new-image",done()&&!remaining()&&result()[0]==0&&result()[1]==0&&!bad);trace("early-swap");
 /* In-flight write cancellation cannot commit a partial replacement. */
 probe_media_swap(0);probe_machine_reset();program(0x45,0,0,0,3,0,2,1024,1);advance(201000000);
 uint64_t writes=probe_media_writes;CHECK("own-write-pending",!done());probe_media_swap(1);advance(450000000);
 CHECK("own-swap-cancels-partial-write",done()&&remaining()>0&&probe_media_writes==writes&&result()[1]==5&&!(iomd.fiq.status&1));
 /* Guest DIR/SEEK sequence: no internal latch pokes. */
 probe_machine_reset();change_program(0);advance(1000000);
 uint8_t dirs[]={0x80,0x80,0,0x80,0,0,0};
 CHECK("DIR-selection-and-per-drive-step-clear",done()&&!memcmp(result(),dirs,sizeof(dirs)));trace("DIR-steps");
 probe_media_eject(true);
 /* Start another authored program without resetting physical drive position. */
 arm_reset(machine.cpu_model);memset(result(),0,32);ram00[0x5fff0/4]=0;change_program(1);advance(probe_now+1000000);
 CHECK("empty-step-does-not-clear-change",done()&&result()[0]==0x80&&result()[1]==0x80&&result()[3]==0);
 probe_media_eject(false);arm_reset(machine.cpu_model);memset(result(),0,32);ram00[0x5fff0/4]=0;change_program(2);advance(probe_now+1000000);
 CHECK("insert-same-track-retains-then-step-clears",done()&&result()[0]==0x80&&result()[1]==0x80&&result()[2]==0&&result()[3]==0);
 probe_media_swap(0);CHECK("repeated-swap-latches-only-content",probe_disc_changed[0]==1&&probe_disc_changed[1]==0);
 probe_media_close();probe_helper_enabled=0;guest_drive=0;
 remove("controller-authored-a.jfd");remove("controller-authored-b.jfd");remove("controller-authored.m3u");
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
 cancellation_tests();helper_tests();ownership_tests();seek_identity_tests();probe_machine_close();printf("controller checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
