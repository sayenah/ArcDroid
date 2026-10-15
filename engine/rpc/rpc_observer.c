/* RPC observation policy and copied state. GPL-2.0-only. */
#include "rpc_observer.h"
#include "rpc_mem_timing.h"
#include "rpc_memory.h"
#include "arm.h"
#include <string.h>

bool arc_rpc_observer_active, arc_rpc_observer_context;
static bool host_active, observers_active;
bool arc_rpc_observer_in_host(void) { return host_active; }
static struct {
    uint64_t epoch, order, instruction, watch_id;
    struct { bool live; uint64_t start, accounted; } batch;
    struct { uint64_t instruction,epoch,ns; uint32_t pc,mode; const uint32_t *word; } origin;
    struct { ArcRpcObserveFilter filter; uint64_t generation;
        bool armed, answer; uint32_t value; uint64_t answer_order; } owners[ARC_RPC_OBSERVERS];
    struct { bool live, wait_return; ArcRpcObserver owner; uint64_t id, generation, deadline;
        uint32_t pc, mode, sp; } watches[ARC_RPC_WATCHES];
    ArcRpcHostWork work[ARC_RPC_WORK_LIMIT]; unsigned work_count;
    ArcRpcHostWorkHandler handlers[ARC_RPC_OBSERVERS];
    bool in_swi, completed; unsigned current_owner;
    uint64_t current_order, current_generation;
    uint16_t current_results;
    ArcRpcObservationKind current_kind;
} observer;
#if defined(ARCDROID_TEST_FAULTS)
static struct {
    ArcRpcObservationTrace records[ARC_RPC_TRACE_LIMIT];
    ArcRpcTraceStatus status; bool paths;
    uint64_t order, instruction, watch_id;
    ArcRpcTraceSelection selection;
    ArcRpcObservationTrace gap;
    struct { bool live, wait_return; uint64_t id, deadline; uint32_t pc,mode,sp; } watches[ARC_RPC_WATCHES];
} trace;
uint64_t arc_rpc_observer_integer_attempts;
static uint64_t event_captures,number_checks;
uint64_t arc_rpc_observer_test_number_checks(void) { return number_checks; }
uint64_t arc_rpc_observer_test_captures(void) { return event_captures; }
static void trace_loss(const ArcRpcObservation *);
#endif
static void watch_lost(unsigned slot);
/* Rebuilt on subscription changes, never on guest instructions. Common OS
 * numbers and the Wimp chunk fit three words; other lists use the generic path. */
static struct { uint64_t low[2],wimp; bool unrestricted; } swi_interest;
static bool cached_swi(uint32_t number)
{
    if(number<128)return (swi_interest.low[number>>6]&(UINT64_C(1)<<(number&63)))!=0;
    return number>=0x400c0u && number<0x40100u &&
        (swi_interest.wimp&(UINT64_C(1)<<(number-0x400c0u)))!=0;
}
static bool filter_swi(const ArcRpcObserveFilter *f,uint32_t swi)
{
#if defined(ARCDROID_TEST_FAULTS)
    ++number_checks;
#endif
    if(!f->swi_count)return true;
    swi&=0xfdffffu;
    for(unsigned n=0;n<f->swi_count;++n)if((f->swis[n]&0xfdffffu)==swi)return true;
    return false;
}
static void gate(void)
{
    observers_active=false;memset(&swi_interest,0,sizeof(swi_interest));
    for(unsigned i=0;i<ARC_RPC_OBSERVERS;++i){
        observers_active|=observer.owners[i].armed;
        const ArcRpcObserveFilter *f=&observer.owners[i].filter;
        if(!observer.owners[i].armed || !(f->kinds&ARC_RPC_OBS_SWI))continue;
        if(!f->swi_count)swi_interest.unrestricted=true;
        for(unsigned n=0;n<f->swi_count;++n){
            uint32_t number=f->swis[n]&0xfdffffu;
            if(number<128)swi_interest.low[number>>6]|=UINT64_C(1)<<(number&63);
            else if(number>=0x400c0u && number<0x40100u)swi_interest.wimp|=UINT64_C(1)<<(number-0x400c0u);
            else swi_interest.unrestricted=true;
        }
    }
    arc_rpc_observer_active=observers_active;
    arc_rpc_observer_context=false;
    for(unsigned i=0;i<ARC_RPC_OBSERVERS;++i)
        arc_rpc_observer_context|=observer.owners[i].armed && (observer.owners[i].filter.kinds&ARC_RPC_OBS_MODE);
    for(unsigned i=0;i<ARC_RPC_WATCHES;++i)arc_rpc_observer_context|=observer.watches[i].live;
#if defined(ARCDROID_TEST_FAULTS)
    arc_rpc_observer_active|=trace.status.enabled;arc_rpc_observer_context|=trace.status.enabled;
#endif
}
/* D-181: reasons/references are pinned to the adopted qualification record. */
#define DEFERRED "awaiting R3c-3 launch hook (S64: *WimpTask starts tasks in SVC mode; F12 calls Wimp_Initialise; 3.5+ prompt buffers lie above 0x8000)"
#define ERROR_EVIDENCE "docs/riscpc-observer-qualification.md: a3-*, b3-*, ga-*, gb-*, gna-*, gnb-*, b14-*, a14-*"
#define DISC_A_EVIDENCE "docs/riscpc-observer-qualification.md: k-*, u-*"
#define DISC_B_EVIDENCE "docs/riscpc-observer-qualification.md: sb-*"
#define CELL(cat,cpu,owner,cap,on,reason) {cat,cpu,1,owner,cap,on,reason}
#define PAIR(cat,cpu,error,disc_a) CELL(cat,cpu,ARC_RPC_OBS_BOOT,ARC_RPC_CAP_OBSERVE,false,DEFERRED), \
    CELL(cat,cpu,ARC_RPC_OBS_LAUNCH,ARC_RPC_CAP_OBSERVE,false,DEFERRED), \
    CELL(cat,cpu,ARC_RPC_OBS_ERROR,ARC_RPC_CAP_OBSERVE,true,error), \
    CELL(cat,cpu,ARC_RPC_OBS_ERROR,ARC_RPC_CAP_COMPLETE,true,ERROR_EVIDENCE), \
    CELL(cat,cpu,ARC_RPC_OBS_DISC,ARC_RPC_CAP_OBSERVE,true,disc_a), \
    CELL(cat,cpu,ARC_RPC_OBS_DISC,ARC_RPC_CAP_REPLACE,true,disc_a), \
    CELL(cat,cpu,ARC_RPC_OBS_DISC,ARC_RPC_CAP_OBSERVE_B,true,DISC_B_EVIDENCE), \
    CELL(cat,cpu,ARC_RPC_OBS_DISC,ARC_RPC_CAP_REPLACE_B,true,DISC_B_EVIDENCE)
