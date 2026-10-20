/* The production host owns preparation, arbitration and bounded measurement.
 * Ordinary-load reachability is selected only by the frontend release gate. GPL-2.0-only. */
#include "rpc_preparation_host.h"
#include "../engine/rpc/rpc_lockstep.h"
#if defined(ARCDROID_RPC_LOCKSTEP)
#define ARC_RPC_HOST_DIRTY() arc_riscpc_preparation_lockstep_dirty()
#else
#define ARC_RPC_HOST_DIRTY() ((void)0)
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdarg.h>
#define HOST_BUCKETS 161u
#define HOST_PHASES 5u
typedef struct {
    uint64_t work_max,run_max,interval_max,runs[HOST_BUCKETS],intervals[HOST_BUCKETS];
    uint64_t over_2t,gaps,frames;
} HostPhase;
struct ArcRpcFrontendHost {
    ArcRpcPreparation *preparation;
    ArcRpcFrontendOps ops;
#if defined(ARCDROID_TEST_PLANS)
    ArcRpcFrontendExtension extension;
#endif
    char progress[512],advisory[512],terminal[512],logged_progress[512];
    unsigned advisory_frames,terminal_frames,frame,phase;
    bool unloading,unload_cleared,unload_lost,released;
    ArcRpcPreparationStats released_stats;
    ArcRpcPreparationState released_state;
    bool extended,stopping,drain_replaced,hold,queued_reset,pending_summary,first_field;
    bool pending_seen,running_seen,fast_known,fast_forward;
    bool ordinary,manual_noticed;
    uint64_t load_ns,frame_ns,previous_ns,hold_start,guest_us,frames,frontend_av,guest_av;
    uint64_t first_pending_ns,running_ns,first_field_ns,reset_exceptions,reset_max_ns;
    uint64_t session_id,checkpoint_exceptions;
    int16_t *silence;size_t silence_capacity;bool silence_refused;
    double rate;
    HostPhase phases[HOST_PHASES]; /* Fixed bounded histogram storage. */
};
static void summary(ArcRpcFrontendHost *,const char *);
static uint64_t now(ArcRpcFrontendHost *h)
{return h->ops.module.clock?h->ops.module.clock(h->ops.module.opaque):arc_riscpc_preparation_clock(NULL);}
static void log_line(ArcRpcFrontendHost *h,enum retro_log_level level,const char *id,const char *text)
{
    if(h->ops.log){char line[4096];snprintf(line,sizeof(line),"%s: %s",id,text);h->ops.log(h->ops.module.opaque,level,line);}
}
static void display(ArcRpcFrontendHost *h,const char *s,ArcRpcNoticeSeverity severity)
{
    if(!h->ops.environment)return;
    if(!*s)severity=ARC_RPC_NOTICE_CLEAR;
    unsigned duration=severity==ARC_RPC_NOTICE_TERMINAL?UINT_MAX:severity==ARC_RPC_NOTICE_ADVISORY?5000u:*s?2000u:1u;
    struct retro_message_ext message={s,duration,severity==ARC_RPC_NOTICE_CLEAR?3u:severity==ARC_RPC_NOTICE_TERMINAL?3u:2u,
        severity==ARC_RPC_NOTICE_TERMINAL?RETRO_LOG_ERROR:RETRO_LOG_INFO,
        RETRO_MESSAGE_TARGET_OSD,RETRO_MESSAGE_TYPE_STATUS,-1};
    h->extended=h->ops.environment(RETRO_ENVIRONMENT_SET_MESSAGE_EXT,&message);
    if(!h->extended){
        struct retro_message fallback={s,severity==ARC_RPC_NOTICE_ADVISORY?300u:*s?120u:1u};
        (void)h->ops.environment(RETRO_ENVIRONMENT_SET_MESSAGE,&fallback);
    }
}
static void progress(ArcRpcFrontendHost *h,const char *text)
{
    snprintf(h->progress,sizeof(h->progress),"%s",text);
    if(strcmp(h->logged_progress,text)){
        log_line(h,RETRO_LOG_INFO,"N-7",text);snprintf(h->logged_progress,sizeof(h->logged_progress),"%s",text);
    }
    if(!*h->terminal && !h->advisory_frames)display(h,text,ARC_RPC_NOTICE_PROGRESS);
}
static void hold_begin(ArcRpcFrontendHost *h)
{
    if(*h->terminal)return;
    h->hold=true;h->hold_start=h->guest_us;progress(h,"Starting RiscPC");
}
static void module_notice(void *opaque,const char *text,ArcRpcNotice event)
{
    ARC_RPC_HOST_DIRTY();
    ArcRpcFrontendHost *h=opaque;
    bool lost=!strcmp(event.id,"N-18");
    if(lost && h->unloading){
        event.severity=ARC_RPC_NOTICE_ADVISORY;
        if(!h->unload_cleared){snprintf(h->advisory,sizeof(h->advisory),"%s",text);h->unload_lost=true;return;}
    }
    if(event.severity==ARC_RPC_NOTICE_PROGRESS){progress(h,text);return;}
    if(event.severity==ARC_RPC_NOTICE_CLEAR){
        if(h->preparation && arc_riscpc_preparation_state(h->preparation)==ARC_RPC_PREP_RUNNING)hold_begin(h);
        return;
    }
    if(event.severity==ARC_RPC_NOTICE_ADVISORY){
        log_line(h,RETRO_LOG_WARN,event.id,text);
        if(*h->terminal)return;
        snprintf(h->advisory,sizeof(h->advisory),"%s",text);h->advisory_frames=300;
        display(h,text,event.severity);
        if(h->extended)h->advisory_frames=(unsigned)(5*h->rate+0.999999);
        return;
    }
    if(strcmp(h->terminal,text))log_line(h,RETRO_LOG_ERROR,event.id,text);
    bool replace=lost || !*h->terminal || (event.stopping && !h->stopping) || (event.drain_outcome && !h->drain_replaced);
    if(!replace)return;
    if(event.drain_outcome)h->drain_replaced=true;
    h->stopping=event.stopping;h->hold=false;
    snprintf(h->terminal,sizeof(h->terminal),"%s",text);h->terminal_frames=60;
    display(h,text,ARC_RPC_NOTICE_TERMINAL);
}
static void module_log(void *opaque,const char *text)
{
    ARC_RPC_HOST_DIRTY();ArcRpcFrontendHost *h=opaque;if(h->ops.log)h->ops.log(h->ops.module.opaque,RETRO_LOG_INFO,text);}
