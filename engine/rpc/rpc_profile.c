/* Test-only bounded aggregate profiling; no guest intervention. GPL-2.0-only. */
#include "rpc_profile.h"
#include "mem.h"
#include "libretro.h"
#include <string.h>

/* The imported allocations have fixed bounds. History holds no borrowed pointer. */
#define PAGE_COUNT ((8u+32u+32u+2u)*256u)
static struct {
    ArcRpcProfile counts;
    uint64_t fetched_at[PAGE_COUNT];
    uint8_t written[PAGE_COUNT];
    unsigned stride, cost, causes, instruction_class, depth;
    uint32_t pc, mode, masks;
    uint64_t stamp, root_calls[ARC_RPC_COST_COUNT];
    uint64_t probes[ARC_RPC_COST_COUNT], calibration_ns, calibration_samples;
    bool enabled, attempt, sampling, passed, fetched, pc_destination;
    uint64_t (*clock_ns)(void);
} profile;


void arc_rpc_profile_rank_reset(void)
{
    if(!profile.enabled)return;
    memset(&profile.counts.ranking,0,sizeof(profile.counts.ranking));
    profile.counts.ranking_attempts=0;
    profile.counts.ranking_fetched=0;profile.counts.al=0;
    ++profile.counts.ranking_epoch;
}
void arc_rpc_profile_rank(unsigned index,uint64_t amount)
{ if(profile.enabled)profile.counts.ranking.values[index]+=amount; }
static uint64_t clock_read(void)
{ ++profile.counts.clock_reads; return profile.clock_ns(); }
static void charge(void)
{
    uint64_t now=clock_read();
    if(profile.cost<ARC_RPC_COST_COUNT){
        ++profile.probes[profile.cost];
        if(now>=profile.stamp)profile.counts.sampled_ns[profile.cost]+=now-profile.stamp;
    }
    profile.stamp=now;
}
int arc_rpc_profile_enter(unsigned cost)
{
    if(!profile.enabled)return -1;
    if(cost==ARC_RPC_COST_BUS)++profile.counts.bus_calls;
    int previous=(int)profile.cost;
    if(!profile.attempt && !profile.depth){
        uint64_t n=++profile.root_calls[cost];
        profile.sampling=profile.stride && (n-1)%profile.stride==0;
        if(profile.sampling)profile.stamp=clock_read();
    }
    ++profile.depth;
    if(!profile.sampling)return -2;
    charge(); profile.cost=cost; ++profile.counts.samples[cost];return previous;
}
void arc_rpc_profile_leave(int previous)
{
    if(previous==-1)return;
    --profile.depth;
    if(previous==-2)return;
    charge();profile.cost=(unsigned)previous;
    if(profile.cost==ARC_RPC_COST_COUNT)profile.sampling=false;
}
void arc_rpc_profile_stage(unsigned cost)
{
    if(!profile.sampling)return;
    charge();profile.cost=cost;++profile.counts.samples[cost];
}
RETRO_API void arc_rpc_profile_configure(unsigned stride,uint64_t (*clock_ns)(void))
{
    memset(&profile,0,sizeof(profile));profile.enabled=true;profile.stride=clock_ns?stride:0;profile.clock_ns=clock_ns;
    if(profile.stride){
        /* Outside requested execution: measure the actual empty charge path.
         * Preserve raw timings; callers may assess probe bias explicitly. */
        profile.cost=ARC_RPC_COST_PROFILE;profile.stamp=clock_read();
        for(unsigned i=0;i<8192;++i)charge();
        profile.calibration_ns=profile.counts.sampled_ns[ARC_RPC_COST_PROFILE];
        profile.calibration_samples=8192;
        memset(&profile.counts,0,sizeof(profile.counts));
        memset(profile.probes,0,sizeof(profile.probes));
    }
    profile.cost=ARC_RPC_COST_COUNT;
}
RETRO_API void arc_rpc_profile_probe_snapshot(uint64_t out[ARC_RPC_COST_COUNT+2])
{
    if(!out)return;
    memcpy(out,profile.probes,sizeof(profile.probes));
    out[ARC_RPC_COST_COUNT]=profile.calibration_ns;
    out[ARC_RPC_COST_COUNT+1]=profile.calibration_samples;
}
RETRO_API void arc_rpc_profile_snapshot(ArcRpcProfile *out)
{ if(out)*out=profile.counts; }
static void close_block(void)
{
    uint64_t length=profile.counts.open_block;
    if(!length)return;
    ++profile.counts.blocks[length>64?64:(unsigned)length-1];
    profile.counts.block_instructions+=length;
    if(length>profile.counts.block_max)profile.counts.block_max=length;
    profile.counts.open_block=0;
}
void arc_rpc_profile_reset(void)
{
    if(!profile.enabled)return;
    if(profile.counts.open_block){
        ++profile.counts.censored_blocks;profile.counts.censored_instructions+=profile.counts.open_block;
        profile.counts.open_block=0;
    }
    ++profile.counts.resets;++profile.counts.epoch;
    memset(profile.fetched_at,0,sizeof(profile.fetched_at));
    memset(profile.written,0,sizeof(profile.written));
    profile.attempt=profile.sampling=false;profile.depth=0;profile.cost=ARC_RPC_COST_COUNT;
}
void arc_rpc_profile_fault(void)
{
    if(!profile.enabled)return;
    ++profile.counts.faults;++profile.counts.exits[ARC_RPC_EXIT_E8];
    if(profile.sampling)++profile.counts.abandoned_samples;
    close_block();profile.attempt=profile.sampling=false;profile.depth=0;profile.cost=ARC_RPC_COST_COUNT;
}
void arc_rpc_profile_begin(uint32_t pc,uint32_t mode,uint32_t masks)
{
    if(!profile.enabled)return;
    bool sampling=profile.stride && profile.counts.attempts%profile.stride==0;
    if(sampling)profile.stamp=clock_read();
    ++profile.counts.attempts;++profile.counts.ranking_attempts;++profile.counts.open_block;
    profile.pc=pc;profile.mode=mode;profile.masks=masks;
    profile.causes=0;profile.attempt=true;profile.passed=profile.fetched=false;
    profile.instruction_class=12;profile.pc_destination=false;
    profile.sampling=sampling;profile.cost=ARC_RPC_COST_PROFILE;
    if(sampling)++profile.counts.samples[ARC_RPC_COST_PROFILE];
}
/* ALU, multiply, branch, load, store, LDM, STM, swap, half/signed,
 * status, coprocessor, SWI, other. Decoder is diagnostic, never a dispatch. */
