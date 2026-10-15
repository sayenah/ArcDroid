/* R3c-2: policy belongs to ArcDroid, never the imported CPU. GPL-2.0-only. */
#include "rpc_observers.h"
#include "rpc_floppy.h"
#include "rpc_timing.h"
#include "rpc_memory.h"
#include <limits.h>
#include <string.h>

static struct {
    unsigned catalogue,cpu,cells;
    uint64_t epoch,error_watch,disc_watch,disc_media,helper_media;
    ArcErrorProfile error_profile;
    ArcDiscProfile disc_profile;
    ArcErrorBox error;
    ArcLaunchOutcome launch;
    ArcBootWatch boot;
    ArcRpcLaunchBinding binding;
    uint64_t launch_started,prompt_watch,prompt_started,host_ordinal;
    uint32_t controller_base,sequence,event,detail,task;
    bool bound_base,registered,dispatched,start_seen,closed,prompt_phase,observation_finished;
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
static const ArcDiscProfile default_disc_profile={(1u<<0)|(1u<<16),(1u<<3)|(1u<<19),ARC_DISC_DEVICE_UNSPECIFIED,true,true};
static const ArcErrorProfile default_error_profile={.contextual=true,.poll_r1=0,.poll_r2=0,
    .poll_modes=(1u<<3)|(1u<<19),.answer_forms=(1u<<3)|(1u<<14),
    .ok_forms=1u<<3,.cancel_forms=(1u<<3)|(1u<<14),.default_cancel_forms=1u<<14};
static const ArcOutcomeProfile launch_profile={120000000u,false,false,false,true};
static const ArcBootProfile boot_profile={120000000u,250000u,0,true,false,true};
static void filters(void);
static const unsigned bits[4]={ARC_RPC_CELL_BOOT,ARC_RPC_CELL_LAUNCH,ARC_RPC_CELL_ERROR,ARC_RPC_CELL_DISC_A|ARC_RPC_CELL_DISC_B};
static bool user(uint32_t mode) { return mode==0u || mode==16u; }
bool arc_rpc_observers_capability(unsigned bit)
{ return state.epoch==arc_rpc_observer_epoch() && (state.cells&bit)==bit; }
static uint64_t now(void) { return arc_rpc_time_ns()/1000u; }
void arc_rpc_observers_retire(void)
{
    if(state.binding.attempt && state.binding.attempt->delivery!=ARC_RPC_DELIVERY_INTERRUPTED)
        arc_riscpc_attempt_interrupt(state.binding.attempt,ARC_RPC_INTERRUPT_FAULT);
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
        /* D-181 qualified error number zero, not serious-error button
         * transformations. Other/unreadable reports remain manual. */
        if(state.error.ok || state.error.cancel){
#if defined(ARCDROID_TEST_FAULTS)
            if(!state.overridden)
#endif
            { uint32_t number=0;
                if(!arc_rpc_inspect_word(e->registers[0],false,&number) || number!=0)
                    state.error.ok=state.error.cancel=state.error.default_cancel=false;
            }
        }
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
static void attempt_event(ArcRpcAttemptEvent event)
{
    ArcRpcLaunchAttempt *a=state.binding.attempt;
    if(a && state.host_ordinal<UINT64_MAX)
        (void)arc_riscpc_attempt_event(a,&a->token,++state.host_ordinal,event);
}
static void prompt_end(void)
{
    state.boot.pending=false;
    if(state.boot.state==ARC_BOOT_WATCH_ARMED || state.boot.state==ARC_BOOT_WATCH_PROMPT)
        state.boot.state=ARC_BOOT_WATCH_CANCELLED;
    if(state.binding.attempt && state.binding.attempt->outcome==ARC_RPC_OUTCOME_WAITING)
        attempt_event(ARC_RPC_EVENT_WAIT_UNOBSERVED);
}
static bool ram(uint32_t at,size_t n,bool privileged)
{
    if(n==0 || n>UINT32_MAX-at)return false;
    for(size_t i=0;;i=i+4<n?i+4:n-1){uint32_t pa=0;
        if(!arc_rpc_inspect_translate(at+(uint32_t)i,privileged,&pa))return false;
        /* Inspector byte access additionally refuses unsupported backing. */
        ArcRpcBacking backing=arc_rpc_observer_backing(arc_rpc_phys_page(pa));
        if(backing.kind!=ARC_RPC_BACKING_RAM0 && backing.kind!=ARC_RPC_BACKING_RAM1 && backing.kind!=ARC_RPC_BACKING_VRAM)return false;
        if(i==n-1)break;
    }return true;
}
static bool authored(const ArcRpcObservation *e,uint32_t words[16])
{
    const ArcRpcLaunchController *c=state.binding.controller;
    if(!c || e->kind!=ARC_RPC_OBS_SWI || !user(e->mode) || (e->swi&~0x20000u)!=6 ||
        e->registers[0] || e->registers[1]!=1 || e->registers[2] ||
        e->registers[4]!=0x53435241u || e->registers[5]!=64 || e->registers[6]!=1 ||
        e->pc<c->query)return false;
    uint32_t base=e->pc-c->query,at=e->registers[3];
    if((base&3) || base>UINT32_MAX-c->size || at!=base+c->status ||
        (state.bound_base && base!=state.controller_base) || !ram(base,c->size,false))return false;
    for(unsigned i=0;i<16;++i)if(!arc_rpc_inspect_word(at+4*i,false,&words[i]))return false;
    ArcRpcLaunchAttempt *a=state.binding.attempt;
    uint64_t token=words[4]|(uint64_t)words[5]<<32,epoch=words[6]|(uint64_t)words[7]<<32;
    if(!a || !arc_riscpc_attempt_matches(a,&a->token) || a->token.serial-1>UINT64_MAX-a->token.epoch ||
        token!=a->token.session || epoch!=a->token.epoch+a->token.serial-1 ||
        words[0]!=0x53435241u || words[1]!=1 || words[2]!=64 || words[3]!=1 ||
        words[8]<1 || words[8]>8 || !words[9] || words[11] || words[12] || words[13] || words[14] || words[15])return false;
    /* Validate the authored image, not just a token-shaped block. Only the
     * status event, sequence and detail may change. No ROM copying. */
    for(size_t i=0;i<c->size;++i){uint8_t byte;
        if((i>=c->status && i<c->status+64) || (words[8]==ARC_RPC_STATUS_REJECTED && i>=c->plan))continue;
        if(!arc_rpc_inspect_byte(base+(uint32_t)i,false,&byte) || byte!=c->image[i])return false;
    }
    state.controller_base=base;state.bound_base=true;return true;
}
static void status_event(const ArcRpcObservation *e)
{
    uint32_t w[16];if(!authored(e,w))return;
    if(w[9]<=state.sequence || w[9]!=state.sequence+1)return;
    ArcRpcLaunchAttempt *a=state.binding.attempt;bool accepted=false;
    switch(w[8]){
    case ARC_RPC_STATUS_CONSUME:
        if(state.sequence || w[10])break;
        if(state.binding.revalidate)state.binding.revalidate(state.binding.opaque);
        accepted=arc_riscpc_attempt_consume(a,&a->token)==ARC_RPC_TRANSITION_APPLIED;
        if(accepted && state.observation_finished){attempt_event(ARC_RPC_EVENT_EXPIRE);arc_rpc_observer_filter(ARC_RPC_OBS_LAUNCH,NULL);}
        break;
    case ARC_RPC_STATUS_REGISTERED:
        if(a->delivery!=ARC_RPC_DELIVERY_CONSUMED || state.registered || !w[10])break;
        state.task=w[10];state.registered=true;accepted=true;break;
    case ARC_RPC_STATUS_DISPATCH:
        if(!state.registered || state.dispatched || w[10])break;
        attempt_event(ARC_RPC_EVENT_DISPATCH);state.dispatched=true;accepted=true;break;
    case ARC_RPC_STATUS_START_RETURNED:
        if(!state.dispatched || state.start_seen)break;
        attempt_event(ARC_RPC_EVENT_START_RETURNED);state.start_seen=true;state.detail=w[10];state.prompt_phase=false;
        arc_rpc_observer_cancel(ARC_RPC_OBS_BOOT);state.prompt_watch=0;prompt_end();accepted=true;break;
    case ARC_RPC_STATUS_RETURNED:case ARC_RPC_STATUS_DESKTOP:
        if(!state.start_seen || state.detail!=w[10] || ((w[8]==ARC_RPC_STATUS_RETURNED)!=(w[10]==0)))break;
        attempt_event(w[8]==ARC_RPC_STATUS_RETURNED?ARC_RPC_EVENT_RETURNED:ARC_RPC_EVENT_DESKTOP);
        accepted=true;break;
    case ARC_RPC_STATUS_REJECTED:
        if(state.dispatched){attempt_event(ARC_RPC_EVENT_EXPIRE);accepted=true;}
        else {attempt_event(ARC_RPC_EVENT_REJECTED);accepted=true;}state.closed=!state.registered;break;
    case ARC_RPC_STATUS_CLOSED:
        if(!state.registered || state.closed || w[10] ||
            (state.event!=ARC_RPC_STATUS_RETURNED && state.event!=ARC_RPC_STATUS_DESKTOP && state.event!=ARC_RPC_STATUS_REJECTED))break;
        state.closed=true;accepted=true;break;
    }
    if(accepted){state.sequence=w[9];state.event=w[8];}
}
static void launch_event(void *opaque,const ArcRpcObservation *e)
{
    (void)opaque;if(e->kind!=ARC_RPC_OBS_SWI || !user(e->mode))return;
    if(state.binding.attempt){
        if(state.observation_finished){status_event(e);return;}
        status_event(e);
        uint32_t swi=e->swi&~0x20000u;
        if(swi==0x400deu){char command[ARC_RPC_PLAN_COMMAND_MAX];
            bool exact=state.bound_base && state.dispatched && !state.start_seen &&
                e->pc==state.controller_base+state.binding.controller->start &&
                e->registers[0]==state.controller_base+state.binding.controller->command &&
                arc_rpc_inspect_string(e->registers[0],false,command,sizeof(command)).status==ARC_RPC_INSPECT_OK &&
                !strcmp(command,state.binding.controller->text);
            if(exact){arc_outcome_swi(&state.launch,e->swi,0,0,0,false,command,e->ns/1000);state.prompt_phase=true;}
            else {state.prompt_phase=false;arc_rpc_observer_cancel(ARC_RPC_OBS_BOOT);state.prompt_watch=0;prompt_end();}
        }else if(swi==0x400c0u && state.dispatched){state.prompt_phase=false;arc_rpc_observer_cancel(ARC_RPC_OBS_BOOT);state.prompt_watch=0;prompt_end();}
        return;
    }
}
static void boot_event(void *opaque,const ArcRpcObservation *e)
{
    if(state.binding.attempt){
        (void)opaque;
        if(e->kind==ARC_RPC_OBS_CONTINUATION || e->kind==ARC_RPC_OBS_WATCH_LOST){
            if(e->watch==state.prompt_watch){state.prompt_watch=0;
                if(e->kind==ARC_RPC_OBS_CONTINUATION && e->matched &&
                    state.binding.attempt->outcome==ARC_RPC_OUTCOME_WAITING)attempt_event(ARC_RPC_EVENT_INPUT_RESUMED);
                prompt_end();}
            return;
        }
        if(e->kind!=ARC_RPC_OBS_SWI || !user(e->mode) || !state.prompt_phase || state.start_seen ||
            (e->swi&~0x20000u)!=0xe || !arc_rpc_observers_capability(ARC_RPC_CELL_BOOT))return;
        if(state.prompt_watch){arc_rpc_observer_cancel(ARC_RPC_OBS_BOOT);state.prompt_watch=0;prompt_end();return;}
        uint32_t r0=e->registers[0],r1=e->registers[1];
        if((r0&0xc0000000u) || !r1 || r1>255 || e->registers[2]!=32 || e->registers[3]!=126 || !ram(r0,(size_t)r1+1,false))return;
        state.prompt_watch=arc_rpc_observer_watch(ARC_RPC_OBS_BOOT,e->pc+4,e->mode,e->registers[13],64000000u);
        if(!state.prompt_watch)return;
        state.prompt_started=e->ns/1000;arc_boot_watch_arm_profile(&state.boot,state.prompt_started,&boot_profile);
        arc_boot_watch_observe(&state.boot,e->swi,r0,r1,32,126,e->pc,e->mode,false,e->ns/1000);return;
    }
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
        if(!arc_disc_request_signal_a(&state.disc_profile,&input)){
            state.disc_reason=!(e->swi&0x20000u)?"non-x":
                (e->mode!=3 && e->mode!=19)?"non-svc":
                !((e->registers[3]==0?ARC_DISC_DEVICE_0:e->registers[3]==UINT32_MAX?ARC_DISC_DEVICE_UNSPECIFIED:0u)&state.disc_profile.device_forms)?"device":"bare-name";return;}
        state.disc_reason="seen";
        arc_rpc_observer_cancel(ARC_RPC_OBS_DISC); /* A supersedes copied B misses synchronously. */
        state.disc.profile=&state.disc_profile;arc_disc_request_swi(&state.disc,&input,state.inserted);
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
    state.disc.profile=&state.disc_profile;arc_disc_request_swi(&state.disc,&input,state.inserted);
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
            .registers=i==ARC_RPC_OBS_LAUNCH?0x70:0,.results=i==ARC_RPC_OBS_ERROR?6:0,.callback=callbacks[i]};
        bool delivery=i==ARC_RPC_OBS_LAUNCH && state.binding.attempt && !state.observation_finished;
        arc_rpc_observer_filter((ArcRpcObserver)i,(delivery || (state.cells&bits[i]))?&filter:NULL);
    }
    ArcRpcHostWorkHandler handler={work_valid,work_resolve,work_mutate,NULL};
    arc_rpc_observer_host_handler(ARC_RPC_OBS_DISC,&handler);
}
void arc_rpc_observers_bind(unsigned catalogue,unsigned cpu)
{
    arc_rpc_observers_retire();state.epoch=arc_rpc_observer_epoch();state.catalogue=catalogue;state.cpu=cpu;
    state.disc_profile=default_disc_profile;
    state.error_profile=default_error_profile;state.error.profile=&state.error_profile;
    state.boot.profile=&boot_profile;state.launch.profile=&launch_profile;
    /* D-181 promotion derives cells only from the exact verified tuple and
     * profile revision; recognition itself is never an enabling rule. */
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
        unsigned revision=cells[i].owner==ARC_RPC_OBS_BOOT || cells[i].owner==ARC_RPC_OBS_LAUNCH?
            ARC_RPC_LAUNCH_PROFILE_REVISION:ARC_RPC_PROFILE_REVISION;
        if(catalogue && q.profile==revision && q.enabled)state.cells|=cells[i].bit;
    }
    state.error.modal_unknown=!(state.cells&ARC_RPC_CELL_ERROR);filters();
}
#if defined(ARCDROID_TEST_FAULTS)
void arc_rpc_observers_test_binding(unsigned *catalogue,unsigned *cpu)
{ *catalogue=state.catalogue;*cpu=state.cpu; }
bool arc_rpc_observers_test_disc_devices(unsigned forms)
{
    if(!state.overridden || (forms&~3u))return false;
    if(forms!=state.disc_profile.device_forms)arc_rpc_observers_disc_clear();
    state.disc_profile.device_forms=forms;return true;
}
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
    bool overridden=state.overridden;unsigned cells=state.cells,devices=state.disc_profile.device_forms;ArcErrorProfile profile=state.error_profile;