static uint64_t module_clock(void *opaque){return now(opaque);}
static bool module_attach(void *opaque,char *error,size_t n)
{
    ARC_RPC_HOST_DIRTY();
    ArcRpcFrontendHost *h=opaque;
    bool ok=!h->ops.module.attach || h->ops.module.attach(h->ops.module.opaque,error,n);
#if defined(ARCDROID_TEST_PLANS)
    if(ok && h->extension.attach_after)ok=h->extension.attach_after(h->extension.opaque,error,n);
#endif
    return ok;
}
static void module_started(void *opaque,bool factory)
{
    ARC_RPC_HOST_DIRTY();
    ArcRpcFrontendHost *h=opaque;(void)h;(void)factory;
#if defined(ARCDROID_TEST_PLANS)
    if(h->extension.started_after)h->extension.started_after(h->extension.opaque,factory);
#endif
}
static void module_diagnostics(void *opaque)
{
    ARC_RPC_HOST_DIRTY();
    ArcRpcFrontendHost *h=opaque;
    if(h->ops.module.diagnostics)h->ops.module.diagnostics(h->ops.module.opaque);
#if defined(ARCDROID_TEST_PLANS)
    if(h->extension.diagnostics_after)h->extension.diagnostics_after(h->extension.opaque);
#endif
}
static void module_attached(void *opaque)
{
    ARC_RPC_HOST_DIRTY();
    ArcRpcFrontendHost *h=opaque;
    if(h->ops.module.attached)h->ops.module.attached(h->ops.module.opaque);
#if defined(ARCDROID_TEST_PLANS)
    if(h->extension.attached_after)h->extension.attached_after(h->extension.opaque);
#endif
}
#define HOST_BOOL_CALLBACK(name) \
static bool module_##name(void *opaque,char *error,size_t n) \
{ArcRpcFrontendHost *h=opaque;return !h->ops.module.name || h->ops.module.name(h->ops.module.opaque,error,n);}
HOST_BOOL_CALLBACK(restage_drive0)
HOST_BOOL_CALLBACK(restage_stopped)
#undef HOST_BOOL_CALLBACK
static bool module_reset_rebind(void *opaque,char *error,size_t n)
{
    ARC_RPC_HOST_DIRTY();
    ArcRpcFrontendHost *h=opaque;
    bool ok=!h->ops.module.reset_rebind || h->ops.module.reset_rebind(h->ops.module.opaque,error,n);
#if defined(ARCDROID_TEST_PLANS)
    if(ok && h->extension.attached_after)h->extension.attached_after(h->extension.opaque);
#endif
    return ok;
}

