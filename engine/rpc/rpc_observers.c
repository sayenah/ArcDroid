/* R3c-2: policy belongs to ArcDroid, never the imported CPU. GPL-2.0-only. */
#include "rpc_observers.h"
#include "rpc_floppy.h"
#include "rpc_timing.h"
#include <limits.h>
#include <string.h>

static struct {
    unsigned catalogue,cpu,cells;
    uint64_t epoch,error_watch,disc_watch,disc_media,helper_media;
    ArcErrorProfile error_profile;
    ArcErrorBox error;
    ArcLaunchOutcome launch;
    ArcBootWatch boot;
    ArcDiscRequest disc;
    bool disc_enabled;
    const char *disc_signal, *disc_reason;
#if defined(ARCDROID_TEST_FAULTS)
    bool overridden;
#endif
    char inserted[11];
    ArcDiscResolveCallback resolve; ArcDiscObservationCallback observe; void *opaque;
    unsigned resolved_candidate;
} state;
static unsigned watch_budget=ARC_RPC_WATCH_ATTEMPT_LIMIT;
static const ArcDiscProfile disc_profile={(1u<<0)|(1u<<16),(1u<<3)|(1u<<19),0,true,false,true};
static const ArcOutcomeProfile launch_profile={600000000u,true,true,true};
static const ArcBootProfile boot_profile={40000000u,2000000u,0x8000u,true,true};
static const unsigned bits[4]={ARC_RPC_CELL_BOOT,ARC_RPC_CELL_LAUNCH,ARC_RPC_CELL_ERROR,ARC_RPC_CELL_DISC_A|ARC_RPC_CELL_DISC_B};
static bool user(uint32_t mode) { return mode==0u || mode==16u; }
bool arc_rpc_observers_capability(unsigned bit)
{ return state.epoch==arc_rpc_observer_epoch() && (state.cells&bit)==bit; }
static uint64_t now(void) { return arc_rpc_time_ns()/1000u; }
void arc_rpc_observers_retire(void)
{
    memset(&state,0,sizeof(state));
    state.error.modal_unknown=true;
}
static void error_loss(void)
{
    arc_error_end(&state.error);state.error.modal_unknown=true;state.error_watch=0;
}
static void error_event(void *opaque,const ArcRpcObservation *e)
{
    (void)opaque;
    if(e->kind==ARC_RPC_OBS_WATCH_LOST || e->kind==ARC_RPC_OBS_CONTINUATION){
        if(e->watch!=state.error_watch)return;
        arc_error_end(&state.error);state.error_watch=0;
        state.error.modal_unknown=e->kind==ARC_RPC_OBS_WATCH_LOST || !e->matched;
        return;
    }
    if(e->kind!=ARC_RPC_OBS_SWI)return;
    uint32_t swi=e->swi&~0x20000u;
    if(swi==0x400dfu){
        /* A privileged nested report also retires the parent's answer. */
        if(state.error_watch){arc_rpc_observer_cancel(ARC_RPC_OBS_ERROR);error_loss();}
        if(!user(e->mode))return;
        arc_rpc_observer_cancel(ARC_RPC_OBS_ERROR);
        arc_error_swi(&state.error,e->swi,e->registers[0],e->registers[1],e->registers[2],
            e->pc,e->mode,e->registers[13],e->registers[14],e->backing.kind==ARC_RPC_BACKING_ROM,false,0);
        state.error_watch=arc_rpc_observer_watch(ARC_RPC_OBS_ERROR,e->pc+4,e->mode,e->registers[13],watch_budget);
        /* Track every user report, even unknown/asynchronous flag forms:
         * this conservatively blocks replacement until its return. */
        if(!state.error_watch){error_loss();return;}
        return;
    }
    if(swi!=6u || e->registers[0]!=129u || !state.error_watch ||
        !arc_rpc_observer_watch_live(ARC_RPC_OBS_ERROR,state.error_watch))return;
    uint8_t key=0;
    if(arc_error_poll(&state.error,e->registers[1],e->registers[2],e->pc,e->mode,
        e->registers[13],e->backing.kind==ARC_RPC_BACKING_ROM,&key)){
        uint32_t results[13]={0};results[1]=key;
        if(!arc_rpc_observer_complete(ARC_RPC_OBS_ERROR,e->order,e->epoch,
            arc_rpc_observer_generation(ARC_RPC_OBS_ERROR),6,results,0))error_loss();
    }
}
static void launch_event(void *opaque,const ArcRpcObservation *e)
{
    (void)opaque;if(e->kind!=ARC_RPC_OBS_SWI || !user(e->mode))return;
    char command[256];const char *start=NULL;
    if((e->swi&~0x20000u)==0x400deu &&
       arc_rpc_inspect_string(e->registers[0],false,command,sizeof(command)).status==ARC_RPC_INSPECT_OK)start=command;
    arc_outcome_swi(&state.launch,e->swi,e->registers[0],e->registers[1],e->registers[2],
        e->backing.kind==ARC_RPC_BACKING_ROM,start,e->ns/1000u);
}
static void boot_event(void *opaque,const ArcRpcObservation *e)
{
    (void)opaque;if(e->kind!=ARC_RPC_OBS_SWI || !user(e->mode))return;
    arc_boot_watch_observe(&state.boot,e->swi,e->registers[0],e->registers[1],e->registers[2],e->registers[3],
        e->pc,e->mode,e->backing.kind==ARC_RPC_BACKING_ROM,e->ns/1000u);
}
/* Scalars sit beside the copied payload so the complete 256-byte inspector
 * string bound remains available to disc paths. */