static const ArcRpcObserverQualification qualifications[]={
    PAIR(1,0,ERROR_EVIDENCE ", o-350-arm610",DISC_A_EVIDENCE),
    PAIR(2,1,ERROR_EVIDENCE,DISC_A_EVIDENCE),PAIR(2,3,ERROR_EVIDENCE,DISC_A_EVIDENCE),
    PAIR(3,2,ERROR_EVIDENCE,DISC_A_EVIDENCE),PAIR(4,1,ERROR_EVIDENCE,DISC_A_EVIDENCE),
    PAIR(4,2,ERROR_EVIDENCE,DISC_A_EVIDENCE ", d-084d"),PAIR(4,3,ERROR_EVIDENCE,DISC_A_EVIDENCE)
};
#undef PAIR
#undef CELL
#undef DISC_B_EVIDENCE
#undef DISC_A_EVIDENCE
#undef ERROR_EVIDENCE
#undef DEFERRED
ArcRpcObserverQualification arc_rpc_observer_qualification(unsigned catalogue,
    unsigned cpu, ArcRpcObserver owner, ArcRpcObserverCapability capability)
{
    for(unsigned i=0;i<sizeof(qualifications)/sizeof(qualifications[0]);++i){
        const ArcRpcObserverQualification *q=&qualifications[i];
        if(q->catalogue==catalogue && q->cpu==cpu && q->observer==owner && q->capability==capability)return *q;
    }
    return (ArcRpcObserverQualification){catalogue,cpu,1,owner,capability,false,
        "unknown firmware, CPU pair or sub-capability"};
}
bool arc_rpc_observer_unavailable(ArcRpcObserver owner,ArcRpcObserverCapability capability)
{ return !arc_rpc_observer_qualification(0,0,owner,capability).enabled; }
uint64_t arc_rpc_observer_epoch(void) { return observer.epoch; }
uint64_t arc_rpc_observer_generation(ArcRpcObserver owner)
{ return (unsigned)owner<ARC_RPC_OBSERVERS?observer.owners[owner].generation:0; }
void arc_rpc_observer_cancel(ArcRpcObserver owner)
{
    if((unsigned)owner>=ARC_RPC_OBSERVERS)return;
    ++observer.owners[owner].generation;observer.owners[owner].answer=false;
    for(unsigned i=0;i<ARC_RPC_WATCHES;++i)if(observer.watches[i].live && observer.watches[i].owner==owner)watch_lost(i);
    unsigned count=0;
    for(unsigned i=0;i<observer.work_count;++i)if(observer.work[i].observer!=owner)observer.work[count++]=observer.work[i];
    observer.work_count=count;gate();
}
void arc_rpc_observer_retire(bool retain_trace)
{
    /* A contained fault can jump past the interpreter's batch return. The
     * existing timing counter has already counted its faulting attempt. */
    if(observer.batch.live && observers_active)
        observer.instruction+=arc_rpc_mem_fast.instructions-observer.batch.start-observer.batch.accounted;
    observer.batch.live=false;
    ++observer.epoch;
    for(unsigned i=0;i<ARC_RPC_OBSERVERS;++i){arc_rpc_observer_cancel((ArcRpcObserver)i);
        observer.owners[i].armed=false;memset(&observer.owners[i].filter,0,sizeof(observer.owners[i].filter));}
    observer.work_count=0;observer.in_swi=observer.completed=false;
    memset(observer.handlers,0,sizeof(observer.handlers));
    memset(&observer.origin,0,sizeof(observer.origin));
#if defined(ARCDROID_TEST_FAULTS)
    for(unsigned i=0;i<trace.status.count;++i){
        const ArcRpcObservationTrace *t=&trace.records[i];
        if(t->event.kind==ARC_RPC_OBS_GAP){
            if(!trace.gap.lost || t->event.order<trace.gap.event.order)trace.gap.event=t->event;
            if(t->lost_end_ns>trace.gap.lost_end_ns)trace.gap.lost_end_ns=t->lost_end_ns;
            trace.gap.lost+=t->lost;
        }else trace_loss(&t->event);
    }
    trace.status.count=0;memset(trace.watches,0,sizeof(trace.watches));
    if(!retain_trace){trace.status.enabled=false;memset(&trace.gap,0,sizeof(trace.gap));}
#else
    (void)retain_trace;
#endif
    gate();
}
ArcRpcBacking arc_rpc_observer_backing(const uint32_t *word)
{
    /* uintptr comparisons avoid subtraction between unrelated allocations. */
    extern uint32_t *rom,*ram00,*ram01,*vram;
    const uint32_t *bases[]={NULL,rom,ram00,ram01,vram};
    const size_t sizes[]={0,8u<<20,32u<<20,32u<<20,2u<<20};
    uintptr_t p=(uintptr_t)word;
    for(unsigned i=1;i<5;++i)if(bases[i] && p>=(uintptr_t)bases[i] &&
        p-(uintptr_t)bases[i]<sizes[i])return (ArcRpcBacking){(ArcRpcBackingKind)i,(uint32_t)(p-(uintptr_t)bases[i])};
    return (ArcRpcBacking){0,0};
}
void arc_rpc_observe_batch_begin(void)
{
    observer.batch.live=arc_rpc_mem_fast.enabled;
    observer.batch.start=arc_rpc_mem_fast.instructions;observer.batch.accounted=0;
}
void arc_rpc_observe_advance(unsigned attempts)
{
    if(observers_active)observer.instruction+=attempts;
    observer.batch.accounted+=attempts;
}
void arc_rpc_observe_batch_end(void) { observer.batch.live=false; }
void arc_rpc_observe_unsubscribed_mode(uint32_t old_mode)
{
    if(!arc_rpc_observer_active)return;
    if(arc_rpc_observer_context)arc_rpc_observe_mode();
    else if(arm.mode!=old_mode && observers_active)++observer.order;
}
void arc_rpc_observe_begin(uint32_t pc)
{
    if(observers_active)++observer.instruction;
#if defined(ARCDROID_TEST_FAULTS)
    if(trace.status.enabled)++trace.instruction;
    if(arc_rpc_mem_fast.integer_batch)++arc_rpc_observer_integer_attempts;
#endif
    observer.origin.instruction=observer.instruction;observer.origin.epoch=observer.epoch;
    observer.origin.ns=arc_rpc_mem_fast.clock?arc_rpc_mem_fast.clock->now_ns:0;
    observer.origin.pc=pc;observer.origin.mode=arm.mode;observer.origin.word=NULL;
}
static ArcRpcObservation capture_event(ArcRpcObservationKind kind, uint16_t mask)
{
#if defined(ARCDROID_TEST_FAULTS)
    ++event_captures;
#endif
    ArcRpcObservation e={.instruction=observer.origin.instruction,.epoch=observer.origin.epoch,
        .ns=observer.origin.ns,.pc=observer.origin.pc,.mode=observer.origin.mode,
        .backing=arc_rpc_observer_backing(observer.origin.word)};
    e.order=0;e.kind=kind;e.old_mode=observer.origin.mode;e.mode=arm.mode;
    e.privileged=(arm.mode&15)!=0;e.width=(arm.mode&16)?32:26;
    e.nzcv=arm.reg[(arm.mode&16)?16:15]&0xf0000000u;
    e.register_mask=(uint16_t)(mask|0x600fu);
    for(unsigned i=0;i<15;++i)if(e.register_mask&(1u<<i))e.registers[i]=arm.reg[i];
    return e;
}
static ArcRpcObservation event(ArcRpcObservationKind kind,uint16_t mask)
{
    ArcRpcObservation e=capture_event(kind,mask);e.order=observers_active?++observer.order:0;return e;
}
static bool watch_eligible(uint32_t source,uint32_t target)
{ return (source==0 || source==16) && source==target; }
#if defined(ARCDROID_TEST_FAULTS)
static bool trace_file_reason(uint32_t reason)
{
    switch(reason){
    case 5:case 12:case 13:case 14:case 15:case 16:case 17:
    case 20:case 21:case 22:case 23:case 255:return true;
    default:return false;
    }
}
static bool trace_find_reason(uint32_t reason)
{ return !(reason&~255u) && ((reason&0xc0u)==0x40u || (reason&0xc0u)==0xc0u); }
static uint16_t trace_mask(uint32_t swi,uint32_t reason)
{
    switch(swi&~0x20000u){
    case 6:return reason==15 || reason==21 || reason==129?7:1; /* error-box byte reasons */
    case 7:return reason==21?3:1; /* bounded word reason/block pointer */
    case 0x29:return reason==15?3:1; /* confirmation reason */
    case 8:return trace_file_reason(reason)?0xb:1;
    case 0xd:return trace_find_reason(reason)?0xb:reason==0?3:1;
    case 5:return 1; /* OS_CLI command pointer */
    case 0xe:return 0xf; /* OS_ReadLine pointer, bound and character limits */
    case 0x33:return reason==1 || reason==2?0xf:1; /* named disc requests only */
    case 0x11:return 7; /* OS_Exit error/identifier/result */
    case 0x400c0:case 0x400c7:case 0x400e1:return 0xf;
    case 0x400dd:return 3; /* Wimp_CloseDown task/identifier */
    case 0x400de:return 1; /* Wimp_StartTask command pointer */
    case 0x400df:return 0x3f; /* extended Wimp_ReportError registers */
    default:return 0;
    }
}
static bool trace_byte(uint32_t va,bool privileged,uint8_t *out)
{
    uint32_t physical;
    if(!arc_rpc_inspect_translate(va,privileged,&physical))return false;
    ArcRpcBacking backing=arc_rpc_observer_backing(arc_rpc_phys_page(physical));
    /* Structured scalars and explicitly opted-in paths still cannot copy
     * ROM bytes. The general inspector remains able to inspect ROM. */
    if(backing.kind==ARC_RPC_BACKING_ROM || backing.kind==ARC_RPC_BACKING_NONE)return false;
    return arc_rpc_inspect_byte(va,privileged,out);
}
static bool trace_word(uint32_t va,bool privileged,uint32_t *out)
{
    if((va&3) || va>UINT32_MAX-3)return false;
    uint32_t value=0;
    for(unsigned i=0;i<4;++i){uint8_t byte;if(!trace_byte(va+i,privileged,&byte))return false;value|=(uint32_t)byte<<(8*i);}
    *out=value;return true;
}
static ArcRpcInspectStatus trace_path(uint32_t va,bool privileged,char *out,size_t bound)
{
    out[0]=0;
    for(size_t i=0;i<bound;++i){uint8_t byte;
        if(i>UINT32_MAX-va || !trace_byte(va+(uint32_t)i,privileged,&byte))return ARC_RPC_INSPECT_REFUSED;
        if(!byte || byte==13)return ARC_RPC_INSPECT_OK;
        if(i+1==bound)return ARC_RPC_INSPECT_TRUNCATED;
        out[i]=(char)byte;out[i+1]=0;
    }
    return ARC_RPC_INSPECT_TRUNCATED;
}
static bool trace_swi_selected(uint32_t swi)
{
    if(!trace.selection.swi_count)return true;
    for(unsigned i=0;i<trace.selection.swi_count;++i)if(trace.selection.swis[i]==swi)return true;
    return false;
}
static bool trace_selected(const ArcRpcObservation *e)
{
    return (trace.selection.classes&e->kind) &&
        (e->kind!=ARC_RPC_OBS_SWI || trace_swi_selected(e->swi));
}
static void trace_loss(const ArcRpcObservation *e)
{
    if(!trace.gap.lost || e->order<trace.gap.event.order)
        trace.gap.event=(ArcRpcObservation){.kind=ARC_RPC_OBS_GAP,.order=e->order,
            .instruction=e->instruction,.epoch=e->epoch,.ns=e->ns};
    if(!trace.gap.lost || e->ns>trace.gap.lost_end_ns)trace.gap.lost_end_ns=e->ns;
    ++trace.gap.lost;
    ++trace.status.lost;trace.status.incomplete=true;
}
static void trace_gap(void)
{
    if(trace.gap.lost && trace.status.count<trace.selection.capacity){
        trace.records[trace.status.count++]=trace.gap;
        memset(&trace.gap,0,sizeof(trace.gap));
    }
}
static void trace_event(const ArcRpcObservation *e)
{
    if(!trace.status.enabled || !trace_selected(e))return;
    ArcRpcObservation identity=*e;identity.order=++trace.order;identity.instruction=trace.instruction;e=&identity;
    trace_gap();
    if(trace.status.count==trace.selection.capacity){trace_loss(e);return;}
    ArcRpcObservationTrace t={.event=*e,.inspection=ARC_RPC_INSPECT_OK};
    uint16_t mask=e->kind==ARC_RPC_OBS_SWI?trace_mask(e->swi,e->registers[0]):
        e->kind==ARC_RPC_OBS_CONTINUATION?1:0;
    t.event.register_mask=(uint16_t)(mask|0x6000u);
    for(unsigned i=0;i<13;++i)if(!(mask&(1u<<i)))t.event.registers[i]=0;
    if(e->kind==ARC_RPC_OBS_SWI && (e->swi&~0x20000u)==7 && e->registers[0]==21){
        uint8_t value;t.scalar_valid=trace_byte(e->registers[1],e->privileged,&value);
        if(t.scalar_valid)t.scalar=value;else t.inspection=ARC_RPC_INSPECT_REFUSED;
    }
    if((e->kind==ARC_RPC_OBS_CONTINUATION && (e->nzcv&0x10000000u)) ||
        (e->kind==ARC_RPC_OBS_SWI && (e->swi&~0x20000u)==0x400df)){
        t.scalar_valid=trace_word(e->registers[0],e->privileged,&t.scalar);
        if(!t.scalar_valid)t.inspection=ARC_RPC_INSPECT_REFUSED;
    }
    uint32_t swi=e->swi&~0x20000u;
    if(trace.paths && e->kind==ARC_RPC_OBS_SWI && (swi==5 || (swi==8 && trace_file_reason(e->registers[0])) ||
        (swi==0xd && trace_find_reason(e->registers[0])) ||
        (swi==0x33 && (e->registers[0]==1 || e->registers[0]==2)) || swi==0x400de))
        t.inspection=trace_path(e->registers[swi==5||swi==0x400de?0:swi==0x33?2:1],e->privileged,t.path,swi==0x33?12:sizeof(t.path));
    trace.records[trace.status.count++]=t;
}
#else
#define trace_event(e) ((void)0)
#endif
static bool owner_matches(unsigned i,const ArcRpcObservation *e)
{
    const ArcRpcObserveFilter *f=&observer.owners[i].filter;
    return observer.owners[i].armed && (f->kinds&e->kind) &&
        (f->modes&(1u<<e->mode)) &&
        (e->kind!=ARC_RPC_OBS_SWI || ((e->swi&f->swi_mask)==(f->swi&f->swi_mask) &&
            filter_swi(f,e->swi)));
}
static void deliver(unsigned i,ArcRpcObservation *e)
{
    ArcRpcObserveFilter f=observer.owners[i].filter;
    e->register_mask=(uint16_t)((f.registers&0x1fffu)|0x600fu);
    for(unsigned r=4;r<13;++r)e->registers[r]=(e->register_mask&(1u<<r))?arm.reg[r]:0;
    observer.current_owner=i;observer.current_order=e->order;
    observer.current_generation=observer.owners[i].generation;observer.current_results=f.results;observer.current_kind=e->kind;
    if(f.callback)f.callback(f.opaque,e);
}
static void watch_lost(unsigned slot)
{
    unsigned owner=observer.watches[slot].owner;
    ArcRpcObservation e=event(ARC_RPC_OBS_WATCH_LOST,0);
    e.watch=observer.watches[slot].id;
    observer.watches[slot].live=false;gate();
    if(observer.owners[owner].filter.kinds&ARC_RPC_OBS_WATCH_LOST)deliver(owner,&e);
}
bool arc_rpc_observer_watch_live(ArcRpcObserver owner,uint64_t id)
{
    for(unsigned i=0;i<ARC_RPC_WATCHES;++i)if(observer.watches[i].live &&
        observer.watches[i].owner==owner && observer.watches[i].id==id)return true;
    return false;
}
bool arc_rpc_observer_watch_renew(ArcRpcObserver owner,uint64_t id,unsigned attempts)
{
    if((unsigned)owner>=ARC_RPC_OBSERVERS || !observer.in_swi ||
        observer.current_owner!=(unsigned)owner || observer.current_kind!=ARC_RPC_OBS_SWI ||
        !observer.owners[owner].armed || !attempts || attempts>ARC_RPC_WATCH_ATTEMPT_LIMIT)return false;
    for(unsigned i=0;i<ARC_RPC_WATCHES;++i)if(observer.watches[i].live &&
        observer.watches[i].owner==owner && observer.watches[i].id==id &&
        observer.watches[i].generation==observer.owners[owner].generation){
        observer.watches[i].deadline=observer.instruction+attempts;return true;
    }
    return false;
}
void arc_rpc_observe_fetched(const uint32_t *page)
{
    observer.origin.word=page+((observer.origin.pc&0x3ffu)>>2);
#if defined(ARCDROID_TEST_FAULTS)
    if(trace.status.enabled)for(unsigned i=0;i<ARC_RPC_WATCHES;++i){
        if(!trace.watches[i].live)continue;
        if(trace.instruction>trace.watches[i].deadline){trace.watches[i].live=false;
            ArcRpcObservation lost=capture_event(ARC_RPC_OBS_CONTINUATION,0);lost.order=++trace.order;lost.instruction=trace.instruction;trace_loss(&lost);continue;}
        if(trace.watches[i].wait_return || arm.mode!=trace.watches[i].mode)continue;
        ArcRpcObservation e=capture_event(ARC_RPC_OBS_CONTINUATION,0);e.watch=trace.watches[i].id;
        e.matched=e.pc==trace.watches[i].pc && arm.reg[13]==trace.watches[i].sp;
        trace.watches[i].live=false;trace_event(&e);
    }
#endif
    for(unsigned i=0;i<ARC_RPC_WATCHES;++i){
        if(!observer.watches[i].live)continue;
        unsigned owner=observer.watches[i].owner;
        if(observer.watches[i].generation!=observer.owners[owner].generation ||
            observer.instruction>observer.watches[i].deadline){watch_lost(i);continue;}
        if(observer.watches[i].wait_return || arm.mode!=observer.watches[i].mode)continue;
        ArcRpcObservation e=event(ARC_RPC_OBS_CONTINUATION,0);
        e.watch=observer.watches[i].id;
        e.matched=e.pc==observer.watches[i].pc && arm.reg[13]==observer.watches[i].sp;
        observer.watches[i].live=false;gate(); /* end before callbacks can replace */
        trace_event(&e);deliver(owner,&e);
    }
}
void arc_rpc_observe_return(uint32_t mode)
{
#if defined(ARCDROID_TEST_FAULTS)
    for(unsigned i=0;i<ARC_RPC_WATCHES;++i)if(trace.watches[i].live && trace.watches[i].mode==mode)
        trace.watches[i].wait_return=false;
#endif
    for(unsigned i=0;i<ARC_RPC_WATCHES;++i)if(observer.watches[i].live && observer.watches[i].mode==mode)
        observer.watches[i].wait_return=false;
}
void arc_rpc_observe_mode(void)
{
    if(!arc_rpc_observer_active)return;
    if(arm.mode==observer.origin.mode)return;
    ArcRpcObservation e=event(ARC_RPC_OBS_MODE,0);
    observer.origin.mode=arm.mode;
    trace_event(&e);
    for(unsigned i=0;i<ARC_RPC_OBSERVERS;++i)if(owner_matches(i,&e))deliver(i,&e);
}
static bool swi_wanted(uint32_t opcode)
{
    bool wanted=false;
#if defined(ARCDROID_TEST_FAULTS)
    wanted=trace.status.enabled;
#endif
    if(!wanted && !swi_interest.unrestricted && !cached_swi(opcode&0xfdffffu))return false;
    for(unsigned i=0;!wanted && i<ARC_RPC_OBSERVERS;++i){
        const ArcRpcObserveFilter *f=&observer.owners[i].filter;
        wanted=observer.owners[i].armed && (f->kinds&ARC_RPC_OBS_SWI) &&
            (f->modes&(1u<<arm.mode)) && ((opcode&0xffffffu&f->swi_mask)==(f->swi&f->swi_mask)) &&
            filter_swi(f,opcode);
    }
    return wanted;
}
static bool selected_swi(uint32_t opcode)
{
    ArcRpcObservation e=event(ARC_RPC_OBS_SWI,0x3f);
    e.swi=opcode&0xffffffu;trace_event(&e);
#if defined(ARCDROID_TEST_FAULTS)
    uint32_t normalized=e.swi&~0x20000u;
    if(trace.status.enabled && (trace.selection.classes&ARC_RPC_OBS_CONTINUATION) && trace_swi_selected(e.swi) && watch_eligible(e.mode,e.mode) &&
        ((normalized==8 && trace_file_reason(e.registers[0])) || (normalized==0xd && trace_find_reason(e.registers[0])))){
        unsigned slot=ARC_RPC_WATCHES;
        for(unsigned i=0;i<ARC_RPC_WATCHES;++i)if(!trace.watches[i].live){slot=i;break;}
        if(slot==ARC_RPC_WATCHES){ArcRpcObservation lost=capture_event(ARC_RPC_OBS_CONTINUATION,0);lost.order=++trace.order;lost.instruction=trace.instruction;trace_loss(&lost);}
        else {trace.watches[slot].live=true;trace.watches[slot].id=++trace.watch_id;trace.watches[slot].wait_return=true;
            trace.watches[slot].deadline=trace.instruction+ARC_RPC_WATCH_ATTEMPT_LIMIT;
            trace.watches[slot].pc=e.pc+4;trace.watches[slot].mode=e.mode;trace.watches[slot].sp=e.registers[13];}
    }
#endif
    observer.in_swi=true;observer.completed=false;
    for(unsigned i=0;i<ARC_RPC_OBSERVERS;++i)if(owner_matches(i,&e)){
        deliver(i,&e);if(observer.completed)break;
    }
    if(observer.completed)arc_rpc_observe_return(arm.mode);
    observer.in_swi=false;
    return observer.completed;
}
bool arc_rpc_observe_swi_at(uint32_t opcode,uint32_t pc,const uint32_t *page)
{
    if(!swi_wanted(opcode)){if(observers_active)++observer.order;return false;}
    observer.origin.instruction=observer.instruction;observer.origin.epoch=observer.epoch;
    observer.origin.ns=arc_rpc_mem_fast.clock?arc_rpc_mem_fast.clock->now_ns:0;
    observer.origin.pc=pc;observer.origin.mode=arm.mode;
    observer.origin.word=page+((pc&0x3ffu)>>2);
    return selected_swi(opcode);
}
bool arc_rpc_observe_swi(uint32_t opcode)
{
    if(!swi_wanted(opcode)){if(observers_active)++observer.order;return false;}
    return selected_swi(opcode);
}