static bool module_identity(void *opaque,uint8_t bytes[32])
{ArcRpcFrontendHost *h=opaque;return h->ops.module.media_identity && h->ops.module.media_identity(h->ops.module.opaque,bytes);}
static ArcRpcFrontendHost *allocate(const ArcRpcFrontendOps *ops)
{
    if(!ops)return NULL;
    ArcRpcFrontendHost *h=calloc(1,sizeof(*h));if(!h)return NULL;
    h->ops=*ops;h->rate=60;h->load_ns=ops->load_entry_ns?ops->load_entry_ns:now(h);return h;
}
static ArcRpcPreparationHost callbacks(ArcRpcFrontendHost *h)
{
    return (ArcRpcPreparationHost){.clock=module_clock,.notice=module_notice,.log=module_log,
        .attach=module_attach,.started=module_started,.diagnostics=module_diagnostics,.attached=module_attached,
        .restage_drive0=module_restage_drive0,.reset_rebind=module_reset_rebind,.restage_stopped=module_restage_stopped,
        .media_identity=module_identity,.lock_backend=h->ops.module.lock_backend,.lock_opaque=h->ops.module.lock_opaque,.opaque=h};
}
void arc_riscpc_host_load_failure(const ArcRpcFrontendOps *ops,const char *id,const char *redacted)
{
    ARC_RPC_HOST_DIRTY();
    if(!ops)return;
    /* Frontend lifecycle calls are serialized. Keep the bounded histogram
     * scratch off the already-large load stack, and retain no borrowed ops. */
    static ArcRpcFrontendHost h;
    memset(&h,0,sizeof(h));h.ops=*ops;h.rate=60;h.load_ns=ops->load_entry_ns?ops->load_entry_ns:now(&h);
    module_notice(&h,redacted,(ArcRpcNotice){ARC_RPC_NOTICE_TERMINAL,id,true,false});
    summary(&h,"pending-failed");summary(&h,"live-release");
    memset(&h,0,sizeof(h));
}
void arc_riscpc_host_diagnostics(ArcRpcFrontendHost *h){
    ARC_RPC_HOST_DIRTY();if(h)module_diagnostics(h);}