typedef struct {
    char object[256]; uint32_t continuation,sp,operation;
    uint64_t watch; unsigned signal;
} DiscCopy;
static DiscCopy disc_copy(const ArcRpcHostWork *work)
{
    DiscCopy copy={.continuation=work->continuation,.sp=work->sp,.operation=work->operation,
        .watch=work->watch,.signal=work->kind};
    if(work->size && work->size<=sizeof(copy.object))memcpy(copy.object,work->bytes,work->size);
    return copy;
}
static bool equal(const char *a,const char *b)
{
    while(*a && *b){unsigned x=(unsigned char)*a++,y=(unsigned char)*b++;
        if(x>='A'&&x<='Z')x+='a'-'A';if(y>='A'&&y<='Z')y+='a'-'A';if(x!=y)return false;}
    return *a==*b;
}
static bool excluded(const char *name)
{
    char helper[11],hd[11];
    return !arc_rpc_observers_identities(helper,hd) ||
        (helper[0] && equal(name,helper)) || (hd[0] && equal(name,hd)) ||
        equal(name,"ArcDroidHD") || equal(name,"1") || equal(name,"4");
}
static bool known_nonmodal(void)
{ return arc_rpc_observers_capability(ARC_RPC_CELL_ERROR) && !state.error.recognised_open && !state.error.modal_unknown; }
bool arc_rpc_observers_replace(ArcDiscSignal signal)
{
    unsigned bit=signal==ARC_DISC_SIGNAL_A?ARC_RPC_CELL_REPLACE_A:ARC_RPC_CELL_REPLACE_B;
    if(!arc_rpc_observers_capability(bit) || !known_nonmodal() ||
        state.disc_media!=arc_rpc_fdc_media_generation(0) || state.helper_media!=arc_rpc_fdc_media_generation(1))return false;
    if(signal==ARC_DISC_SIGNAL_A)return state.disc.a_active && !excluded(state.disc.a_name);
    return state.disc.b_triggered && !excluded(state.inserted);
}
static bool work_valid(void *opaque,const ArcRpcHostWork *w)
{
    (void)opaque;
    if(!w->size || w->size>ARC_RPC_COPY_LIMIT || w->bytes[w->size-1])return false;
    DiscCopy copy=disc_copy(w);
    return state.disc_enabled && w->epoch==state.epoch &&
        w->media_generation==arc_rpc_fdc_media_generation(0) &&
        w->helper_generation==arc_rpc_fdc_media_generation(1) &&
        arc_rpc_observers_capability(copy.signal==ARC_DISC_SIGNAL_A?ARC_RPC_CELL_DISC_A:ARC_RPC_CELL_DISC_B) &&
        !excluded(copy.signal==ARC_DISC_SIGNAL_A?copy.object:state.inserted);
}
static bool work_resolve(void *opaque,const ArcRpcHostWork *w)
{
    (void)opaque;DiscCopy copy=disc_copy(w);
    if(copy.signal!=ARC_DISC_SIGNAL_B || !state.resolve)return false;
    state.resolved_candidate=state.resolve(state.opaque,copy.object,(ArcDiscOperation)copy.operation);
    state.disc_reason=state.resolved_candidate!=UINT_MAX?"candidate-resolved":"no-candidate";
    return state.resolved_candidate!=UINT_MAX;
}
static void work_mutate(void *opaque,const ArcRpcHostWork *w)
{
    (void)opaque;DiscCopy copy=disc_copy(w);
    /* Restore the copied identity, not the later SWI's mutable arm. */
    ArcDiscRequest observation=state.disc;
    strcpy(observation.object,copy.object);observation.continuation=copy.continuation;
    observation.sp=copy.sp;observation.operation=(ArcDiscOperation)copy.operation;
    ArcDiscResult result=arc_disc_request_resolved(&observation,state.resolved_candidate,w->ns/1000u);
    memcpy(state.disc.identities,observation.identities,sizeof(state.disc.identities));
    state.disc.next_order=observation.next_order;state.disc.b_triggered=observation.b_triggered;
    state.disc.triggered_slot=observation.triggered_slot;
    strcpy(state.disc.triggered_object,observation.triggered_object);
    state.disc.triggered_continuation=observation.triggered_continuation;state.disc.triggered_sp=observation.triggered_sp;
    state.disc.triggered_us=observation.triggered_us;state.disc.observations=observation.observations;
    if(result.observed && state.observe){
        bool retry=state.observe(state.opaque,copy.object,result.candidate,result.observation,result.trigger);
        if(retry)arc_disc_request_retry(&state.disc);
    }
}
static void queue_miss(const ArcRpcObservation *e)
{
    DiscCopy copy={.continuation=state.disc.continuation,.sp=state.disc.sp,
        .operation=state.disc.operation,.watch=e->watch,.signal=ARC_DISC_SIGNAL_B};
    size_t length=strlen(state.disc.object);
    if(length>=sizeof(copy.object)){arc_rpc_observers_disc_clear();return;}
    memcpy(copy.object,state.disc.object,length+1);
    ArcRpcHostWork work={.observer=ARC_RPC_OBS_DISC,.generation=arc_rpc_observer_generation(ARC_RPC_OBS_DISC),
        .epoch=e->epoch,.media_generation=state.disc_media,.helper_generation=state.helper_media,.ns=e->ns,.kind=ARC_DISC_SIGNAL_B,.size=length+1,
        .operation=copy.operation,.continuation=copy.continuation,.sp=copy.sp,.watch=copy.watch};
    memcpy(work.bytes,copy.object,length+1);
    if(!arc_rpc_observer_queue(&work))arc_disc_request_clear(&state.disc);
}
static void disc_event(void *opaque,const ArcRpcObservation *e)
{
    (void)opaque;if(!state.disc_enabled)return;
    if(e->kind==ARC_RPC_OBS_WATCH_LOST){state.disc_reason="watch-lost";state.disc.armed=false;state.disc_watch=0;return;}
    if(e->kind==ARC_RPC_OBS_CONTINUATION){
        if(e->watch!=state.disc_watch)return;
        uint32_t error=0;bool valid=arc_rpc_inspect_word(e->registers[0],false,&error);
        ArcDiscResult result=arc_disc_request_outcome(&state.disc,e->matched?state.disc.continuation:UINT32_MAX,
            0,e->registers[13],e->registers[0],(e->nzcv&0x10000000u)!=0,valid,error);
        state.disc_reason=result.needs_resolution?"miss-queued":"outcome-refused";
        if(result.needs_resolution)queue_miss(e);
        else if(!(e->nzcv&0x10000000u) &&
            (state.disc.operation==ARC_DISC_LOAD || e->registers[0]!=0))
            arc_rpc_observer_cancel(ARC_RPC_OBS_DISC); /* found object cancels copied misses synchronously */
        state.disc_watch=0;return;
    }
    if(e->kind!=ARC_RPC_OBS_SWI)return;
    uint32_t swi=e->swi&~0x20000u;
    if(swi==0x400c7u || swi==0x400e1u || swi==0x400c0u || swi==0x400ddu || swi==0x400deu || swi==0x11u || swi==0x40u ||
        (swi==0x33u && e->registers[0]==4u)){arc_rpc_observers_disc_clear();return;}
    if(swi==0x33u){
        state.disc_signal="A";
        state.disc_reason=!arc_rpc_observers_capability(ARC_RPC_CELL_DISC_A)?"observation-disabled":
            e->backing.kind!=ARC_RPC_BACKING_ROM?"non-rom":
            (e->registers[0]!=1 && e->registers[0]!=2)?"upcall-reason":
            e->registers[1]!=8?"filesystem":NULL;
        if(state.disc_reason)return;
        char name[12];
        if(arc_rpc_inspect_string(e->registers[2],true,name,sizeof(name)).status!=ARC_RPC_INSPECT_OK){state.disc_reason="name-inspection";return;}
        /* Identity inspection/resolution is host-only. A copied name is
         * exposed at the boundary, then revalidated by the frontend commit. */
        ArcDiscSwi input={.swi=e->swi,.r0=e->registers[0],.r1=8,.r3=e->registers[3],.mode=e->mode,.in_rom=true,
            .name_readable=true,.name=name};
        if(!arc_disc_request_signal_a(&disc_profile,&input)){
            state.disc_reason=!(e->swi&0x20000u)?"non-x":
                (e->mode!=3 && e->mode!=19)?"non-svc":e->registers[3]!=0?"device":"bare-name";return;}
        state.disc_reason="seen";
        arc_rpc_observer_cancel(ARC_RPC_OBS_DISC); /* A supersedes copied B misses synchronously. */
        state.disc.profile=&disc_profile;arc_disc_request_swi(&state.disc,&input,state.inserted);
        state.disc_media=arc_rpc_fdc_media_generation(0);state.helper_media=arc_rpc_fdc_media_generation(1);
        return;
    }
    ArcDiscOperation operation;
    if(!arc_disc_request_operation(e->swi,e->registers[0],&operation))return;
    state.disc.armed=false;state.disc_signal="B";
    state.disc_reason=!arc_rpc_observers_capability(ARC_RPC_CELL_DISC_B)?"observation-disabled":!user(e->mode)?"non-user":NULL;
    if(state.disc_reason)return;
    char path[256];
    if(arc_rpc_inspect_string(e->registers[1],false,path,sizeof(path)).status!=ARC_RPC_INSPECT_OK){state.disc_reason="path-inspection";return;}
    ArcDiscSwi input={.swi=e->swi,.r0=e->registers[0],.mode=e->mode,.address=e->pc,.user_sp=e->registers[13],
        .name_readable=true,.name=path,.now_us=e->ns/1000u};
    state.disc.profile=&disc_profile;arc_disc_request_swi(&state.disc,&input,state.inserted);
    state.disc_reason=state.disc.armed?"armed":"path-refused";
    if(state.disc.armed){
        state.disc_watch=arc_rpc_observer_watch(ARC_RPC_OBS_DISC,e->pc+4,e->mode,e->registers[13],watch_budget);
        state.disc_media=arc_rpc_fdc_media_generation(0);state.helper_media=arc_rpc_fdc_media_generation(1);
        if(!state.disc_watch){state.disc_reason="watch-admission";state.disc.armed=false;}
    }
}
static void filters(void)
{
    ArcRpcObserveCallback callbacks[]={boot_event,launch_event,error_event,disc_event};
    for(unsigned i=0;i<4;++i){
        ArcRpcObserveFilter filter={.modes=~0u,.kinds=ARC_RPC_OBS_SWI|ARC_RPC_OBS_CONTINUATION|ARC_RPC_OBS_WATCH_LOST,
            .results=i==ARC_RPC_OBS_ERROR?6:0,.callback=callbacks[i]};
        arc_rpc_observer_filter((ArcRpcObserver)i,(state.cells&bits[i])?&filter:NULL);
    }
    ArcRpcHostWorkHandler handler={work_valid,work_resolve,work_mutate,NULL};
    arc_rpc_observer_host_handler(ARC_RPC_OBS_DISC,&handler);
}
void arc_rpc_observers_bind(unsigned catalogue,unsigned cpu)
{
    arc_rpc_observers_retire();state.epoch=arc_rpc_observer_epoch();state.catalogue=catalogue;state.cpu=cpu;
    state.error_profile=(ArcErrorProfile){.contextual=true};state.error.profile=&state.error_profile;
    state.boot.profile=&boot_profile;state.launch.profile=&launch_profile;
    /* Every cell is disabled. Future promotion derives cells only from the
     * exact verified tuple; recognition itself is never an enabling rule. */
    const struct {ArcRpcObserver owner;ArcRpcObserverCapability capability;unsigned bit;} cells[]={
        {ARC_RPC_OBS_BOOT,ARC_RPC_CAP_OBSERVE,ARC_RPC_CELL_BOOT},
        {ARC_RPC_OBS_LAUNCH,ARC_RPC_CAP_OBSERVE,ARC_RPC_CELL_LAUNCH},
        {ARC_RPC_OBS_ERROR,ARC_RPC_CAP_OBSERVE,ARC_RPC_CELL_ERROR},
        {ARC_RPC_OBS_ERROR,ARC_RPC_CAP_COMPLETE,ARC_RPC_CELL_ANSWER},
        {ARC_RPC_OBS_DISC,ARC_RPC_CAP_OBSERVE,ARC_RPC_CELL_DISC_A},
        {ARC_RPC_OBS_DISC,ARC_RPC_CAP_REPLACE,ARC_RPC_CELL_REPLACE_A},
        {ARC_RPC_OBS_DISC,ARC_RPC_CAP_OBSERVE_B,ARC_RPC_CELL_DISC_B},
        {ARC_RPC_OBS_DISC,ARC_RPC_CAP_REPLACE_B,ARC_RPC_CELL_REPLACE_B}
    };
    for(unsigned i=0;i<sizeof(cells)/sizeof(cells[0]);++i){
        ArcRpcObserverQualification q=arc_rpc_observer_qualification(catalogue,cpu,cells[i].owner,cells[i].capability);
        if(catalogue && q.profile==ARC_RPC_PROFILE_REVISION && q.enabled)state.cells|=cells[i].bit;
    }
    state.error.modal_unknown=!(state.cells&ARC_RPC_CELL_ERROR);filters();
}
#if defined(ARCDROID_TEST_FAULTS)
void arc_rpc_observers_test_binding(unsigned *catalogue,unsigned *cpu)
{ *catalogue=state.catalogue;*cpu=state.cpu; }
void arc_rpc_observers_test_buttons(uint32_t ok,uint32_t cancel,uint32_t defaults)
{ state.error_profile.ok_forms=ok;state.error_profile.cancel_forms=cancel;state.error_profile.default_cancel_forms=defaults; }
void arc_rpc_observers_test_watch_budget(unsigned attempts)
{ watch_budget=attempts<=ARC_RPC_WATCH_ATTEMPT_LIMIT?attempts:ARC_RPC_WATCH_ATTEMPT_LIMIT; }
void arc_rpc_observers_override(unsigned mask,uint32_t r1,uint32_t r2,uint32_t forms)
{
    state.overridden=true;state.cells=mask&255u;state.epoch=arc_rpc_observer_epoch();
    state.error_profile=(ArcErrorProfile){.contextual=true,.poll_r1=r1,.poll_r2=r2,.poll_modes=(1u<<3)|(1u<<19),.answer_forms=(mask&ARC_RPC_CELL_ANSWER)?forms:0};
    for(unsigned flags=0;flags<32;++flags){
        bool ok=(flags&1u) || !(flags&3u),cancel=(flags&2u)!=0;
        uint32_t bit=UINT32_C(1)<<flags;
        if(ok)state.error_profile.ok_forms|=bit;
        if(cancel)state.error_profile.cancel_forms|=bit;
        if(cancel && (!ok || (flags&4u)))state.error_profile.default_cancel_forms|=bit;
    }
    state.error.profile=&state.error_profile;state.boot.profile=&boot_profile;state.launch.profile=&launch_profile;
    arc_error_end(&state.error);state.error.modal_unknown=!(mask&ARC_RPC_CELL_ERROR);
    filters();
}
#endif
void arc_rpc_observers_rebase(unsigned actual_cpu)
{
    /* Hardware reset preserves attached media and host callback leases, but
     * never a pending watch, answer, miss history or launch/prompt arm. */
    unsigned catalogue=state.catalogue,cpu=actual_cpu;
    bool enabled=state.disc_enabled;char inserted[11];memcpy(inserted,state.inserted,sizeof(inserted));
    ArcDiscResolveCallback resolve=state.resolve;ArcDiscObservationCallback observe=state.observe;void *opaque=state.opaque;
#if defined(ARCDROID_TEST_FAULTS)
    bool overridden=state.overridden;unsigned cells=state.cells;ArcErrorProfile profile=state.error_profile;
#endif
    arc_rpc_observers_bind(catalogue,cpu);
#if defined(ARCDROID_TEST_FAULTS)
    if(overridden){arc_rpc_observers_override(cells,profile.poll_r1,profile.poll_r2,profile.answer_forms);
        arc_rpc_observers_test_buttons(profile.ok_forms,profile.cancel_forms,profile.default_cancel_forms);}
#endif
    arc_rpc_observers_disc_configure(enabled,inserted,resolve,observe,opaque);
}
void arc_rpc_observers_frame(uint64_t time)
{
    if(state.epoch!=arc_rpc_observer_epoch())return;
    arc_outcome_frame(&state.launch,time);arc_boot_watch_frame(&state.boot,time);
}
void arc_rpc_observers_error_snapshot(ArcErrorBox *out)
{
    if(!out)return;*out=state.error;
    if(!arc_rpc_observers_capability(ARC_RPC_CELL_ERROR)){memset(out,0,sizeof(*out));out->modal_unknown=true;}
    out->answer_available=out->answer_available && out->pending==ARC_ERROR_NONE && out->consumed==ARC_ERROR_NONE &&
        arc_rpc_observers_capability(ARC_RPC_CELL_ANSWER);
}
bool arc_rpc_observers_answer(ArcErrorAnswer answer)
{ return arc_rpc_observers_capability(ARC_RPC_CELL_ANSWER) && state.error_watch &&
    arc_rpc_observer_watch_live(ARC_RPC_OBS_ERROR,state.error_watch) && arc_error_request(&state.error,answer); }
