/* Session preparation owns offline resources; host time never enters a guest
 * deadline, input ordinal, storage decision or live-session schedule. GPL-2.0-only. */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
#include "rpc_preparation.h"
#include "../engine/rpc/rpc_lockstep.h"
#if defined(ARCDROID_RPC_LOCKSTEP)
uint64_t arc_rpc_lockstep_preparation_generation=0;
void arc_riscpc_preparation_lockstep_dirty(void){++arc_rpc_lockstep_preparation_generation;}
#define ARC_RPC_PREPARATION_DIRTY() (++arc_rpc_lockstep_preparation_generation)
#else
#define ARC_RPC_PREPARATION_DIRTY() ((void)0)
#endif
#include "install_disc.h"
#include "install_store.h"
#include "rpc_launch_helper.h"
#include "rpc_launch_record_store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <time.h>
#endif
uint64_t arc_riscpc_preparation_clock(void *opaque)
{
    ARC_RPC_PREPARATION_DIRTY();
    (void)opaque;
#if defined(_WIN32)
    LARGE_INTEGER v,f;
    if (!QueryPerformanceCounter(&v) || !QueryPerformanceFrequency(&f) || !f.QuadPart) return 0;
    uint64_t n=(uint64_t)v.QuadPart,d=(uint64_t)f.QuadPart;
    return n/d*UINT64_C(1000000000)+n%d*UINT64_C(1000000000)/d;
#else
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC,&t)) return 0;
    return (uint64_t)t.tv_sec*UINT64_C(1000000000)+(uint64_t)t.tv_nsec;
#endif
}
#if defined(ARCDROID_TEST_FAULTS)
/* AC-4's construction-failure seam. The frozen reset body stays identical;
 * shipping resolves every helper call directly to the production builder. */
static bool arc_riscpc_helper_failure;
RETRO_API void arc_riscpc_test_fail_helper(void){arc_riscpc_helper_failure=true;}
static bool arc_riscpc_test_helper_build(void *bytes,size_t n)
{
    if(arc_riscpc_helper_failure){arc_riscpc_helper_failure=false;return false;}
    return arc_riscpc_helper_build(bytes,n);
}
#define arc_riscpc_helper_build arc_riscpc_test_helper_build
#endif
struct ArcRpcPreparation {
    ArcRpcPreparationHost host;
    ArcLock content_owner,defaults_owner;
    uint64_t content_wait,defaults_wait,unit_now,identity_started;
    bool content_waiting,defaults_waiting,observations_begun;
    bool hd_close_ok,stopped_window,marker_checked,released,ownership_lost;
    const char *notice_id;
    bool draining,drain_started,drain_ok;
    unsigned drain_frames,live_frames;
    uint8_t drain_payload[240];
    ArcRpcPreparationRequest request;
    ArcRpcFirmwareFingerprint fingerprint;
    unsigned verified_catalogue;
    char sha1[41],save[1024],system[1024],source[1024],media_path[1024],helper_path[1024],seed_path[1024],error[512];
    uint8_t *generated_helper;
    ArcRpcLaunchPlan launch;
    ArcRpcLaunchAttempt attempt;
    ArcRpcLaunchController controller;
    ArcRpcBootKey launch_key;
    uint8_t launch_media_identity[32];
    bool launch_captured,launch_prepared,retirement_pending;
    bool launch_suppressed,execution_barrier,launch_unconfigured;
    ArcRpcLaunchRecordStore observations;
    bool record_attempted;
    uint8_t firmware_digest[32];
    struct {uint8_t *bytes;size_t used;unsigned count;uint32_t offset,size;
        bool missing,verified;ArcRpcHdIdentity identity;
        struct {uint32_t offset,size,at;} ranges[4096];} product;
    uint8_t *firmware,seed[256],captured[256],block[131072];
    ArcEngineMedia media,helper;
    ArcRpcCmosStore record,defaults;
    ArcRpcCmosEvidence evidence;
    ArcRpcHdStore *disc;
    ArcRpcBoot *boot;
    ArcIoFile *import;
    ArcSha1 hash1; ArcSha256 hash256;
    uint32_t crc,lba;
    size_t verify_at;
    unsigned substep,pacing,notice_frames;
    bool needs_population,receipted,io_pending,have_seed,refusal_reported;
    ArcRpcPreparationState state;
    const char *status;
    ArcRpcPreparationStats stats;
    uint64_t costs[ARC_RPC_PREP_COST_KINDS][2][ARC_RPC_COST_SAMPLES];
    unsigned cost_at[ARC_RPC_PREP_COST_KINDS][2];
};
static const char *status(const ArcRpcPreparation *p)
{
    ARC_RPC_PREPARATION_DIRTY();
    ArcRpcPreparationState state=p->state;
    if(state>=ARC_RPC_PREP_RECORD && p->receipted && !(state>=ARC_RPC_PREP_DEFAULTS && state<=ARC_RPC_PREP_SAVE_RECORD)) return "Starting RiscPC";
    if (state <= ARC_RPC_PREP_RECOVER) return "Checking the RiscPC hard disc";
    if (state == ARC_RPC_PREP_VALIDATE) return "Checking RiscPC boot files";
    if (state <= ARC_RPC_PREP_SAVE_RECORD) return "Preparing RiscPC settings for first use";
    if (state <= ARC_RPC_PREP_START) return "Preparing the RiscPC hard disc";
    return "";
}
static void notice_event(ArcRpcPreparation *p,const char *s,ArcRpcNoticeSeverity severity,const char *id,bool outcome)
{
    ARC_RPC_PREPARATION_DIRTY();
    if(p->host.notice)p->host.notice(p->host.opaque,s,(ArcRpcNotice){severity,id,p->state==ARC_RPC_PREP_FAILED,outcome});
}
static void notice(ArcRpcPreparation *p,const char *s,bool terminal)
{ARC_RPC_PREPARATION_DIRTY();notice_event(p,s,terminal?ARC_RPC_NOTICE_TERMINAL:*s?ARC_RPC_NOTICE_PROGRESS:ARC_RPC_NOTICE_CLEAR,
    terminal?(p->notice_id?p->notice_id:"N-B begin"):"N-7",false);}
static void refresh(ArcRpcPreparation *p,bool tick)
{
    ARC_RPC_PREPARATION_DIRTY();
    const char *s=status(p);
    if (p->state >= ARC_RPC_PREP_RUNNING || p->refusal_reported) return;
    if (p->status != s || (tick && !p->notice_frames)) {
        notice(p,s,false);p->status=s;p->notice_frames=60;
    }
    if(tick && p->notice_frames)--p->notice_frames;
}
static void snapshot_cmos(uint8_t payload[240])
{
    ARC_RPC_PREPARATION_DIRTY();
    uint8_t b[256],m[32];uint64_t w,l,now;unsigned s;
    arc_engine_rpc_evidence(b,m,&w,&l,&now,&s);memcpy(payload,b+16,240);
}
static void identity_observer(void *opaque,bool begin)
{
    ARC_RPC_PREPARATION_DIRTY();
    ArcRpcPreparation *p=opaque;uint64_t stamp=p->host.clock(p->host.opaque);
    if(begin){p->identity_started=stamp;++p->stats.identity_checks;return;}
    uint64_t cost=stamp>=p->identity_started?stamp-p->identity_started:0;
    if(cost>p->stats.identity_max_ns)p->stats.identity_max_ns=cost;
    if(cost>p->stats.lock_max_ns)p->stats.lock_max_ns=cost;
}
static void measure_owner(ArcRpcPreparation *p,ArcLock *owner)
{
    ARC_RPC_PREPARATION_DIRTY();owner->identity_observer=identity_observer;owner->identity_opaque=p;}