ArcRpcFrontendHost *arc_riscpc_host_begin(const ArcRpcPreparationRequest *request,const ArcRpcFrontendOps *ops,char *error,size_t n)
{
    ArcRpcFrontendHost *h=allocate(ops);if(!h)return NULL;
    h->session_id=request?request->session_id:0;h->ordinary=request && request->ordinary;
    ArcRpcPreparationHost base=callbacks(h);h->preparation=arc_riscpc_preparation_begin(request,&base,error,n);
    if(!h->preparation){module_notice(h,error&&*error?error:"RiscPC preparation could not begin",(ArcRpcNotice){ARC_RPC_NOTICE_TERMINAL,"N-B begin",true,false});summary(h,"pending-failed");summary(h,"live-release");free(h);return NULL;}
    return h;
}
#if defined(ARCDROID_TEST_PLANS)
ArcRpcFrontendHost *arc_riscpc_host_low_level(const ArcRpcPreparationRequest *request,const ArcRpcFrontendOps *ops,
    const ArcRpcFrontendExtension *extension,char *error,size_t n)
{
    ArcRpcFrontendHost *h=allocate(ops);if(!h)return NULL;
    if(extension)h->extension=*extension;
    h->session_id=request?request->session_id:0;h->ordinary=request && request->ordinary;
    ArcRpcPreparationHost base=callbacks(h);h->preparation=arc_riscpc_preparation_low_level(request,&base,error,n);
    /* The low-level parser caller reports load failure once through the
     * catalogue host path, including failures before this entry. */
    if(!h->preparation){free(h);return NULL;}
    return h;
}
#endif
ArcRpcPreparation *arc_riscpc_host_preparation(const ArcRpcFrontendHost *h){return h?h->preparation:NULL;}
bool arc_riscpc_host_pending(const ArcRpcFrontendHost *h)
{return h && (!h->preparation || arc_riscpc_preparation_state(h->preparation)!=ARC_RPC_PREP_RUNNING || !arc_riscpc_preparation_execution_allowed(h->preparation));}
static unsigned bucket(uint64_t ns,double rate)
{double units=(double)ns*rate/1000000000.0*20.0;return units>=160?160u:(unsigned)units;}
/* Nearest-rank p99 in units of 0.05 T. 161 denotes the >8 T overflow
 * bucket; duration maxima retain the exact observed nanoseconds. */