static unsigned classify(uint32_t op)
{
    if((op&0x0f000000u)==0x0f000000u)return 11;
    if((op&0x0e000000u)==0x0a000000u)return 2;
    if((op&0x0e000000u)==0x08000000u)return op&(1u<<20)?5:6;
    if((op&0x0c000000u)==0x04000000u)return op&(1u<<20)?3:4;
    if((op&0x0c000000u)==0x0c000000u)return 10;
    if((op&0x0fb00ff0u)==0x01000090u)return 7;
    if((op&0x0fc000f0u)==0x00000090u || (op&0x0f8000f0u)==0x00800090u)return 1;
    if((op&0x0e000090u)==0x00000090u && (op&0x60))return 8;
    if((op&0x0fbf0fffu)==0x010f0000u || (op&0x0db0f000u)==0x0120f000u)return 9;
    return (op&0x0c000000u)==0?0:12;
}
static unsigned backing_page(const void *base,uint32_t offset)
{
    unsigned start;
    if(base==rom)start=0;
    else if(base==ram00)start=8u*256u;
    else if(base==ram01)start=40u*256u;
    else if(base==vram && vram)start=72u*256u;
    else return PAGE_COUNT;
    return start+(offset>>12);
}
void arc_rpc_profile_fetched(const uint32_t *page,uint32_t op,bool passed)
{
    if(!profile.enabled)return;
    if((op>>28)==14)++profile.counts.al;
    ++profile.counts.ranking_fetched;
    ++profile.counts.fetched;profile.fetched=true;profile.passed=passed;
    unsigned c=classify(op);profile.instruction_class=c;++profile.counts.classes[c];
    if(passed)++profile.counts.condition_passed;
    /* uintptr_t ordering avoids relational comparisons between allocations. */
    const uint32_t *bases[]={rom,ram00,ram01,vram};
    const unsigned sizes[]={8u<<20,32u<<20,32u<<20,2u<<20};
    for(unsigned i=0;i<4;++i){
        uintptr_t b=(uintptr_t)bases[i],p=(uintptr_t)page;
        if(b && p>=b && p-b<sizes[i]){
            unsigned key=backing_page(bases[i],(uint32_t)(p-b));
            profile.fetched_at[key]=profile.counts.attempts;break;
        }
    }
    if(!passed)return;
    if(c==2){++profile.counts.branches;profile.causes|=1u<<ARC_RPC_EXIT_E1;}
    bool pc_write=(c==0 && ((op>>12)&15)==15 && (((op>>21)&15)<8 || ((op>>21)&15)>11)) ||
        ((c==3 || c==8) && (op&(1u<<20)) && ((op>>12)&15)==15) ||
        (c==5 && (op&(1u<<15)));
    profile.pc_destination=pc_write;
    if(c>=5 && c<=10)profile.causes|=1u<<ARC_RPC_EXIT_E3;
}
void arc_rpc_profile_exception(uint32_t vector)
{
    if(!profile.enabled || !profile.attempt)return;
    unsigned cause=vector==0x10?ARC_RPC_EXIT_E7B:vector==0x14?ARC_RPC_EXIT_E7A:
        vector==0x1c || vector==0x20?ARC_RPC_EXIT_E6:ARC_RPC_EXIT_E10;
    profile.causes|=1u<<cause;
}
void arc_rpc_profile_side_exit(void)
{ if(profile.enabled && profile.attempt)profile.causes|=1u<<ARC_RPC_EXIT_E4; }
void arc_rpc_profile_maintenance(void)
{ if(profile.enabled && profile.attempt)profile.causes|=1u<<ARC_RPC_EXIT_E11; }
void arc_rpc_profile_access(uint32_t physical,bool write)
{
    if(!profile.enabled || !profile.attempt)return;
    if(write)++profile.counts.writes;else ++profile.counts.reads;
    (void)physical;
}
void arc_rpc_profile_memo(void) { if(profile.enabled)++profile.counts.fetch_memo_hits; }
void arc_rpc_profile_fill(void) { if(profile.enabled)++profile.counts.cache_fills; }
void arc_rpc_profile_mmio(bool write)
{
    if(!profile.enabled || !profile.attempt)return;
    if(write)++profile.counts.mmio_writes;else ++profile.counts.mmio_reads;
    profile.causes|=1u<<ARC_RPC_EXIT_E4;
}