static void release(ArcRpcPreparation *p)
{
    ARC_RPC_PREPARATION_DIRTY();
    if(p->released)return;
    p->released=true;
    if(p->attempt.live)arc_riscpc_attempt_interrupt(&p->attempt,ARC_RPC_INTERRUPT_UNLOAD);
    p->retirement_pending=false;
    arc_engine_stop(); /* Detach IDE and both borrowed media before any free. */
    arc_riscpc_launch_record_close(&p->observations);
    free(p->generated_helper);p->generated_helper=NULL;
    free(p->product.bytes);memset(&p->product,0,sizeof(p->product));
    if (p->boot) { arc_riscpc_boot_free(p->boot); p->boot=NULL; }
    if (p->import) { (void)arc_io_close_file(p->import);p->import=NULL; }
    if (p->disc) { p->hd_close_ok=arc_riscpc_hd_close(p->disc,p->ownership_lost || !arc_lock_held(&p->content_owner))&&p->hd_close_ok;p->disc=NULL; }
    arc_lock_release(&p->defaults_owner);p->defaults_waiting=false;p->observations_begun=false;
    arc_riscpc_cmos_store_close(&p->record);arc_riscpc_cmos_store_close(&p->defaults);
}
static bool owner_lost(ArcLock *owner)
{return !arc_lock_held(owner) && arc_lock_verify(owner)==ARC_LOCK_IDENTITY_LOST;}
static void failed(ArcRpcPreparation *p,const char *s)
{
    ARC_RPC_PREPARATION_DIRTY();
    if(owner_lost(&p->content_owner) || owner_lost(&p->defaults_owner)){
        bool first_loss=!p->ownership_lost;
        p->notice_id="N-18";p->draining=false;p->drain_ok=false;p->ownership_lost=true;
        s="Another app changed ArcDroid's RiscPC save files; this session stopped without saving. Close and reload this content.";
        if(first_loss && p->host.log)p->host.log(p->host.opaque,"N-18 unsaved: latest CMOS snapshot could not be saved");
    }
    p->retirement_pending=false;
    if(p->attempt.live)arc_riscpc_attempt_interrupt(&p->attempt,ARC_RPC_INTERRUPT_FAULT);
    if(p->host.diagnostics)p->host.diagnostics(p->host.opaque);else arc_engine_rpc_diagnostics();
    if (s && s != p->error) snprintf(p->error,sizeof(p->error),"%s",s);
    if (!p->error[0]) strcpy(p->error,"RiscPC session preparation failed");
    size_t cause_length=strlen(p->error);
    while(cause_length && (p->error[cause_length-1]=='\n' || p->error[cause_length-1]=='\r'))p->error[--cause_length]=0;
    arc_engine_stop();p->state=ARC_RPC_PREP_FAILED;
    arc_lock_release(&p->defaults_owner);p->defaults_waiting=false;
    notice(p,p->error,true);p->status=NULL;
    /* Keep detached resources until reset/unload: cleanup never combines a
     * second namespace operation with the failing frame's attempt. */
}
static bool copied(char out[1024],const char *in)
{
    ARC_RPC_PREPARATION_DIRTY();
    int n=snprintf(out,1024,"%s",in?in:"");return n>=0 && n<1024;
}
static void restart(ArcRpcPreparation *p)
{
    ARC_RPC_PREPARATION_DIRTY();
    p->released=false;p->drain_ok=true;p->draining=false;p->drain_started=false;p->drain_frames=0;
    p->launch_prepared=p->launch_captured=p->record_attempted=p->launch_unconfigured=false;
    memset(&p->record,0,sizeof(p->record));memset(&p->defaults,0,sizeof(p->defaults));
    p->state=ARC_RPC_PREP_VERIFY;p->substep=0;p->verify_at=0;p->crc=0;p->verified_catalogue=0;
    p->lba=0;p->io_pending=false;p->needs_population=p->receipted=false;p->refusal_reported=false;
    p->status=NULL;p->notice_frames=0;p->error[0]=0;p->marker_checked=false;
    arc_sha1_init(&p->hash1);arc_sha256_init(&p->hash256);
}
static ArcRpcPreparation *begin(const ArcRpcPreparationRequest *r,
    const ArcRpcPreparationHost *h,bool low,char *e,size_t n)
{
    ARC_RPC_PREPARATION_DIRTY();
    if(r && !r->firmware){
        if(e&&n)snprintf(e,n,"RISC OS %s firmware is missing; check <system>/arcdroid/firmware/",arc_riscpc_selection_os_name(r->selection.os));
        return NULL;
    }
    if (!r || r->key.model>=4 || r->key.layout<1 || r->key.layout>3 ||
        (r->firmware_size!=2097152 && r->firmware_size!=4194304 && r->firmware_size!=8388608) ||
        (r->populate && (!r->hard_disc || r->import_path)) ||
        (!low && (r->selection.reason!=ARC_RPC_SELECTION_SELECTED ||
        !arc_riscpc_selection_qualified(r->selection.model,r->selection.os) ||
        (unsigned)r->selection.model!=r->key.model+1 || r->key.defaults ||
        r->key.layout!=(r->selection.os==ARC_RPC_OS_350?1u:r->selection.os==ARC_RPC_OS_360?2u:3u) || !r->fingerprint ||
        r->fingerprint->os!=r->selection.os || !r->hard_disc || !r->populate))) {
        if(e&&n)snprintf(e,n,"Invalid resolved RiscPC preparation request");return NULL;
    }
#if defined(ARCDROID_TEST_FAULTS)
    arc_riscpc_helper_failure=false;
#endif
    ArcRpcPreparation *p=calloc(1,sizeof(*p));
    if (!p){if(e&&n)snprintf(e,n,"RiscPC preparation allocation failed");return NULL;}
    p->request=*r;p->pacing=8;p->hd_close_ok=p->drain_ok=true;if(h)p->host=*h;
    if(r->hard_disc && !arc_install_store_native_path(r->save))goto invalid;
    if(r->launch){uint8_t digest[32];
        if(!r->session_id || !arc_riscpc_plan_digest(r->launch,digest))goto invalid;
        p->launch=*r->launch;p->request.launch=&p->launch;
        if(!arc_riscpc_attempt_init(&p->attempt,r->session_id,1))goto invalid;
    }
    if(!p->host.clock)p->host.clock=arc_riscpc_preparation_clock;
    if(!copied(p->save,r->save)||!copied(p->system,r->system)||!copied(p->source,r->import_path)||!copied(p->seed_path,r->seed_path))goto invalid;
    p->request.save=p->save;p->request.system=p->system;p->request.import_path=p->source[0]?p->source:NULL;p->request.seed_path=p->seed_path[0]?p->seed_path:NULL;
    p->firmware=malloc(r->firmware_size);if(!p->firmware)goto invalid;
    memcpy(p->firmware,r->firmware,r->firmware_size);p->request.firmware=p->firmware;
    if(r->seed){memcpy(p->seed,r->seed,256);p->have_seed=true;}p->request.seed=NULL;
    if(r->fingerprint){
        p->fingerprint=*r->fingerprint;
        if(!arc_riscpc_cmos_hex(r->fingerprint->sha1,40,p->sha1))goto invalid;
        p->fingerprint.sha1=p->sha1;p->request.fingerprint=&p->fingerprint;
    }
    if(r->media){p->media=*r->media;if(r->media->path){if(!copied(p->media_path,r->media->path))goto invalid;p->media.path=p->media_path;}p->request.media=&p->media;}
    if(r->helper){p->helper=*r->helper;if(r->helper->path){if(!copied(p->helper_path,r->helper->path))goto invalid;p->helper.path=p->helper_path;}p->request.helper=&p->helper;}
    (void)arc_lock_content(&p->content_owner,p->save,p->request.key.content,p->host.lock_backend,p->host.lock_opaque);
    measure_owner(p,&p->content_owner);
    arc_engine_stop();restart(p);if(e&&n)*e=0;return p;
invalid:
    if(e&&n)snprintf(e,n,"RiscPC preparation allocation or path failed");
    free(p->firmware);free(p);return NULL;
}
ArcRpcPreparation *arc_riscpc_preparation_begin(const ArcRpcPreparationRequest *r,const ArcRpcPreparationHost *h,char *e,size_t n)
{
    ARC_RPC_PREPARATION_DIRTY(); return begin(r,h,false,e,n); }
ArcRpcPreparation *arc_riscpc_preparation_low_level(const ArcRpcPreparationRequest *r,const ArcRpcPreparationHost *h,char *e,size_t n)
{
    ARC_RPC_PREPARATION_DIRTY(); return begin(r,h,true,e,n); }
static bool start(ArcRpcPreparation *p,bool factory)
{
    ARC_RPC_PREPARATION_DIRTY();
    return arc_engine_rpc_start_begin(p->request.key.model,p->firmware,p->request.firmware_size,
        factory?NULL:p->seed,factory?ARC_RPC_CMOS_GENERATION_EPOCH:p->request.epoch,
        factory,p->error,sizeof(p->error));
}
static void record_begin(ArcRpcPreparation *p)
{
    ARC_RPC_PREPARATION_DIRTY();
    if(arc_riscpc_cmos_store_begin(&p->record,p->save,&p->request.key,&p->content_owner,p->error,sizeof(p->error))<0)failed(p,p->error);
    else p->state=ARC_RPC_PREP_RECORD;
}
static void after_cmos(ArcRpcPreparation *p)
{
    ARC_RPC_PREPARATION_DIRTY();
    if(p->needs_population){p->state=ARC_RPC_PREP_POPULATE;p->substep=0;}
    else {p->substep=0;p->state=ARC_RPC_PREP_START;}
}
static void save_record(ArcRpcPreparation *p,const uint8_t payload[240])
{
    ARC_RPC_PREPARATION_DIRTY();
    if(!arc_riscpc_cmos_store_put_begin(&p->record,payload,NULL,p->error,sizeof(p->error)))failed(p,p->error);
    else p->state=ARC_RPC_PREP_SAVE_RECORD;
}
static void digest(const void *bytes,size_t n,uint8_t out[32])
{
    ARC_RPC_PREPARATION_DIRTY();ArcSha256 hash;arc_sha256_init(&hash);arc_sha256_update(&hash,bytes,n);arc_sha256_final(&hash,out);}