#endif
    arc_rpc_observers_bind(catalogue,cpu);
#if defined(ARCDROID_TEST_FAULTS)
    if(overridden){arc_rpc_observers_override(cells,profile.poll_r1,profile.poll_r2,profile.answer_forms);
        arc_rpc_observers_test_buttons(profile.ok_forms,profile.cancel_forms,profile.default_cancel_forms);
        (void)arc_rpc_observers_test_disc_devices(devices);}
#endif
    arc_rpc_observers_disc_configure(enabled,inserted,resolve,observe,opaque);
}
void arc_rpc_observers_frame(uint64_t time)
{
    if(state.epoch!=arc_rpc_observer_epoch())return;
    if(state.binding.attempt){
        if(state.observation_finished)return;
        if(state.prompt_watch && time-state.prompt_started>=2000000){arc_rpc_observer_cancel(ARC_RPC_OBS_BOOT);state.prompt_watch=0;prompt_end();}
        if(state.prompt_watch && arc_rpc_observer_watch_live(ARC_RPC_OBS_BOOT,state.prompt_watch)){
            arc_boot_watch_frame(&state.boot,time);
            if(state.boot.state==ARC_BOOT_WATCH_PROMPT)attempt_event(ARC_RPC_EVENT_WAITING);
        }
        if(state.closed || time-state.launch_started>=120000000){
            if(!arc_riscpc_attempt_established(state.binding.attempt))
                attempt_event(state.binding.attempt->delivery==ARC_RPC_DELIVERY_PREPARED?ARC_RPC_EVENT_UNOBSERVED:ARC_RPC_EVENT_EXPIRE);
            state.prompt_phase=false;state.observation_finished=true;
            /* A helper not yet entered can arrive after observation expiry.
             * Its first query is still synchronously consumed before an
             * effect. A narrow delivery filter needs no continuation/mode. */
            ArcRpcObserveFilter late={.swi=6,.swi_mask=0xfdffff,.modes=(1u<<0)|(1u<<16),
                .kinds=ARC_RPC_OBS_SWI,.registers=0x70,.callback=launch_event};
            arc_rpc_observer_filter(ARC_RPC_OBS_LAUNCH,state.sequence?NULL:&late);
            arc_rpc_observer_filter(ARC_RPC_OBS_BOOT,NULL);state.prompt_watch=0;prompt_end();
        }return;
    }
    arc_outcome_frame(&state.launch,time);arc_boot_watch_frame(&state.boot,time);
}
bool arc_rpc_observers_attempt(const ArcRpcLaunchBinding *binding)
{
    if(!binding || !binding->attempt || !binding->controller || !binding->revalidate ||
        state.binding.attempt || binding->attempt->delivery!=ARC_RPC_DELIVERY_PREPARED ||
        !arc_riscpc_attempt_matches(binding->attempt,&binding->attempt->token))return false;
    state.binding=*binding;state.launch_started=now();state.host_ordinal=0;
    state.sequence=state.event=state.detail=state.task=0;
    state.bound_base=state.registered=state.dispatched=state.start_seen=state.closed=state.prompt_phase=state.observation_finished=false;
    binding->attempt->capabilities=(arc_rpc_observers_capability(ARC_RPC_CELL_LAUNCH)?ARC_RPC_ATTEMPT_OUTCOME_CAP:0)|
        (arc_rpc_observers_capability(ARC_RPC_CELL_BOOT)?ARC_RPC_ATTEMPT_PROMPT_CAP:0);
    arc_outcome_arm_profile(&state.launch,binding->controller->text,now(),&launch_profile);
    filters();return true;
}
void arc_rpc_observers_attempt_input(void)
{
    if(!state.binding.attempt)return;
    if(state.boot.state==ARC_BOOT_WATCH_PROMPT)state.boot.state=ARC_BOOT_WATCH_CANCELLED;
    /* A hint dismissal is not evidence that the pending read returned. */
}
void arc_rpc_observers_attempt_interrupt(ArcRpcAttemptInterrupt reason)
{
    ArcRpcLaunchBinding old=state.binding;
    if(old.attempt)arc_riscpc_attempt_interrupt(old.attempt,reason);
    state.binding=(ArcRpcLaunchBinding){0};state.prompt_phase=false;state.prompt_watch=0;
    arc_rpc_observer_filter(ARC_RPC_OBS_LAUNCH,NULL);arc_rpc_observer_filter(ARC_RPC_OBS_BOOT,NULL);
    if(old.aborted)old.aborted(old.opaque,reason);
}
void arc_rpc_observers_attempt_media(void)
{if(state.binding.media_changed)state.binding.media_changed(state.binding.opaque);}
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
{ if(state.binding.attempt && state.prompt_phase && arc_rpc_observers_capability(ARC_RPC_CELL_BOOT))
    arc_boot_watch_arm_profile(&state.boot,now(),&boot_profile); }
void arc_rpc_observers_boot_cancel(void) { arc_boot_watch_cancel(&state.boot); }
ArcBootWatchState arc_rpc_observers_boot_state(void)
{ return arc_rpc_observers_capability(ARC_RPC_CELL_BOOT)?state.boot.state:ARC_BOOT_WATCH_DISABLED; }
bool arc_rpc_observers_launch_arm(const char *command)
{ return state.binding.attempt && command && !strcmp(command,state.binding.controller->text) &&
    arc_rpc_observers_capability(ARC_RPC_CELL_LAUNCH); }
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
