/* ArcDroid authored ATA PIO guest exercises, GPL-2.0-only. */
#include "probe.h"
#include "ide_probe.h"
#include "install_disc.h"
#include "arcdroid_core.h"
#include <inttypes.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#else
#include <unistd.h>
#endif
#define DATA 0x030107c0u
#define STATUS 0x030107dcu
#define BUFFER 0x10040000u
#define DONE 0x1005fff0u
static unsigned at,checks,failures;
#define CHECK(n,c) do {checks++;if(!(c)){fprintf(stderr,"FAIL %s\n",n);failures++;}}while(0)
static void word(uint32_t v){rom[at++]=v;}
static void load(unsigned r,uint32_t v){word(0xe59f0000|(r<<12));word(0xea000000);word(v);}
static void store(uint32_t a,uint32_t v){load(0,a);load(1,v);word(0xe5801000);}
static void branch(unsigned c,unsigned target){word((c<<28)|0x0a000000|((target-at-2)&0xffffff));}
static void ready(unsigned mask){load(0,STATUS);unsigned loop=at;word(0xe5901000);word(0xe3110000|mask);branch(mask==0x80?1:0,loop);}
static void program(unsigned command,unsigned lba,unsigned sectors,int writing){
 memset(rom,0,ROMSIZE);at=0;
 store(0x030107d8,0xe0|((lba>>24)&15));store(0x030107c8,sectors);
 store(0x030107cc,lba&255);store(0x030107d0,(lba>>8)&255);store(0x030107d4,(lba>>16)&255);store(STATUS,command);
 if(command==0x20||command==0x30||command==0xec){
  load(2,BUFFER);load(3,DATA);load(4,256);load(5,sectors);load(6,0x3157);
  unsigned sector=at;ready(8);unsigned loop=at;
  if(writing){word(0xe5836000);word(0xe2866001);}else{word(0xe5936000);word(0xe4826004);}
  word(0xe2544001);branch(1,loop);word(0xe3a04c01);word(0xe2555001);branch(1,sector);
 }
 ready(0x80);load(0,STATUS);word(0xe5901000);load(2,DONE);word(0xe5821004);word(0xe3a01001);word(0xe5821000);unsigned end=at;branch(14,end);
}
static void run(unsigned cmd,unsigned lba,unsigned count,int writing){
 probe_machine_reset();program(cmd,lba,count,writing);
 while(probe_now<20000000){probe_audio_count=0;probe_advance(probe_now+1000000);}
}
int main(int argc,char **argv){
 if(argc!=2)return 2;probe_ide_enabled=1;probe_machine_init(Model_RPCSA110,100);
 probe_ide_open(argv[1],"none","none",NULL);
 run(0xec,0,1,0);CHECK("identify completes",ram00[0x5fff0/4]==1);uint32_t *b=ram00+0x40000/4;
 CHECK("identify geometry",b[1]==32&&b[3]==16&&b[6]==63);CHECK("identify bounded capacity",b[60]==ARC_INSTALL_HD_SECTORS&&b[61]==0);
 run(0x30,100,2,1);CHECK("guest write completes",ram00[0x5fff0/4]==1&&ram00[0x5fff4/4]==0x40&&probe_ide_writes==2);
 CHECK("close commits",probe_ide_close());
 /* Cold machine restart opens exactly the previously-created session image as
    a verified read-only baseline, cloned into a fresh child run directory. */
 char name[1400],child[1400],sha[41];snprintf(name,sizeof(name),"%s/scratch.hdf",argv[1]);
 FILE *f=fopen(name,"rb");uint8_t *bytes=malloc(ARC_INSTALL_HD_BYTES);CHECK("baseline read",f&&bytes&&fread(bytes,1,ARC_INSTALL_HD_BYTES,f)==ARC_INSTALL_HD_BYTES);
 if(!f||!bytes)return 1;fclose(f);arc_sha1_hex(bytes,ARC_INSTALL_HD_BYTES,sha);free(bytes);
 snprintf(child,sizeof(child),"%s/cold",argv[1]);probe_machine_close();probe_machine_init(Model_RPCSA110,100);probe_ide_open(child,name,sha,NULL);
 run(0x20,100,2,0);b=ram00+0x40000/4;unsigned bad=0;for(unsigned i=0;i<512;i++)if(b[i]!=0x3157+i)bad++;
 CHECK("cold guest readback",ram00[0x5fff0/4]==1&&ram00[0x5fff4/4]==0x40&&bad==0&&probe_ide_reads==2);
 run(0x40,ARC_INSTALL_HD_SECTORS,1,0);CHECK("LBA bound guest error",ram00[0x5fff0/4]==1&&ram00[0x5fff4/4]==0x41);
 CHECK("bounded host size",probe_ide_writes==0&&probe_ide_io_failures==0);
 /* Fault injection touches only the authored child scratch backing, never a
    private input. A short backing read must escape the run, not supply zeros. */
 snprintf(name,sizeof(name),"%s/scratch.hdf",child);
#ifdef _WIN32
 int fd=_open(name,_O_BINARY|_O_RDWR);CHECK("truncate fixture",fd>=0&&_chsize(fd,512)==0);if(fd>=0)_close(fd);
#else
 CHECK("truncate fixture",truncate(name,512)==0);
#endif
 probe_can_escape=1;
 if(!setjmp(probe_escape)){run(0x20,100,1,0);CHECK("short read must fail",0);}
 probe_can_escape=0;CHECK("short I/O detected",probe_failed&&probe_ide_io_failures==1);
 CHECK("cold close",probe_ide_close());probe_machine_close();
 printf("ide checks=%u failures=%u baseline=%s\n",checks,failures,sha);return failures?1:0;
}