static bool product_read(void *opaque,uint32_t offset,void *out,size_t n)
{
    ARC_RPC_PREPARATION_DIRTY();
    ArcRpcPreparation *p=opaque;
    for(unsigned i=0;i<p->product.count;++i)if(offset>=p->product.ranges[i].offset &&
        offset-p->product.ranges[i].offset<=p->product.ranges[i].size && n<=p->product.ranges[i].size-(offset-p->product.ranges[i].offset)){
        memcpy(out,p->product.bytes+p->product.ranges[i].at+offset-p->product.ranges[i].offset,n);return true;}
    if(!p->product.missing){p->product.offset=offset;p->product.size=(uint32_t)n;p->product.missing=true;}
    return false;
}
/* 0 pending, 1 verified, -1 refused; one bounded read or pure discovery/unit. */
static int product_step(ArcRpcPreparation *p)
{
    ARC_RPC_PREPARATION_DIRTY();
    if(p->launch.action!=ARC_RPC_PLAN_PRODUCT || p->product.verified)return 1;
    if(!p->product.bytes){
        if(!arc_riscpc_hd_identity(p->disc,&p->product.identity))return -1;
        p->product.bytes=malloc(1048576);return p->product.bytes?0:-1;
    }
    if(p->product.missing){
        uint32_t offset=p->product.offset,n=p->product.size;
        if((offset&511) || !n || (n&511) || n>5120 || p->product.count==4096 || n>1048576-p->product.used ||
            !arc_riscpc_hd_read(p->disc,offset/512,n/512,p->product.bytes+p->product.used))return -1;
        unsigned i=p->product.count++;p->product.ranges[i].offset=offset;p->product.ranges[i].size=n;p->product.ranges[i].at=(uint32_t)p->product.used;
        p->product.used+=n;p->product.missing=false;return 0;
    }
    ArcLaunchDiscovery discovery;
    if(!arc_launch_discover_rpc(product_read,p,&discovery))return -1;
    if(p->product.missing)return 0;
    ArcRpcHdIdentity after;
    if(!discovery.readable || !arc_riscpc_hd_identity(p->disc,&after))return -1;
    for(unsigned i=0;i<discovery.candidate_count;++i){ArcRpcLaunchPlan plan;
        if(arc_riscpc_plan_build_product(&discovery,(int)i,(p->launch.flags&ARC_RPC_PLAN_RAMFS)!=0,
            p->launch.media,p->product.identity.bytes,after.bytes,&plan)==ARC_RPC_PLAN_OK && !strcmp(plan.target,p->launch.target)){
            free(p->product.bytes);p->product.bytes=NULL;p->product.verified=true;return 1;}
    }return -1;
}
static bool launch_bindings(ArcRpcPreparation *p,ArcRpcBootKey *key)
{
    ARC_RPC_PREPARATION_DIRTY();
    ArcRpcHdIdentity identity;
    if(!p->request.launch || !p->host.media_identity || !arc_riscpc_hd_identity(p->disc,&identity))return false;
    memset(key,0,sizeof(*key));key->cpu=p->request.key.model+1;key->plan_revision=2;key->profile_revision=ARC_RPC_LAUNCH_PROFILE_REVISION;key->action=p->launch.action;
    if(!p->host.media_identity(p->host.opaque,p->launch_media_identity))return false;
    memcpy(key->firmware,p->firmware_digest,32);
    uint8_t config[280]={0};memcpy(config,p->seed+16,240);
    config[240]=(uint8_t)p->request.key.model;config[241]=(uint8_t)p->request.key.layout;
    config[242]=p->request.hard_disc;config[243]=p->request.populate;config[244]=p->request.helper_override;
    config[245]=p->request.media && p->request.media->writable;
    memcpy(config+248,p->launch_media_identity,32);
    digest(config,sizeof(config),key->configuration);
    digest(p->request.key.content,40,key->content);memcpy(key->media,p->launch.media,32);memcpy(key->store,identity.bytes,32);
    if(!arc_riscpc_plan_digest(&p->launch,key->plan))return false;
    memcpy(p->block,p->controller.image,p->controller.size);
    memset(p->block+p->controller.status+16,0,16);memset(p->block+p->controller.latch+10,'0',48);
    digest(p->block,p->controller.size,key->helper);return true;
}
static bool launch_record_valid(ArcRpcPreparation *p)
{
    ARC_RPC_PREPARATION_DIRTY();
    ArcRpcHdIdentity identity;
    /* Firmware/plan are immutable copies. Relevant live CMOS excludes RTC.
     * Neither CMOS snapshot nor identity acquisition performs host I/O. */
    if(!p->launch_captured || p->state!=ARC_RPC_PREP_RUNNING)return false;
    if(!arc_riscpc_attempt_matches(&p->attempt,&p->attempt.token)){p->launch_captured=false;return false;}
    uint8_t live[240];snapshot_cmos(live);
    bool valid=!memcmp(live,p->seed+16,240) && arc_riscpc_hd_identity(p->disc,&identity) &&
        !memcmp(identity.bytes,p->launch_key.store,32);
    if(valid && p->host.media_identity){uint8_t media[32];valid=p->host.media_identity(p->host.opaque,media) && !memcmp(media,p->launch_media_identity,32);}
    if(!valid)p->launch_captured=false;
    return valid;
}
static void launch_revalidate(void *opaque)
{
    ARC_RPC_PREPARATION_DIRTY();
    ArcRpcPreparation *p=opaque;bool valid=launch_record_valid(p);
    if(!valid)arc_riscpc_launch_record_discard(&p->observations);
}
static void launch_media_changed(void *opaque)
{
    ARC_RPC_PREPARATION_DIRTY();ArcRpcPreparation *p=opaque;p->launch_captured=false;arc_riscpc_launch_record_discard(&p->observations);}
static void launch_aborted(void *opaque,ArcRpcAttemptInterrupt reason)
{
    ARC_RPC_PREPARATION_DIRTY();
    ArcRpcPreparation *p=opaque;
    if(reason!=ARC_RPC_INTERRUPT_SWAP_ROLLBACK)return;
    p->execution_barrier=true;p->launch_suppressed=true;p->launch_captured=false;
    uint8_t latest[240];snapshot_cmos(latest);char error[256]={0};
    uint64_t started=p->host.clock(p->host.opaque);
    bool completed=arc_riscpc_preparation_complete_publication(p,error,sizeof(error));
    bool saved=completed && arc_riscpc_cmos_store_put(&p->record,latest,NULL,error,sizeof(error));
    snprintf(p->error,sizeof(p->error),"RiscPC disc rollback failed; session stopped; latest CMOS snapshot %s",saved?"saved":"could not be saved");
    uint64_t ended=p->host.clock(p->host.opaque),cost=ended>=started?ended-started:0;
    ++p->stats.rollback_exceptions;if(cost>p->stats.rollback_max_ns)p->stats.rollback_max_ns=cost;
    p->notice_id="N-B rollback";failed(p,p->error);
}
/* This lease is preparation-owned, never queued observer work. The loaded
 * controller image remains intact for subsequent status validation. */
static void launch_consumed(void *opaque)
{
    ARC_RPC_PREPARATION_DIRTY();ArcRpcPreparation *p=opaque;p->retirement_pending=true;}
