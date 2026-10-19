/* SPDX-License-Identifier: GPL-2.0-only */
/* Independent cyclic-cache model and directed L1 expectations. No core run
 * produced expectations. Includes the candidate only to inspect private state. */
static unsigned test_hint_writes;
#define L1_HINT_WRITE(side,index,way) (++test_hint_writes, (side)->hints[index]=(way))
#include "../engine/rpc/rpc_mem_timing.c"
#undef NDEBUG
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
struct iomd iomd;
void fatal(const char *format,...)
{ va_list ap;va_start(ap,format);vfprintf(stderr,format,ap);va_end(ap);abort(); }
static ArcRpcScheduler test_clock;
/* White-box accessors: never compiled into a core or shipping object. */
static LookupSide *side_state(Cache *c) { return lookup_side(c); }
static void set_hint(Cache *c,uint32_t va,unsigned way)
{ side_state(c)->hints[hint_index(c,va>>c->shift)]=(uint8_t)way; }
static void integrity(Cache *c)
{
    unsigned live=0;
    for(unsigned i=0;i<c->sets*c->ways;++i){
        assert(side_state(c)->keys[i]==(c->tags[i].valid?c->tags[i].va:UINT32_MAX));
        if(!c->tags[i].valid)continue;
        ++live;
        for(unsigned j=i+1;j<(i/c->ways+1)*c->ways;++j)
            assert(!c->tags[j].valid || c->tags[i].va!=c->tags[j].va);
    }
    assert(live==c->populated);
}
static void bind_model(unsigned model,unsigned gate)
{
    memset(&iomd,0,sizeof(iomd));iomd.clkctrl=7;
    assert(arc_rpc_scheduler_init(&test_clock,100));
    arc_rpc_mem_bind(&test_clock,model,100);
    arc_rpc_mem_clock_switch(true);arc_rpc_mem_control(0x100d);
    l1_instruction.gate=l1_data.gate=gate;
}
/* Independent arithmetic expectations across every set, hint slot and way,
 * including the maximum virtual line and repeated stores. */
