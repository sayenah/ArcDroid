/* SPDX-License-Identifier: GPL-2.0-only */
/* Authored host CPU-time benchmark: real lookup/tag writer, no observers,
 * counters, scheduler service, ROM or title data. Build with shipping flags. */
#include "../engine/rpc/rpc_mem_timing.c"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
struct iomd iomd;
void fatal(const char *format,...)
{ va_list ap;va_start(ap,format);vfprintf(stderr,format,ap);va_end(ap);abort(); }
int main(int argc,char **argv)
{
    unsigned iterations=argc>1?(unsigned)strtoul(argv[1],NULL,10):5000000;
    const unsigned models[]={1,3,2,2},gates[]={0,0,0,3};
    ArcRpcScheduler scheduler;
    for(unsigned k=0;k<4;++k){
        arc_rpc_scheduler_init(&scheduler,100);
        arc_rpc_mem_bind(&scheduler,models[k],100);
        Cache *c=&m.instruction;lookup_side(c)->gate=gates[k];
        uint64_t hits=0,checksum=0;clock_t begin=clock();
        for(unsigned n=0;n<iterations;++n){
            uint32_t va=0x10000000u+(n<<c->shift),line=va>>c->shift;
            Tag *hit=lookup(c,va);hits+=hit!=NULL;
            unsigned set=line&(c->sets-1),way=c->victim[set],slot=set*c->ways+way;
            if(!c->tags[slot].valid)++c->populated;
            tag_write(c,slot,(Tag){line,va,1,0});
            c->victim[set]=(uint8_t)((way+1)&(c->ways-1));
            checksum+=c->tags[slot].va;
        }
        double seconds=(double)(clock()-begin)/CLOCKS_PER_SEC;
        if(hits || !seconds)return 1;
        printf("model=%u gate=%u operations=%u ns_per_lookup_fill=%.3f checksum=%llu\n",
            models[k],gates[k],iterations,seconds*1e9/iterations,(unsigned long long)checksum);
    }
    return 0;
}