static bool launch_boundary(void *opaque)
{
    ARC_RPC_PREPARATION_DIRTY();
    ArcRpcPreparation *p=opaque;
    if(!p->retirement_pending)return true;
    if(!p->generated_helper || !arc_riscpc_helper_build(p->generated_helper,ARC_RPC_HELPER_SIZE) ||
        !arc_engine_rpc_mount(1,&p->helper,p->error,sizeof(p->error))){
        p->notice_id="N-B retire";failed(p,"RiscPC helper retirement failed; session stopped");return false;
    }
    p->retirement_pending=false;return true;
}
static bool cmos_namespace(const ArcRpcCmosStore *s)
{ ARC_RPC_PREPARATION_DIRTY(); return s->phase==0 || s->phase==12 || s->phase==14 || s->phase==15 || s->phase==17; }
static bool next_namespace(ArcRpcPreparation *p)
{
    ARC_RPC_PREPARATION_DIRTY();
    switch(p->state){
    case ARC_RPC_PREP_VERIFY:return arc_lock_next_namespace(&p->content_owner);
    case ARC_RPC_PREP_RECOVER:return !p->observations.ready?
        arc_riscpc_launch_record_namespace(&p->observations):p->disc && arc_riscpc_hd_next_namespace(p->disc,0);
    case ARC_RPC_PREP_VALIDATE:return arc_riscpc_boot_next_namespace(p->boot);
    case ARC_RPC_PREP_RECORD:case ARC_RPC_PREP_SAVE_RECORD:return cmos_namespace(&p->record);
    case ARC_RPC_PREP_DEFAULTS:return !arc_lock_held(&p->defaults_owner)?arc_lock_next_namespace(&p->defaults_owner):cmos_namespace(&p->defaults);
    case ARC_RPC_PREP_SAVE_DEFAULT:return cmos_namespace(&p->defaults);
    case ARC_RPC_PREP_START:{
        if(p->substep!=2 || !p->request.launch || p->launch_suppressed || !p->disc)return false;
        ArcRpcHdStats store;arc_riscpc_hd_stats(p->disc,&store);
        return store.state!=ARC_RPC_HD_IDLE && store.state<ARC_RPC_HD_READ_ONLY;
    }
    case ARC_RPC_PREP_POPULATE:
        if(p->boot)return arc_riscpc_boot_next_namespace(p->boot);
        if(p->io_pending)return arc_riscpc_hd_next_namespace(p->disc,1);
        if(p->substep==2)return arc_riscpc_hd_next_namespace(p->disc,2);
        if(p->substep==3)return arc_riscpc_hd_next_namespace(p->disc,3);
        return false;
    default:return false;
    }
}
static bool acquire_owner(ArcRpcPreparation *p,bool defaults)
{
    ArcLock *owner=defaults?&p->defaults_owner:&p->content_owner;
    uint64_t now=p->unit_now;
    bool *waiting=defaults?&p->defaults_waiting:&p->content_waiting;
    uint64_t *since=defaults?&p->defaults_wait:&p->content_wait;
    if(!*waiting){*waiting=true;*since=now;}
    bool namespace_unit=arc_lock_next_namespace(owner);
    ArcLockResult result=arc_lock_acquire(owner);
    if(!namespace_unit)++p->stats.lock_attempts;
    if(result==ARC_LOCK_CONTENDED || result==ARC_LOCK_TRANSIENT)++p->stats.lock_wait_frames;
    if(result==ARC_LOCK_HELD)return true;
    if(result==ARC_LOCK_CONTENDED || result==ARC_LOCK_TRANSIENT){
        uint64_t limit=defaults?UINT64_C(120000000000):UINT64_C(10000000000);
        if(now<*since || now-*since<limit)return false;
        if(result==ARC_LOCK_CONTENDED){p->notice_id=defaults?"N-16":"N-14";failed(p,defaults?
            "Another copy of RetroArch is preparing RiscPC settings. Wait for it to finish, then close and reload this content.":
            "This RiscPC title is open in another RetroArch. Close it there or restart RetroArch, then close and reload this content.");return false;}
    }
    p->notice_id=result==ARC_LOCK_IDENTITY_LOST?"N-18":result==ARC_LOCK_ROOT_REFUSED?"N-15 lock-root":
        result==ARC_LOCK_UNSUPPORTED?"N-15 lock-unsupported":result==ARC_LOCK_ERROR?"N-15 lock-error":"N-15 lock-create";
    failed(p,result==ARC_LOCK_IDENTITY_LOST?
        "Another app changed ArcDroid's RiscPC save files; this session stopped without saving. Close and reload this content.":
        "ArcDroid cannot lock RiscPC save files in this folder. Choose another save folder in RetroArch's directory settings.");
    return false;
}
static void unit(ArcRpcPreparation *p)
{
    ARC_RPC_PREPARATION_DIRTY();
    bool done=false;
    static const char *families[]={"N-B firmware","N-B hd","N-B boot","N-B cmos","N-B cmos",
        "N-B generate","N-B cmos","N-B cmos","N-B boot","N-B engine"};
    if(p->state<ARC_RPC_PREP_RUNNING)p->notice_id=families[p->state];
    switch(p->state){
    case ARC_RPC_PREP_VERIFY:{
        if(!arc_lock_held(&p->content_owner)){(void)acquire_owner(p,false);break;}
        if(p->request.hard_disc && !p->observations_begun){
            if(!arc_riscpc_launch_record_begin(&p->observations,p->save,p->request.key.content,&p->content_owner)){
                failed(p,"RiscPC boot observation recovery could not begin");break;}
            p->observations_begun=true;break;
        }
        size_t take=p->request.firmware_size-p->verify_at;if(take>32768)take=32768;
        const uint8_t *data=p->firmware+p->verify_at;
        /* All requested sessions derive exact recognition; a caller-supplied
         * fingerprint is additionally verified, never treated as authority. */
        arc_sha1_update(&p->hash1,data,take);p->crc=arc_crc32_append(p->crc,data,take);
        arc_sha256_update(&p->hash256,data,take);p->verify_at+=take;
        if(p->verify_at!=p->request.firmware_size)break;
        char sha1[41];uint8_t sha256[32];arc_sha1_final(&p->hash1,sha1);arc_sha256_final(&p->hash256,sha256);
        memcpy(p->firmware_digest,sha256,32);
        const ArcRpcFirmwareFingerprint *f=p->request.fingerprint;
        if(f && (f->size!=p->request.firmware_size || f->crc32!=p->crc || strcmp(f->sha1,sha1))){
            snprintf(p->error,sizeof(p->error),"RISC OS %s firmware cannot be verified; check <system>/arcdroid/firmware/",
                arc_riscpc_selection_os_name(f->os));failed(p,p->error);break;
        }
        size_t catalogue_count=0;const ArcRpcFirmwareFingerprint *catalogue=arc_riscpc_firmware_catalogue(&catalogue_count);
        ArcDigest digest={.size=p->request.firmware_size,.crc32=p->crc};memcpy(digest.sha1,sha1,sizeof(digest.sha1));
        const ArcRpcFirmwareFingerprint *recognized=arc_riscpc_firmware_recognize(&digest,catalogue,catalogue_count);
        p->verified_catalogue=recognized?(unsigned)recognized->os:0;
        static const char hex[]="0123456789abcdef";
        for(unsigned i=0;i<32;++i){p->request.key.firmware[2*i]=hex[sha256[i]>>4];p->request.key.firmware[2*i+1]=hex[sha256[i]&15];}
        p->request.key.firmware[64]=0;
        p->state=ARC_RPC_PREP_RECOVER;break;
    }
    case ARC_RPC_PREP_RECOVER:{
        if(!p->request.hard_disc){record_begin(p);break;}
        if(!p->observations.ready){bool ready=false;
            if(!arc_riscpc_launch_record_recover(&p->observations,&ready))failed(p,"RiscPC boot observation could not be invalidated before recovery");
            break;
        }
        if(!p->disc){p->disc=arc_riscpc_hd_open_begin(p->save,p->request.key.content,&p->content_owner,p->error,sizeof(p->error));if(!p->disc)failed(p,p->error);break;}
        ArcRpcHdOutcome o=arc_riscpc_hd_open_step(p->disc);
        if(o==ARC_RPC_HD_OPEN_PENDING)break;
        ArcRpcHdIdentity accepted_identity;
        if(!arc_riscpc_hd_identity(p->disc,&accepted_identity))arc_riscpc_launch_record_discard(&p->observations);
        if(o==ARC_RPC_HD_UNSUPPORTED||o==ARC_RPC_HD_CORRUPT||o==ARC_RPC_HD_UNAVAILABLE){failed(p,arc_riscpc_hd_outcome_notice(o));break;}
        if(p->request.populate && o==ARC_RPC_HD_LEGACY){failed(p,"The RiscPC hard disc is without a creation receipt; files have been preserved.");break;}
        if(p->source[0] && o!=ARC_RPC_HD_UNPUBLISHED){failed(p,"RiscPC hd_import refused: published store");break;}
        ArcRpcHdReceipt receipt;
        if(arc_riscpc_hd_receipt(p->disc,&receipt) && receipt.authored_revision==1)
            notice_event(p,"This RiscPC hard disc uses startup revision 1; launch delivery is unavailable. Manual input remains available.",ARC_RPC_NOTICE_ADVISORY,"N-8",false);
        p->needs_population=o==ARC_RPC_HD_UNPUBLISHED;p->receipted=o==ARC_RPC_HD_RECEIPTED;
        if(p->request.populate && p->needs_population){
            p->boot=arc_riscpc_boot_begin(p->disc,p->system,p->error,sizeof(p->error));
            if(!p->boot)failed(p,p->error);else p->state=ARC_RPC_PREP_VALIDATE;
        }else record_begin(p);
        break;
    }
    case ARC_RPC_PREP_VALIDATE:case ARC_RPC_PREP_POPULATE:
        if(p->boot){
            ArcRpcBootState s=arc_riscpc_boot_step(p->boot,1);
            if(s==ARC_RPC_BOOT_FAILED||s==ARC_RPC_BOOT_CANCELLED||s==ARC_RPC_BOOT_LEGACY){
                snprintf(p->error,sizeof(p->error),"RiscPC boot files in <system>/arcdroid/riscpc/boot/ cannot be used (%s). Replace them with unmodified copies.",arc_riscpc_boot_notice(p->boot));failed(p,p->error);
            }else if(p->state==ARC_RPC_PREP_VALIDATE && arc_riscpc_boot_validation_complete(p->boot))record_begin(p);
            else if(s==ARC_RPC_BOOT_DONE){
                ArcRpcBootStats a;arc_riscpc_boot_stats(p->boot,&a);
                if(p->host.log){char line[256];snprintf(line,sizeof(line),"boot-packages: HardDisc4=%s PlingSystem=%s files=%llu bytes=%llu",
                    a.qualified[0]?"qualified":"unqualified",a.qualified[1]?"qualified":"unqualified",(unsigned long long)a.files,(unsigned long long)a.file_bytes);p->host.log(p->host.opaque,line);}
                arc_riscpc_boot_free(p->boot);p->boot=NULL;p->substep=0;p->state=ARC_RPC_PREP_START;
            }
            break;
        }
        /* Low-level blank/import uses the same bounded population operations. */
        if(p->substep==0){
            if(p->source[0]){p->import=arc_io_open_stream(p->source,false);
                if(!p->import || arc_io_file_length(p->import)!=ARC_RPC_HD_BYTES){failed(p,"RiscPC hd_import refused: raw size or unavailable source");break;}}
            if(!(p->source[0]?arc_riscpc_hd_population_begin(p->disc):arc_riscpc_hd_population_blank_begin(p->disc))){failed(p,"RiscPC offline creation refused");break;}
            p->substep=1;break;
        }
        if(p->substep==1){
            if(p->io_pending){
                if(!arc_riscpc_hd_population_io_step(p->disc,&done)){failed(p,"RiscPC hard-disc creation failed");break;}
                if(done){p->io_pending=false;p->lba+=256;}break;
            }
            if(p->lba==ARC_RPC_HD_SECTORS){
                if(p->import){bool ok=arc_io_close_file(p->import);p->import=NULL;if(!ok){failed(p,"RiscPC import close failed");break;}}
                p->substep=2;break;
            }
            bool ok=p->import?arc_io_read_file(p->import,p->block,sizeof(p->block))==sizeof(p->block):
                arc_hd_format_range(256,13,(uint64_t)p->lba*512,p->block,sizeof(p->block));
            if(!ok){failed(p,"RiscPC import read or format failed");break;}
            bool nonzero=p->import!=NULL;
            for(size_t i=0;!nonzero&&i<sizeof(p->block);++i)nonzero=p->block[i]!=0;
            if(!nonzero){p->lba+=256;break;}
            if(!arc_riscpc_hd_population_io_begin(p->disc,p->lba,256,p->block,true)){failed(p,"RiscPC candidate write refused");break;}
            p->io_pending=true;break;
        }
        if(p->substep==2){if(!arc_riscpc_hd_population_seal_step(p->disc,&done))failed(p,"RiscPC hard-disc seal failed");else if(done)p->substep=3;break;}
        if(!arc_riscpc_hd_population_publish_step(p->disc,&done))failed(p,"RiscPC hard-disc publication failed");else if(done){p->substep=0;p->state=ARC_RPC_PREP_START;}
        break;
    case ARC_RPC_PREP_RECORD:{
        if(p->substep==1){
            void *data=NULL;size_t bytes=0;
            bool ok=arc_io_read_all_limit(p->seed_path,&data,&bytes,256,NULL) && bytes==256;
            if(ok){memcpy(p->seed,data,256);p->have_seed=true;}free(data);p->substep=0;
            if(!ok){p->notice_id="N-B seed";failed(p,"RPC CMOS seed is unavailable or has the wrong size");}
            else save_record(p,p->seed+16);
            break;
        }
        ArcRpcCmosResult r=arc_riscpc_cmos_store_step(&p->record,p->error,sizeof(p->error));
        if(r==ARC_RPC_CMOS_INSPECTING)break;
        if(r<0){failed(p,p->error);break;}
        if(!p->marker_checked && !p->record.refused){
            p->marker_checked=true;
            if(arc_riscpc_cmos_store_unsaved(&p->record))notice_event(p,"RiscPC settings from the last session could not be saved.",ARC_RPC_NOTICE_ADVISORY,"N-13",false);
        }
        if(r==ARC_RPC_CMOS_FOUND){
            if(p->record.refused){notice_event(p,p->error,ARC_RPC_NOTICE_TERMINAL,"cmos-refused",false);p->refusal_reported=true;p->status=NULL;}
            memset(p->seed,0,16);memcpy(p->seed+16,p->record.payload,240);after_cmos(p);break;}
        if(p->have_seed){save_record(p,p->seed+16);break;}
        if(p->seed_path[0]){p->substep=1;break;}
        ArcRpcCmosKey k=p->request.key;k.defaults=true;k.procedure=ARC_RPC_CMOS_PROCEDURE_REVISION;k.timing=ARC_RPC_CMOS_TIMING_REVISION;
        (void)arc_lock_defaults(&p->defaults_owner,p->save,k.firmware,k.model,k.procedure,k.timing,p->host.lock_backend,p->host.lock_opaque);
        measure_owner(p,&p->defaults_owner);
        p->defaults.key=k;p->state=ARC_RPC_PREP_DEFAULTS;
        break;
    }
    case ARC_RPC_PREP_DEFAULTS:{
        if(!arc_lock_held(&p->defaults_owner)){
            if(acquire_owner(p,true)){
                ArcRpcCmosKey key=p->defaults.key;
                if(arc_riscpc_cmos_store_begin(&p->defaults,p->save,&key,&p->defaults_owner,p->error,sizeof(p->error))<0)failed(p,p->error);
            }
            break;
        }
        ArcRpcCmosResult r=arc_riscpc_cmos_store_step(&p->defaults,p->error,sizeof(p->error));
        if(r==ARC_RPC_CMOS_INSPECTING)break;
        if(r<0){failed(p,p->error);break;}
        if(r==ARC_RPC_CMOS_FOUND){save_record(p,p->defaults.payload);arc_lock_release(&p->defaults_owner);p->defaults_waiting=false;break;}
        if(!start(p,true))failed(p,p->error);
        else {p->substep=0;p->state=ARC_RPC_PREP_GENERATE;}
        break;
    }
    case ARC_RPC_PREP_GENERATE:{
        if(!p->substep){
            if(!arc_engine_rpc_start_step(&done,p->error,sizeof(p->error))){failed(p,p->error);break;}
            if(done){
                if(p->host.started)p->host.started(p->host.opaque,true);
                if(!arc_engine_rpc_capture_schedule(ARC_RPC_CMOS_CAPTURE_NS,ARC_RPC_CMOS_DELETE_NS)){failed(p,"RiscPC isolated capture schedule failed");break;}
                p->substep=1;
            }
            break;
        }
        if(!arc_engine_rpc_advance(ARC_RPC_GENERATION_QUANTUM_NS)){failed(p,"RiscPC isolated settings generation failed");break;}
        arc_engine_rpc_diagnostics();
        if(p->host.diagnostics)p->host.diagnostics(p->host.opaque);
        ++p->stats.generation_quanta;
        uint64_t now;unsigned s;
        arc_engine_rpc_evidence(p->captured,p->evidence.mask,&p->evidence.writes,&p->evidence.last_write,&now,&s);
        if(s==1)break;
        static const unsigned rates[]={20,35,100,35};p->evidence.rate=rates[p->request.key.model];p->evidence.epoch=ARC_RPC_CMOS_GENERATION_EPOCH;
        if(s!=2||now!=ARC_RPC_CMOS_CAPTURE_NS||!arc_riscpc_cmos_oracle(p->request.key.layout,p->captured+16,&p->evidence)){
            failed(p,"RiscPC settings generation failed: required mask, stability or checksum");break;
        }
        if(!arc_riscpc_cmos_two_floppies(p->request.key.layout,p->captured+16)){
            failed(p,"RiscPC generated helper-drive configuration failed");break;
        }
        arc_engine_stop();
        if(!arc_riscpc_cmos_store_put_begin(&p->defaults,p->captured+16,&p->evidence,p->error,sizeof(p->error)))failed(p,p->error);
        else p->state=ARC_RPC_PREP_SAVE_DEFAULT;
        break;
    }
    case ARC_RPC_PREP_SAVE_DEFAULT:
        if(!arc_riscpc_cmos_store_put_step(&p->defaults,&done,p->error,sizeof(p->error)))failed(p,p->error);
        else if(done){save_record(p,p->defaults.payload);arc_lock_release(&p->defaults_owner);p->defaults_waiting=false;}break;
    case ARC_RPC_PREP_SAVE_RECORD:
        if(!arc_riscpc_cmos_store_put_step(&p->record,&done,p->error,sizeof(p->error)))failed(p,p->error);
        else if(done){memset(p->seed,0,16);memcpy(p->seed+16,p->record.payload,240);after_cmos(p);}break;
    case ARC_RPC_PREP_START:{
        if(!p->substep){if(!start(p,false))failed(p,p->error);else p->substep=1;break;}
        if(p->substep==1){
            if(!arc_engine_rpc_start_step(&done,p->error,sizeof(p->error)))failed(p,p->error);
            else if(done){if(p->host.started)p->host.started(p->host.opaque,false);p->substep=2;}
            break;
        }
        if(p->substep==2){
            if(p->request.launch && !p->launch_suppressed && (p->seed[199]&7u)<2){
                p->launch_suppressed=p->launch_unconfigured=true;
            }
            if(p->request.launch && !p->launch_suppressed && p->disc){
                ArcRpcHdStats store;arc_riscpc_hd_stats(p->disc,&store);
                if(store.state!=ARC_RPC_HD_IDLE && store.state<ARC_RPC_HD_READ_ONLY){arc_riscpc_hd_tick(p->disc);break;}
            }
            if(p->request.launch && !p->launch_suppressed && p->launch.action==ARC_RPC_PLAN_PRODUCT && !p->product.verified){
                int result=product_step(p);p->notice_id="N-B product";if(result<0)failed(p,"RiscPC installed product could not be verified against current state");
                break;
            }
            ArcRpcHdReceipt r;
            if(p->request.populate && !p->request.helper && !p->request.helper_override &&
               arc_riscpc_hd_receipt(p->disc,&r) && r.authored_revision==2){
                p->generated_helper=malloc(ARC_RPC_HELPER_SIZE);
                bool built=false;
                if(p->generated_helper){
                    if(p->request.launch && !p->launch_suppressed){
                        ArcRpcAttemptToken candidate={.session=p->attempt.session,.epoch=p->attempt.epoch,.serial=p->attempt.last_serial+1},issued;
                        built=arc_riscpc_plan_digest(&p->launch,candidate.plan) &&
                            arc_riscpc_controller_build(&p->launch,&candidate,&p->controller);
                        built=built && arc_riscpc_attempt_prepare(&p->attempt,&p->launch,0,&issued) &&
                            issued.serial==candidate.serial &&
                            arc_riscpc_helper_carrier(p->generated_helper,ARC_RPC_HELPER_SIZE,p->controller.image,p->controller.size);
                        p->launch_prepared=built;
                    }else built=arc_riscpc_helper_build(p->generated_helper,ARC_RPC_HELPER_SIZE);
                }
                if(!built){
                    p->notice_id="N-B helper";failed(p,"RiscPC helper construction failed");break;
                }
                p->stats.helper_bytes=ARC_RPC_HELPER_SIZE;
                p->helper=(ArcEngineMedia){.writable=false,.kind=ARC_MEDIA_ADL_640K,
                    .geometry=ARC_GEOMETRY_640K_L,.data=p->generated_helper,.size=ARC_RPC_HELPER_SIZE,
                    .effective_extent=ARC_MEDIA_EXTENT_UNRESTRICTED};
            }
            p->substep=3;break;
        }
        const ArcEngineMedia *helper=p->generated_helper?&p->helper:p->request.helper;
        ArcIdeBackend ide={0};if(p->disc)arc_riscpc_hd_backend(p->disc,&ide);
        arc_io_measure_begin();
        bool attached=arc_engine_rpc_attach(p->request.media,helper,p->disc?&ide:NULL,p->error,sizeof(p->error)) &&
            (!p->host.attach||p->host.attach(p->host.opaque,p->error,sizeof(p->error)));
        if(!attached)arc_engine_stop(); /* Include partial media cleanup in the census. */
        ArcIoMeasurement io=arc_io_measure_end();
        p->stats.attachment_operations=io.operations;p->stats.attachment_bytes=io.bytes;
        p->stats.attachment_namespace=io.namespace_operations;
        if(!attached){p->notice_id="N-B attach";failed(p,p->error);break;}
        arc_engine_rpc_observers_bind(p->verified_catalogue);
        if(p->host.attached)p->host.attached(p->host.opaque);
        p->launch_captured=p->launch_prepared && launch_bindings(p,&p->launch_key);
        if(p->launch_prepared){ArcRpcLaunchBinding binding={&p->attempt,&p->controller,launch_revalidate,p,launch_media_changed,launch_aborted,launch_consumed,launch_boundary};
            if(!arc_engine_rpc_launch_bind(&binding)){p->notice_id="N-B launch";failed(p,"RiscPC launch binding failed");break;}}
        p->state=ARC_RPC_PREP_RUNNING;p->execution_barrier=false;
        if(!p->refusal_reported)notice(p,"",false);
        if(p->launch_unconfigured)notice_event(p,"RiscPC launch delivery is unavailable: helper drive :1 is unconfigured. Manual input remains available.",ARC_RPC_NOTICE_ADVISORY,"launch-unavailable",false);
        p->status=NULL;break;
    }
    default:break;
    }
}
/* Recent maxima belong to a state/unit kind and namespace class. Construction
 * and virtual advances are separate kinds even within GENERATE. Cold admission
 * uses the ceiling; a valid clock replaces it with the last sixteen costs. */
