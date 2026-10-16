/* Directed driver boundaries using existing test APIs only. GPL-2.0-only. */
#include "rpc_session.h"
#include "rpc_observer.h"
#include "rpc_lockstep_observers.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
static HMODULE library;
static void *find(const char *name){return (void *)GetProcAddress(library,name);}
#else
#include <dlfcn.h>
static void *library;
static void *find(const char *name){return dlsym(library,name);}
#endif
#define LOAD(v,n) do{void *p=find(n);if(!p){fprintf(stderr,"missing API %s\n",n);return 2;}memcpy(&v,&p,sizeof(v));}while(0)
#define CHECK(c) do{if(!(c)){fprintf(stderr,"boundary check line %d\n",__LINE__);return 1;}}while(0)
static bool (*filter)(ArcRpcObserver,const ArcRpcObserveFilter *);
static unsigned callbacks;
static void unsubscribe(void *opaque,const ArcRpcObservation *event)
{
    (void)opaque;
    if(event->kind==ARC_RPC_OBS_SWI){++callbacks;filter(ARC_RPC_OBS_ERROR,NULL);}
}
int main(int argc,char **argv)
{
    if(argc!=7)return 2;
#ifdef _WIN32
    library=LoadLibraryA(argv[1]);
#else
    library=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
#endif
    CHECK(library);
    bool (*start)(ArcRpcModel,const uint8_t *,size_t,const uint8_t *,uint64_t,char *,size_t);
    bool (*factory)(ArcRpcModel,const uint8_t *,size_t,uint64_t,char *,size_t),(*advance)(uint64_t);
    void (*snapshot)(ArcRpcTestSnapshot *),(*capture)(ArcRpcCapture *);
    const ArcEngineBackendOps *(*engine)(void);
    LOAD(start,"arc_rpc_test_start");LOAD(factory,"arc_rpc_test_factory_start");LOAD(advance,"arc_rpc_test_advance");
    LOAD(snapshot,"arc_rpc_test_snapshot");LOAD(capture,"arc_rpc_test_capture_status");LOAD(engine,"arc_rpc_test_engine_api");
    LOAD(filter,"arc_rpc_observer_test_filter");
    uint8_t *rom=malloc(2097152);CHECK(rom);FILE *file=fopen(argv[2],"rb");CHECK(file);
    CHECK(fread(rom,1,2097152,file)==2097152);CHECK(fgetc(file)==EOF);CHECK(fclose(file)==0);
    ArcRpcModel model=(ArcRpcModel)strtoul(argv[3],NULL,10);unsigned rate=(unsigned)strtoul(argv[4],NULL,10);
    const char *mode=argv[5];unsigned observer=(unsigned)strtoul(argv[6],NULL,10);uint8_t seed[256]={0};char error[256];ArcRpcTestSnapshot s={0};
    const ArcEngineBackendOps *api=engine();CHECK(api);
    if(!strcmp(mode,"capture")){
        CHECK(factory(model,rom,2097152,0,error,sizeof(error)));
        CHECK(lockstep_observers(find,observer));
        const uint64_t deadline=UINT64_C(30000000000);
        while(s.now_ns<deadline-1){uint64_t delta=deadline-1-s.now_ns;if(delta>16666667)delta=16666667;CHECK(advance(delta));snapshot(&s);}
        ArcRpcCapture c;capture(&c);CHECK(c.state==ARC_RPC_CAPTURE_RUNNING);
        CHECK(advance(16666667));snapshot(&s);capture(&c);
        CHECK(s.now_ns==deadline && c.state==ARC_RPC_CAPTURE_DONE && c.elapsed_ns==deadline && c.writes>0);
        CHECK(advance(1000));snapshot(&s);CHECK(s.now_ns==deadline);
        printf("G-1 capture clipped now=%" PRIu64 " writes=%" PRIu64 "\n",s.now_ns,c.writes);
    }else if(!strcmp(mode,"limit")){
        CHECK(start(model,rom,2097152,seed,0,error,sizeof(error)));
        CHECK(lockstep_observers(find,observer));
        uint64_t first=(1000u+rate-1u)/rate;if(model==ARC_RPC_SA110 && first<63)first=63;
        CHECK(advance(first-1));snapshot(&s);CHECK(s.now_ns==first-1 && s.executed==0);
        CHECK(advance(1));snapshot(&s);CHECK(s.now_ns==first && s.executed==1);
        CHECK(advance(1));snapshot(&s);CHECK(s.executed==1);
        printf("G-1 NULL-batch first issue=%" PRIu64 " executed=%" PRIu64 "\n",first,s.executed);
    }else if(!strcmp(mode,"field")){
        CHECK(start(model,rom,2097152,seed,0,error,sizeof(error)));
        CHECK(lockstep_observers(find,observer));
        ArcInputState input={0};ArcEngineFrame frame={0};
        uint64_t entry=0;
        for(unsigned calls=0;calls<4;++calls){
            snapshot(&s);entry=s.now_ns;CHECK(api->run_frame(&input,&frame));
            if(frame.field_completed)break;
        }
        snapshot(&s);
        CHECK(frame.field_completed && frame.width==32 && frame.height==8 && s.now_ns-entry<=16666667 && s.executed>0);
        printf("G-1 field completed now=%" PRIu64 " executed=%" PRIu64 "\n",s.now_ns,s.executed);
    }else if(!strcmp(mode,"cheap-swi")){
        ArcRpcTestSnapshot before={0};
        for(unsigned leg=0;leg<2;++leg){
            CHECK(start(model,rom,2097152,seed,0,error,sizeof(error)));
            CHECK(lockstep_observers(find,observer));
            if(leg){ArcRpcObserveFilter f={.swi=0,.swi_mask=0xffffff,.modes=~0u,.kinds=ARC_RPC_OBS_SWI,.callback=unsubscribe};CHECK(filter(ARC_RPC_OBS_ERROR,&f));}
            CHECK(advance(16666667));CHECK(advance(16666667));snapshot(&s);
            if(!leg)before=s;
            else CHECK(s.now_ns==before.now_ns && s.executed==before.executed && s.idle_ns==before.idle_ns && s.irq==before.irq && s.fiq==before.fiq && !memcmp(s.regs,before.regs,sizeof(s.regs)));
            api->stop();
        }
        CHECK(callbacks==1);
        puts("G-1 cheap SWI: key swi=0 mask=0xffffff modes=all kinds=SWI; unsubscribe callback=1; guest projection identical");
    }else return 2;
    api->stop();free(rom);
#ifdef _WIN32
    FreeLibrary(library);
#else
    CHECK(dlclose(library)==0);
#endif
    return 0;
}
