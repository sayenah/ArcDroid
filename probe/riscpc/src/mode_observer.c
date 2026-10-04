/* ArcDroid passive numeric mode-request observer, GPL-2.0-only.
 * It never calls a guest memory accessor, alters a TLB, raises an abort,
 * performs I/O reads or changes registers. No guest strings are recorded. */
#include "probe.h"
#include "cp15.h"
#include <inttypes.h>
static unsigned pending_vdu22, requests;
void probe_mode_reset(void) { pending_vdu22=requests=0; }
static int physical(uint32_t a,uint32_t *value) {
 if(a&3)return 0;a&=0x1fffffff;
 if((a&0x1f000000)==0){*value=rom[(a&0x7fffff)/4];return 1;}
 if((a&0x1f000000)==0x02000000) {
  if(!mem_vrammask)return 0;*value=vram[(a&mem_vrammask)/4];return 1;
 }
 switch(a&0x1c000000) {
 case 0x10000000:*value=ram00[(a&mem_rammask)/4];return 1;
 case 0x14000000:*value=ram01[(a&mem_rammask)/4];return 1;
 case 0x18000000:case 0x1c000000:if(!ram1)return 0;*value=ram1[(a&0x7ffffff)/4];return 1;
 default:return 0;
 }
}
static int peek(uint32_t a,uint32_t *value) {
 if(a&3)return 0;
 uint32_t p=a,d,e;
 if(mmu) {
  uint32_t table=cp15_read(2<<16)&0xffffc000;
  if(!physical(table+((a>>18)&0x3ffc),&d))return 0;
  if((d&3)==2)p=(d&0xfff00000)|(a&0xfffff);
  else if((d&3)==1) {
   if(!physical((d&0xfffffc00)+((a>>10)&0x3fc),&e))return 0;
   if((e&3)==1)p=(e&0xffff0000)|(a&0xffff);
   else if((e&3)==2)p=(e&0xfffff000)|(a&0xfff);
   else return 0;
  } else return 0;
 }
 return physical(p,value);
}
static void record(uint32_t n,uint32_t a,uint32_t b,uint32_t c,int selector) {
 if(!probe_mode_log||requests++>=4096)return;
 uint32_t words[32]={0};unsigned count=0;
 /* Five header words, then variable/value pairs and a -1 terminator. */
 if(selector&&b>=256&&!(b&3))for(;count<32;count++) {
  if(b>UINT32_MAX-count*4||!peek(b+count*4,&words[count]))break;
  if(count>=5&&!((count-5)&1)&&words[count]==UINT32_MAX){count++;break;}
 }
 if(fprintf(probe_mode_log,"%"PRIu64",%05x,%08x,%08x,%08x,%08x,%08x,%08x,%u",probe_now,n,a,b,c,arm.reg[3],arm.reg[4],arm.reg[5],count)<0)fatal("Mode trace write failed");
 for(unsigned i=0;i<count;i++)if(fprintf(probe_mode_log,",%08x",words[i])<0)fatal("Mode trace write failed");
 if(fputc('\n',probe_mode_log)==EOF)fatal("Mode trace write failed");
}
void probe_observe_swi(uint32_t n) {
 n&=~0x20000u;
 if(n==0x65)record(n,arm.reg[0],arm.reg[1],arm.reg[2],arm.reg[0]==0);
 else if(n==0x400e3)record(n,0,arm.reg[0],arm.reg[1],1);
 else if(n==0x3f)record(n,0,arm.reg[0],arm.reg[1],1);
 /* Mode services expose the firmware's effective monitor, bandwidth and
  * video-memory description. They are observed, never intercepted. */
 else if(n==0x30&&(arm.reg[1]==0x50||arm.reg[1]==0x8d))record(n,arm.reg[1],arm.reg[2],arm.reg[3],0);
 else if(n==6&&(arm.reg[0]==135||arm.reg[0]==160))record(n,arm.reg[0],arm.reg[1],arm.reg[2],0);
 else if(n==0||(n>=0x100&&n<0x200)) {
  unsigned c=n==0?(arm.reg[0]&255):(n&255);
  if(pending_vdu22){record(0x16,0,c,0,0);pending_vdu22=0;}
  else if(c==22)pending_vdu22=1;
 }
}
