/* Session preparation owns offline resources; host time never enters a guest
 * deadline, input ordinal, storage decision or live-session schedule. GPL-2.0-only. */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
#include "rpc_preparation.h"
#include "install_disc.h"
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
struct ArcRpcPreparation {
    ArcRpcPreparationHost host;
    ArcRpcPreparationRequest request;
    ArcRpcFirmwareFingerprint fingerprint;
    char sha1[41],save[1024],system[1024],source[1024],media_path[1024],helper_path[1024],seed_path[1024],error[512];
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
    uint64_t costs[ARC_RPC_PREP_CANCELLED][2][ARC_RPC_COST_SAMPLES];
    unsigned cost_at[ARC_RPC_PREP_CANCELLED][2];
};
static const char *status(const ArcRpcPreparation *p)
{
    ArcRpcPreparationState state=p->state;
    if(state>=ARC_RPC_PREP_RECORD && p->receipted) return "Starting RiscPC";
    if (state <= ARC_RPC_PREP_RECOVER) return "Checking the RiscPC hard disc";
    if (state == ARC_RPC_PREP_VALIDATE) return "Checking RiscPC boot files";
    if (state <= ARC_RPC_PREP_SAVE_RECORD) return "Preparing RiscPC settings for first use";
    if (state <= ARC_RPC_PREP_START) return "Preparing the RiscPC hard disc";
    return "";
}
static void notice(ArcRpcPreparation *p,const char *s,bool terminal)
{ if (p->host.notice) p->host.notice(p->host.opaque,s,terminal); }
static void refresh(ArcRpcPreparation *p,bool tick)
{
    const char *s=status(p);
    if (p->state >= ARC_RPC_PREP_RUNNING || p->refusal_reported) return;
    if (p->status != s || (tick && !p->notice_frames)) {
        notice(p,s,false);p->status=s;p->notice_frames=60;
    }
    if(tick && p->notice_frames)--p->notice_frames;
}
static void snapshot_cmos(uint8_t payload[240])
{
    uint8_t b[256],m[32];uint64_t w,l,now;unsigned s;
    arc_engine_rpc_evidence(b,m,&w,&l,&now,&s);memcpy(payload,b+16,240);
}
static void release(ArcRpcPreparation *p)
{
    arc_engine_stop(); /* Detach IDE and both borrowed media before any free. */
    if (p->boot) { arc_riscpc_boot_free(p->boot); p->boot=NULL; }
    if (p->import) { (void)arc_io_close_file(p->import);p->import=NULL; }
    if (p->disc) { arc_riscpc_hd_close(p->disc);p->disc=NULL; }
    arc_riscpc_cmos_store_close(&p->record);arc_riscpc_cmos_store_close(&p->defaults);
}
static void failed(ArcRpcPreparation *p,const char *s)
{
    if (s && s != p->error) snprintf(p->error,sizeof(p->error),"%s",s);
    if (!p->error[0]) strcpy(p->error,"RiscPC session preparation failed");
    arc_engine_stop();p->state=ARC_RPC_PREP_FAILED;
    notice(p,"",false);notice(p,p->error,true);p->status=NULL;
    /* Keep detached resources until reset/unload: cleanup never combines a
     * second namespace operation with the failing frame's attempt. */
}
static bool copied(char out[1024],const char *in)
{
    int n=snprintf(out,1024,"%s",in?in:"");return n>=0 && n<1024;
}
static void restart(ArcRpcPreparation *p)
{
    memset(&p->record,0,sizeof(p->record));memset(&p->defaults,0,sizeof(p->defaults));
    p->state=ARC_RPC_PREP_VERIFY;p->substep=0;p->verify_at=0;p->crc=0;
    p->lba=0;p->io_pending=false;p->needs_population=p->receipted=false;p->refusal_reported=false;
    p->status=NULL;p->notice_frames=0;p->error[0]=0;
    arc_sha1_init(&p->hash1);arc_sha256_init(&p->hash256);
}
static ArcRpcPreparation *begin(const ArcRpcPreparationRequest *r,
    const ArcRpcPreparationHost *h,bool low,char *e,size_t n)
{
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
    ArcRpcPreparation *p=calloc(1,sizeof(*p));
    if (!p){if(e&&n)snprintf(e,n,"RiscPC preparation allocation failed");return NULL;}
    p->request=*r;p->pacing=8;if(h)p->host=*h;
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
    arc_engine_stop();restart(p);if(e&&n)*e=0;return p;
invalid:
    if(e&&n)snprintf(e,n,"RiscPC preparation allocation or path failed");
    free(p->firmware);free(p);return NULL;
}
ArcRpcPreparation *arc_riscpc_preparation_begin(const ArcRpcPreparationRequest *r,const ArcRpcPreparationHost *h,char *e,size_t n)
{ return begin(r,h,false,e,n); }
ArcRpcPreparation *arc_riscpc_preparation_low_level(const ArcRpcPreparationRequest *r,const ArcRpcPreparationHost *h,char *e,size_t n)
{ return begin(r,h,true,e,n); }
static bool start(ArcRpcPreparation *p,bool factory)
{
    return arc_engine_rpc_start_begin(p->request.key.model,p->firmware,p->request.firmware_size,
        factory?NULL:p->seed,factory?ARC_RPC_CMOS_GENERATION_EPOCH:p->request.epoch,
        factory,p->error,sizeof(p->error));
}
static void record_begin(ArcRpcPreparation *p)
{
    if(arc_riscpc_cmos_store_begin(&p->record,p->save,&p->request.key,p->error,sizeof(p->error))<0)failed(p,p->error);
    else p->state=ARC_RPC_PREP_RECORD;
}
static void after_cmos(ArcRpcPreparation *p)
{
    if(p->needs_population){p->state=ARC_RPC_PREP_POPULATE;p->substep=0;}
    else {p->substep=0;p->state=ARC_RPC_PREP_START;}
}
static void save_record(ArcRpcPreparation *p,const uint8_t payload[240])
{
    if(!arc_riscpc_cmos_store_put_begin(&p->record,payload,NULL,p->error,sizeof(p->error)))failed(p,p->error);
    else p->state=ARC_RPC_PREP_SAVE_RECORD;
}
static bool cmos_namespace(const ArcRpcCmosStore *s)
{ return s->phase==0 || s->phase==12 || s->phase==14 || s->phase==15; }
static bool next_namespace(ArcRpcPreparation *p)
{
    switch(p->state){
    case ARC_RPC_PREP_RECOVER:return p->disc && arc_riscpc_hd_next_namespace(p->disc,0);
    case ARC_RPC_PREP_VALIDATE:return arc_riscpc_boot_next_namespace(p->boot);
    case ARC_RPC_PREP_RECORD:case ARC_RPC_PREP_SAVE_RECORD:return cmos_namespace(&p->record);
    case ARC_RPC_PREP_DEFAULTS:case ARC_RPC_PREP_SAVE_DEFAULT:return cmos_namespace(&p->defaults);
    case ARC_RPC_PREP_POPULATE:
        if(p->boot)return arc_riscpc_boot_next_namespace(p->boot);
        if(p->io_pending)return arc_riscpc_hd_next_namespace(p->disc,1);
        if(p->substep==2)return arc_riscpc_hd_next_namespace(p->disc,2);
        if(p->substep==3)return arc_riscpc_hd_next_namespace(p->disc,3);
        return false;
    default:return false;
    }
}
static void unit(ArcRpcPreparation *p)
{
    bool done=false;
    switch(p->state){
    case ARC_RPC_PREP_VERIFY:{
        size_t take=p->request.firmware_size-p->verify_at;if(take>32768)take=32768;
        const uint8_t *data=p->firmware+p->verify_at;
        /* Only resolved/catalogued requests have CRC/SHA-1 to compare. The
         * explicit low-level entry still derives its complete SHA-256 key. */
        if(p->request.fingerprint){arc_sha1_update(&p->hash1,data,take);p->crc=arc_crc32_append(p->crc,data,take);}
        arc_sha256_update(&p->hash256,data,take);p->verify_at+=take;
        if(p->verify_at!=p->request.firmware_size)break;
        char sha1[41];uint8_t sha256[32];arc_sha1_final(&p->hash1,sha1);arc_sha256_final(&p->hash256,sha256);
        const ArcRpcFirmwareFingerprint *f=p->request.fingerprint;
        if(f && (f->size!=p->request.firmware_size || f->crc32!=p->crc || strcmp(f->sha1,sha1))){
            snprintf(p->error,sizeof(p->error),"RISC OS %s firmware cannot be verified; check <system>/arcdroid/firmware/",
                arc_riscpc_selection_os_name(f->os));failed(p,p->error);break;
        }
        static const char hex[]="0123456789abcdef";
        for(unsigned i=0;i<32;++i){p->request.key.firmware[2*i]=hex[sha256[i]>>4];p->request.key.firmware[2*i+1]=hex[sha256[i]&15];}
        p->request.key.firmware[64]=0;
        p->state=ARC_RPC_PREP_RECOVER;break;
    }
    case ARC_RPC_PREP_RECOVER:{
        if(!p->request.hard_disc){record_begin(p);break;}
        if(!p->disc){p->disc=arc_riscpc_hd_open_begin(p->save,p->request.key.content,p->error,sizeof(p->error));if(!p->disc)failed(p,p->error);break;}
        ArcRpcHdOutcome o=arc_riscpc_hd_open_step(p->disc);
        if(o==ARC_RPC_HD_OPEN_PENDING)break;
        if(o==ARC_RPC_HD_UNSUPPORTED||o==ARC_RPC_HD_CORRUPT||o==ARC_RPC_HD_UNAVAILABLE){failed(p,arc_riscpc_hd_outcome_notice(o));break;}
        if(p->request.populate && o==ARC_RPC_HD_LEGACY){failed(p,"The RiscPC hard disc is without a creation receipt; files have been preserved.");break;}
        if(p->source[0] && o!=ARC_RPC_HD_UNPUBLISHED){failed(p,"RiscPC hd_import refused: published store");break;}
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
                snprintf(p->error,sizeof(p->error),"Boot files at <system>/arcdroid/riscpc/boot/: %s",arc_riscpc_boot_notice(p->boot));failed(p,p->error);
            }else if(p->state==ARC_RPC_PREP_VALIDATE && arc_riscpc_boot_validation_complete(p->boot))record_begin(p);
            else if(s==ARC_RPC_BOOT_DONE){
                ArcRpcBootStats a;arc_riscpc_boot_stats(p->boot,&a);
                if(p->host.log){char line[256];snprintf(line,sizeof(line),"RPC boot packages: HardDisc4=%s PlingSystem=%s files=%llu bytes=%llu",
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
            if(!ok)failed(p,"RPC CMOS seed is unavailable or has the wrong size");
            else save_record(p,p->seed+16);
            break;
        }
        ArcRpcCmosResult r=arc_riscpc_cmos_store_step(&p->record,p->error,sizeof(p->error));
        if(r==ARC_RPC_CMOS_INSPECTING)break;
        if(r<0){failed(p,p->error);break;}
        if(r==ARC_RPC_CMOS_FOUND){
            if(p->record.refused){notice(p,"",false);notice(p,p->error,true);p->refusal_reported=true;p->status=NULL;}
            memset(p->seed,0,16);memcpy(p->seed+16,p->record.payload,240);after_cmos(p);break;}
        if(p->have_seed){save_record(p,p->seed+16);break;}
        if(p->seed_path[0]){p->substep=1;break;}
        ArcRpcCmosKey k=p->request.key;k.defaults=true;k.procedure=ARC_RPC_CMOS_PROCEDURE_REVISION;k.timing=ARC_RPC_CMOS_TIMING_REVISION;
        if(arc_riscpc_cmos_store_begin(&p->defaults,p->save,&k,p->error,sizeof(p->error))<0)failed(p,p->error);
        else p->state=ARC_RPC_PREP_DEFAULTS;
        break;
    }
    case ARC_RPC_PREP_DEFAULTS:{
        ArcRpcCmosResult r=arc_riscpc_cmos_store_step(&p->defaults,p->error,sizeof(p->error));
        if(r==ARC_RPC_CMOS_INSPECTING)break;
        if(r<0){failed(p,p->error);break;}
        if(r==ARC_RPC_CMOS_FOUND){save_record(p,p->defaults.payload);break;}
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
        ++p->stats.generation_quanta;
        uint64_t now;unsigned s;
        arc_engine_rpc_evidence(p->captured,p->evidence.mask,&p->evidence.writes,&p->evidence.last_write,&now,&s);
        if(s==1)break;
        static const unsigned rates[]={20,35,100,35};p->evidence.rate=rates[p->request.key.model];p->evidence.epoch=ARC_RPC_CMOS_GENERATION_EPOCH;
        if(s!=2||now!=ARC_RPC_CMOS_CAPTURE_NS||!arc_riscpc_cmos_oracle(p->request.key.layout,p->captured+16,&p->evidence)){
            failed(p,"RiscPC settings generation failed: required mask, stability or checksum");break;
        }
        arc_engine_stop();
        if(!arc_riscpc_cmos_store_put_begin(&p->defaults,p->captured+16,&p->evidence,p->error,sizeof(p->error)))failed(p,p->error);
        else p->state=ARC_RPC_PREP_SAVE_DEFAULT;
        break;
    }
    case ARC_RPC_PREP_SAVE_DEFAULT:
        if(!arc_riscpc_cmos_store_put_step(&p->defaults,&done,p->error,sizeof(p->error)))failed(p,p->error);
        else if(done)save_record(p,p->defaults.payload);break;
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
        ArcIdeBackend ide={0};if(p->disc)arc_riscpc_hd_backend(p->disc,&ide);
        arc_io_measure_begin();
        bool attached=arc_engine_rpc_attach(p->request.media,p->request.helper,p->disc?&ide:NULL,p->error,sizeof(p->error)) &&
            (!p->host.attach||p->host.attach(p->host.opaque,p->error,sizeof(p->error)));
        if(!attached)arc_engine_stop(); /* Include partial media cleanup in the census. */
        ArcIoMeasurement io=arc_io_measure_end();
        p->stats.attachment_operations=io.operations;p->stats.attachment_bytes=io.bytes;
        p->stats.attachment_namespace=io.namespace_operations;
        if(!attached){failed(p,p->error);break;}
        p->state=ARC_RPC_PREP_RUNNING;if(!p->refusal_reported)notice(p,"",false);p->status=NULL;break;
    }
    default:break;
    }
}
/* Recent maxima belong to a state/unit kind and namespace class. Construction
 * and virtual advances are separate kinds even within GENERATE. Cold admission
 * uses the ceiling; a valid clock replaces it with the last sixteen costs. */
