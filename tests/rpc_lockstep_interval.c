/* Test-only intrusive interval loader for independent timing equations.
 * SPDX-License-Identifier: GPL-2.0-only */
#include "rpc_session.h"
#include "rpc_lockstep_observers.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
static HMODULE library;
static void *find(const char *name) { return (void *)GetProcAddress(library,name); }
#else
#include <dlfcn.h>
static void *library;
static void *find(const char *name) { return dlsym(library,name); }
#endif
#define LOAD(variable,name) do { void *p=find(name); if(!p){fprintf(stderr,"missing interval API: %s\n",name);return 2;} memcpy(&variable,&p,sizeof(variable)); } while(0)
int main(int argc,char **argv)
{
    if(argc!=11)return 2;
#ifdef _WIN32
    library=LoadLibraryA(argv[1]);
#else
    library=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
#endif
    if(!library)return 2;
    bool (*start)(ArcRpcModel,const uint8_t *,size_t,const uint8_t *,uint64_t,char *,size_t);
    bool (*advance)(uint64_t);
    void (*measure)(uint64_t,uint64_t,bool),(*interval)(ArcRpcMemInterval *),(*rate)(unsigned);
    LOAD(start,"arc_rpc_test_start");LOAD(advance,"arc_rpc_test_advance");
    LOAD(measure,"arc_rpc_test_timing_measure");LOAD(interval,"arc_rpc_test_timing_interval");
    LOAD(rate,"arc_rpc_session_test_rate");
    uint8_t *rom=calloc(1,2097152);if(!rom)return 2;
    FILE *file=fopen(argv[2],"rb");if(!file){free(rom);return 2;}
    size_t read=fread(rom,1,2097152,file);int extra=fgetc(file);int closed=fclose(file);
    if(read!=2097152 || extra!=EOF || closed){free(rom);return 2;}
    uint8_t seed[256]={0};char error[256];
    if(!start((ArcRpcModel)strtoul(argv[3],NULL,10),rom,2097152,seed,0,error,sizeof(error))){fprintf(stderr,"interval start: %s\n",error);free(rom);return 2;}
    if(!lockstep_observers(find,(unsigned)strtoul(argv[10],NULL,10))){free(rom);return 2;}
    rate((unsigned)strtoul(argv[4],NULL,10));
    uint64_t first=strtoull(argv[5],NULL,10),last=strtoull(argv[6],NULL,10);
    uint64_t numerator=strtoull(argv[8],NULL,10),denominator=strtoull(argv[9],NULL,10);
    if(!first || last<first || !denominator){free(rom);return 2;}
    measure(first,last,atoi(argv[7])!=0);
    ArcRpcMemInterval value={0};
    for(unsigned calls=0;!value.done && calls<1000;++calls){
        if(!advance(100000)){fprintf(stderr,"interval advance failed\n");free(rom);return 1;}
        interval(&value);
    }
    free(rom);
    if(!value.done || value.end.instructions-value.start.instructions!=last-first+1)return 1;
    uint64_t ticks=value.end_ticks-value.start_ticks;
    printf("ticks=%" PRIu64 " denominator=%u instructions=%" PRIu64 " words=%" PRIu64 "\n",ticks,value.end.denominator,
           value.end.instructions-value.start.instructions,value.end.words-value.start.words);
    if(numerator>UINT64_MAX/value.end.denominator || ticks>UINT64_MAX/denominator)return 2;
    return ticks*denominator==numerator*value.end.denominator?0:1;
}