static unsigned cost_kind(const ArcRpcPreparation *p,bool advance)
{
    ARC_RPC_PREPARATION_DIRTY();
    if((p->state==ARC_RPC_PREP_VERIFY && !arc_lock_held(&p->content_owner)) ||
       (p->state==ARC_RPC_PREP_DEFAULTS && !arc_lock_held(&p->defaults_owner)))return ARC_RPC_PREP_LOCK_KIND;
    if(advance)return ARC_RPC_PREP_RUNNING;
    if(p->state==ARC_RPC_PREP_START && p->substep==2 && p->request.launch &&
        !p->launch_suppressed && p->launch.action==ARC_RPC_PLAN_PRODUCT && !p->product.verified)return ARC_RPC_PREP_PRODUCT_KIND;
    if(p->state!=ARC_RPC_PREP_POPULATE)return (unsigned)p->state;
    if(p->boot)return ARC_RPC_PREP_CANCELLED+arc_riscpc_boot_next_work(p->boot);
    ArcRpcBootWork work=p->io_pending||p->substep==3?ARC_RPC_BOOT_WORK_WRITE:
        p->substep==2?ARC_RPC_BOOT_WORK_HASH:
        p->substep==1?(p->import?ARC_RPC_BOOT_WORK_READ:ARC_RPC_BOOT_WORK_FORMAT):ARC_RPC_BOOT_WORK_OTHER;
    return ARC_RPC_PREP_CANCELLED+ARC_RPC_BOOT_DONE*ARC_RPC_BOOT_WORK_COUNT+(unsigned)work;
}
static uint64_t estimate(const ArcRpcPreparation *p,unsigned kind,bool ns,uint64_t ceiling)
{
    ARC_RPC_PREPARATION_DIRTY();
    uint64_t cost=0;
    for(unsigned i=0;i<ARC_RPC_COST_SAMPLES;++i)if(p->costs[kind][ns][i]>cost)cost=p->costs[kind][ns][i];
    return cost && cost<ceiling?cost:ceiling;
}
ArcRpcPreparationState arc_riscpc_preparation_frame(ArcRpcPreparation *p)
{
    ARC_RPC_PREPARATION_DIRTY();
    if(!p || p->state>=ARC_RPC_PREP_RUNNING)return p?p->state:ARC_RPC_PREP_CANCELLED;
    uint64_t start_ns=p->host.clock(p->host.opaque),previous=start_ns,used=0,reserved=0;
    bool generation=p->state==ARC_RPC_PREP_GENERATE,faulty=false;
    p->stats.steps=p->stats.generation_quanta=p->stats.namespace_units=p->stats.attachment_units=0;p->stats.namespace_attempts=0;
    p->stats.attachment_operations=p->stats.attachment_bytes=p->stats.attachment_namespace=0;
    p->stats.estimated_ns=0;p->stats.helper_bytes=0;
    p->stats.budget_ns=generation?ARC_RPC_GENERATION_BUDGET_NS:ARC_RPC_PREP_OFFLINE_BUDGET_NS;
    refresh(p,true);
    while(p->state<ARC_RPC_PREP_RUNNING && p->stats.steps<ARC_RPC_PREP_STEPS){
        if((p->state==ARC_RPC_PREP_GENERATE)!=generation)break;
        bool ns=next_namespace(p),attachment=p->state==ARC_RPC_PREP_START && p->substep==3;
        bool advance=generation && p->substep==1;
        unsigned kind=cost_kind(p,advance);
        uint64_t ceiling=advance?ARC_RPC_GENERATION_UNIT_NS:ns||attachment||kind==ARC_RPC_PREP_PRODUCT_KIND?ARC_RPC_PREP_NAMESPACE_UNIT_NS:ARC_RPC_PREP_UNIT_NS;
        uint64_t budget=generation?ARC_RPC_GENERATION_BUDGET_NS:ARC_RPC_PREP_OFFLINE_BUDGET_NS;
        uint64_t cost=attachment||faulty?ceiling:estimate(p,kind,ns,ceiling);
        /* The final frontend attachment is indivisible, may do several file
         * and namespace callbacks, and never shares a frame with other work. */
        if(attachment && p->stats.steps)break;
        uint64_t charged=faulty?(used>reserved?used:reserved):used;
        if(charged>budget || cost>budget-charged || (advance && p->stats.generation_quanta>=p->pacing))break;
        /* A first indivisible unit retains the full allowance. Later units
         * leave the offline/generation margin for cost variation. */
        uint64_t headroom=generation?ARC_RPC_GENERATION_HEADROOM_NS:ARC_RPC_PREP_HEADROOM_NS;
        if(p->stats.steps && (charged>budget-headroom || cost>budget-headroom-charged))break;
        p->stats.unit_kind=kind;p->stats.estimated_ns=cost;
        ArcRpcHdStats hd_before={0},hd_after={0};if(p->disc)arc_riscpc_hd_stats(p->disc,&hd_before);
        uint64_t cmos_before=p->record.stats.namespace_operations+p->defaults.stats.namespace_operations+p->observations.namespace_operations;
        bool waiting=(p->state==ARC_RPC_PREP_VERIFY && !arc_lock_held(&p->content_owner)) ||
            (p->state==ARC_RPC_PREP_DEFAULTS && !arc_lock_held(&p->defaults_owner));
        p->unit_now=previous;
        if(waiting)arc_io_measure_begin();
        unit(p);
        ArcIoMeasurement lock_io={0};if(waiting)lock_io=arc_io_measure_end();
        ++p->stats.steps;if(ns)++p->stats.namespace_units;reserved+=ceiling;refresh(p,false);
        if(p->disc)arc_riscpc_hd_stats(p->disc,&hd_after);
        uint64_t attempts=hd_after.namespace_operations-hd_before.namespace_operations+
            p->record.stats.namespace_operations+p->defaults.stats.namespace_operations+p->observations.namespace_operations-cmos_before;
        if(attachment){++p->stats.attachment_units;attempts+=p->stats.attachment_namespace;}
        attempts+=lock_io.namespace_operations;
        p->stats.namespace_attempts+=attempts;
        p->stats.budget_ns=budget;
        uint64_t now=p->host.clock(p->host.opaque);
        if(now<previous){
            faulty=true;used=UINT64_MAX;
        }else if(now==previous){
            /* Any stall activates declared costs from the last clock
             * progress, including the already-admitted stalled unit. */
            faulty=true;
        }else{
            uint64_t actual=now-previous;
            if(kind==ARC_RPC_PREP_LOCK_KIND && !ns && actual>p->stats.lock_max_ns)p->stats.lock_max_ns=actual;
            used=now>=start_ns?now-start_ns:UINT64_MAX;faulty=false;
            reserved=used;
            p->costs[kind][ns][p->cost_at[kind][ns]++%ARC_RPC_COST_SAMPLES]=actual;
            if(ns && actual>ARC_RPC_PREP_NAMESPACE_UNIT_NS)++p->stats.namespace_exceptions;
            if(((attempts||advance) && actual>cost) || actual>ceiling){previous=now;break;}
        }
        previous=now;
        if(waiting || attachment || (attempts && !ns))break;
    }
    p->stats.elapsed_ns=used;p->stats.reserved_ns=faulty?reserved:0;++p->stats.frames;
    if(used>p->stats.max_frame_ns)p->stats.max_frame_ns=used;
    p->stats.state=p->state;return p->state;
}
ArcRpcPreparationState arc_riscpc_preparation_state(const ArcRpcPreparation *p)
{
    ARC_RPC_PREPARATION_DIRTY(); return p?p->state:ARC_RPC_PREP_CANCELLED; }