void arc_rpc_profile_store(const void *base,uint32_t offset)
{
    if(!profile.enabled || !profile.attempt)return;
    unsigned key=backing_page(base,offset);
    if(key>=PAGE_COUNT)return;
    uint64_t at=profile.fetched_at[key];
    if(at && profile.counts.attempts-at<=ARC_RPC_PROFILE_RECENT){
        ++profile.counts.recent_writes;
        if(!profile.written[key]){profile.written[key]=1;++profile.counts.recent_pages;}
        profile.causes|=1u<<ARC_RPC_EXIT_E9;
    }
}
void arc_rpc_profile_end(uint32_t pc,uint32_t mode,uint32_t masks,bool idle)
{
    if(!profile.enabled)return;
    if(profile.pc_destination && !(profile.causes&((1u<<ARC_RPC_EXIT_E7A)|(1u<<ARC_RPC_EXIT_E7B)|(1u<<ARC_RPC_EXIT_E10)))){
        ++profile.counts.pc_writes;profile.causes|=1u<<ARC_RPC_EXIT_E1;
    }
    if(mode!=profile.mode || masks!=profile.masks || idle)profile.causes|=1u<<ARC_RPC_EXIT_E4;
    if(pc!=profile.pc+4 && !profile.causes)profile.causes|=1u<<ARC_RPC_EXIT_E1;
    unsigned c=profile.instruction_class;
    if(profile.fetched && (!profile.passed || c<=4) &&
        !(profile.causes&~(1u<<ARC_RPC_EXIT_E1)))++profile.counts.candidates;
    for(unsigned i=0;i<ARC_RPC_PROFILE_EXITS;++i)
        if(profile.causes&(1u<<i))++profile.counts.exits[i];
    if(profile.causes)close_block();
    if(profile.sampling)charge();
    profile.attempt=profile.sampling=false;profile.depth=0;profile.cost=ARC_RPC_COST_COUNT;
}
void arc_rpc_profile_batch(bool quota,bool idle,bool stopped)
{
    if(!profile.enabled)return;
    ++profile.counts.exits[ARC_RPC_EXIT_E5];
    if(quota)++profile.counts.batch_quota;else ++profile.counts.batch_boundary;
    if(idle)++profile.counts.batch_idle;
    if(stopped)++profile.counts.batch_test_stop;
}
void arc_rpc_profile_event(unsigned event)
{ if(profile.enabled && event<ARC_RPC_PROFILE_EVENTS)++profile.counts.events[event]; }