static uint64_t estimate(const ArcRpcPreparation *p,unsigned kind,bool ns,uint64_t ceiling)
{
    uint64_t cost=0;
    for(unsigned i=0;i<ARC_RPC_COST_SAMPLES;++i)if(p->costs[kind][ns][i]>cost)cost=p->costs[kind][ns][i];
    return cost?cost:ceiling;
}
ArcRpcPreparationState arc_riscpc_preparation_frame(ArcRpcPreparation *p)
{
    if(!p || p->state>=ARC_RPC_PREP_RUNNING)return p?p->state:ARC_RPC_PREP_CANCELLED;
    uint64_t start_ns=p->host.clock(p->host.opaque),previous=start_ns,used=0,reserved=0;
    bool generation=p->state==ARC_RPC_PREP_GENERATE,namespace_frame=false,faulty=false;
    p->stats.steps=p->stats.generation_quanta=p->stats.namespace_units=p->stats.attachment_units=0;p->stats.namespace_attempts=0;
    p->stats.attachment_operations=p->stats.attachment_bytes=p->stats.attachment_namespace=0;
    p->stats.budget_ns=generation?ARC_RPC_GENERATION_BUDGET_NS:ARC_RPC_PREP_BUDGET_NS;
    refresh(p,true);
    while(p->state<ARC_RPC_PREP_RUNNING && p->stats.steps<ARC_RPC_PREP_STEPS){
        if((p->state==ARC_RPC_PREP_GENERATE)!=generation)break;
        bool ns=next_namespace(p),attachment=p->state==ARC_RPC_PREP_START && p->substep==2;
        bool advance=generation && p->substep==1;
        unsigned kind=advance?ARC_RPC_PREP_RUNNING:(unsigned)p->state;
        uint64_t ceiling=advance?ARC_RPC_GENERATION_UNIT_NS:ns||attachment?ARC_RPC_PREP_NAMESPACE_NS:ARC_RPC_PREP_UNIT_NS;
        uint64_t budget=generation?ARC_RPC_GENERATION_BUDGET_NS:ns||attachment||namespace_frame?ARC_RPC_PREP_NAMESPACE_NS:ARC_RPC_PREP_BUDGET_NS;
        uint64_t cost=attachment||faulty?ceiling:estimate(p,kind,ns,ceiling);
        /* The final frontend attachment is indivisible, may do several file
         * and namespace callbacks, and never shares a frame with other work. */
        if(attachment && p->stats.steps)break;
        uint64_t charged=faulty?(used>reserved?used:reserved):used;
        if(charged>budget || cost>budget-charged || (advance && p->stats.generation_quanta>=p->pacing))break;
        /* Leave 2.67 ms for pending presentation and cost variation once a
         * frame has begun. A cold indivisible advance retains the full 4 T. */
        if(generation && p->stats.steps && (charged>budget-ARC_RPC_GENERATION_HEADROOM_NS ||
            cost>budget-ARC_RPC_GENERATION_HEADROOM_NS-charged))break;
        ArcRpcHdStats hd_before={0},hd_after={0};if(p->disc)arc_riscpc_hd_stats(p->disc,&hd_before);
        uint64_t cmos_before=p->record.stats.namespace_operations+p->defaults.stats.namespace_operations;
        unit(p);++p->stats.steps;if(ns)++p->stats.namespace_units;reserved+=ceiling;refresh(p,false);
        if(p->disc)arc_riscpc_hd_stats(p->disc,&hd_after);
        uint64_t attempts=hd_after.namespace_operations-hd_before.namespace_operations+
            p->record.stats.namespace_operations+p->defaults.stats.namespace_operations-cmos_before;
        if(attachment){++p->stats.attachment_units;attempts+=p->stats.attachment_namespace;}
        p->stats.namespace_attempts+=attempts;namespace_frame|=ns||attempts!=0;
        p->stats.budget_ns=budget;
        uint64_t now=p->host.clock(p->host.opaque);
        if(now<previous){
            faulty=true;used=UINT64_MAX;
        }else if(now==previous){
            /* Quantized monotonic clocks often repeat during cheap units.
             * Keep the estimate, and fall back only if the whole frame has
             * made no clock progress. A later stuck clock is capped as well. */
            if(!used)faulty=true;
        }else{
            uint64_t actual=now-previous;used=now>=start_ns?now-start_ns:UINT64_MAX;faulty=false;
            p->costs[kind][ns][p->cost_at[kind][ns]++%ARC_RPC_COST_SAMPLES]=actual<ceiling?actual:ceiling;
            if(((attempts||advance) && actual>cost) || actual>ceiling){previous=now;break;}
        }
        previous=now;
        if(attachment || (attempts && !ns))break;
    }
    p->stats.elapsed_ns=used;p->stats.reserved_ns=faulty?reserved:0;++p->stats.frames;
    if(used>p->stats.max_frame_ns)p->stats.max_frame_ns=used;
    p->stats.state=p->state;return p->state;
}
ArcRpcPreparationState arc_riscpc_preparation_state(const ArcRpcPreparation *p)
{ return p?p->state:ARC_RPC_PREP_CANCELLED; }
const char *arc_riscpc_preparation_error(const ArcRpcPreparation *p)
{ return p?p->error:""; }
void arc_riscpc_preparation_stats(const ArcRpcPreparation *p,ArcRpcPreparationStats *s)
{ if(p&&s)*s=p->stats; }
ArcRpcHdStore *arc_riscpc_preparation_disc(ArcRpcPreparation *p)
{ return p?p->disc:NULL; }
bool arc_riscpc_preparation_reset(ArcRpcPreparation *p)
{
    if(!p)return false;
    notice(p,"",false);
    if(p->state==ARC_RPC_PREP_RUNNING){bool ok=arc_engine_reset(p->error,sizeof(p->error));if(!ok)failed(p,p->error);return ok;}
    release(p);restart(p);return true;
}
void arc_riscpc_preparation_cancel(ArcRpcPreparation *p)
{ if(p){notice(p,"",false);release(p);p->state=ARC_RPC_PREP_CANCELLED;p->status=NULL;} }
bool arc_riscpc_preparation_persist(ArcRpcPreparation *p,char *e,size_t n)
{
    if(!p || p->state!=ARC_RPC_PREP_RUNNING || !arc_engine_is_rpc() || !arc_engine_active())return true;
    uint8_t payload[240];snapshot_cmos(payload);
    return arc_riscpc_cmos_store_put(&p->record,payload,NULL,e,n);
}
void arc_riscpc_preparation_maintenance(ArcRpcPreparation *p)
{
    if(!p || p->state!=ARC_RPC_PREP_RUNNING)return;
    if(p->disc){arc_riscpc_hd_tick(p->disc);
        const char *s=arc_riscpc_hd_diagnostic(p->disc);if(s&&p->host.log)p->host.log(p->host.opaque,s);
        s=arc_riscpc_hd_notice(p->disc);if(s)notice(p,s,true);}
    char e[256]={0};
    if(!arc_riscpc_preparation_persist(p,e,sizeof(e)) && !p->refusal_reported){notice(p,e,true);p->refusal_reported=true;}
}
bool arc_riscpc_preparation_free(ArcRpcPreparation *p,char *e,size_t n)
{
    if(!p)return true;
    bool ok=true;
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
    if(!ok && !p->refusal_reported)notice(p,e&&n&&*e?e:"RiscPC settings could not be saved on unload",true);
    notice(p,"",false);release(p);free(p->firmware);free(p);return ok;
}
bool arc_riscpc_preparation_pacing(ArcRpcPreparation *p,unsigned quanta)
{
    if(!p || (quanta!=1 && quanta!=8) || p->state>ARC_RPC_PREP_GENERATE)return false;
    if(p->state==ARC_RPC_PREP_GENERATE){uint8_t b[256],m[32];uint64_t w,l,now;unsigned s;
        arc_engine_rpc_evidence(b,m,&w,&l,&now,&s);if(now)return false;}
    p->pacing=quanta;return true;
}
