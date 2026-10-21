/* SPDX-License-Identifier: GPL-2.0-only */
/* Directed timing-state tests; exact constants derive from D-139 equations. */
#include "../engine/rpc/rpc_mem_timing.c"
#undef NDEBUG
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
struct iomd iomd;
#if defined(ARCDROID_RPC_PROFILE)
uint32_t *rom,*ram00,*ram01,*vram;
#endif
void fatal(const char *format, ...)
{ va_list ap;va_start(ap,format);vfprintf(stderr,format,ap);va_end(ap);fprintf(stderr," at instruction=%llu pending=%u due=%llu\n",(unsigned long long)arc_rpc_mem_fast.instructions,arc_rpc_mem_fast.pending,(unsigned long long)arc_rpc_mem_fast.cpu_due_ns);abort(); }
static ArcRpcScheduler clock_state;
static void init(unsigned model,unsigned rate)
{
#if defined(ARCDROID_RPC_PROFILE)
    arc_rpc_profile_configure(0,NULL);
#endif
    memset(&iomd,0,sizeof(iomd));iomd.clkctrl=7;
    assert(arc_rpc_scheduler_init(&clock_state,rate));
    arc_rpc_mem_bind(&clock_state,model,rate);
    arc_rpc_mem_clock_switch(true);arc_rpc_mem_control(0x100d);
}
static void service(bool idle)
{
    uint64_t next=arc_rpc_mem_next(UINT64_MAX,idle);
    assert(next>clock_state.now_ns);clock_state.now_ns=next;arc_rpc_mem_run(next);
}
static void issue(bool store,bool read)
{
    while(!arc_rpc_mem_ready(false)) service(false);
    assert(arc_rpc_mem_endpoint_admit(1)==1);
    arc_rpc_mem_exercise_begin(false);arc_rpc_mem_batch_begin(NULL);arc_rpc_mem_instruction_begin();
    arc_rpc_mem_opcode(store?0xe5801000:0xe1a00000);
    if(store || read)arc_rpc_mem_access(0x10008000,0x10008000,4,store,false,store?4:0,true);
    arc_rpc_mem_instruction_end(4);arc_rpc_mem_endpoint_return();
    arc_rpc_mem_run(clock_state.now_ns);
}
static void intervals(void)
{
    init(2,100);arc_rpc_mem_measure(1,1,false);issue(false,false);
    ArcRpcExercise unavailable;assert(interval.done && !arc_rpc_mem_exercise_interval(&unavailable));
    for(unsigned drained=0;drained<2;++drained){
        init(2,100);assert(arc_rpc_mem_endpoint(1,1,drained));issue(false,false);
        assert(interval.done && interval.start_ticks==0 && interval.end_ticks==28000);
        assert(interval.end.instructions==1 && interval.end.hits==0);
        assert(!arc_rpc_mem_ready(false) && arc_rpc_mem_endpoint_admit(200)==0);
        init(2,100);assert(arc_rpc_mem_endpoint(1,1,drained));issue(false,true);
        while(!interval.done)service(true);
        assert(interval.end_ticks==28000+5*87500 && interval.end.words==1);
        init(2,100);assert(arc_rpc_mem_endpoint(2,2,drained));issue(true,false);
        ArcRpcExercise before=arc_rpc_exercise;
        issue(false,false);
        if(drained){assert(!interval.done && pending_end);
            ARC_RPC_COUNT(service_s1,7); /* included only by the drain snapshot */
            while(!interval.done)service(true);}
        assert(interval.done && interval.start_ticks==28000);
        assert(interval.end_ticks==(drained?28000+5*87500:56000));
        assert(interval.end.instructions==2);
        ArcRpcExercise window;assert(arc_rpc_mem_exercise_interval(&window));
        for(unsigned i=0;i<ARC_RPC_COUNT_TOTAL;++i)
            assert(window.values[i]==arc_rpc_exercise.values[i]-before.values[i]);
        assert(window.values[ARC_RPC_COUNT_service_s1]==(drained?7u:0u));
    }
    /* C(1) survives a video hold separating completion from the next issue. */
    init(2,100);assert(arc_rpc_mem_endpoint(2,2,false));issue(false,false);
    arc_rpc_mem_video_hold(350);arc_rpc_mem_run(clock_state.now_ns);
    issue(false,false);assert(interval.start_ticks==28000 && interval.end_ticks==1036000);
    init(2,100);assert(arc_rpc_mem_endpoint(2,2,false));issue(false,false);
    arc_rpc_mem_idle(true);clock_state.now_ns=1000;arc_rpc_mem_idle(false);
    issue(false,false);assert(interval.start_ticks==28000 && interval.end_ticks==2828000);
    /* Fractional ticks are retained rather than rounded to visible ns. */
    init(1,35);assert(arc_rpc_mem_endpoint(1,2,false));issue(false,false);issue(false,false);
    assert(interval.end_ticks==56000 && interval.end.denominator==980);
    for(unsigned quota=1;quota<=200;quota=quota==1?7:quota==7?200:201){
        init(2,100);assert(arc_rpc_mem_endpoint(quota+1,quota+2,false));
        assert(arc_rpc_mem_endpoint_admit(200)==quota);
        for(unsigned i=0;i<quota+2;++i)issue(false,false);
        assert(interval.start_ticks==quota*28000u && interval.end_ticks==(quota+2)*28000u);
    }
}
static void measurement_state(void)
{
    init(2,100);issue(false,false);
    ArcRpcMeasureState before,after;arc_rpc_mem_measure_state(&before);
    assert(!arc_rpc_mem_endpoint(2,3,false));arc_rpc_mem_measure_state(&after);
    assert(arc_rpc_mem_measure_equal(&before,&after));
    assert(arc_rpc_mem_endpoint(3,3,false));issue(false,false);issue(false,false);
    assert(interval.done && interval.start_ticks==56000 && interval.end_ticks==84000);
    init(2,100);assert(!arc_rpc_mem_endpoint(0,3,false));assert(!arc_rpc_mem_endpoint(3,2,false));
    assert(arc_rpc_mem_endpoint(2,3,true));
    ArcRpcMeasureState a,b;arc_rpc_mem_measure_state(&a);assert(arc_rpc_mem_measure_equal(&a,&a));
#define CHANGE(field) do {b=a;b.field^=1;assert(!arc_rpc_mem_measure_equal(&a,&b));} while(0)
    CHANGE(first);CHANGE(last);CHANGE(previous_completion);CHANGE(end_completion);
    CHANGE(drained);CHANGE(endpoint);CHANGE(intrusive);CHANGE(stopped);CHANGE(previous_valid);CHANGE(pending_end);
    CHANGE(interval.started);CHANGE(interval.done);CHANGE(interval.start_ticks);CHANGE(interval.end_ticks);CHANGE(interval.start_fetch_hits);CHANGE(interval.end_fetch_hits);
#define STATS(field) CHANGE(interval.start.field);CHANGE(interval.end.field)
    STATS(instructions);STATS(words);STATS(fills);STATS(dirty_halves);STATS(packets);STATS(hits);
    STATS(completed_ticks);STATS(buffer_wait_ticks);STATS(buffered_words);STATS(buffered_packets);STATS(denominator);
#undef STATS
#undef CHANGE
    puts("endpoint directed: first/last, quota 1/7/200, request/drain, older writes, hold, wake, fractions, state fields");
}

