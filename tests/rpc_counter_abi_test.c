/* SPDX-License-Identifier: GPL-2.0-only */
#include "rpc_session.h"
#include "rpc_profile.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
static HMODULE library;
#define SYM(name) ((void *)GetProcAddress(library,name))
#else
#include <dlfcn.h>
static void *library;
#define SYM(name) dlsym(library,name)
#endif
#define LOAD(var,name) do {void *p=SYM(name);assert(p);memcpy(&var,&p,sizeof(var));}while(0)
int main(int argc,char **argv)
{
    assert(argc==2);
#ifdef _WIN32
    library=LoadLibraryA(argv[1]);
#else
    library=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
#endif
    assert(library);
    bool (*exercise)(ArcRpcExercise *,unsigned,size_t),(*interval)(ArcRpcExercise *,unsigned,size_t);
    bool (*ranking)(ArcRpcExercise *,uint64_t *,unsigned,size_t);
    LOAD(exercise,"arc_rpc_test_exercise_v3");LOAD(interval,"arc_rpc_test_interval_exercise_v3");LOAD(ranking,"arc_rpc_test_ranking_v3");
    bool (*start)(ArcRpcModel,const uint8_t *,size_t,const uint8_t *,uint64_t,char *,size_t);
    bool (*advance)(uint64_t),(*endpoint)(uint64_t,uint64_t,bool);
    void (*deinit)(void);
    LOAD(start,"arc_rpc_test_start");LOAD(advance,"arc_rpc_test_advance");
    LOAD(endpoint,"arc_rpc_test_timing_endpoint");LOAD(deinit,"retro_deinit");
    uint8_t *rom=calloc(2097152,1),seed[256]={0};char error[256];assert(rom);
    rom[0]=0xfe;rom[1]=0xff;rom[2]=0xff;rom[3]=0xea;
    assert(start(ARC_RPC_ARM710,rom,2097152,seed,0,error,sizeof(error)));
    assert(endpoint(1,1,false));assert(advance(1000000));
    ArcRpcExercise valid;
    assert(exercise(&valid,ARC_RPC_COUNTER_ABI,sizeof(valid)));
    /* A completed endpoint makes the interval copy reachable. A missing ABI
     * check must fail these controls rather than hide behind !interval.done. */
    assert(interval(&valid,ARC_RPC_COUNTER_ABI,sizeof(valid)));
    ArcRpcExercise e,before;memset(&e,0xa5,sizeof(e));before=e;
    for(unsigned bad=0;bad<3;++bad){
        unsigned revision=bad==0?ARC_RPC_COUNTER_ABI-1:ARC_RPC_COUNTER_ABI;
        size_t size=bad==0?sizeof(e):bad==1?sizeof(e)-8:sizeof(e)+8;
        uint64_t attempts=0xabc;
        assert(!exercise(&e,revision,size) && !memcmp(&e,&before,sizeof(e)));
        assert(!interval(&e,revision,size) && !memcmp(&e,&before,sizeof(e)));
        assert(!ranking(&e,&attempts,revision,size) && !memcmp(&e,&before,sizeof(e)) && attempts==0xabc);
    }
    void *p=SYM("arc_rpc_profile_snapshot_v3");
    if(p){bool (*snapshot)(ArcRpcProfile *,unsigned,size_t);memcpy(&snapshot,&p,sizeof(snapshot));
        ArcRpcProfile out,saved;memset(&out,0x5a,sizeof(out));saved=out;
        assert(!snapshot(&out,0,sizeof(out)) && !memcmp(&out,&saved,sizeof(out)));
        assert(!snapshot(&out,ARC_RPC_COUNTER_ABI,sizeof(out)-8) && !memcmp(&out,&saved,sizeof(out)));
        assert(!snapshot(&out,ARC_RPC_COUNTER_ABI,sizeof(out)+8) && !memcmp(&out,&saved,sizeof(out)));
        assert(snapshot(&out,ARC_RPC_COUNTER_ABI,sizeof(out)));
        uint64_t attempts;
        assert(ranking(&valid,&attempts,ARC_RPC_COUNTER_ABI,sizeof(valid)));
    }
    deinit();free(rom);return 0;
}