static unsigned percentile(const uint64_t histogram[HOST_BUCKETS])
{
    uint64_t count=0,at=0;for(unsigned i=0;i<HOST_BUCKETS;++i)count+=histogram[i];
    uint64_t rank=(count*99+99)/100;if(!rank)return 0;
    for(unsigned i=0;i<HOST_BUCKETS;++i){at+=histogram[i];if(at>=rank)return i+1;}
    return HOST_BUCKETS;
}
static void append(char *line,size_t size,size_t *at,const char *format,...)
{
    if(*at>=size)return;
    va_list args;va_start(args,format);int n=vsnprintf(line+*at,size-*at,format,args);va_end(args);
    if(n>0)*at+=(size_t)n<size-*at?(size_t)n:size-*at;
}
static long long endpoint(bool present,uint64_t value,uint64_t load)
{return present && value>=load?(long long)((value-load)/1000000):-1;}
static void summary(ArcRpcFrontendHost *h,const char *phase)
{
    char text[3800];size_t at=0;uint64_t current=now(h),elapsed=current>=h->load_ns?current-h->load_ns:0;
    ArcRpcPreparationStats stats={0};if(h->released)stats=h->released_stats;else arc_riscpc_preparation_stats(h->preparation,&stats);
    append(text,sizeof(text),&at,"phase=%s session=%llu kind=%u frames=%llu elapsed_ms=%llu state=%u frontend_av=%llu guest_av=%llu fast_forward=%s first_pending_ms=%lld running_ms=%lld first_field_ms=%lld desktop=unavailable D169=%llu D162=%llu reset_exceptions=%llu reset_max_ns=%llu rollback_exceptions=%llu rollback_max_ns=%llu restart_record_exceptions=%llu restart_record_max_ns=%llu restart_defaults_exceptions=%llu restart_defaults_max_ns=%llu lock_attempts=%llu lock_wait_frames=%llu identity_checks=%llu identity_max_ns=%llu lock_max_ns=%llu",
        phase,(unsigned long long)h->session_id,h->ops.kind,(unsigned long long)h->frames,(unsigned long long)(elapsed/1000000),h->released?h->released_state:arc_riscpc_preparation_state(h->preparation),
        (unsigned long long)h->frontend_av,(unsigned long long)h->guest_av,h->fast_known?(h->fast_forward?"yes":"no"):"unknown",
        endpoint(h->pending_seen,h->first_pending_ns,h->load_ns),endpoint(h->running_seen,h->running_ns,h->load_ns),endpoint(h->first_field,h->first_field_ns,h->load_ns),
        (unsigned long long)stats.namespace_exceptions,(unsigned long long)h->checkpoint_exceptions,
        (unsigned long long)h->reset_exceptions,(unsigned long long)h->reset_max_ns,
        (unsigned long long)stats.rollback_exceptions,(unsigned long long)stats.rollback_max_ns,
        (unsigned long long)stats.restart_record_exceptions,(unsigned long long)stats.restart_record_max_ns,
        (unsigned long long)stats.restart_defaults_exceptions,(unsigned long long)stats.restart_defaults_max_ns,
        (unsigned long long)stats.lock_attempts,(unsigned long long)stats.lock_wait_frames,
        (unsigned long long)stats.identity_checks,(unsigned long long)stats.identity_max_ns,(unsigned long long)stats.lock_max_ns);
    static const char *names[]={"offline","generation","attach","live","drain"};
    for(unsigned i=0;i<HOST_PHASES;++i){HostPhase *p=&h->phases[i];
        append(text,sizeof(text),&at," %s={frames:%llu,work_max_ns:%llu,run_max_ns:%llu,run_p99_005T:%u,interval_max_ns:%llu,interval_p99_005T:%u,over_2T:%llu,frontend_gaps:%llu}",
            names[i],(unsigned long long)p->frames,(unsigned long long)p->work_max,(unsigned long long)p->run_max,percentile(p->runs),
            (unsigned long long)p->interval_max,percentile(p->intervals),(unsigned long long)p->over_2t,(unsigned long long)p->gaps);
    }
    log_line(h,RETRO_LOG_INFO,"summary",text);
}
void arc_riscpc_host_frame_begin(ArcRpcFrontendHost *h)
{
    ARC_RPC_HOST_DIRTY();
    if(!h)return;
    ArcRpcPreparationState state=arc_riscpc_preparation_state(h->preparation);
    h->phase=state==ARC_RPC_PREP_GENERATE?1u:state==ARC_RPC_PREP_START?2u:state==ARC_RPC_PREP_RUNNING?3u:arc_riscpc_preparation_draining(h->preparation)?4u:0u;
    uint64_t current=now(h);h->frame_ns=current;++h->frames;++h->frame;
    HostPhase *p=&h->phases[h->phase];++p->frames;
    bool fast=false;
    if(h->ops.environment && h->ops.environment(RETRO_ENVIRONMENT_GET_FASTFORWARDING,&fast)){h->fast_known=true;h->fast_forward|=fast;}
    if(h->previous_ns && current>=h->previous_ns){uint64_t interval=current-h->previous_ns;
        if(interval>500000000)++p->gaps;
        else{++p->intervals[bucket(interval,h->rate)];if(interval>p->interval_max)p->interval_max=interval;
            if((double)interval>2000000000.0/h->rate)++p->over_2t;}}
    h->previous_ns=current;
    if(h->advisory_frames && !--h->advisory_frames && !*h->terminal)display(h,h->progress,ARC_RPC_NOTICE_PROGRESS);
    if(*h->terminal && !h->extended && h->terminal_frames && !--h->terminal_frames){display(h,h->terminal,ARC_RPC_NOTICE_TERMINAL);h->terminal_frames=60;}
    if(h->hold && h->frame%60==0)progress(h,"Starting RiscPC");
}
void arc_riscpc_host_frame_end(ArcRpcFrontendHost *h)
{
    ARC_RPC_HOST_DIRTY();
    if(!h)return;
    uint64_t current=now(h),duration=current>=h->frame_ns?current-h->frame_ns:0;
    HostPhase *p=&h->phases[h->phase];++p->runs[bucket(duration,h->rate)];if(duration>p->run_max)p->run_max=duration;
    ArcRpcPreparationState state=arc_riscpc_preparation_state(h->preparation);
    if(!h->pending_seen){h->pending_seen=true;h->first_pending_ns=current;}
    if(!h->running_seen && state==ARC_RPC_PREP_RUNNING){h->running_seen=true;h->running_ns=current;}
    if(!h->pending_summary && state>=ARC_RPC_PREP_RUNNING){summary(h,"pending");h->pending_summary=true;}
}
void arc_riscpc_host_pending_frame(ArcRpcFrontendHost *h)
{
    ARC_RPC_HOST_DIRTY();
    if(!h)return;uint64_t start=now(h);
    if(arc_riscpc_preparation_draining(h->preparation))arc_riscpc_preparation_failed_frame(h->preparation);
    else (void)arc_riscpc_preparation_frame(h->preparation);
    uint64_t current=now(h),cost=current>=start?current-start:0;
    if(cost>h->phases[h->phase].work_max)h->phases[h->phase].work_max=cost;
    if(!arc_riscpc_preparation_draining(h->preparation) && h->queued_reset){h->queued_reset=false;(void)arc_riscpc_host_reset(h);}
}
void arc_riscpc_host_maintenance(ArcRpcFrontendHost *h)
{
    ARC_RPC_HOST_DIRTY();
    if(!h || arc_riscpc_host_pending(h))return;
    ArcRpcHdStats hd={0};ArcRpcHdStore *disc=arc_riscpc_preparation_disc(h->preparation);if(disc)arc_riscpc_hd_stats(disc,&hd);
    uint64_t start=now(h);arc_riscpc_preparation_maintenance(h->preparation);uint64_t end=now(h),cost=end>=start?end-start:0;
    if(cost>h->phases[3].work_max)h->phases[3].work_max=cost;
    if(hd.state>=ARC_RPC_HD_ROTATE && hd.state<=ARC_RPC_HD_GC && cost>ARC_RPC_PREP_OFFLINE_BUDGET_NS)++h->checkpoint_exceptions;
}
const int16_t *arc_riscpc_host_silence(ArcRpcFrontendHost *h,size_t frames)
{
    if(!h || frames>SIZE_MAX/(2*sizeof(int16_t)))return NULL;
    if(frames>h->silence_capacity){
        int16_t *samples=calloc(frames,2*sizeof(int16_t));
        if(!samples){
            if(!h->silence_refused)log_line(h,RETRO_LOG_ERROR,"N-B engine","RiscPC audio output could not be prepared.");
            h->silence_refused=true;return NULL;
        }
        free(h->silence);h->silence=samples;h->silence_capacity=frames;
    }
    return h->silence;
}
void arc_riscpc_host_clear(ArcRpcFrontendHost *h)
{
    ARC_RPC_HOST_DIRTY();
    if(!h)return;h->hold=h->stopping=h->drain_replaced=false;h->advisory_frames=h->terminal_frames=0;
    *h->progress=*h->terminal=*h->advisory=*h->logged_progress=0;display(h,"",ARC_RPC_NOTICE_CLEAR);
}
bool arc_riscpc_host_reset(ArcRpcFrontendHost *h)
{
    ARC_RPC_HOST_DIRTY();
    if(!h)return false;
    if(arc_riscpc_preparation_draining(h->preparation)){h->queued_reset=true;return true;}
    arc_riscpc_host_clear(h);char error[256]={0};
    uint64_t started=now(h);bool running=!arc_riscpc_host_pending(h);
    (void)arc_riscpc_preparation_complete_publication(h->preparation,error,sizeof(error));
    bool ok=arc_riscpc_preparation_reset(h->preparation);
    /* The frontend re-announces its 60 Hz baseline on a stopped Restart.
     * Pending histograms and advisory lifetimes use that announced rate. */
    if(!running)h->rate=60;
    uint64_t ended=now(h);
    if(running && !ok){++h->reset_exceptions;uint64_t cost=ended>=started?ended-started:0;if(cost>h->reset_max_ns)h->reset_max_ns=cost;}
    if(ok && !arc_riscpc_host_pending(h)){h->guest_us=0;hold_begin(h);}
    if(!arc_riscpc_preparation_take_hd_close(h->preparation))arc_riscpc_host_advisory(h,"N-17 reset","Recent RiscPC hard disc changes could not be saved.");
    return ok;
}
static uint64_t hold_bound(ArcRpcFrontendHost *h)
{
    /* Rows: 3.50, 3.60, 3.70, 3.71; columns: ARM610, ARM710, SA-110, ARM7500. */
    static const unsigned seconds[4][4]={{25,0,0,0},{0,25,0,25},{0,0,25,0},{0,25,25,25}};
    unsigned model=0,layout=0;arc_riscpc_preparation_pair(h->preparation,&model,&layout);
    unsigned bound=layout>=1 && layout<=4 && model<4?seconds[layout-1][model]:0;
    return UINT64_C(1000000)*(bound?bound:25u); /* Explicit low-level unqualified pairs. */
}
void arc_riscpc_host_guest_time(ArcRpcFrontendHost *h,uint64_t us)
{
    ARC_RPC_HOST_DIRTY();
    if(!h)return;h->guest_us=us;
    if(h->hold && us>=h->hold_start && us-h->hold_start>=hold_bound(h)){
        h->hold=false;*h->progress=0;
        if(!h->ordinary && !*h->terminal && !h->advisory_frames)display(h,"",ARC_RPC_NOTICE_CLEAR);
    }
    /* Yield to an advisory's minimum lifetime; never infer desktop readiness. */
    if(h->ordinary && !h->hold && !h->manual_noticed && !h->stopping && !*h->terminal && !h->advisory_frames &&
       arc_riscpc_preparation_state(h->preparation)==ARC_RPC_PREP_RUNNING){
        h->manual_noticed=true;arc_riscpc_host_advisory(h,"N-12","Automatic launch is not available yet. Use the RiscPC desktop to start this title.");
    }
}
void arc_riscpc_host_first_field(ArcRpcFrontendHost *h,unsigned width,unsigned height,double rate,bool issued)
{
    ARC_RPC_HOST_DIRTY();
    if(!h)return;h->rate=rate;
    if(h->first_field)return;h->first_field=true;h->first_field_ns=now(h);
    char text[192];snprintf(text,sizeof(text),"width=%u height=%u rate=%.6f av_issued=%u frame=%u",width,height,rate,issued,h->frame);log_line(h,RETRO_LOG_INFO,"status first-field",text);
}
void arc_riscpc_host_av_change(ArcRpcFrontendHost *h,bool guest){
    ARC_RPC_HOST_DIRTY();if(h){if(guest)++h->guest_av;else ++h->frontend_av;}}