const char *arc_riscpc_preparation_error(const ArcRpcPreparation *p)
{
    ARC_RPC_PREPARATION_DIRTY(); return p?p->error:""; }
void arc_riscpc_preparation_stats(const ArcRpcPreparation *p,ArcRpcPreparationStats *s)
{
    ARC_RPC_PREPARATION_DIRTY(); if(p&&s)*s=p->stats; }
ArcRpcHdStore *arc_riscpc_preparation_disc(ArcRpcPreparation *p)
{
    ARC_RPC_PREPARATION_DIRTY(); return p?p->disc:NULL; }
bool arc_riscpc_preparation_helper_override(ArcRpcPreparation *p,const ArcEngineMedia *view)
{
    ARC_RPC_PREPARATION_DIRTY();
    if(!p || !p->request.helper_override || p->state!=ARC_RPC_PREP_START || !view)return false;
    if(view->path && !copied(p->helper_path,view->path))return false;
    p->helper=*view;if(view->path)p->helper.path=p->helper_path;p->request.helper=&p->helper;return true;
}
bool arc_riscpc_preparation_helper_write_policy(ArcRpcPreparation *p,bool writable)
{
    ARC_RPC_PREPARATION_DIRTY();
    if(!p || !(p->stopped_window || (p->execution_barrier && p->state==ARC_RPC_PREP_RUNNING)))return false;
    if(p->request.helper_override && p->request.helper)p->helper.writable=writable;
    return true;
}
void arc_riscpc_preparation_attempt(const ArcRpcPreparation *p,ArcRpcLaunchAttempt *out)
{
    ARC_RPC_PREPARATION_DIRTY();if(out){if(p)*out=p->attempt;else memset(out,0,sizeof(*out));}}
