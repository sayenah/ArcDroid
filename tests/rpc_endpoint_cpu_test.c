/* SPDX-License-Identifier: GPL-2.0-only */
#define ARC_RPC_OBSERVERS_TEST
#include "rpc_observer_test.c"
#include "rpc_profile.h"
static void count_equal(void)
{
    ArcRpcProfile p;arc_rpc_profile_snapshot(&p);
    assert(!memcmp(&p.ranking,&arc_rpc_exercise,sizeof(arc_rpc_exercise)));
    assert(p.ranking_attempts==p.attempts);
    assert(p.ranking_fetched==p.fetched);
}
static void translation_counts(CPUModel model)
{
    fixture(model,19,0x100);arc_rpc_profile_configure(0,NULL);
    uint32_t va=0x00400100;ram00[0x4010/4]=0x10000402;
    enable(model,0);memmode=0;
    translateaddress(va,0,0);assert(arm.event&0x40); /* denied install is still a walk */
    assert(arc_rpc_exercise.values[ARC_RPC_COUNT_tlb_data_walk]==1);
    arm.event=0;translateaddress(va,0,0);assert(arm.event&0x40);
    assert(arc_rpc_exercise.values[ARC_RPC_COUNT_tlb_data_hit]==1);
    assert(!arc_rpc_exercise.values[ARC_RPC_COUNT_tlb_data_eligible]);
    memmode=1;arm.event=0;translateaddress(va,0,0);assert(!(arm.event&0x40));
    assert(arc_rpc_exercise.values[ARC_RPC_COUNT_tlb_data_hit]==2);
    assert(arc_rpc_exercise.values[ARC_RPC_COUNT_tlb_data_eligible]==1);
    arm.event=0x40;translateaddress(va,0,0);
    assert(arc_rpc_exercise.values[ARC_RPC_COUNT_tlb_data_hit]==3);
    assert(arc_rpc_exercise.values[ARC_RPC_COUNT_tlb_data_eligible]==1);
    arm.event=0;assert(getpccache(va));
    assert(arc_rpc_exercise.values[ARC_RPC_COUNT_tlb_fetch_hit]==1);
    assert(arc_rpc_exercise.values[ARC_RPC_COUNT_tlb_fetch_eligible]==1);
    arm.event=0x40;assert(!getpccache(va));
    assert(arc_rpc_exercise.values[ARC_RPC_COUNT_tlb_fetch_hit]==2);
    assert(arc_rpc_exercise.values[ARC_RPC_COUNT_tlb_fetch_eligible]==1);
    flush(model);assert(getpccache(va));
    assert(arc_rpc_exercise.values[ARC_RPC_COUNT_tlb_fetch_walk]==1);
    memmode=0;assert(!getpccache(va));
    assert(arc_rpc_exercise.values[ARC_RPC_COUNT_tlb_fetch_hit]==3);
    assert(arc_rpc_exercise.values[ARC_RPC_COUNT_tlb_fetch_eligible]==1);
    count_equal();
}
static void noop(void *p,const ArcRpcObservation *e) { (void)p;(void)e; }
static ArcRpcScheduler endpoint_clock;
static void execution_init(CPUModel model,unsigned rate,unsigned path)
{
    fixture(model,19,0x100);
    if(path){ArcRpcObserveFilter f={.modes=~0u,.kinds=path==1?ARC_RPC_OBS_MODE:ARC_RPC_OBS_SWI,.callback=noop};
        assert(arc_rpc_observer_test_filter(ARC_RPC_OBS_DISC,&f));}
    for(unsigned i=64;i<512;++i)rom[i]=0xe2800001; /* ADD r0,r0,#1 */
    assert(arc_rpc_scheduler_init(&endpoint_clock,rate));iomd.clkctrl=7;
    arc_rpc_mem_bind(&endpoint_clock,model,rate);arc_rpc_mem_clock_switch(true);
    arc_rpc_mem_fast.memo_valid=true;arc_rpc_mem_fast.memo_line=0x100>>arc_rpc_mem_fast.memo_shift;
    arc_rpc_profile_configure(0,NULL);
}
static void ready(void)
{
    while(!arc_rpc_mem_ready(false)){
        uint64_t next=arc_rpc_mem_next(UINT64_MAX,false);assert(next>endpoint_clock.now_ns);
        endpoint_clock.now_ns=next;arc_rpc_mem_run(next);
    }
}
static void endpoint_execution(CPUModel model)
{
    for(unsigned path=0;path<3;++path)for(unsigned rate_case=0;rate_case<2;++rate_case){
        unsigned rate=rate_case?35:100;
        ARMState expected;
        execution_init(model,rate,path);ready();ArcRpcCpuBatch batch={&endpoint_clock,NULL,100000};
        assert(arc_rpc_arm_exec_batch(3,&batch)==3);expected=arm;
        execution_init(model,rate,path);assert(arc_rpc_mem_endpoint(2,3,false));ready();
        assert(arc_rpc_arm_exec_batch(200,&batch)==1);ready();
        assert(arc_rpc_arm_exec_batch(200,&batch)==2);
        assert(!memcmp(&expected,&arm,sizeof(arm)));
        ArcRpcMemInterval t;arc_rpc_mem_interval(&t);
        assert(t.started && t.done && t.start_ticks==28000 && t.end_ticks==84000);
        assert(arc_rpc_arm_exec_batch(200,NULL)==0);
        unsigned index=(rate_case?ARC_RPC_COUNT_rational_unobserved:ARC_RPC_COUNT_integer_unobserved)+path;
        assert(arc_rpc_exercise.values[index]==(rate_case?1:3));
        count_equal();
        /* NULL-batch entry has the same ordinal bounds and never enters batch_next. */
        execution_init(model,rate,path);assert(arc_rpc_mem_endpoint(1,1,false));ready();
        assert(arc_rpc_arm_exec_count(200)==1);assert(arc_rpc_arm_exec_count(200)==0);
        arc_rpc_mem_interval(&t);assert(t.done && t.end_ticks==28000);
    }
}
static void ranking_reset(void)
{
    arc_rpc_profile_configure(0,NULL);arc_rpc_mem_bind(NULL,0,0);
    ArcRpcProfile p;arc_rpc_profile_snapshot(&p);assert(p.ranking_epoch==1);
    arc_rpc_exercise_add(ARC_RPC_COUNT_integer_unobserved,9);
    arc_rpc_profile_fetched(NULL,0xe2800001,true);
    arc_rpc_profile_snapshot(&p);assert(p.al==1 && p.ranking_fetched==1);
    arc_rpc_mem_bind(NULL,0,0);arc_rpc_profile_snapshot(&p);assert(p.ranking_epoch==2 && !p.ranking_attempts && !p.ranking_fetched && !p.al);
    for(unsigned i=0;i<ARC_RPC_COUNT_TOTAL;++i)assert(!p.ranking.values[i] && !arc_rpc_exercise.values[i]);
}
int main(void)
{
    arm_init();protected=1;if(setjmp(boundary))return 1;
    config.mem_size=64;config.vram_size=2;mem_init();
    for(unsigned model=0;model<4;++model){translation_counts((CPUModel)model);endpoint_execution((CPUModel)model);}
    ranking_reset();arc_rpc_mem_bind(NULL,0,0);arc_rpc_memory_close();
    puts("endpoint CPU: four models, three loops, integral/rational counts, prefix parity, NULL stop, TLB denied/pending/opportunity, schema-2 totals");return 0;
}