void arc_rpc_observers_discard(void) { arc_error_discard(&state.error); }
void arc_rpc_observers_boot_arm(void)
{ if(arc_rpc_observers_capability(ARC_RPC_CELL_BOOT))arc_boot_watch_arm_profile(&state.boot,now(),&boot_profile); }
void arc_rpc_observers_boot_cancel(void) { arc_boot_watch_cancel(&state.boot); }
ArcBootWatchState arc_rpc_observers_boot_state(void)
{ return arc_rpc_observers_capability(ARC_RPC_CELL_BOOT)?state.boot.state:ARC_BOOT_WATCH_DISABLED; }
bool arc_rpc_observers_launch_arm(const char *command)
{ return arc_rpc_observers_capability(ARC_RPC_CELL_LAUNCH) && arc_outcome_arm_profile(&state.launch,command,now(),&launch_profile); }
void arc_rpc_observers_launch_cancel(const char *why) { arc_outcome_cancel(&state.launch,why); }
void arc_rpc_observers_refused(unsigned drive) { arc_outcome_refused(&state.launch,drive); }
void arc_rpc_observers_launch_snapshot(ArcLaunchOutcome *out)
{ if(out){*out=state.launch;if(!arc_rpc_observers_capability(ARC_RPC_CELL_LAUNCH))memset(out,0,sizeof(*out));} }
void arc_rpc_observers_disc_clear(void)
{ arc_rpc_observer_cancel(ARC_RPC_OBS_DISC);arc_disc_request_clear(&state.disc);state.disc_watch=0; }
void arc_rpc_observers_disc_configure(bool enabled,const char *inserted,ArcDiscResolveCallback resolve,ArcDiscObservationCallback observe,void *opaque)
{
    arc_rpc_observers_disc_clear();state.disc_enabled=enabled;
    size_t length=inserted?strlen(inserted):0;
    state.inserted[0]=0;if(length<=10 && inserted)memcpy(state.inserted,inserted,length+1);
    state.resolve=resolve;state.observe=observe;state.opaque=opaque;
}
void arc_rpc_observers_disc_snapshot(ArcDiscRequest *out)
{
    if(!out)return;*out=state.disc;
    if(out->a_active && (state.disc_media!=arc_rpc_fdc_media_generation(0) ||
        state.helper_media!=arc_rpc_fdc_media_generation(1) || excluded(out->a_name)))out->a_active=false;
}
void arc_rpc_observers_disc_notified(void) { state.disc.a_notified=true; }