bool arc_riscpc_preparation_execution_allowed(const ArcRpcPreparation *p)
{
    ARC_RPC_PREPARATION_DIRTY();return p && p->state==ARC_RPC_PREP_RUNNING && !p->execution_barrier && arc_engine_active() && arc_engine_is_rpc();}
bool arc_riscpc_preparation_observation(ArcRpcPreparation *p,ArcRpcBootRecord *out)
{
    ARC_RPC_PREPARATION_DIRTY();
    if(!p || !out || !p->observations.have_old || !launch_record_valid(p) ||
        !arc_riscpc_boot_record_matches(&p->observations.old,&p->launch_key)){
        if(p)arc_riscpc_launch_record_discard(&p->observations);return false;
    }*out=p->observations.old;return true;
}
void arc_riscpc_preparation_interrupt(ArcRpcPreparation *p,ArcRpcAttemptInterrupt reason)
{
    ARC_RPC_PREPARATION_DIRTY();if(p && p->attempt.live){arc_riscpc_attempt_interrupt(&p->attempt,reason);p->launch_captured=false;}}
bool arc_riscpc_preparation_reset(ArcRpcPreparation *p)
{
    ARC_RPC_PREPARATION_DIRTY();
    if(!p)return false;
    while(p->draining)arc_riscpc_preparation_failed_frame(p);
    if(p->ownership_lost){failed(p,NULL);return false;}
    p->error[0]=0;p->notice_id="N-B reset";
    p->execution_barrier=true;p->launch_suppressed=true;
    arc_riscpc_preparation_interrupt(p,ARC_RPC_INTERRUPT_RESET);
    if(p->state==ARC_RPC_PREP_RUNNING){
        if(!arc_engine_active() || !arc_engine_is_rpc()){
            failed(p,"RiscPC reset failed; session stopped; latest CMOS snapshot unavailable");return false;
        }
        uint8_t latest[240];snapshot_cmos(latest);
        arc_engine_rpc_eject(1);
        bool ok=p->host.restage_drive0 && p->host.restage_drive0(p->host.opaque,p->error,sizeof(p->error));
        ArcRpcHdReceipt receipt;
        bool generated=p->request.populate && !p->request.helper && !p->request.helper_override &&
            arc_riscpc_hd_receipt(p->disc,&receipt) && receipt.authored_revision==2;
        if(ok && generated){
            if(!p->generated_helper)p->generated_helper=malloc(ARC_RPC_HELPER_SIZE);
            ok=p->generated_helper && arc_riscpc_helper_build(p->generated_helper,ARC_RPC_HELPER_SIZE);
            if(ok){p->helper=(ArcEngineMedia){.kind=ARC_MEDIA_ADL_640K,.geometry=ARC_GEOMETRY_640K_L,
                .data=p->generated_helper,.size=ARC_RPC_HELPER_SIZE,.effective_extent=ARC_MEDIA_EXTENT_UNRESTRICTED};
                ok=arc_engine_rpc_mount(1,&p->helper,p->error,sizeof(p->error));}
        }else if(ok && p->request.helper)ok=arc_engine_rpc_mount(1,p->request.helper,p->error,sizeof(p->error));
        if(ok)ok=p->host.reset_rebind && p->host.reset_rebind(p->host.opaque,p->error,sizeof(p->error));
        if(ok && p->attempt.live)ok=arc_riscpc_attempt_noop_ready(&p->attempt,true);
        if(!ok){
            char snapshot_error[256]={0};bool saved=arc_riscpc_cmos_store_put(&p->record,latest,NULL,snapshot_error,sizeof(snapshot_error));
            char cause[256];snprintf(cause,sizeof(cause),"%s",*p->error?p->error:"RiscPC reset transaction failed");
            snprintf(p->error,sizeof(p->error),"%s; session stopped; latest CMOS snapshot %s",cause,saved?"saved":"could not be saved");
            failed(p,p->error);
        }else {p->retirement_pending=false;p->execution_barrier=false;}
        return ok;
    }
    release(p);
    memset(&p->media,0,sizeof(p->media));p->request.media=NULL;
    p->stopped_window=true;
    bool staged=!p->host.restage_stopped || p->host.restage_stopped(p->host.opaque,p->error,sizeof(p->error));
    p->stopped_window=false;
    if(!staged){p->notice_id="N-B restage-stopped";failed(p,*p->error?p->error:"The disc could not be reloaded");return false;}
    restart(p);
    return true;
}
void arc_riscpc_preparation_cancel(ArcRpcPreparation *p)
{ ARC_RPC_PREPARATION_DIRTY(); if(p){p->draining=false;p->drain_ok=true;release(p);arc_lock_release(&p->content_owner);p->state=ARC_RPC_PREP_CANCELLED;p->status=NULL;} }
bool arc_riscpc_preparation_persist(ArcRpcPreparation *p,char *e,size_t n)
{
    ARC_RPC_PREPARATION_DIRTY();
    if(!p || p->state!=ARC_RPC_PREP_RUNNING || !arc_engine_is_rpc() || !arc_engine_active())return true;
    uint8_t payload[240];snapshot_cmos(payload);
    return arc_riscpc_cmos_store_put(&p->record,payload,NULL,e,n);
}
static void fault_failed(ArcRpcPreparation *p,const char *cause,const char *id)
{
    ARC_RPC_PREPARATION_DIRTY();
    snapshot_cmos(p->drain_payload);p->drain_started=false;p->drain_frames=0;p->drain_ok=false;
    /* A refused CMOS store still needs its one FAILED outcome step. The
     * token-bound begin refuses without I/O and reports could-not-be-saved. */
    p->draining=arc_lock_held(&p->content_owner);
    p->notice_id=id;failed(p,cause);
}
void arc_riscpc_preparation_maintenance(ArcRpcPreparation *p)
{
    ARC_RPC_PREPARATION_DIRTY();
    if(!p || p->state!=ARC_RPC_PREP_RUNNING)return;
    ArcLockResult identity=arc_lock_verify(&p->content_owner);
    if(identity!=ARC_LOCK_HELD){
        p->notice_id="N-18";failed(p,"Another app changed ArcDroid's RiscPC save files; this session stopped without saving. Close and reload this content.");return;
    }
    const char *fault=NULL;if(arc_engine_faulted(&fault)){
        fault_failed(p,fault&&*fault?fault:"RiscPC session faulted","N-B fault");return;}
    if(p->disc){
        arc_riscpc_hd_tick(p->disc);
        const char *s=arc_riscpc_hd_diagnostic(p->disc);if(s&&p->host.log){char line[1024];snprintf(line,sizeof(line),"hd-diagnostic: %s",s);p->host.log(p->host.opaque,line);}
        bool terminal=arc_riscpc_hd_notice_is_terminal(p->disc);
        s=arc_riscpc_hd_notice(p->disc);if(s)notice_event(p,s,terminal?ARC_RPC_NOTICE_TERMINAL:ARC_RPC_NOTICE_ADVISORY,terminal?"N-B hd":"hd-recovered",false);}
    char e[256]={0};
    bool persisted=true,done=false;
    if(p->record.putting)persisted=arc_riscpc_cmos_store_put_step(&p->record,&done,e,sizeof(e));
    else if(!p->record.refused){
        uint8_t payload[240];snapshot_cmos(payload);
        persisted=arc_riscpc_cmos_store_put_begin(&p->record,payload,NULL,e,sizeof(e));
    }
    ++p->live_frames;
    if(owner_lost(&p->content_owner)){failed(p,NULL);return;}
    if(!persisted && !p->refusal_reported){notice_event(p,e,ARC_RPC_NOTICE_TERMINAL,"cmos-persist",false);p->refusal_reported=true;}
    bool valid=launch_record_valid(p);
    if(!valid){arc_riscpc_launch_record_discard(&p->observations);
        if(p->observations.published){
            if(!arc_riscpc_launch_record_invalidate(&p->observations)){fault_failed(p,"RiscPC boot observation invalidation failed; session stopped","N-B observation");return;}
            p->observations.published=false;
        }
    }
    if(p->observations.phase>=10){bool done=false;
        if(!arc_riscpc_launch_record_publish_step(&p->observations,valid,&done)){
            if(owner_lost(&p->content_owner)){failed(p,NULL);return;}
            arc_riscpc_launch_record_discard(&p->observations);
        }
    }else if(!p->record_attempted && arc_riscpc_attempt_established(&p->attempt)){
        p->record_attempted=true;ArcRpcBootRecord observation;ArcRpcBootKey current=p->launch_key;
        if(valid && arc_riscpc_boot_record_capture(&p->attempt,&p->launch_key,&current,&observation))
            (void)arc_riscpc_launch_record_publish_begin(&p->observations,&observation);
    }
}
bool arc_riscpc_preparation_free_stats(ArcRpcPreparation *p,char *e,size_t n,ArcRpcPreparationStats *stats)
{
    ARC_RPC_PREPARATION_DIRTY();
    if(!p)return true;
    while(p->draining)arc_riscpc_preparation_failed_frame(p);
    if(!p->ownership_lost && arc_lock_held(&p->content_owner) && arc_lock_verify(&p->content_owner)!=ARC_LOCK_HELD)failed(p,NULL);
    bool ok=p->drain_ok && !p->ownership_lost;
    if(p->state==ARC_RPC_PREP_RUNNING && arc_engine_active()){
        uint8_t latest[240];snapshot_cmos(latest);
        bool done=false;
        while(p->record.putting && ok && !done)ok=arc_riscpc_cmos_store_put_step(&p->record,&done,e,n);
        if(!p->record.reconciled && !p->record.refused){
            ArcRpcCmosResult r=ARC_RPC_CMOS_INSPECTING;
            while(r==ARC_RPC_CMOS_INSPECTING)r=arc_riscpc_cmos_store_step(&p->record,e,n);
            ok=r==ARC_RPC_CMOS_FOUND||r==ARC_RPC_CMOS_MISSING;
        }
        if(ok)ok=arc_riscpc_cmos_store_put(&p->record,latest,NULL,e,n);
    }
    if(!p->ownership_lost && (owner_lost(&p->content_owner) || owner_lost(&p->defaults_owner))){
        failed(p,NULL);ok=false;
    }
    if(!ok && !p->ownership_lost){
        notice_event(p,"RiscPC settings could not be saved on unload",ARC_RPC_NOTICE_ADVISORY,"unload-cmos",false);
        if(!p->ownership_lost && !p->record.refused && !arc_riscpc_cmos_store_mark_unsaved(&p->record))
            notice_event(p,"RiscPC settings could not be saved on unload",ARC_RPC_NOTICE_ADVISORY,"unload-marker",false);
    }
    release(p);
    if(!p->hd_close_ok){
        if(!p->ownership_lost && owner_lost(&p->content_owner)){failed(p,NULL);ok=false;}
        if(!p->ownership_lost)notice_event(p,"Recent RiscPC hard disc changes could not be saved.",ARC_RPC_NOTICE_ADVISORY,"N-17 unload",false);
    }ok=p->hd_close_ok&&ok;arc_lock_release(&p->content_owner);p->stats.state=p->state;if(stats)*stats=p->stats;free(p->firmware);free(p);return ok;
}
bool arc_riscpc_preparation_free(ArcRpcPreparation *p,char *e,size_t n)
{
    return arc_riscpc_preparation_free_stats(p,e,n,NULL);
}
bool arc_riscpc_preparation_pacing(ArcRpcPreparation *p,unsigned quanta)
{
    ARC_RPC_PREPARATION_DIRTY();
    if(!p || (quanta!=1 && quanta!=8) || p->state>ARC_RPC_PREP_GENERATE)return false;
    if(p->state==ARC_RPC_PREP_GENERATE){uint8_t b[256],m[32];uint64_t w,l,now;unsigned s;
        arc_engine_rpc_evidence(b,m,&w,&l,&now,&s);if(now)return false;}
    p->pacing=quanta;return true;
}

