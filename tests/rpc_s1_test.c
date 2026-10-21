/* SPDX-License-Identifier: GPL-2.0-only */
/* S1 batch-path oracle: known equations plus full raw state replay. */
#include "../engine/rpc/rpc_mem_timing.c"
#undef NDEBUG
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
struct iomd iomd;
void fatal(const char *format, ...)
{ va_list ap; va_start(ap,format); vfprintf(stderr,format,ap); va_end(ap); abort(); }
static ArcRpcScheduler clock_state;
typedef struct {
    __typeof__(m) memory;
    ArcRpcMemFast fast;
    ArcRpcScheduler clock;
    ArcRpcMeasureState measurement;
    ArcRpcExercise counters;
    uint64_t services;
    bool result;
} Result;
static void init(unsigned model)
{
    memset(&iomd,0,sizeof(iomd)); iomd.clkctrl=7;
    assert(arc_rpc_scheduler_init(&clock_state,100));
    arc_rpc_mem_bind(&clock_state,model,100);
    arc_rpc_mem_clock_switch(true);
    arc_rpc_mem_control(0x100d);
    uint64_t d=arc_rpc_mem_fast.denominator;
    clock_state.now_ns=10;
    arc_rpc_mem_fast.cpu_due_ns=20;
    arc_rpc_mem_fast.cpu_due_ticks=20*d;
    arc_rpc_mem_fast.due_floor=20;
    arc_rpc_mem_fast.due_fraction=0;
    arc_rpc_mem_fast.bus_busy=true;
    m.cycle_active=m.active_valid=true;
    m.finish=100*d+1;
    m.active_kind=BUFFERED; m.bcount=1; m.bwords=1;
    m.active=(Packet){.kind=BUFFERED,.count=1,.width={4},.address={0x10000000}};
    m.buffer[0]=m.active;
    m.grant_left=1;
}
static Result capture(bool result)
{
#if defined(ARCDROID_RPC_CROSSCHECK)
    uint64_t calls, failed;
    const char *field=arc_rpc_crosscheck_status(&calls,&failed);
    if(field){fprintf(stderr,"S1 cross-check mismatch: %s\n",field);exit(1);}
#endif
    Result r={0};r.memory=m;r.fast=arc_rpc_mem_fast;r.clock=clock_state;
    arc_rpc_mem_measure_state(&r.measurement);r.counters=arc_rpc_exercise;
    r.services=exercise_batch.services;r.result=result;return r;
}
static void equal(const Result *a,const Result *b)
{
    assert(a->result==b->result);
    assert(!memcmp(&a->memory,&b->memory,sizeof(m)));
    assert(!memcmp(&a->fast,&b->fast,sizeof(a->fast)));
    assert(!memcmp(&a->clock,&b->clock,sizeof(a->clock)));
    assert(arc_rpc_mem_measure_equal(&a->measurement,&b->measurement));
    ArcRpcExercise left=a->counters,right=b->counters;
    /* Only the intentionally excluded taken scalar differs in on/off replay. */
    left.values[ARC_RPC_COUNT_service_s1_taken]=right.values[ARC_RPC_COUNT_service_s1_taken]=0;
    assert(!memcmp(&left,&right,sizeof(left)));
    assert(a->services==b->services);
}
/* Each setup is replayed, never restored from a snapshot containing addresses. */
static void matrix(void)
{
    const char *names[]={"busy", "next tie", "limit tied video", "finish tie",
        "fraction below ceiling", "same visible ns", "queued below finish",
        "queued at finish", "full buffer", "overdue", "due now", "measure gate",
        "pending demand behind buffer", "video active", "no active cycle"};
    for(unsigned model=0;model<4;++model)for(unsigned k=0;k<15;++k){
        Result reference={0};
        for(unsigned off=1;;--off){
            init(model);arc_rpc_mem_s1_force_off(off!=0);
            uint64_t d=arc_rpc_mem_fast.denominator;
            ArcRpcCpuBatch batch={&clock_state,NULL,1000};
            if(k==1)clock_state.next_ns=20;
            if(k==2){batch.limit_ns=20;m.queued_video=50*d;}
            if(k==3 || k==7) {
                arc_rpc_mem_fast.cpu_due_ns=101;arc_rpc_mem_fast.due_floor=101;
                arc_rpc_mem_fast.cpu_due_ticks=101*d;
            }
            if(k==4 || k==5){
                arc_rpc_mem_fast.cpu_due_ns=k==4?100:101;
                arc_rpc_mem_fast.cpu_due_ticks=100*d+(k==4?-1:2);
                arc_rpc_mem_fast.due_floor=arc_rpc_mem_fast.cpu_due_ticks/d;
                arc_rpc_mem_fast.due_fraction=arc_rpc_mem_fast.cpu_due_ticks%d;
                m.finish=100*d+10;
            }
            if(k==6 || k==7)m.queued_video=50*d;
            if(k==8){m.bcount=model==0?2:model==2?8:4;m.bwords=m.bcount;
                for(unsigned n=0;n<m.bcount;++n)m.buffer[n]=m.active;}
            if(k==9 || k==10){arc_rpc_mem_fast.cpu_due_ns=k==9?9:10;
                arc_rpc_mem_fast.cpu_due_ticks=arc_rpc_mem_fast.cpu_due_ns*d;
                arc_rpc_mem_fast.due_floor=arc_rpc_mem_fast.cpu_due_ns;}
            if(k==11)arc_rpc_mem_measure(0,1,false),arc_rpc_mem_fast.instructions=1;
            if(k==12){
                arc_rpc_mem_fast.pending=true;m.count=1;
                m.command[0]=(Packet){.kind=DEMAND,.count=1,.width={4},.address={0x10008000}};
            }
            if(k==13){m.cycle_active=m.active_valid=false;m.video_active=true;}
            if(k==14){m.cycle_active=m.active_valid=false;}
            bool result=k==11?arc_rpc_mem_batch_service(&batch):arc_rpc_mem_batch_next(&batch,false);
            Result r=capture(result);
            bool eligible=k==0 || k==4 || k==6 || k==8;
            assert(r.counters.values[ARC_RPC_COUNT_service_s1]==eligible || k==11);
            assert(r.counters.values[ARC_RPC_COUNT_service_s1_taken]==(eligible && !off));
            if(k==1 || k==2)assert(!result && clock_state.now_ns==10 && m.cycle_active);
            if(k==0 || k==4 || k==6 || k==8){
                assert(result && clock_state.now_ns==(k==4?100u:20u));
                assert(m.cycle_active && !m.video_active && m.active.index==0);
                assert(m.queued_video==(k==6?50*d:0));
                assert(exercise_batch.services==1 && arc_rpc_mem_fast.bus_busy && !m.restart);
            }
            if(k==3 || k==5)assert(result && !m.cycle_active && m.stats.words==0);
            if(k==7)assert(result && clock_state.now_ns==151 && !m.video_active && !m.queued_video && m.restart);
            if(k==9 || k==10)assert(result && clock_state.now_ns==10);
            if(k==11)assert(!result && clock_state.now_ns==101);
            if(k==12)assert(result && !arc_rpc_mem_fast.pending);
#if defined(ARCDROID_RPC_CROSSCHECK)
            if (eligible && !off) {
                uint64_t calls=0,failed=0;
                assert(!arc_rpc_crosscheck_status(&calls,&failed) && calls>0 && !failed);
                printf("S1 site model=%u case=%u comparisons=%llu\n",model,k,(unsigned long long)calls);
            }
#endif
            if(off)reference=r;else equal(&reference,&r);
            if(!off)break;
        }
        printf("S1 model=%u %s: exact on/off\n",model,names[k]);
    }
}
static void integer_flush(void)
{
    for(unsigned model=0;model<4;++model)for(unsigned boundary=0;boundary<2;++boundary){
      Result reference={0};
      for(unsigned off=0;off<2;++off){
        init(model);arc_rpc_mem_measure(0,0,false);arc_rpc_mem_s1_force_off(off!=0);
        assert(arc_rpc_mem_fast.base_ticks==28000 && !arc_rpc_mem_fast.base_fraction);
        clock_state.now_ns=20;
        arc_rpc_mem_fast.issue_ticks=28000;arc_rpc_mem_fast.issue_floor=10;
        arc_rpc_mem_fast.cpu_due_ticks=56000;arc_rpc_mem_fast.cpu_due_ns=30;
        arc_rpc_mem_fast.issue_fraction=arc_rpc_mem_fast.due_fraction=0;
        arc_rpc_mem_fast.integer_batch=true;
        arc_rpc_mem_fast.instructions=2;
        exercise_batch.start=0;exercise_batch.live=exercise_batch.integral=exercise_batch.batched=true;
        ArcRpcCpuBatch batch={&clock_state,NULL,boundary?20:1000};
        bool result=arc_rpc_mem_batch_next(&batch,false);
        assert(result==!boundary && !arc_rpc_mem_fast.integer_batch);
        assert(arc_rpc_mem_fast.issue_ticks==(boundary?56000u:84000u));
        assert(arc_rpc_mem_fast.issue_floor==(boundary?20u:30u));
        assert(arc_rpc_mem_fast.issue_fraction==0 && arc_rpc_mem_fast.cpu_due_ticks==84000);
        assert(arc_rpc_mem_fast.due_floor==30 && arc_rpc_mem_fast.due_fraction==0);
        assert(arc_rpc_exercise.values[ARC_RPC_COUNT_integer_unobserved]==2);
        assert(arc_rpc_exercise.values[ARC_RPC_COUNT_service_s1]==!boundary);
        assert(arc_rpc_exercise.values[ARC_RPC_COUNT_service_s1_taken]==(!boundary && !off));
        assert(exercise_batch.services==!boundary);
        Result r=capture(result);
        if(!off)reference=r;else equal(&reference,&r);
        if (result) { /* Complete the admitted final instruction before batch return. */
            arc_rpc_mem_instruction_begin();arc_rpc_mem_instruction_end(4);
        }
        arc_rpc_mem_exercise_end();
        assert(arc_rpc_exercise.values[ARC_RPC_COUNT_rational_unobserved]==0);
      }
    }
    puts("S1 integer flush: four models, immediate true/false state, full on/off replay and integer/rational counts");
}
static void pending_video(void)
{
    for(unsigned model=0;model<4;++model)for(unsigned final_demand=0;final_demand<2;++final_demand){
      Result reference={0};
      for(unsigned off=0;off<2;++off){
        init(model);arc_rpc_mem_s1_force_off(off!=0);
        m.bcount=m.bwords=0;arc_rpc_mem_fast.pending=true;
        if(final_demand){
            /* Final demand retirement needs the inner guard without video. */
            m.active_kind=DEMAND;m.count=1;
            m.active.kind=DEMAND;m.command[0]=m.active;
        }else{
            m.cycle_active=m.active_valid=false;m.video_active=true;
        }
        ArcRpcCpuBatch batch={&clock_state,NULL,1000};
        bool result=arc_rpc_mem_batch_next(&batch,false);assert(result);
        assert(!arc_rpc_mem_fast.pending && !m.video_active && !m.count);
        assert(m.stats.instructions==0 && m.stats.completed_ticks==m.finish);
        assert(!arc_rpc_exercise.values[ARC_RPC_COUNT_service_s1_taken]);
        Result r=capture(result);
        if(!off)reference=r;else equal(&reference,&r);
      }
    }
    puts("S1 commands guard: four models, full on/off replay of empty pending video and final demand retirement");
}
static void repricing(void)
{
    for(unsigned model=0;model<4;++model)for(unsigned rom=0;rom<2;++rom){
      Result entry_reference={0},word_reference={0};
      for(unsigned off=0;off<2;++off){
        init(model);arc_rpc_mem_s1_force_off(off!=0);
        uint64_t d=arc_rpc_mem_fast.denominator;
        m.active.count=2;m.active.width[1]=4;
        if(rom){m.active.address[0]=0;m.active.address[1]=4;}
        else m.active.address[1]=0x10000004;
        m.bwords=2;m.buffer[0]=m.active;
        ArcRpcCpuBatch batch={&clock_state,NULL,1000};
        bool result=arc_rpc_mem_batch_next(&batch,false);assert(result);
        assert(m.active.index==0);
        assert(arc_rpc_exercise.values[ARC_RPC_COUNT_service_s1_taken]==!off);
        Result entry=capture(result);
        if(!off)entry_reference=entry;else equal(&entry_reference,&entry);
        if(rom)iomd.romcr0=5|32; /* next ROM word now costs two quanta */
        else iomd.clkctrl=5;
        clock_state.now_ns=101;arc_rpc_mem_run(101);
        assert(m.active.index==1 && m.finish==100*d+1+(!rom && model==3?4u:2u)*87500u);
        Result word=capture(result);
        if(!off)word_reference=word;else equal(&word_reference,&word);
      }
    }
    puts("S1 ROMCR/CLKCTL writes: four models, full on/off entry/word replay and independent cycle costs");
}
int main(void)
{
    matrix();integer_flush();pending_video();repricing();
#if defined(ARCDROID_RPC_CROSSCHECK)
    uint64_t calls=0,failed=0;
    const char *field=arc_rpc_crosscheck_status(&calls,&failed);
    if(field){fprintf(stderr,"S1 cross-check %s mismatches=%llu\n",field,(unsigned long long)failed);return 1;}
    printf("S1 cross-check comparisons=%llu mismatches=0\n",(unsigned long long)calls);

#endif
    return 0;
}