uint64_t arc_rpc_observer_watch(ArcRpcObserver owner,uint32_t pc,uint32_t mode,uint32_t sp,unsigned attempts)
{
    if((unsigned)owner>=ARC_RPC_OBSERVERS || !observer.in_swi || observer.current_owner!=(unsigned)owner ||
        !observer.owners[owner].armed || !watch_eligible(observer.origin.mode,mode) || mode>31 || (pc&3) || !attempts || attempts>ARC_RPC_WATCH_ATTEMPT_LIMIT)return 0;
    unsigned slot=ARC_RPC_WATCHES;
    for(unsigned i=0;i<ARC_RPC_WATCHES;++i)if(observer.watches[i].live && observer.watches[i].owner==owner){slot=i;break;}
    if(slot==ARC_RPC_WATCHES)for(unsigned i=0;i<ARC_RPC_WATCHES;++i)if(!observer.watches[i].live){slot=i;break;}
    if(slot==ARC_RPC_WATCHES)return 0;
    if(observer.watches[slot].live)watch_lost(slot);
    observer.watches[slot].live=true;observer.watches[slot].owner=owner;
    observer.watches[slot].id=++observer.watch_id;observer.watches[slot].generation=observer.owners[owner].generation;
    observer.watches[slot].deadline=observer.instruction+attempts;
    observer.watches[slot].wait_return=true;
    observer.watches[slot].pc=pc;observer.watches[slot].mode=mode;observer.watches[slot].sp=sp;
    gate();return observer.watch_id;
}
bool arc_rpc_observer_complete(ArcRpcObserver owner,uint64_t order,uint64_t epoch,
    uint64_t generation,uint16_t mask,const uint32_t registers[13],uint32_t cv)
{
    if((unsigned)owner>=ARC_RPC_OBSERVERS || !observer.in_swi || observer.completed ||
        observer.current_owner!=(unsigned)owner || observer.current_kind!=ARC_RPC_OBS_SWI ||
        observer.current_order!=order || observer.epoch!=epoch || generation!=observer.current_generation ||
        !observer.owners[owner].armed || observer.owners[owner].generation!=generation || (mask&~0x1fffu) ||
        (mask&~observer.current_results) || (mask&&!registers) || (cv&~0x30000000u))return false;
    for(unsigned i=0;i<13;++i)if(mask&(1u<<i))arm.reg[i]=registers[i];
    unsigned flags=(arm.mode&16)?16:15;
    arm.reg[flags]=(arm.reg[flags]&~0x30000000u)|cv;
    observer.completed=true;return true;
}
bool arc_rpc_observer_answer(ArcRpcObserver owner,uint32_t value)
{
    if((unsigned)owner>=ARC_RPC_OBSERVERS || !observer.owners[owner].armed)return false;
    observer.owners[owner].answer=true;observer.owners[owner].value=value;
    observer.owners[owner].answer_order=observer.order;return true;
}
bool arc_rpc_observer_take_answer(ArcRpcObserver owner,uint32_t *value)
{
    if((unsigned)owner>=ARC_RPC_OBSERVERS || !value || !observer.in_swi ||
        observer.current_owner!=(unsigned)owner || !observer.owners[owner].answer ||
        observer.current_order<=observer.owners[owner].answer_order)return false;
    *value=observer.owners[owner].value;observer.owners[owner].answer=false;return true;
}
bool arc_rpc_observer_queued(ArcRpcObserver owner,unsigned kind)
{
    if((unsigned)owner>=ARC_RPC_OBSERVERS)return false;
    for(unsigned i=0;i<observer.work_count;++i){
        const ArcRpcHostWork *w=&observer.work[i];
        if(w->observer==owner && w->kind==kind)return true;
    }
    return false;
}
void arc_rpc_observer_drop_queued(ArcRpcObserver owner,unsigned kind,const void *bytes,size_t size)
{
    if((unsigned)owner>=ARC_RPC_OBSERVERS || !bytes || !size || size>ARC_RPC_COPY_LIMIT)return;
    unsigned count=0;
    for(unsigned i=0;i<observer.work_count;++i){
        const ArcRpcHostWork *w=&observer.work[i];
        if(w->observer==owner && w->kind==kind && w->size==size && !memcmp(w->bytes,bytes,size))continue;
        observer.work[count++]=*w;
    }
    observer.work_count=count;
}
bool arc_rpc_observer_queue(const ArcRpcHostWork *work)
{
    if(!work || (unsigned)work->observer>=ARC_RPC_OBSERVERS || work->size>ARC_RPC_COPY_LIMIT ||
        work->epoch!=observer.epoch || work->generation!=observer.owners[work->observer].generation ||
        !observer.owners[work->observer].armed)return false;
    if(observer.work_count==ARC_RPC_WORK_LIMIT){arc_rpc_observer_cancel(work->observer);return false;}
    observer.work[observer.work_count++]=*work;return true;
}
void arc_rpc_observer_host_handler(ArcRpcObserver owner,const ArcRpcHostWorkHandler *h)
{ if((unsigned)owner<ARC_RPC_OBSERVERS){arc_rpc_observer_cancel(owner);observer.handlers[owner]=h?*h:(ArcRpcHostWorkHandler){0};} }
static bool work_valid(const ArcRpcHostWork *w,const ArcRpcHostWorkHandler *h)
{
    return w->epoch==observer.epoch && w->generation==observer.owners[w->observer].generation &&
        observer.owners[w->observer].armed && h->valid && h->valid(h->opaque,w) &&
        w->epoch==observer.epoch && w->generation==observer.owners[w->observer].generation;
}
void arc_rpc_observer_drain(void)
{
    if(host_active)return;
    host_active=true;
    /* Pop a copied request before invoking host callbacks: cancellation can
     * compact the remaining queue, and resolution can retire the epoch. */
    unsigned budget=ARC_RPC_WORK_LIMIT;
    while(observer.work_count && budget--){
        ArcRpcHostWork w=observer.work[0];
        --observer.work_count;memmove(observer.work,observer.work+1,observer.work_count*sizeof(*observer.work));
        ArcRpcHostWorkHandler h=observer.handlers[w.observer];
        if(work_valid(&w,&h) && h.resolve && h.resolve(h.opaque,&w) && work_valid(&w,&h) && h.mutate)h.mutate(h.opaque,&w);
    }
    host_active=false;
}
bool arc_rpc_observer_filter(ArcRpcObserver owner,const ArcRpcObserveFilter *filter)
{
    if((unsigned)owner>=ARC_RPC_OBSERVERS || (filter && ((filter->results&~0x1fffu) || (filter->registers&~0x1fffu) || filter->swi_count>32 || (filter->swi_count && !filter->swis))))return false;
    arc_rpc_observer_cancel(owner);observer.owners[owner].filter=filter?*filter:(ArcRpcObserveFilter){0};
    observer.owners[owner].armed=filter!=NULL;gate();return true;
}
#if defined(ARCDROID_TEST_FAULTS)
RETRO_API bool arc_rpc_observer_test_filter(ArcRpcObserver o,const ArcRpcObserveFilter *f)
{ return arc_rpc_observer_filter(o,f); }
const ArcRpcObserverQualification *arc_rpc_observer_test_qualifications(size_t *count)
{ *count=sizeof(qualifications)/sizeof(qualifications[0]);return qualifications; }
RETRO_API void arc_rpc_observer_trace(bool enabled,bool private_paths)
{
    /* Reset metadata; count gates all record reads. Avoid touching the entire
     * qualification buffer on ordinary test-session loads. */
    trace.order=trace.instruction=trace.watch_id=arc_rpc_observer_integer_attempts=0;
    trace.status=(ArcRpcTraceStatus){.enabled=enabled};trace.paths=private_paths;
    memset(&trace.gap,0,sizeof(trace.gap));memset(trace.watches,0,sizeof(trace.watches));
    trace.selection=(ArcRpcTraceSelection){.classes=ARC_RPC_OBS_SWI,.capacity=ARC_RPC_TRACE_LIMIT};gate();
}
RETRO_API bool arc_rpc_observer_trace_select(const ArcRpcTraceSelection *selection)
{
    if(!selection || !selection->classes || (selection->classes&~7u) ||
        !selection->capacity || selection->capacity>ARC_RPC_TRACE_LIMIT ||
        selection->swi_count>64 || trace.status.count || trace.gap.lost)return false;
    for(unsigned i=0;i<selection->swi_count;++i){
        if(selection->swis[i]>0xffffffu)return false;
        for(unsigned j=0;j<i;++j)if(selection->swis[j]==selection->swis[i])return false;
    }
    trace.selection=*selection;return true;
}
RETRO_API void arc_rpc_observer_trace_status(ArcRpcTraceStatus *out) { if(out){*out=trace.status;out->integer_attempts=arc_rpc_observer_integer_attempts;} }
RETRO_API size_t arc_rpc_observer_trace_read(ArcRpcObservationTrace *out,size_t n)
{
    if(!out)return 0;
    trace_gap();
    if(n>trace.status.count)n=trace.status.count;
    memcpy(out,trace.records,n*sizeof(*out));
    trace.status.count-=(unsigned)n;memmove(trace.records,trace.records+n,trace.status.count*sizeof(*out));trace_gap();return n;
}
RETRO_API void arc_rpc_observer_trace_drain(ArcRpcTraceSink sink,void *opaque)
{
    trace_gap();
    unsigned n=trace.status.count;
    bool failed=!sink;
    for(unsigned i=0;i<n;++i)if(!sink || !sink(opaque,&trace.records[i])){
        for(unsigned j=i;j<n;++j)if(trace.records[j].event.kind!=ARC_RPC_OBS_GAP)++trace.status.lost;
        failed=true;break;
    }
    trace.status.count=0;
    if(trace.gap.lost){
        if(!failed && !sink(opaque,&trace.gap))failed=true;
        memset(&trace.gap,0,sizeof(trace.gap));
    }
    if(failed)trace.status.failed=trace.status.incomplete=true;
}
RETRO_API void arc_rpc_observer_test_seed_pending(void)
{
    ArcRpcObserveFilter f={.modes=~0u,.kinds=7};arc_rpc_observer_test_filter(ARC_RPC_OBS_ERROR,&f);
    arc_rpc_observer_answer(ARC_RPC_OBS_ERROR,1);
    observer.watches[0].live=true;observer.watches[0].owner=ARC_RPC_OBS_ERROR;
    observer.watches[0].generation=observer.owners[ARC_RPC_OBS_ERROR].generation;
    ArcRpcHostWork w={.observer=ARC_RPC_OBS_ERROR,.epoch=observer.epoch,.generation=observer.owners[ARC_RPC_OBS_ERROR].generation};
    arc_rpc_observer_queue(&w);arc_rpc_observer_trace(true,false);
    trace.records[0]=(ArcRpcObservationTrace){0};trace.status.count=1;
}
RETRO_API uint64_t arc_rpc_observer_test_epoch(void) { return observer.epoch; }
uint64_t arc_rpc_observer_test_events(void) { return observer.order; }
ArcRpcObservation arc_rpc_observer_test_origin(void)
{ return (ArcRpcObservation){.instruction=observer.origin.instruction,.epoch=observer.origin.epoch,
    .ns=observer.origin.ns,.pc=observer.origin.pc,.mode=observer.origin.mode,
    .backing=arc_rpc_observer_backing(observer.origin.word)}; }
uint64_t arc_rpc_observer_test_attempts(void) { return observer.instruction; }
RETRO_API bool arc_rpc_observer_test_clean(void)
{
    if(observer.work_count || observer.in_swi)return false;
    for(unsigned i=0;i<ARC_RPC_WATCHES;++i)if(observer.watches[i].live)return false;
    for(unsigned i=0;i<ARC_RPC_OBSERVERS;++i)if(observer.owners[i].answer||observer.owners[i].armed)return false;
    return trace.status.count==0;
}
#endif