void arc_riscpc_preparation_pair(const ArcRpcPreparation *p,unsigned *model,unsigned *os)
{if(model)*model=p?p->request.key.model:0;if(os)*os=p?(p->verified_catalogue?p->verified_catalogue:(unsigned)p->request.selection.os):0;}
bool arc_riscpc_preparation_owned(const ArcRpcPreparation *p)
{return p && arc_lock_held(&p->content_owner);}

unsigned arc_riscpc_preparation_catalogue(const ArcRpcPreparation *p)
{return p?p->verified_catalogue:0;}
bool arc_riscpc_preparation_hd_close_ok(const ArcRpcPreparation *p)
{return !p || p->hd_close_ok;}
bool arc_riscpc_preparation_take_hd_close(ArcRpcPreparation *p)
{
    ARC_RPC_PREPARATION_DIRTY();if(!p)return true;bool ok=p->hd_close_ok;p->hd_close_ok=true;return ok;}
bool arc_riscpc_preparation_replace_media(ArcRpcPreparation *p,const ArcEngineMedia *media)
{
    ARC_RPC_PREPARATION_DIRTY();
    if(!p || !(p->stopped_window || (p->state==ARC_RPC_PREP_RUNNING && p->execution_barrier)))return false;
    if(media){
        if(media->path && !copied(p->media_path,media->path))return false;
        p->media=*media;if(media->path)p->media.path=p->media_path;p->request.media=&p->media;
    }else{memset(&p->media,0,sizeof(p->media));p->request.media=NULL;}
    return true;
}
bool arc_riscpc_preparation_complete_publication(ArcRpcPreparation *p,char *error,size_t n)
{
    ARC_RPC_PREPARATION_DIRTY();
    if(!p)return false;
    ArcRpcCmosStore *stores[]={&p->record,&p->defaults};
    for(unsigned i=0;i<2;++i){
        if(!stores[i]->putting)continue;
        uint64_t started=p->host.clock(p->host.opaque);bool done=false,ok=arc_lock_verify(stores[i]->owner)==ARC_LOCK_HELD;
        while(stores[i]->putting && !done && ok)ok=arc_riscpc_cmos_store_put_step(stores[i],&done,error,n);
        uint64_t ended=p->host.clock(p->host.opaque),cost=ended>=started?ended-started:0;
        if(p->state!=ARC_RPC_PREP_RUNNING){
            if(i){++p->stats.restart_defaults_exceptions;if(cost>p->stats.restart_defaults_max_ns)p->stats.restart_defaults_max_ns=cost;}
            else{++p->stats.restart_record_exceptions;if(cost>p->stats.restart_record_max_ns)p->stats.restart_record_max_ns=cost;}
        }
        if(!ok){if(owner_lost(stores[i]->owner))failed(p,NULL);return false;}
    }
    return true;
}
bool arc_riscpc_preparation_draining(const ArcRpcPreparation *p)
{return p && p->draining;}
void arc_riscpc_preparation_failed_frame(ArcRpcPreparation *p)
{
    ARC_RPC_PREPARATION_DIRTY();
    if(!p || p->state!=ARC_RPC_PREP_FAILED || !p->draining)return;
    if(arc_lock_verify(&p->content_owner)!=ARC_LOCK_HELD){failed(p,NULL);return;}
    bool done=false,ok=arc_lock_held(&p->content_owner);
    ++p->drain_frames;
    char error[256]={0};
    if(ok && p->record.putting){
        ok=arc_riscpc_cmos_store_put_step(&p->record,&done,error,sizeof(error));
        if(ok && done && p->drain_started){
            p->draining=false;p->drain_ok=true;
            char cause[512];snprintf(cause,sizeof(cause),"%.390s; latest CMOS snapshot saved",p->error);
            notice_event(p,cause,ARC_RPC_NOTICE_TERMINAL,"N-B drain",true);return;
        }
    }else if(ok && !p->drain_started){
        p->drain_started=true;
        ok=arc_riscpc_cmos_store_put_begin(&p->record,p->drain_payload,NULL,error,sizeof(error));
        if(ok && !p->record.putting){
            p->draining=false;p->drain_ok=true;
            char cause[512];snprintf(cause,sizeof(cause),"%.390s; latest CMOS snapshot saved",p->error);
            notice_event(p,cause,ARC_RPC_NOTICE_TERMINAL,"N-B drain",true);return;
        }
    }else ok=false;
    if(owner_lost(&p->content_owner)){failed(p,NULL);return;}
    if(!ok || p->drain_frames>=60){
        p->draining=false;
        char cause[512];snprintf(cause,sizeof(cause),"%.380s; latest CMOS snapshot could not be saved",p->error);
        notice_event(p,cause,ARC_RPC_NOTICE_TERMINAL,"N-B drain",true);
    }
}
#if defined(ARCDROID_RPC_LOCKSTEP)
/* Pure owning-TU read: foreign test bindings have no preparation obligation. */
bool arc_rpc_lockstep_retirement_pending(const void *opaque,bool (*boundary)(void *))
{
    return opaque && boundary==launch_boundary && ((const ArcRpcPreparation *)opaque)->retirement_pending;
}
#endif

#if defined(ARCDROID_RPC_LOCKSTEP)
/* Read only semantic state through its owner. Foreign bindings return zero;
 * neither native handles, process addresses nor wall-clock costs enter v1. */
uint64_t arc_rpc_lockstep_preparation_value(const void *opaque,bool (*boundary)(void *),unsigned field)
{
    if(!opaque || boundary!=launch_boundary)return 0;
    const ArcRpcPreparation *p=opaque;
    switch(field){
    case 0:return p->content_waiting;
    case 1:return p->defaults_waiting;
    case 2:return p->observations_begun;
    case 3:return p->hd_close_ok;
    case 4:return p->stopped_window;
    case 5:return p->marker_checked;
    case 6:return p->released;
    case 7:return p->ownership_lost;
    case 8:return p->draining;
    case 9:return p->drain_started;
    case 10:return p->drain_ok;
    case 11:return p->drain_frames;
    case 12:return p->content_owner.valid;
    case 13:return p->content_owner.opened;
    case 14:return p->content_owner.held;
    case 15:return p->content_owner.lost;
    case 16:return p->content_owner.parents_done;
    case 17:return p->defaults_owner.valid;
    case 18:return p->defaults_owner.opened;
    case 19:return p->defaults_owner.held;
    case 20:return p->defaults_owner.lost;
    case 21:return p->defaults_owner.parents_done;
    case 22:return p->record.marker_checked;
    case 23:return p->record.marker_present;
    case 24:return p->defaults.marker_checked;
    case 25:return p->defaults.marker_present;
    case 26:{
        if(!p->draining && !p->drain_started)return 0;
        ArcSha256 hash;uint8_t digest[32];uint64_t value=0;
        arc_sha256_init(&hash);arc_sha256_update(&hash,p->drain_payload,sizeof(p->drain_payload));arc_sha256_final(&hash,digest);
        for(unsigned n=0;n<8;++n)value|=(uint64_t)digest[n]<<(8*n);
        return value;
    }
    case 30:return p->record.owner==NULL?0:p->record.owner==&p->content_owner?1:p->record.owner==&p->defaults_owner?2:3;
    case 31:return p->defaults.owner==NULL?0:p->defaults.owner==&p->content_owner?1:p->defaults.owner==&p->defaults_owner?2:3;
    case 32:return p->observations.owner==NULL?0:p->observations.owner==&p->content_owner?1:p->observations.owner==&p->defaults_owner?2:3;
    case 33:return arc_riscpc_hd_lockstep_owner(p->disc,&p->content_owner);
    default:return arc_riscpc_host_lockstep_value(p->host.opaque,p->host.clock,field-27u);
    }
}
#endif