static void unchanged(Cache *c,uint32_t va,int expected);
static void geometry_and_stores(void)
{
    for(unsigned model=0;model<4;++model){
        bind_model(model,2);
        for(unsigned data=0;data<(model==2?2u:1u);++data){
            Cache *c=data?&m.data:&m.instruction;LookupSide *side=side_state(c);
            assert((1u<<side->set_shift)==c->sets);
            assert((1u<<side->way_shift)==c->ways);
            assert(side->set_mask==c->sets-1 && side->way_mask==c->ways-1);
            for(unsigned line=0;line<c->sets*8;++line)
                assert(hint_index(c,line)==(line%c->sets)*4+(line/c->sets)%4);
            uint32_t last=UINT32_MAX>>c->shift;
            assert(hint_index(c,last)==(last%c->sets)*4+(last/c->sets)%4);
            for(unsigned slot=0;slot<c->sets*c->ways;++slot){
                assert((slot & side->way_mask)==slot%c->ways);
                assert(((slot+1)&side->way_mask)==(slot+1)%c->ways);
                assert(((slot/c->ways)<<side->way_shift)==slot-slot%c->ways);
            }
            test_hint_writes=0;
            hint_store(side,0,0);assert(test_hint_writes==1);
            hint_store(side,0,0);assert(test_hint_writes==1);
            hint_store(side,0,1);assert(test_hint_writes==2);
            Tag tag={0,0,1,0};tag_write(c,0,tag);assert(test_hint_writes==3);
            tag_write(c,0,tag);assert(test_hint_writes==3);
            c->populated=1;
#if !defined(ARCDROID_RPC_L1_POISON)
            unchanged(c,0,0);assert(test_hint_writes==3);
#endif
        }
    }
}
static void drain(void)
{
    arc_rpc_mem_end_slow();
    while(m.count || m.bcount || m.active_valid || m.video_active){
        arc_rpc_mem_run(test_clock.now_ns);
        if(!(m.count || m.bcount || m.active_valid || m.video_active))break;
        uint64_t next=arc_rpc_mem_next(UINT64_MAX,true);
        assert(next>test_clock.now_ns);test_clock.now_ns=next;
    }
}
static uint32_t address(Cache *c,unsigned n)
{ return (0x10000000u>>c->shift | (n*c->sets))<<c->shift; }
static void fill_slot(Cache *c,unsigned way,unsigned n)
{
    uint32_t va=address(c,n);
    if(!c->tags[way].valid)++c->populated;
    tag_write(c,way,(Tag){va>>c->shift,va,1,0});
}
static void unchanged(Cache *c,uint32_t va,int expected)
{
    Cache before=*c;ArcRpcMemStats stats=m.stats;ArcRpcMemFast fast=arc_rpc_mem_fast;
    ArcRpcExercise counts=arc_rpc_exercise;
    Tag *t=LOOKUP(c,va,false);
#if defined(ARCDROID_RPC_CROSSCHECK)
    uint64_t checks,failed;const char *field=arc_rpc_crosscheck_status(&checks,&failed);
    if(failed)fatal("crosscheck mismatch: %s",field);
#endif
    assert(t==(expected<0?NULL:&c->tags[expected]));
    assert(!memcmp(c,&before,sizeof(before)));
    assert(!memcmp(&m.stats,&stats,sizeof(stats)));
    assert(!memcmp(&arc_rpc_mem_fast,&fast,sizeof(fast)));
    unsigned result=!before.populated?0:expected<0?1:2;
    unsigned probes=result==0?0:result==1?c->ways:(unsigned)expected%c->ways+1;
    assert(arc_rpc_exercise.values[ARC_RPC_COUNT_lookup_access_empty+result]==counts.values[ARC_RPC_COUNT_lookup_access_empty+result]+1);
    assert(arc_rpc_exercise.values[ARC_RPC_COUNT_lookup_access_probes]==counts.values[ARC_RPC_COUNT_lookup_access_probes]+probes);
    if(result==2)assert(arc_rpc_exercise.values[ARC_RPC_COUNT_ACCESS_WAYS+(unsigned)expected%c->ways]==counts.values[ARC_RPC_COUNT_ACCESS_WAYS+(unsigned)expected%c->ways]+1);
    integrity(c);
}
static void directed(unsigned model,bool data,unsigned gate)
{
    bind_model(model,gate);Cache *c=data?&m.data:&m.instruction;
    unchanged(c,0,-1); /* sentinel cannot alias reachable line zero */
    for(unsigned n=0;n<c->ways;++n)fill_slot(c,n,n);
    unsigned slots[]={0,c->ways/2,c->ways-1};
    for(unsigned k=0;k<3;++k){
        unsigned n=slots[k];uint32_t va=address(c,n);
        set_hint(c,va,n);
        uint64_t floor=arc_rpc_exercise.values[ARC_RPC_COUNT_l1_hint_accepted];
        unchanged(c,va,(int)n);
#if !defined(ARCDROID_RPC_L1_POISON)
        assert(arc_rpc_exercise.values[ARC_RPC_COUNT_l1_hint_accepted]==floor+(gate!=0 && !(gate>=3 && n==0)));
#else
        (void)floor;
#endif
        set_hint(c,va,c->ways);
        uint64_t accepted=arc_rpc_exercise.values[ARC_RPC_COUNT_l1_hint_accepted];
        unchanged(c,va,(int)n);
        assert(arc_rpc_exercise.values[ARC_RPC_COUNT_l1_hint_accepted]==accepted); /* bound */
        set_hint(c,va,(n+1)%c->ways);unchanged(c,va,(int)n); /* wrong VA */
    }
    #if !defined(ARCDROID_RPC_CROSSCHECK) && !defined(ARCDROID_RPC_L1_POISON)
    if(c->sets>1){
        uint32_t va=address(c,1);unsigned way=c->ways;
        Tag saved=c->tags[way];tag_write(c,way,(Tag){va>>c->shift,va,1,0});
        set_hint(c,va,way);
        uint64_t accepted=arc_rpc_exercise.values[ARC_RPC_COUNT_l1_hint_accepted];
        Tag *t=LOOKUP(c,va,false);assert(t==&c->tags[1]);
        assert(arc_rpc_exercise.values[ARC_RPC_COUNT_l1_hint_accepted]==accepted);
        tag_write(c,way,saved);
    }
    #endif
    unchanged(c,address(c,c->ways+3),-1); /* zero mask, UBSan */
    /* Locked SWP bypasses resident tags on every model, without replacement,
     * hit, dirty, fill or memo effects. Only its demand packet is collected. */
    Cache old_i=m.instruction,old_d=m.data;ArcRpcMemStats old_stats=m.stats;
    bool old_memo=arc_rpc_mem_fast.memo_valid;uint64_t old_fetch_hits=arc_rpc_mem_fast.fetch_hits;
    arc_rpc_mem_fast.recording=true;arc_rpc_mem_fast.opcode=0xe1000090;
    uint32_t swp_va=address(c,1);arc_rpc_mem_access(swp_va,swp_va,4,true,false,0,true);
    assert(!memcmp(&old_i,&m.instruction,sizeof(old_i)) && !memcmp(&old_d,&m.data,sizeof(old_d)));
    assert(m.stats.hits==old_stats.hits && m.stats.fills==old_stats.fills && m.stats.dirty_halves==old_stats.dirty_halves);
    assert(arc_rpc_mem_fast.memo_valid==old_memo && arc_rpc_mem_fast.fetch_hits==old_fetch_hits);
    assert(m.command[m.head].locked);drain();
    uint32_t stale=address(c,0);set_hint(c,stale,0);
    fill_slot(c,0,c->ways+1);unchanged(c,stale,-1); /* refill */
    uint32_t duplicate=address(c,1);
    Tag invalid=c->tags[1];invalid.valid=0;invalid.dirty=0;
    tag_write(c,0,invalid);--c->populated;set_hint(c,duplicate,0);
    unchanged(c,duplicate,1); /* invalid VA equal to valid VA */
    if(model==2 && data){
        c->tags[1].dirty=3;uint8_t cursor=c->victim[0];
        uint32_t key=side_state(c)->keys[1];
        arc_rpc_mem_fast.recording=true;arc_rpc_mem_maintenance(7,1,10,duplicate);drain();
        assert(c->tags[1].valid && !c->tags[1].dirty && side_state(c)->keys[1]==key);
        assert(m.stats.dirty_halves==2 && c->victim[0]==cursor);
        set_hint(c,duplicate,1);arc_rpc_mem_fast.recording=true;
        arc_rpc_mem_maintenance(7,1,6,duplicate);drain();
        unchanged(c,duplicate,-1);assert(c->victim[0]==cursor);
    }
    invalidate(c);integrity(c);unchanged(c,duplicate,-1);
    fill_slot(c,0,0);set_hint(c,address(c,0),0);invalidate(c);integrity(c);
    unchanged(c,0,-1);
    tag_write(c,0,(Tag){0,0,1,0});c->populated=1;unchanged(c,0,0);
    /* Reset/unbind must drop derived state even without a subsequent clock. */
    arc_rpc_mem_bind(NULL,0,0);
    for(unsigned i=0;i<512;++i){assert(l1_instruction.keys[i]==UINT32_MAX && l1_data.keys[i]==UINT32_MAX);
        assert(l1_instruction.hints[i]==255 && l1_data.hints[i]==255);}
    arc_rpc_mem_bind(&test_clock,(model+1)%4,100);
    assert(l1_instruction.gate==((model+1)%4==0?2u:((model+1)%4==2?3u:0u)));
    assert(l1_data.gate==3); /* Bound SA D geometry, unused by unified models. */integrity(&m.instruction);integrity(&m.data);
}
/* Own types and algorithms: no lookup/tag_write/cache_for helper shared with
 * the candidate. Invalid VA/physical are excluded, dirty is always checked. */