static uint64_t value(unsigned index) {
#if defined(ARCDROID_RPC_PROFILE)
    ArcRpcProfile p;arc_rpc_profile_snapshot_v4(&p,ARC_RPC_COUNTER_ABI,sizeof(p));
    assert(p.ranking.values[index]==arc_rpc_exercise.values[index]);
#endif
    return arc_rpc_exercise.values[index];
}
static void lookup_counts(void)
{
    for(unsigned maintenance=0;maintenance<2;++maintenance){
        init(2,100);Cache *c=&m.data;
        unsigned base=maintenance?ARC_RPC_COUNT_lookup_maintenance_empty:ARC_RPC_COUNT_lookup_access_empty;
        assert(!LOOKUP(c,0x1000,maintenance));assert(value(base)==1 && !value(base+3));
        c->populated=1;tag_write(c,3,(Tag){0x80,0x1000,1,0});
        assert(LOOKUP(c,0x1000,maintenance)==&c->tags[3]);
        assert(value(base+2)==1 && value(base+3)==4 && value(base+4)==4 && value(base+5)==1);
        unsigned ways=maintenance?ARC_RPC_COUNT_MAINTENANCE_WAYS:ARC_RPC_COUNT_ACCESS_WAYS;
        assert(value(ways+3)==1);
        assert(!LOOKUP(c,0x2000,maintenance));assert(value(base+1)==1 && value(base+3)==36);
    }
}
static void l1_counts(void)
{
    init(0,100);Cache *c=&m.instruction;c->populated=1;
    tag_write(c,3,(Tag){0,0,1,0});
    assert(LOOKUP(c,0,false)==&c->tags[3]);
    assert(value(ARC_RPC_COUNT_l1_hint_accepted)==1);
    lookup_side(c)->hints[hint_index(c,0)]=255;
    assert(LOOKUP(c,0,false)==&c->tags[3]);
    assert(value(ARC_RPC_COUNT_l1_hint_rejected)==1);
    assert(value(ARC_RPC_COUNT_l1_mirror_search)==1);
    /* Recheck failures are unreachable with an exact mirror. This explicit
     * white-box corruption tests its diagnostic increment, never poison mode. */
#if defined(ARCDROID_RPC_CROSSCHECK)
    uint64_t calls,failed;
    /* The deliberate mirror corruption below is outside the production
     * invariant. Require a clean latch before injecting its expected fault. */
    assert(!arc_rpc_crosscheck_status(&calls,&failed) && !failed);
#endif
    lookup_side(c)->keys[0]=0;lookup_side(c)->hints[hint_index(c,0)]=255;
    assert(!LOOKUP(c,0,false));
    assert(value(ARC_RPC_COUNT_l1_mirror_recheck_failed)==1);
    lookup_side(c)->keys[0]=UINT32_MAX;lookup_side(c)->gate=1;
    lookup_side(c)->hints[hint_index(c,0)]=255;
    assert(LOOKUP(c,0,false)==&c->tags[3]);
    assert(value(ARC_RPC_COUNT_l1_linear_fallback)==1);
    assert(value(ARC_RPC_COUNT_l1_hint_rejected)==3);
    assert(value(ARC_RPC_COUNT_l1_mirror_search)==2);
#if defined(ARCDROID_RPC_CROSSCHECK)
    const char *field=arc_rpc_crosscheck_status(&calls,&failed);
    assert(field && !strcmp(field,"lookup.cache_slot") && failed==1);
    /* Consume only this asserted negative control. Real failures before or
     * after it still fail the fixture and survive every timing rebind. */
    arc_rpc_crosscheck_reset();
#endif
}
static void maintenance_callsite(void)
{
    init(2,100);arc_rpc_mem_fast.recording=true;
    arc_rpc_mem_maintenance(7,1,6,0x10008000);
    assert(value(ARC_RPC_COUNT_lookup_maintenance_empty)==1);
    m.data.populated=1;tag_write(&m.data,3,(Tag){0x10008000>>5,0x10008000,1,0});
    arc_rpc_mem_maintenance(7,1,6,0x10008000);
    assert(value(ARC_RPC_COUNT_lookup_maintenance_hit)==1 && value(ARC_RPC_COUNT_lookup_maintenance_hit_probes)==4);
    m.data.populated=1;tag_write(&m.data,3,(Tag){0,0,1,0});
    arc_rpc_mem_maintenance(7,1,6,0x10008000);
    assert(value(ARC_RPC_COUNT_lookup_maintenance_miss)==1 && value(ARC_RPC_COUNT_lookup_maintenance_probes)==36);
}
static void endpoint_shortcut(void)
{
    for(unsigned commit=0;commit<2;++commit)for(unsigned drained=0;drained<2;++drained){
        init(2,100);assert(arc_rpc_mem_endpoint(1,1,drained));
        while(!arc_rpc_mem_ready(false))service(false);
        assert(arc_rpc_mem_endpoint_admit(1)==1);
        arc_rpc_mem_instruction_begin();arc_rpc_mem_opcode(0xe5901000);
        arc_rpc_mem_access(0x10008000,0x10008000,4,false,false,8,true);
        arc_rpc_mem_instruction_end(4);arc_rpc_mem_endpoint_return();
        ArcRpcCpuBatch batch={&clock_state,NULL,commit?10000:11};
        assert(!arc_rpc_mem_batch_service(&batch));
        assert(value(ARC_RPC_COUNT_shortcut_commits)==commit);
        assert(value(ARC_RPC_COUNT_shortcut_rollbacks)==!commit);
        while(!interval.done)service(true);
        assert(interval.end_ticks==28000+22*87500 && interval.end.instructions==1);
    }
}
static void fill_counts(void)
{
    for(unsigned fetch=0;fetch<2;++fetch)for(unsigned memory=0;memory<2;++memory){
        init(2,100);arc_rpc_mem_fast.recording=true;
        uint32_t address=memory?0x10008000:0x8000;
        arc_rpc_mem_access(address,address,4,false,fetch,8,true);
        unsigned index=(fetch?ARC_RPC_COUNT_fill_fetch_memory:ARC_RPC_COUNT_fill_data_memory)+(memory?0:1);
        assert(value(index)==1);
        for(unsigned i=ARC_RPC_COUNT_fill_fetch_memory;i<=ARC_RPC_COUNT_fill_data_rom;++i)
            assert(value(i)==(i==index));
    }
    for(unsigned memory=0;memory<2;++memory)for(unsigned commit=0;commit<2;++commit){
        init(2,100);while(!arc_rpc_mem_ready(false))service(false);
        arc_rpc_mem_instruction_begin();arc_rpc_mem_opcode(0xe5901000);
        uint32_t address=memory?0x10008000:0x8000;
        arc_rpc_mem_access(address,address,4,false,false,8,true);
        arc_rpc_mem_instruction_end(4);
        ArcRpcCpuBatch batch={&clock_state,NULL,commit?10000:11};
        assert(arc_rpc_mem_batch_service(&batch)==(commit!=0));
        assert(value(ARC_RPC_COUNT_shortcut_attempts)==1);
        assert(value(ARC_RPC_COUNT_shortcut_commits)==commit);
        assert(value(ARC_RPC_COUNT_shortcut_rollbacks)==!commit);
        assert(value(ARC_RPC_COUNT_shortcut_memory_fills)==(memory?commit:0u));
    }
}
static void service_counts(void)
{
    const unsigned expected[]={ARC_RPC_COUNT_service_pending,ARC_RPC_COUNT_service_video,
        ARC_RPC_COUNT_service_due_finish,ARC_RPC_COUNT_service_s1,ARC_RPC_COUNT_service_busy,ARC_RPC_COUNT_service_other};
    for(unsigned kind=0;kind<6;++kind){
        init(2,100);clock_state.now_ns=10;arc_rpc_mem_fast.cpu_due_ns=20;
        m.cycle_active=kind==2 || kind==3;m.video_active=kind==1;
        m.active_valid=m.cycle_active;
        m.finish=kind==2?56000:kind==1 || kind==3?280000:0;
        arc_rpc_mem_fast.pending=kind==0;arc_rpc_mem_fast.bus_busy=kind>=1 && kind<=4;
        ArcRpcCpuBatch batch={&clock_state,NULL,kind==3?1000:10};
        (void)arc_rpc_mem_batch_service(&batch);
        for(unsigned i=ARC_RPC_COUNT_service_pending;i<=ARC_RPC_COUNT_service_other;++i)
            assert(value(i)==(i==expected[kind]));
        assert(value(ARC_RPC_COUNT_service_finish_valid)==(kind>=1 && kind<=3));
        assert(value(ARC_RPC_COUNT_service_finish_invalid)==!(kind>=1 && kind<=3));
    }
    /* Each strict edge independently excludes S1, including an overdue due
     * time and both sources of the service boundary. */
    const unsigned edges[][3]={{10,20,21},{10,20,20},{10,20,19},
        {20,20,1000},{21,20,1000},{19,20,1000}};
    for(unsigned event=0;event<2;++event)for(unsigned k=0;k<6;++k){
        init(2,100);clock_state.now_ns=edges[k][0];
        arc_rpc_mem_fast.cpu_due_ns=edges[k][1];
        m.cycle_active=true;m.active_valid=true;m.finish=280000;arc_rpc_mem_fast.bus_busy=true;
        clock_state.next_ns=event?edges[k][2]:1000;
        ArcRpcCpuBatch batch={&clock_state,NULL,event?1000:edges[k][2]};
        (void)arc_rpc_mem_batch_service(&batch);
        bool eligible=k==0 || k==5;
        assert(value(ARC_RPC_COUNT_service_s1)==eligible);
        assert(value(ARC_RPC_COUNT_service_s1_taken)==eligible);
        assert(value(ARC_RPC_COUNT_service_busy)==!eligible);
    }
}
static void excluded_counters(void)
{
    init(2,100);ArcRpcMeasureState a,b;arc_rpc_mem_measure_state(&a);
    unsigned char before[sizeof(m)+sizeof(arc_rpc_mem_fast)],after[sizeof(before)];
    assert(arc_rpc_mem_observation_snapshot(before,sizeof(before))==sizeof(before));
    assert(arc_rpc_mem_l1_state(false));assert(!arc_rpc_mem_l1_state(true));
    arc_rpc_mem_s1_force_off(true);
    ARC_RPC_COUNT(service_s1,1);ARC_RPC_COUNT(service_s1_taken,1);arc_rpc_mem_measure_state(&b);
    assert(arc_rpc_mem_measure_equal(&a,&b));
    assert(arc_rpc_mem_observation_snapshot(after,sizeof(after))==sizeof(after));
    assert(!memcmp(before,after,sizeof(before)));
    assert(arc_rpc_mem_endpoint(2,3,true));issue(false,false);
    arc_rpc_exercise_path=2;arc_rpc_mem_bind(NULL,0,0);
    arc_rpc_mem_measure_state(&b);assert(arc_rpc_mem_measure_equal(&a,&b));assert(!arc_rpc_exercise_path && !s1_forced_off);
    assert(arc_rpc_mem_l1_state(false));
    for(unsigned i=0;i<ARC_RPC_COUNT_TOTAL;++i)assert(!value(i));
}
int main(void) { intervals();measurement_state();lookup_counts();l1_counts();maintenance_callsite();fill_counts();endpoint_shortcut();service_counts();excluded_counters();

#if defined(ARCDROID_RPC_CROSSCHECK)
    uint64_t calls=0,failed=0;
    const char *field=arc_rpc_crosscheck_status(&calls,&failed);
    if(field){fprintf(stderr,"RPC cross-check mismatch: %s\n",field);return 1;}
#endif
    puts("exercise counters: lookup outcomes/ways/probes, service classes/finish validity, fill regions, shortcut commit/rollback, exclusion/reset");return 0; }