void arc_rpc_observers_disc_status(ArcRpcDiscStatus *out)
{
    if(!out)return;
    memset(out,0,sizeof(*out));out->last_signal=state.disc_signal?state.disc_signal:"none";
    out->last_reason=state.disc_reason?state.disc_reason:"waiting-signal";
    out->signal=state.disc.a_active?"A":out->last_signal;
    memcpy(out->name,state.disc.a_name,sizeof(out->name));
    out->active=state.disc.a_active;out->notified=state.disc.a_notified;
    out->gate=out->last_reason;
    if(!state.disc_enabled)out->gate="frontend-disabled";
    else if(!(state.cells&(ARC_RPC_CELL_DISC_A|ARC_RPC_CELL_DISC_B)))out->gate="observation-disabled";
    if(state.disc.a_active){
        char helper[11],hd[11];
        out->gate=state.disc_media!=arc_rpc_fdc_media_generation(0) ||
            state.helper_media!=arc_rpc_fdc_media_generation(1)?"stale-media":
            arc_rpc_observers_identities(helper,hd)==false?"identity-unknown":
            (helper[0] && equal(state.disc.a_name,helper))?"helper-excluded":
            (hd[0] && equal(state.disc.a_name,hd))?"hd-excluded":
            excluded(state.disc.a_name)?"device-excluded":"seen";
    }
    unsigned replacement=state.disc.a_active || !state.disc_signal || state.disc_signal[0]!='B'?ARC_RPC_CELL_REPLACE_A:ARC_RPC_CELL_REPLACE_B;
    out->replacement=!arc_rpc_observers_capability(replacement)?"replacement-disabled":
        !arc_rpc_observers_capability(ARC_RPC_CELL_ERROR)?"error-observation-disabled":
        state.error.recognised_open?"user-error-open":state.error.modal_unknown?"modal-unknown":"eligible";
}