void arc_riscpc_host_advisory(ArcRpcFrontendHost *h,const char *id,const char *text)
{
    ARC_RPC_HOST_DIRTY();if(h)module_notice(h,text,(ArcRpcNotice){ARC_RPC_NOTICE_ADVISORY,id,false,false});}
bool arc_riscpc_host_free(ArcRpcFrontendHost *h,char *error,size_t n)
{
    ARC_RPC_HOST_DIRTY();
    if(!h)return true;
    /* A completed drain may replace its fault terminal. Finish that outcome
     * before the unload clear; only unload failure advisories follow it. */
    h->unloading=true;
    while(arc_riscpc_preparation_draining(h->preparation))arc_riscpc_preparation_failed_frame(h->preparation);
    char lost[512];snprintf(lost,sizeof(lost),"%s",h->unload_lost?h->advisory:"");
    arc_riscpc_host_clear(h);h->unload_cleared=true;
    if(*lost)module_notice(h,lost,(ArcRpcNotice){ARC_RPC_NOTICE_ADVISORY,"N-18",true,false});
    if(!h->pending_summary){summary(h,"pending-release");h->pending_summary=true;}
    h->released_state=arc_riscpc_preparation_state(h->preparation);
    bool ok=arc_riscpc_preparation_free_stats(h->preparation,error,n,&h->released_stats);
    h->preparation=NULL;h->released=true;h->released_state=h->released_stats.state;summary(h,"live-release");
    free(h->silence);free(h);return ok;
}

#if defined(ARCDROID_TEST_FAULTS)
bool arc_riscpc_host_terminal(const ArcRpcFrontendHost *h){return h && *h->terminal;}
#endif

#if defined(ARCDROID_RPC_LOCKSTEP)
/* Recognize only this host's callback context; no foreign opaque is dereferenced. */
uint64_t arc_riscpc_host_lockstep_value(const void *opaque,uint64_t (*clock)(void *),unsigned field)
{
    if(!opaque || clock!=module_clock)return 0;
    const ArcRpcFrontendHost *h=opaque;
    switch(field){
    case 0:return h->queued_reset;
    case 1:return h->released;
    case 2:return h->unloading;
    default:return 0;
    }
}
#endif
