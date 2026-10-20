/* AY-1: authored guest, live/persistent image and session lifecycle. GPL-2.0-only. */
#include "arcdroid_engine.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
static HMODULE library;
static void *symbol(const char *s) { return (void *)GetProcAddress(library,s); }
#else
#include <dlfcn.h>
static void *library;
static void *symbol(const char *s) { return dlsym(library,s); }
#endif
#define LOAD(v,n) do {void *p=symbol(n);assert(p);memcpy(&(v),&p,sizeof(v));} while(0)
#define API(n) __typeof__(&n) p_##n; LOAD(p_##n,#n)
int main(int argc,char **argv)
{
   assert(argc==3);
#ifdef _WIN32
   library=LoadLibraryA(argv[1]);
#else
   library=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
#endif
   assert(library);
   API(arc_engine_start); API(arc_engine_reset); API(arc_engine_stop);
   API(arc_engine_run_frame); API(arc_engine_cmos); API(arc_engine_rpc_start); API(arc_engine_rpc_advance);
   API(arc_cmos_default); API(arc_cmos_machine_image); API(arc_machine_descriptor);
   void (*clock_set)(int,int,int,int,int,int,int); LOAD(clock_set,"cmos_test_clock");
   bool (*live_image)(uint8_t *); LOAD(live_image,"arc_engine_test_cmos_image");
   bool (*guest_write)(unsigned,uint8_t); LOAD(guest_write,"arc_engine_test_cmos_bus_write");
   uint32_t (*read_word)(uint32_t); LOAD(read_word,"readmemf_debug");
   FILE *file=fopen(argv[2],"rb");assert(file);
   uint8_t *rom=malloc(524288);assert(rom && fread(rom,1,524288,file)==524288 && !fclose(file));
   uint32_t *rpc_rom=malloc(2097152);assert(rpc_rom);
   for(unsigned i=0;i<2097152/4;++i)rpc_rom[i]=0xeafffffe;
   const ArcOsClass classes[]={ARC_OS_ARTHUR_030,ARC_OS_ARTHUR_120,ARC_OS_RISCOS_200,ARC_OS_RISCOS_201,
      ARC_OS_RISCOS_300,ARC_OS_RISCOS_310,ARC_OS_RISCOS_311,ARC_OS_RISCOS_319};
   const unsigned years[]={1999,2000,2024,2025,2026,2027,2099,2100};
   unsigned sessions=0;char error[512];uint8_t mask[32]={0},seed[256],live[256],saved[240],defaults[240];
   for(unsigned switching=0;switching<2;++switching)
      for(unsigned c=0;c<(switching?200u:8u);++c)
         for(unsigned m=0;m<(switching?1u:4u);++m) {
            ArcOsClass os=classes[c%8];
            const ArcMachineDescriptor *machine=p_arc_machine_descriptor(switching?c%4:m);
            if(!(machine->compatible_os&ARC_OS_MASK(os))) {
               if(!switching)continue;
               machine=p_arc_machine_descriptor(0);
            }
            for(unsigned y=0;y<(switching?1u:8u);++y) {
               unsigned year=years[switching?c%8:y];
               assert(p_arc_cmos_default(os,machine->io,defaults));
               for(unsigned reopen=0;reopen<(switching?1u:2u);++reopen) {
                  assert(p_arc_cmos_machine_image(os,machine->io,defaults,false,seed));
                  clock_set(year,7,15,12,0,0,0);
                  if(switching){
                     assert(p_arc_engine_rpc_start(0,(uint8_t *)rpc_rom,2097152,seed,946684800,false,error,sizeof(error)));
                     assert(p_arc_engine_rpc_advance(1000000));p_arc_engine_stop();
                  }
                  assert(p_arc_engine_start(rom,524288,NULL,NULL,machine,os,NULL,seed,mask,NULL,NULL,error,sizeof(error)));
                  assert(live_image(live));
                  assert(live[192]==year%100 && live[193]==year/100);
                  p_arc_engine_cmos(saved);
                  assert(!memcmp(saved,seed+16,240)); /* host overlay never saved */
                  ArcInputState input={0};ArcEngineFrame frame;
                  for(unsigned f=0;read_word(0x02000408)!=0xa5;++f){assert(f<10);assert(p_arc_engine_run_frame(&input,&frame));}
                  assert(read_word(0x02000400)==year && read_word(0x02000404)==1);
                  /* Player-set year and matching checksum survive engine reset.
                   * Move host clock four years to expose accidental repatching. */
                  assert(live_image(live));unsigned edited=year+8;
                  live[192]=(uint8_t)(edited%100);live[193]=(uint8_t)(edited/100);
                  unsigned sum=1;for(unsigned i=16;i<256;++i)if(i!=63)sum+=live[i];live[63]=(uint8_t)sum;
                  assert(guest_write(192,live[192]) && guest_write(193,live[193]) && guest_write(63,live[63]));
                  clock_set(year+4,7,15,12,0,0,0);
                  assert(p_arc_engine_reset(error,sizeof(error)));
                  assert(live_image(live) && live[192]==edited%100 && live[193]==edited/100);
                  for(unsigned f=0;read_word(0x02000408)!=0xa5;++f){assert(f<10);assert(p_arc_engine_run_frame(&input,&frame));}
                  assert(read_word(0x02000400)==edited && read_word(0x02000404)==1);
                  p_arc_engine_cmos(saved);assert(saved[176]==edited%100 && saved[177]==edited/100);
                  /* Reopen uses the prior guest-written persisted payload. */
                  memcpy(defaults,saved,240);p_arc_engine_stop();++sessions;
               }
            }
         }
   clock_set(0,0,0,0,0,0,0);free(rom);free(rpc_rom);
   printf("Archimedes calendar: %u sessions, every firmware class/profile, all year residues, reopen, reset and RPC switches passed\n",sessions);
   return 0;
}
