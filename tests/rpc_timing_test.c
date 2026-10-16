/* Authored-ROM D-139 calibration against independent rational expectations.
 * GPL-2.0-only. The core is observed, never used to calculate expected time. */
#include "rpc_session.h"
#include "rpc_timing_rom.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
static HMODULE library;
static void *symbol(const char *name) { return (void *)GetProcAddress(library,name); }
#else
#include <dlfcn.h>
static void *library;
static void *symbol(const char *name) { return dlsym(library,name); }
#endif
#define LOAD(var,name) do { void *p=symbol(name); assert(p); memcpy(&var,&p,sizeof(var)); } while(0)
static bool (*start)(ArcRpcModel,const uint8_t *,size_t,const uint8_t *,uint64_t,char *,size_t);
static bool (*advance)(uint64_t);
static void (*measure)(uint64_t,uint64_t,bool);
static bool (*exercise)(ArcRpcExercise *);
static bool (*endpoint)(uint64_t,uint64_t,bool);
static void (*interval)(ArcRpcMemInterval *);
static void (*rate)(unsigned);
static void (*snapshot)(ArcRpcTestSnapshot *);
int main(int argc,char **argv)
{
    assert(argc>=2);
#ifdef _WIN32
    library=LoadLibraryA(argv[1]);
#else
    library=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
#endif
    assert(library);
    LOAD(start,"arc_rpc_test_start"); LOAD(advance,"arc_rpc_test_advance");
    LOAD(measure,"arc_rpc_test_timing_measure"); LOAD(interval,"arc_rpc_test_timing_interval");
    LOAD(endpoint,"arc_rpc_test_timing_endpoint");LOAD(exercise,"arc_rpc_test_interval_exercise");
    LOAD(rate,"arc_rpc_session_test_rate"); LOAD(snapshot,"arc_rpc_test_snapshot");
    uint8_t *rom=malloc(2097152); assert(rom); uint8_t seed[256]={0}; char error[256];
    unsigned tested=0;
    uint64_t integer=0,rational=0,s1=0;
    for(unsigned i=0;i<sizeof(rpc_timing_cases)/sizeof(rpc_timing_cases[0]);++i) {
        const RpcTimingCase *c=&rpc_timing_cases[i];
        if(argc>2 && c->model!=(unsigned)atoi(argv[2]))continue;
        if(argc>3 && !strstr(c->name,argv[3]))continue;
        memset(rom,0,2097152);
        for(unsigned j=0;j<c->count;++j)for(unsigned byte=0;byte<4;++byte)
            rom[4*j+byte]=(uint8_t)(c->words[j]>>(8*byte));
        ArcRpcMemInterval reference={0};
        for (unsigned mode=0; mode<2; ++mode) {
        assert(start((ArcRpcModel)c->model,rom,2097152,seed,0,error,sizeof(error)));
        rate(c->rate);
        if (mode) assert(endpoint(c->first,c->last,c->drained!=0));
        else measure(c->first,c->last,c->drained!=0);
        ArcRpcMemInterval t={0}; unsigned calls=0;
        while(!t.done && calls<200000) {
            if(!advance((calls%3==1)?333333u:1000000u)) {
                ArcRpcTestSnapshot s; snapshot(&s);
                fprintf(stderr,"fault model=%u %s at %" PRIu64 " instructions=%" PRIu64 " pc=%08x\n",c->model,c->name,s.now_ns,s.executed,s.regs[15]);return 1;
            }
            interval(&t);++calls;
        }
        uint64_t actual=t.end_ticks-t.start_ticks;
        uint64_t expected=c->numerator*t.end.denominator/c->denominator;
        if(!t.done || actual!=expected || t.end.instructions-t.start.instructions!=c->last-c->first+1) {
            fprintf(stderr,"model=%u rate=%u %s: done=%u first=%" PRIu64 " last=%" PRIu64 " actual=%" PRIu64 "/%u expected=%" PRIu64 "/%" PRIu64 " instructions=%" PRIu64 " words=%" PRIu64 " fills=%" PRIu64 " packets=%" PRIu64 "\n",
                c->model,c->rate,c->name,t.done,c->first,c->last,actual,t.end.denominator,c->numerator,c->denominator,
                t.end.instructions-t.start.instructions,t.end.words-t.start.words,t.end.fills-t.start.fills,t.end.packets-t.start.packets);return 1;
        }
        if(mode){
            ArcRpcExercise e;assert(exercise(&e));
            for(unsigned path=0;path<3;++path){integer+=e.values[ARC_RPC_COUNT_integer_unobserved+path];rational+=e.values[ARC_RPC_COUNT_rational_unobserved+path];}
            s1+=e.values[ARC_RPC_COUNT_service_s1];
        }
        if(strstr(c->name,"partial-ldm-abort")||strstr(c->name,"partial-stm-abort")) {
            assert(t.end.words-t.start.words==2);
            ArcRpcTestSnapshot s;snapshot(&s);assert((s.regs[16]&31)==23);
        }
        if(strstr(c->name,"hot-dma-")) {
            assert(t.end.words==t.start.words && t.end.fills==t.start.fills);
            assert(t.end.packets==t.start.packets); /* hot CPU; only device DMA holds */
        }
        if(strstr(c->name,"rom-castout-")) {
            assert(t.end.words-t.start.words==268);
            assert(t.end.fills-t.start.fills==33);
            assert(t.end.dirty_halves-t.start.dirty_halves==1);
            assert(t.end.packets-t.start.packets==1);
            /* The priming and measured store have a value different from the
             * zero-filled ROM location. No functional ROM write is allowed. */
            ArcRpcTestSnapshot s; snapshot(&s); assert(s.regs[3]==0);
            measure(0,0,false); assert(advance(100000));
            snapshot(&s); assert(s.regs[3]==0);
        }
        if (!mode) reference=t;
        else if (memcmp(&reference,&t,sizeof(t))) {
            fprintf(stderr,"interval mismatch model=%u %s start=%" PRIu64 "/%" PRIu64 " end=%" PRIu64 "/%" PRIu64 "\n",c->model,c->name,reference.start_ticks,t.start_ticks,reference.end_ticks,t.end_ticks);
#define ARC_RPC_DIFFERENCE(part,field) if(reference.part.field!=t.part.field)fprintf(stderr,#part "." #field ": %" PRIu64 "/%" PRIu64 "\n",(uint64_t)reference.part.field,(uint64_t)t.part.field)
#define ARC_RPC_BOTH(field) ARC_RPC_DIFFERENCE(start,field);ARC_RPC_DIFFERENCE(end,field)
            ARC_RPC_BOTH(instructions);ARC_RPC_BOTH(words);ARC_RPC_BOTH(fills);ARC_RPC_BOTH(dirty_halves);ARC_RPC_BOTH(packets);ARC_RPC_BOTH(hits);
            ARC_RPC_BOTH(completed_ticks);ARC_RPC_BOTH(buffer_wait_ticks);ARC_RPC_BOTH(buffered_words);ARC_RPC_BOTH(buffered_packets);ARC_RPC_BOTH(denominator);
#undef ARC_RPC_BOTH
#undef ARC_RPC_DIFFERENCE
            return 1;
        }
        }
        ++tested;
    }
    if(argc<3 || atoi(argv[2])==0){
        memset(rom,0,2097152);rom[0]=0xfe;rom[1]=0xff;rom[2]=0xff;rom[3]=0xea;
        ArcRpcTestSnapshot ordinary,armed;
        assert(start(ARC_RPC_ARM610,rom,2097152,seed,0,error,sizeof(error)));rate(100);
        assert(advance(9));snapshot(&ordinary);assert(ordinary.executed==0);
        assert(advance(1));snapshot(&ordinary);assert(ordinary.executed==1);
        assert(start(ARC_RPC_ARM610,rom,2097152,seed,0,error,sizeof(error)));rate(100);
        assert(endpoint(1,1,false));assert(advance(9));snapshot(&armed);assert(!armed.executed);
        assert(advance(1));snapshot(&armed);assert(armed.executed==1);
        assert(!memcmp(ordinary.regs,armed.regs,sizeof(ordinary.regs)));
        ArcRpcMemInterval t={0};for(unsigned j=0;j<100 && !t.done;++j){assert(advance(1000));interval(&t);}
        snapshot(&armed);assert(t.done && armed.executed==1);
        fprintf(stderr,"endpoint driver: one functional instruction exactly at the 10 ns limit, no extra issue\n");
    }
    free(rom); assert(tested);
    fprintf(stderr,"endpoint measured intervals: integer=%" PRIu64 " rational=%" PRIu64 " s1=%" PRIu64 "\n",integer,rational,s1);
    printf("rpc timing: %u authored workloads matched exact independent rational durations\n",tested);
    return 0;
}
