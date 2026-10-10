/* Real MMIO/interpreter/scheduler/backend guest proof. GPL-2.0-only. */
#include "rpc_session.h"
#include "engine_test.h"
#include "rpc_ide_rom.h"
#include "rpc_hd_store.h"
#include "rpc_hd_vfs.h"
#include <time.h>
#ifdef _WIN32
#include <windows.h>
static HMODULE library;
static void *symbol(const char *s){return (void *)GetProcAddress(library,s);}
#else
#include <dlfcn.h>
static void *library;
static void *symbol(const char *s){return dlsym(library,s);}
#endif
#define LOAD(v,n) do{void *p=symbol(n);assert(p);memcpy(&(v),&p,sizeof(v));}while(0)
static bool (*start)(ArcRpcModel,const uint8_t *,size_t,const uint8_t *,uint64_t,char *,size_t);
static bool (*advance)(uint64_t),(*ram)(unsigned,uint32_t *),(*attach)(const ArcIdeBackend *);
static void (*guest_snapshot)(ArcRpcTestSnapshot *),(*audio)(ArcRpcAudioCounts *);
static const ArcEngineBackendOps *(*engine_api)(void);
static ArcRpcAudioCounts sound_counts[2];
static ArcIdeBackend backend;static unsigned commits;static bool delayed;
static bool commit(void *opaque,uint32_t lba,uint32_t count,const uint8_t *p)
{(void)opaque;++commits;assert(lba==511&&count==2);
 if(delayed){clock_t until=clock()+CLOCKS_PER_SEC/100;while(clock()<until){} }
 return backend.write(backend.opaque,lba,count,p);}
static ArcRpcTestSnapshot run(unsigned pacing,bool slow)
{
    fresh_files();ArcRpcHdStore *store=arc_riscpc_hd_open("/save","0123456789012345678901234567890123456789",true,NULL,0);assert(store);
    for(unsigned i=0;i<3;++i)arc_riscpc_hd_tick(store);arc_riscpc_hd_backend(store,&backend);
    uint8_t *rom=calloc(1,2097152);assert(rom);for(unsigned i=0;i<2097152;i+=4){uint32_t w=i/4<sizeof(rpc_ide_rom)/4?rpc_ide_rom[i/4]:0xeafffffe;
        for(unsigned b=0;b<4;++b)rom[i+b]=(uint8_t)(w>>(b*8));}
    uint8_t seed[256]={0};char error[256];assert(start(ARC_RPC_ARM610,rom,2097152,seed,946684800,error,sizeof(error)));free(rom);
    ArcIdeBackend guest=backend;guest.write=commit;commits=0;delayed=slow;assert(attach(&guest));assert(engine_api()->capabilities().ide);
    unsigned ticks=pacing?40:4;for(unsigned i=0;i<ticks;++i)if(!advance(UINT64_C(40000000)/ticks)){ArcRpcTestSnapshot failed;guest_snapshot(&failed);fprintf(stderr,"advance failed at %llu ns instructions=%llu active=%d fault=%d pc=%08x\n",(unsigned long long)failed.now_ns,(unsigned long long)failed.executed,failed.active,failed.faulted,failed.regs[15]);assert(0);}
    uint32_t w;assert(ram(0,&w)&&!(w&2));assert(ram(4,&w)&&(w&2));assert(ram(8,&w)&&w==0x50);
    assert(ram(12,&w)&&w==0x50);assert(ram(16,&w)&&!(w&2));assert(ram(20,&w)&&w==1024*0x5a);assert(ram(24,&w)&&w==0xa5);
    assert(commits==1);uint8_t p[1024];assert(arc_riscpc_hd_read(store,511,2,p));for(unsigned i=0;i<1024;++i)assert(p[i]==0x5a);
    ArcRpcTestSnapshot s;guest_snapshot(&s);assert(s.events[5]==4&&!s.faulted&&s.now_ns==40000000);audio(&sound_counts[pacing]);
    assert(attach(NULL));assert(engine_api()->reset(NULL,0));assert(advance(1000));ArcRpcTestSnapshot reset;guest_snapshot(&reset);assert(reset.events[5]==0);
    engine_api()->stop();arc_riscpc_hd_close(store);return s;
}
int main(int argc,char **argv)
{
    assert(argc==2);
#ifdef _WIN32
    library=LoadLibraryA(argv[1]);
#else
    library=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
#endif
    assert(library);LOAD(start,"arc_rpc_test_start");LOAD(advance,"arc_rpc_test_advance");LOAD(ram,"arc_rpc_test_read_ram");
    LOAD(guest_snapshot,"arc_rpc_test_snapshot");LOAD(attach,"arc_engine_test_ide_backend");LOAD(engine_api,"arc_rpc_test_engine_api");LOAD(audio,"arc_rpc_test_audio_counts");
    ArcRpcTestSnapshot a=run(0,false),b=run(1,true);
    assert(sound_counts[0].generated==sound_counts[1].generated&&sound_counts[0].dma_frames==sound_counts[1].dma_frames&&sound_counts[0].queued==sound_counts[1].queued);
    assert(a.trace_count==b.trace_count&&!memcmp(a.events,b.events,sizeof(a.events))&&a.executed==b.executed);
    for(unsigned i=0;i<a.trace_count;++i)assert(a.trace[i].event==b.trace[i].event&&a.trace[i].ns==b.trace[i].ns);
    puts("rpc-ide-guest: real guest, single commit, nIEN/status, virtual timing across pacing and delayed host, reset/detach pass");
    fresh_files();return 0;
}