typedef struct { unsigned present,dirty;uint32_t line,physical; } L1ModelTag;
typedef struct { L1ModelTag tags[512];unsigned cursor[128],sets,ways,shift,live; } L1ModelCache;
typedef struct { L1ModelCache i,d;uint64_t hits,fills,castouts;bool memo;uint32_t memo_line; } L1Model;
static void model_reset(L1Model *r,unsigned cpu)
{
    memset(r,0,sizeof(*r));
    const unsigned sets[]={4,128,16,64},ways[]={64,4,32,4};
    r->i.sets=sets[cpu];r->i.ways=ways[cpu];r->i.shift=cpu==2?5:4;
    r->d.sets=16;r->d.ways=32;r->d.shift=5;
}
static int model_find(L1ModelCache *c,uint32_t va)
{
    uint32_t line=va>>c->shift;unsigned start=(line%c->sets)*c->ways;
    for(unsigned i=start;i<start+c->ways;++i)
        if(c->tags[i].present && c->tags[i].line==line)return (int)i;
    return -1;
}
static void model_access(L1Model *r,unsigned cpu,bool fetch,bool write,uint32_t va,bool locked,bool mapped)
{
    L1ModelCache *c=cpu==2 && !fetch?&r->d:&r->i;
    int slot=locked?-1:model_find(c,va);
    if(slot>=0){++r->hits;if(fetch){r->memo=true;r->memo_line=va>>c->shift;}
        else if(write && cpu==2)c->tags[slot].dirty|=1u<<((va>>4)%2);
        return;
    }
    if((write && !mapped) || locked)return;
    unsigned set=(va>>c->shift)%c->sets;
    L1ModelTag *t=&c->tags[set*c->ways+c->cursor[set]];
    if(t->present && cpu==2 && !fetch)r->castouts+=(t->dirty&1)+((t->dirty>>1)&1);
    if(!t->present)++c->live;
    *t=(L1ModelTag){1,write?1u<<((va>>4)%2):0,va>>c->shift,va&~((1u<<c->shift)-1)};
    c->cursor[set]=(c->cursor[set]+1)%c->ways;++r->fills;r->memo=false;
    if(fetch){r->memo=true;r->memo_line=va>>c->shift;}
}
static void compare_cache(L1ModelCache *r,Cache *c)
{
    assert(c->sets==r->sets && c->ways==r->ways && c->shift==r->shift && c->populated==r->live);
    for(unsigned j=0;j<128;++j)assert(c->victim[j]==r->cursor[j]);
    for(unsigned j=0;j<512;++j){
        assert(c->tags[j].valid==r->tags[j].present && c->tags[j].dirty==r->tags[j].dirty);
        if(r->tags[j].present){assert(c->tags[j].va==r->tags[j].line && c->tags[j].physical==r->tags[j].physical);
            for(unsigned k=j+1;k<(j/r->ways+1)*r->ways;++k)
                assert(!r->tags[k].present || r->tags[k].line!=r->tags[j].line);}
    }
    integrity(c);
}
static void compare_model(L1Model *r)
{
    compare_cache(&r->i,&m.instruction);compare_cache(&r->d,&m.data);
    assert(m.stats.hits==r->hits && !arc_rpc_mem_fast.fetch_hits);
    assert(m.stats.fills==r->fills && m.stats.dirty_halves==r->castouts);
    assert(arc_rpc_mem_fast.memo_valid==r->memo);
    if(r->memo)assert(arc_rpc_mem_fast.memo_line==r->memo_line);
}
static uint32_t random_step(uint32_t *state)
{ *state=*state*1664525u+1013904223u;return *state; }
static void random_model(unsigned cpu,bool data,unsigned gate,uint32_t seed)
{
    L1Model r;model_reset(&r,cpu);bind_model(cpu,gate);
    uint64_t castouts=0,fills=0;unsigned full=0,single=0,rebinds=0;
    for(unsigned n=0;n<20000;++n){
        uint32_t bits=random_step(&seed);unsigned op=bits>>28;
        /* Preserve long residency runs so replacement/dirty castout is
         * load-bearing; lifecycle and full invalidation still occur often. */
        if(op==13 && (bits&31)!=0)op=0;
        if(op==15 && (bits&31)!=1)op=0;
        if(op==14 && !(cpu==2 && data))op=0;
        uint64_t before_castouts=r.castouts,before_fills=r.fills;
        bool fetch=!data,write=data && (bits&128);Cache *c=data?&m.data:&m.instruction;
        L1ModelCache *rc=data?&r.d:&r.i;
        if(cpu!=2){c=&m.instruction;rc=&r.i;fetch=(bits&128)!=0;write=!fetch && (bits&256);}
        uint32_t va=0x10000000u+(((bits>>8)%96)*c->sets+(bits%4))* (1u<<c->shift)+(bits&16);
        if(op<12){
            bool locked=data && (bits&512) && op==11;
            bool mapped=data && op==10; if(mapped)write=true;
            arc_rpc_mem_fast.recording=true;arc_rpc_mem_fast.opcode=locked||mapped?0xe1000090:0xe5900000;
            model_access(&r,cpu,fetch,write,va,locked,mapped);
            arc_rpc_mem_access(va,va,4,write,fetch,locked?0:mapped?12:8,true);drain();
        } else if(op==12){
            int slot=model_find(rc,va);unchanged(c,va,slot);
        } else if(op==13){
            /* Public full invalidate, preserves victim cursors and dirty halves
             * are discarded without castout. SA crm selects exactly one cache. */
            for(unsigned j=0;j<512;++j)rc->tags[j].present=rc->tags[j].dirty=0;
            ++full;rc->live=0;r.memo=false;arc_rpc_mem_fast.recording=true;
            arc_rpc_mem_maintenance(7,0,data?6:5,0);drain();
        } else if(op==14 && cpu==2 && data){
            ++single;int slot=model_find(rc,va);bool clean=bits&128;
            if(slot>=0){L1ModelTag *t=&rc->tags[slot];
                if(clean){r.castouts+=(t->dirty&1)+((t->dirty>>1)&1);t->dirty=0;}
                else{t->present=t->dirty=0;--rc->live;r.memo=false;}}
            arc_rpc_mem_fast.recording=true;arc_rpc_mem_maintenance(7,1,clean?10:6,va);drain();
        } else {++rebinds;model_reset(&r,cpu);bind_model(cpu,gate);}
        compare_model(&r);
        if(op!=15){castouts+=r.castouts-before_castouts;fills+=r.fills-before_fills;}
    }
    assert(fills>0 && full>0 && rebinds>0);
    if(cpu==2 && data)assert(castouts>0 && single>0);
    printf("L1 reach fills=%llu dirty-castouts=%llu full=%u single=%u rebind=%u\n",
        (unsigned long long)fills,(unsigned long long)castouts,full,single,rebinds);
    printf("L1 model cpu=%u data=%u gate=%u seed-stream end=%08x operations=20000 exact\n",cpu,data,gate,seed);
}
static void split_and_unified(unsigned gate)
{
    bind_model(2,gate);fill_slot(&m.instruction,7,1);fill_slot(&m.data,3,1);
    uint32_t va=address(&m.data,1);set_hint(&m.data,va,7);
    unchanged(&m.instruction,va,7);unchanged(&m.data,va,3);
    arc_rpc_mem_fast.recording=true;arc_rpc_mem_access(va,va,4,true,false,8,true);
    assert(m.data.tags[3].dirty==1 && !m.instruction.tags[7].dirty);drain();
    /* G-6: code and data occupy one set; cyclic data fill evicts current code. */
    for(unsigned model=0;model<4;++model)if(model!=2){
        bind_model(model,gate);Cache *c=&m.instruction;
        uint32_t code=address(c,0);
        for(unsigned n=0;n<=c->ways;++n){arc_rpc_mem_fast.recording=true;
            uint32_t a=address(c,n);arc_rpc_mem_access(a,a,4,false,n==0,8,true);drain();}
        assert(!arc_rpc_mem_fast.memo_valid && c->victim[0]==1 && m.stats.fills==c->ways+1);
        unchanged(c,code,-1);integrity(c);
        arc_rpc_mem_fast.recording=true;
        arc_rpc_mem_access(code,code,4,false,true,8,true);drain();
        assert(c->victim[0]==2 && m.stats.fills==c->ways+2);
        assert(arc_rpc_mem_fast.memo_valid && arc_rpc_mem_fast.memo_line==code>>c->shift);
        unchanged(c,code,1);
    }
}
static void reference_isolation(void)
{
#if defined(ARCDROID_RPC_CROSSCHECK)
    bind_model(0,2);Cache *c=&m.instruction;fill_slot(c,7,0);set_hint(c,address(c,0),255);
    LookupSide before=*lookup_side(c);ArcRpcExercise counters=arc_rpc_exercise;
    assert(lookup_reference(c,address(c,0))==&c->tags[7]);
    assert(!memcmp(&before,lookup_side(c),sizeof(before)));
    assert(!memcmp(&counters,&arc_rpc_exercise,sizeof(counters)));
#endif
}
/* Way-0 candidates must not consult even a valid hint first. A rejected hint
 * also cannot trigger remainder work or a store; poison must not touch it. */
