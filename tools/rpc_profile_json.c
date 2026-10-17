/* Aggregates only: no guest bytes, addresses or private identity. GPL-2.0-only. */
#include "rpc_profile_json.h"
#include <inttypes.h>
#include <stdlib.h>
static int compare(const void *a,const void *b)
{ uint64_t x=*(const uint64_t *)a,y=*(const uint64_t *)b;return (x>y)-(x<y); }
static void named(FILE *f,const char *key,const char *const *names,const uint64_t *values,unsigned n)
{
    fprintf(f,",\"%s\":{",key);
    for(unsigned i=0;i<n;++i)fprintf(f,"%s\"%s\":%" PRIu64,i?",":"",names[i],values[i]);
    fputc('}',f);
}
void arc_runner_profile_json(FILE *f,const ArcRpcProfile *p,uint64_t frame,uint64_t guest_ns,
    const char *kind,uint64_t *times,size_t n,const uint64_t *probes)
{
    static const char *const classes[]={"alu","multiply","branch","load","store","ldm","stm","swap","half_signed","status","coprocessor","swi","other"};
    static const char *const exits[]={"E1","E2","E3","E4","E5","E6","E7a","E7b","E8","E9","E10","E11"};
    static const char *const costs[]={"fetch_translation","dispatch","timing","bus_service","mmio","devices","profiling"};
    static const char *const events[]={"timer0","timer1","keyboard","mouse","fdc","ide","sound","video","input"};
    _Static_assert(sizeof(events)/sizeof(events[0]) == ARC_RPC_EVENT_COUNT, "profile JSON event names");
    uint64_t total=0;for(size_t i=0;i<n;++i)total+=times[i];
    if(n)qsort(times,n,sizeof(*times),compare);
    fprintf(f,"{\"kind\":\"%s\",\"frame\":%" PRIu64 ",\"guest_ns\":%" PRIu64,kind,frame,guest_ns);
#define VALUE(field) fprintf(f,",\"" #field "\":%" PRIu64,p->field)
    VALUE(ranking_attempts);VALUE(ranking_fetched);VALUE(ranking_epoch);VALUE(al);VALUE(epoch);VALUE(attempts);VALUE(fetched);VALUE(condition_passed);VALUE(candidates);
    VALUE(block_instructions);VALUE(block_max);VALUE(open_block);VALUE(censored_blocks);VALUE(censored_instructions);
    VALUE(fetch_memo_hits);VALUE(cache_fills);VALUE(bus_calls);
    VALUE(branches);VALUE(pc_writes);VALUE(reads);VALUE(writes);VALUE(mmio_reads);VALUE(mmio_writes);
    VALUE(recent_writes);VALUE(recent_pages);VALUE(resets);VALUE(faults);
    VALUE(batch_quota);VALUE(batch_boundary);VALUE(batch_idle);VALUE(batch_test_stop);
    VALUE(clock_reads);VALUE(abandoned_samples);
#undef VALUE
    fputs(",\"ranking\":{",f);
    static const char *const rank_names[]={
#define RANK_NAME(name) #name,
        ARC_RPC_COUNTER_NAMES(RANK_NAME)
#undef RANK_NAME
    };
    for(unsigned i=0;i<ARC_RPC_COUNT_SCALARS;++i)fprintf(f,"%s\"%s\":%" PRIu64,i?",":"",rank_names[i],p->ranking.values[i]);
    for(unsigned caller=0;caller<2;++caller){
        fprintf(f,",\"lookup_%s_ways\":[",caller?"maintenance":"access");
        for(unsigned way=0;way<64;++way)fprintf(f,"%s%" PRIu64,way?",":"",p->ranking.values[ARC_RPC_COUNT_ACCESS_WAYS+64*caller+way]);
        fputc(']',f);
    }
    fputc('}',f);
    named(f,"classes",classes,p->classes,ARC_RPC_PROFILE_CLASSES);
    named(f,"exits",exits,p->exits,ARC_RPC_PROFILE_EXITS);
    named(f,"sampled_ns",costs,p->sampled_ns,ARC_RPC_COST_COUNT);
    named(f,"samples",costs,p->samples,ARC_RPC_COST_COUNT);
    named(f,"events",events,p->events,ARC_RPC_PROFILE_EVENTS);
    named(f,"probes",costs,probes,ARC_RPC_COST_COUNT);
    fprintf(f,",\"probe_calibration_ns\":%" PRIu64 ",\"probe_calibration_iterations\":%" PRIu64,
        probes[ARC_RPC_COST_COUNT],probes[ARC_RPC_COST_COUNT+1]);
    fputs(",\"blocks\":[",f);
    for(unsigned i=0;i<ARC_RPC_PROFILE_BUCKETS;++i)fprintf(f,"%s%" PRIu64,i?",":"",p->blocks[i]);
    fprintf(f,"],\"frames\":%zu,\"core_ns\":%" PRIu64 ",\"p50_ns\":%" PRIu64 ",\"p95_ns\":%" PRIu64 ",\"max_ns\":%" PRIu64 "}",
        n,total,n?times[(n*50+99)/100-1]:0,n?times[(n*95+99)/100-1]:0,n?times[n-1]:0);
}