static void way0_priority(unsigned model,bool data,unsigned gate)
{
    bind_model(model,gate);Cache *c=data?&m.data:&m.instruction;
    fill_slot(c,0,0);fill_slot(c,c->ways-1,1);
    uint32_t va=address(c,0);
    unsigned hints[]={0,c->ways-1,c->ways,255};
    for(unsigned i=0;i<sizeof(hints)/sizeof(hints[0]);++i){
        set_hint(c,va,hints[i]);LookupSide before=*side_state(c);
        ArcRpcExercise counts=arc_rpc_exercise;unsigned writes=test_hint_writes;
#if defined(ARCDROID_RPC_L1_POISON)
        uint32_t poison=l1_poison_state;
#endif
        unchanged(c,va,0);
        assert(!memcmp(&before,side_state(c),sizeof(before)));
        assert(test_hint_writes==writes);
        for(unsigned counter=ARC_RPC_COUNT_l1_hint_accepted;counter<=ARC_RPC_COUNT_l1_linear_fallback;++counter)
            assert(arc_rpc_exercise.values[counter]==counts.values[counter]);
#if defined(ARCDROID_RPC_L1_POISON)
        assert(l1_poison_state==poison);
#endif
    }
    /* Once way 0 misses, both the validated hint and its fallback remain live. */
    va=address(c,1);set_hint(c,va,c->ways);
    uint64_t rejected=arc_rpc_exercise.values[ARC_RPC_COUNT_l1_hint_rejected];
    uint64_t searched=arc_rpc_exercise.values[gate==3?ARC_RPC_COUNT_l1_linear_fallback:ARC_RPC_COUNT_l1_mirror_search];
    unchanged(c,va,(int)c->ways-1);
#if !defined(ARCDROID_RPC_L1_POISON)
    assert(arc_rpc_exercise.values[ARC_RPC_COUNT_l1_hint_rejected]==rejected+1);
    assert(arc_rpc_exercise.values[gate==3?ARC_RPC_COUNT_l1_linear_fallback:ARC_RPC_COUNT_l1_mirror_search]==searched+1);
    set_hint(c,va,c->ways-1);
    uint64_t accepted=arc_rpc_exercise.values[ARC_RPC_COUNT_l1_hint_accepted];
    unchanged(c,va,(int)c->ways-1);
    assert(arc_rpc_exercise.values[ARC_RPC_COUNT_l1_hint_accepted]==accepted+1);
#else
    (void)rejected;(void)searched;
#endif
}
int main(void)
{
    geometry_and_stores();
    reference_isolation();
    for(unsigned gate=3;gate<5;++gate)for(unsigned cpu=0;cpu<4;++cpu){
        way0_priority(cpu,false,gate);if(cpu==2)way0_priority(cpu,true,gate);
    }
    for(unsigned gate=0;gate<5;++gate)for(unsigned cpu=0;cpu<4;++cpu){
        directed(cpu,false,gate);random_model(cpu,false,gate,0x61a20001u+cpu);
        if(cpu==2){directed(cpu,true,gate);random_model(cpu,true,gate,0xd1100061u);}
    }
    for(unsigned gate=0;gate<5;++gate)split_and_unified(gate);
    puts("L1 directed matrix and 500000 independent operations passed");return 0;
}
