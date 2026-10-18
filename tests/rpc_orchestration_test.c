/* Production-source harness: no plan/fault compile definitions. GPL-2.0-only. */
#if defined(ARCDROID_TEST_PLANS) || defined(ARCDROID_TEST_FAULTS)
#error Production orchestration harness must not enable test flags
#endif
#include "rpc_store_owner.h"
#include "rpc_preparation_host.h"
#include "rpc_launch_record_store.h"
#include "rpc_hd_vfs.h"
#include "rpc/rpc_rtc.h"
#include "rpc/rpc_observers.h"
#include "rpc/rpc_floppy.h"
#include "rpc_launch_helper.h"
#include "install_disc.h"
#include "rpc/rpc_namespace.h"
#include "rpcemu/src/arm.h"
#include "rpcemu/src/mem.h"
#include "rpcemu/src/fdc.h"
static unsigned notices,terminal_notices,clears,attach_calls;
static char last_notice[512],last_notice_id[96];
static uint64_t clock_value,clock_delta;
static unsigned clock_mode,clock_reads,all_clock_reads,charged_steps;
static bool measuring_frame;
static unsigned observations,stall_after;
static unsigned spike_at;
static bool work_seen[ARC_RPC_BOOT_WORK_COUNT];
static bool phase_seen[ARC_RPC_BOOT_CANCELLED+1];
static uint64_t oracle[ARC_RPC_PREP_COST_KINDS][2][16];
static unsigned oracle_at[ARC_RPC_PREP_COST_KINDS][2],previous_ns;
static uint64_t frame_budget;
static bool last_clear,attachment_io,attachment_fail,reopen_expected;
static unsigned last_kind;
static ArcRpcPreparation *current;
static ArcRpcFrontendHost *frontend_test_host;
static bool use_frontend_host,host_extended;
static char host_display[512];
static unsigned host_duration,host_frames,host_n18_unsaved;
static unsigned host_messages,host_clears,host_logs,host_n13,host_unload,host_marker,host_n17;
static bool measure_summaries;
static int fast_answer=-1;
static unsigned summary_count;
static char measured_summaries[2][4096];
static bool host_environment(unsigned command,void *data)
{
    if(command==RETRO_ENVIRONMENT_GET_FASTFORWARDING && fast_answer>=0){*(bool *)data=fast_answer!=0;return true;}
    if(command==RETRO_ENVIRONMENT_SET_MESSAGE_EXT){
        struct retro_message_ext *m=data;assert(m->target==RETRO_MESSAGE_TARGET_OSD);
        if(!*m->msg)assert(m->priority>=3);
        if(!host_extended)return false;
        assert(m->duration==1 || m->duration==2000 || m->duration==5000 || m->duration==UINT_MAX);
        host_duration=m->duration;snprintf(host_display,sizeof(host_display),"%s",m->msg);++host_messages;if(!*m->msg)++host_clears;return true;
    }
    if(command==RETRO_ENVIRONMENT_SET_MESSAGE){
        struct retro_message *m=data;assert(!host_extended);
        assert(m->frames==1 || m->frames==120 || m->frames==300);
        host_frames=m->frames;snprintf(host_display,sizeof(host_display),"%s",m->msg);++host_messages;if(!*m->msg)++host_clears;return true;
    }
    return false;
}
static void host_log(void *opaque,enum retro_log_level level,const char *text)
{
    (void)opaque;(void)level;assert(strchr(text,':'));assert(!strstr(text,"PRIVATE-SENTINEL"));++host_logs;
    if(!strncmp(text,"N-18 unsaved:",13))++host_n18_unsaved;
    if(!strncmp(text,"N-13:",5))++host_n13;if(!strncmp(text,"unload-cmos:",12))++host_unload;if(!strncmp(text,"unload-marker:",14))++host_marker;
    if(!strncmp(text,"N-17 ",5)){assert(host_clears);++host_n17;}
    if(measure_summaries && !strncmp(text,"summary:",8)){assert(summary_count<2);snprintf(measured_summaries[summary_count++],4096,"%s",text);}
}

static unsigned test_model;
static bool helper_override;
static bool populate_request=true;
static const ArcEngineMedia *explicit_helper;
static unsigned revision1_notices;
static bool launch_test,reset_probe;
static bool guest_cells,guest_subject;
static const char *launch_product;
static unsigned reset_failure,reset_steps;
static uint64_t injected_session=1000;
static bool reset_drive0(void *opaque,char *error,size_t n)
{(void)opaque;(void)error;(void)n;
 if(reset_probe){assert(!arc_riscpc_preparation_execution_allowed(current));
    char helper[11],disc[11];assert(arc_rpc_observers_identities(helper,disc) && !*helper);
    assert(reset_steps++==0);if(reset_failure==1)return false;}
 return true;}
static bool reset_rebind(void *opaque,char *error,size_t n)
{(void)opaque;
 if(reset_probe){assert(!arc_riscpc_preparation_execution_allowed(current));assert(reset_steps++==1);if(reset_failure==2)return false;}
 return arc_engine_reset(error,n);}
static unsigned stopped_calls;
static bool stopped_failure;
static bool restage_stopped(void *opaque,char *error,size_t n)
{
    (void)opaque;++stopped_calls;
    assert(!arc_engine_active() && !arc_riscpc_preparation_execution_allowed(current));
    test_vfs_no_open_handles();
    unsigned owners=0;for(unsigned i=0;i<16;++i)owners+=rpc_fixture_locks[i].used;
    assert(owners==(unsigned)arc_riscpc_preparation_owned(current));
    assert(arc_riscpc_preparation_replace_media(current,NULL));
    assert(arc_riscpc_preparation_helper_write_policy(current,explicit_helper?explicit_helper->writable:true));
    if(stopped_failure){snprintf(error,n,"The disc could not be reloaded");return false;}
    if(error&&n)*error=0;return true;
}
static uint64_t helper_construction_bytes;
static ArcRpcSelectionOs test_os=ARC_RPC_OS_350;
static const char *id="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
static uint64_t clock_read(void *opaque)
{
    (void)opaque;++all_clock_reads;
    /* Scheduler clocks assign a cost to each completed unit. Identity timing
     * probes inside that unit observe its clock, not a second simulated unit. */
    if(measuring_frame && clock_mode>=3 && clock_reads){
        ArcRpcPreparationStats stats;arc_riscpc_preparation_stats(current,&stats);
        if(stats.steps==charged_steps)return clock_value;
        charged_steps=stats.steps;
    }
    if(clock_mode==1)return 0;
    if(clock_mode==2)return clock_value--;
    if(clock_mode==3){
        if(clock_reads++){ArcRpcPreparationStats s;arc_riscpc_preparation_stats(current,&s);clock_value+=s.budget_ns;}
        return clock_value;
    }
    if(clock_mode==10){
        if(clock_reads++ && (clock_reads==2 || clock_reads%8==0))clock_value+=1000;
        return clock_value;
    }
    if(clock_mode>=11){
        if(clock_reads++){
            ArcRpcPreparationStats s;arc_riscpc_preparation_stats(current,&s);
            uint64_t cost=50000;
            if(clock_mode==11 && frame_budget==ARC_RPC_GENERATION_BUDGET_NS)cost=13000000;
            if(clock_mode==12 && s.unit_kind==ARC_RPC_PREP_VERIFY){
                ++observations;if(observations==1)cost=2000000;
                else if(observations==2)assert(s.estimated_ns==1000000);
            }
            if(clock_mode==13 && s.unit_kind==ARC_RPC_PREP_VERIFY){
                ++observations;
                assert(s.estimated_ns==(observations==1?1000000:observations<=17?900000:50000));
                if(observations==1)cost=900000;
            }
            if(clock_mode==14){
                bool ns=s.namespace_units>previous_ns;previous_ns=s.namespace_units;
                uint64_t expected=0;
                for(unsigned i=0;i<16;++i)if(oracle[s.unit_kind][ns][i]>expected)expected=oracle[s.unit_kind][ns][i];
                uint64_t ceiling=ns||s.attachment_units?8333333:1000000;
                if(!expected || expected>ceiling)expected=ceiling;
                if(s.attachment_units)expected=ceiling;
                assert(s.estimated_ns==expected);
                if(s.unit_kind>=ARC_RPC_PREP_CANCELLED && s.unit_kind<ARC_RPC_PREP_PRODUCT_KIND){
                    unsigned unit=s.unit_kind-ARC_RPC_PREP_CANCELLED;
                    unsigned work=unit%ARC_RPC_BOOT_WORK_COUNT,phase=unit/ARC_RPC_BOOT_WORK_COUNT;
                    assert(phase<=ARC_RPC_BOOT_CANCELLED);work_seen[work]=true;phase_seen[phase]=true;
                    cost=work==ARC_RPC_BOOT_WORK_HASH?800000:work==ARC_RPC_BOOT_WORK_FORMAT?900000:
                         work==ARC_RPC_BOOT_WORK_WRITE?100000:work==ARC_RPC_BOOT_WORK_READ?70000:50000;
                }
                oracle[s.unit_kind][ns][oracle_at[s.unit_kind][ns]++%16]=cost;
            }
            if(clock_mode==15 && ++observations>stall_after)cost=0;
            if(clock_mode==16){
                ArcRpcPreparationStats s;arc_riscpc_preparation_stats(current,&s);
                if(!spike_at && s.unit_kind==ARC_RPC_PREP_RECOVER && s.namespace_attempts>previous_ns &&
                    s.estimated_ns<=100000 && s.steps<80){cost=500000;spike_at=s.steps;}
                previous_ns=(unsigned)s.namespace_attempts;
            }
            clock_value+=cost;
        }
        return clock_value;
    }
    if(clock_mode>=4){
        if(clock_reads++){
            uint64_t cost=clock_mode==4?50000:clock_mode==5?750000:
                clock_mode==6?100000:clock_mode==9?1:
                frame_budget==ARC_RPC_GENERATION_BUDGET_NS?(clock_mode==7?16000000:2000000):50000;
            if(clock_mode==6){ArcRpcPreparationStats s;arc_riscpc_preparation_stats(current,&s);if(s.namespace_units==2)cost=500000;}
            clock_value+=cost;
        }
        return clock_value;
    }
    clock_value+=clock_delta;return clock_value;
}
static void notice(void *opaque,const char *s,ArcRpcNotice event)
{
    bool terminal=event.severity==ARC_RPC_NOTICE_TERMINAL;
    (void)opaque;++notices;if(strstr(s,"startup revision 1"))++revision1_notices;if(reopen_expected && strstr(s,"first use")){
        ArcRpcPreparationState state=arc_riscpc_preparation_state(current);
        assert(state>=ARC_RPC_PREP_DEFAULTS && state<=ARC_RPC_PREP_SAVE_RECORD);
        assert(!strcmp(s,"Preparing RiscPC settings for first use")); /* A-6 delta 3. */
    }
    if(terminal){assert(*s);++terminal_notices;last_kind=2;}
    else last_kind=*s?1:0;
    last_clear=!s[0];if(last_clear)++clears;
    snprintf(last_notice,sizeof(last_notice),"%s",s);snprintf(last_notice_id,sizeof(last_notice_id),"%s",event.id);
}
static void observer_attached(void *opaque)
{
    (void)opaque;
    if(guest_subject){arc_rpc_observers_bind(guest_cells?(test_model==0?1:4):0,test_model);return;}
    /* Flag-free harness reaches the production arms/snapshots. No override
     * exists in this executable; every unqualified cell refuses authority. */
    arc_engine_boot_watch_arm();assert(arc_engine_boot_watch_state()==ARC_BOOT_WATCH_DISABLED);
    assert(!arc_engine_launch_outcome_arm("Run Authored"));
    ArcLaunchOutcome launch;arc_engine_launch_outcome_snapshot(&launch);assert(launch.state==ARC_OUTCOME_OFF);
    ArcErrorBox box;arc_engine_error_snapshot(&box);assert(!box.recognised_open && box.modal_unknown);
}
static bool attach(void *opaque,char *e,size_t n)
{
    (void)opaque;(void)e;(void)n;++attach_calls;
    uint8_t b[256],m[32];uint64_t w,l,now;unsigned state;
    arc_engine_rpc_evidence(b,m,&w,&l,&now,&state);
    ArcRpcCalendar actual,expected;assert(arc_rpc_rtc_read(&actual));
    assert(arc_rpc_calendar(1234567890,0,&expected));assert(!memcmp(&actual,&expected,sizeof(actual)));
    assert(now==0 && state==0); /* No requested instruction before final attachment. */
    ArcRpcHdStore *disc=arc_riscpc_preparation_disc(current);
    assert(disc && arc_riscpc_hd_receipt(disc,NULL));
    char helper[11],hd[11];assert(arc_rpc_observers_identities(helper,hd));
    ArcRpcHdReceipt r;assert(arc_riscpc_hd_receipt(disc,&r));
    assert(!strcmp(helper,explicit_helper?"Explicit":helper_override||!populate_request||r.authored_revision==1?"":"ArcDroid"));
    if(attachment_io || attachment_fail){
        void *data=NULL;size_t bytes=0;
        if(!arc_io_read_all_limit(attachment_fail?"/missing-media":"/system/attachment.raw",&data,&bytes,5,NULL)){
            snprintf(e,n,"final media unavailable");return false;
        }
        bool ok=arc_io_write_all("/save/media-view",data,bytes);free(data);if(!ok)return false;
    }
    return true;
}
static void file_bytes(const char *p,const void *data,size_t n)
{
    int i=find(p,true);assert(i>=0);free(files[i].data);
    files[i].data=malloc(n);assert(files[i].data);memcpy(files[i].data,data,n);
    files[i].size=files[i].capacity=n;
}
static bool create_sentinel(const char *path)
{
    const char *leaf=strrchr(path,'/');assert(leaf && (strstr(leaf,"/c-")==leaf || strstr(leaf,"/d-")==leaf) && strstr(leaf,".lock"));
    int i=find(path,true);if(i<0)return false;
    assert(!files[i].data || files[i].size==0);
    if(!files[i].data){files[i].data=malloc(1);if(!files[i].data)return false;files[i].size=files[i].capacity=0;}
    return true;
}
static void file_copy(const char *p,const char *source)
{
    FILE *f=fopen(source,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long n=ftell(f);assert(n>0);rewind(f);
    uint8_t *b=malloc((size_t)n);assert(b&&fread(b,1,(size_t)n,f)==(size_t)n);assert(!fclose(f));file_bytes(p,b,(size_t)n);free(b);
}
static void disk_hash(uint8_t output[32])
{
    ArcSha256 h;arc_sha256_init(&h);
    /* Canonical lexical order, independent of allocation/publication ordinals. */
    const char *previous="";
    for(;;){int best=-1;
        for(unsigned i=0;i<TEST_FILES;++i)if(files[i].present && strstr(files[i].path,"/save/arcdroid/") &&
            !strstr(files[i].path,".tmp") && !strstr(files[i].path,"/locks-riscpc/") && strcmp(files[i].path,previous)>0 &&
            (best<0||strcmp(files[i].path,files[best].path)<0))best=(int)i;
        if(best<0)break;
        previous=files[best].path;arc_sha256_update(&h,previous,strlen(previous));
        arc_sha256_update(&h,files[best].data,files[best].size);
    }
    arc_sha256_final(&h,output);
}
static ArcRpcPreparation *new_request(const uint8_t *rom,size_t size,bool seed,
    bool fingerprint,ArcRpcPreparationHost host)
{
    rpc_fixture_create_sentinel=create_sentinel;
    ArcRpcPreparationRequest r={0};r.firmware=rom;r.firmware_size=size;r.key.model=test_model;r.key.layout=test_os==ARC_RPC_OS_350?1:test_os==ARC_RPC_OS_360?2:3;
    strcpy(r.key.content,id);r.save="/save";r.system="/system";r.epoch=1234567890;
    r.hard_disc=true;r.populate=populate_request;r.helper_override=helper_override;r.helper=explicit_helper;
    ArcRpcLaunchPlan launch={.action=ARC_RPC_PLAN_BOOT,.media={1}};strcpy(launch.target,"ADFS::0.$.!Boot");
    if(launch_product){launch.action=ARC_RPC_PLAN_PRODUCT;snprintf(launch.target,sizeof(launch.target),"%s",launch_product);}
    if(launch_test){r.launch=&launch;r.session_id=++injected_session;}
    /* PRM CMOS allocation (RO3.6), logical 5/11/16/135;
     * core/cmos_defaults.c documents n+0x40 wrapping past 255 to 16.
     * https://www.riscos.com/support/developers/prm/cmos.html
     * NetFS, ADFS drive 2, NoBoot, altered floppy/IDE counts; extra raw
     * 19/253/checksum bytes remain deliberately authoritative too. */
    uint8_t bytes[256]={0};memset(bytes,0xee,16);bytes[69]=5;bytes[75]=2;bytes[80]=0;
    bytes[19]=0;bytes[253]=9;bytes[199]=0x95;bytes[63]=0x51;
    if(seed)r.seed=bytes;
    ArcDigest d=arc_digest(rom,size);
    ArcRpcFirmwareFingerprint f={test_os,size,d.crc32,d.sha1};
    if(fingerprint){r.fingerprint=&f;r.selection=(ArcRpcSelection){.reason=ARC_RPC_SELECTION_SELECTED,.model=(ArcRpcSelectionModel)(test_model+1),.os=test_os};}
    char error[512]={0};
    host.restage_drive0=reset_drive0;host.reset_rebind=reset_rebind;host.restage_stopped=restage_stopped;
    if(use_frontend_host){
        ArcRpcFrontendOps ops={.kind=ARC_SESSION_KIND_RISCPC,.module=host,.environment=host_environment,.log=host_log};
        frontend_test_host=arc_riscpc_host_begin(&r,&ops,error,sizeof(error));
        current=arc_riscpc_host_preparation(frontend_test_host);
    }else current=fingerprint?arc_riscpc_preparation_begin(&r,&host,error,sizeof(error)):
                        arc_riscpc_preparation_low_level(&r,&host,error,sizeof(error));
    assert(current && !error[0]);return current;
}
static ArcRpcPreparationState frame(ArcRpcPreparation *p)
{
    unsigned clear_before=clears;
    uint64_t ns=namespace_calls;ArcRpcPreparationState old=arc_riscpc_preparation_state(p);
    clock_reads=previous_ns=0;frame_budget=old==ARC_RPC_PREP_GENERATE?UINT64_C(66666664):UINT64_C(13333333);
    measuring_frame=true;charged_steps=0;
    ArcRpcPreparationState state=arc_riscpc_preparation_frame(p);ArcRpcPreparationStats s;
    measuring_frame=false;
    arc_riscpc_preparation_stats(p,&s);
    if(state==ARC_RPC_PREP_FAILED)assert(clears==clear_before);
    helper_construction_bytes+=s.helper_bytes;
    assert(s.steps<=ARC_RPC_PREP_STEPS);
    assert(s.namespace_attempts==namespace_calls-ns);
    /* The unit first detecting a stall was admitted by its previous estimate;
     * its declared cost can consume the remaining allowance. No next unit fits. */
    assert(s.reserved_ns<=frame_budget+(old==ARC_RPC_PREP_GENERATE?UINT64_C(66666664):UINT64_C(8333333)));
    assert(s.budget_ns==frame_budget); /* Every offline kind gets 0.80 T. */
    assert(s.generation_quanta<=8);
    if(old==ARC_RPC_PREP_GENERATE && state==ARC_RPC_PREP_GENERATE && arc_engine_active()){
        ArcRpcCalendar actual,expected;uint8_t b[256],m[32];uint64_t w,l,now;unsigned capture;
        arc_engine_rpc_evidence(b,m,&w,&l,&now,&capture);
        assert(arc_rpc_rtc_read(&actual));assert(arc_rpc_calendar(ARC_RPC_CMOS_GENERATION_EPOCH,now,&expected));
        assert(!memcmp(&actual,&expected,sizeof(actual)));
        assert(now%ARC_RPC_GENERATION_QUANTUM_NS==0);
    }
    if(clock_mode==3 || clock_mode>=4)assert(s.elapsed_ns<=s.budget_ns);
    if(state!=ARC_RPC_PREP_RUNNING && old!=ARC_RPC_PREP_GENERATE && arc_engine_active()){
        uint8_t b[256],m[32];uint64_t w,l,now;unsigned capture;
        arc_engine_rpc_evidence(b,m,&w,&l,&now,&capture);
        assert(now==0 && ((state==ARC_RPC_PREP_GENERATE && capture==1) || (state==ARC_RPC_PREP_START && capture==0)));
    }
    return state;
}
static void settle(ArcRpcPreparation *p)
{
    for(unsigned i=0;arc_riscpc_preparation_state(p)<ARC_RPC_PREP_RUNNING;++i){assert(i<100000);frame(p);}
}
static void finish(ArcRpcPreparation *p)
{
    unsigned clears_before=clears;char e[512]={0};assert(arc_riscpc_preparation_free(p,e,sizeof(e)));
    current=NULL;assert(clears==clears_before);assert(!arc_engine_active());test_vfs_no_open_handles();
}
static void state_matrix(const uint8_t *rom,size_t size,ArcRpcPreparationHost host,bool sanitizer,bool mutant)
{
    for(unsigned action=0;action<3;++action)
        for(unsigned target=0;target<ARC_RPC_PREP_RUNNING;++target){
            if(mutant && target>=ARC_RPC_PREP_SAVE_DEFAULT)continue;
            restore();attach_calls=0;terminal_notices=0;clock_mode=3;clock_delta=2500000;
            ArcRpcPreparation *p=new_request(rom,size,sanitizer && target>=ARC_RPC_PREP_SAVE_RECORD,false,host);
            /* One unit/frame makes every transition observable. */
            assert(arc_riscpc_preparation_pacing(p,1));
            for(unsigned i=0;arc_riscpc_preparation_state(p)!=target;++i){assert(i<100000);frame(p);assert(arc_riscpc_preparation_state(p)<ARC_RPC_PREP_RUNNING);}
            assert(!attach_calls);
            if(target==ARC_RPC_PREP_GENERATE && action==0){
                unsigned old_notices=notices;
                for(unsigned j=0;j<121;++j)frame(p);
                assert(notices>=old_notices+2 && notices<=old_notices+3 && !terminal_notices);
            }
            if(target==ARC_RPC_PREP_RECORD || target==ARC_RPC_PREP_GENERATE){
                ArcRpcHdStats a;arc_riscpc_hd_stats(arc_riscpc_preparation_disc(p),&a);
                assert(a.population && !a.published && !a.commands);
            }
            if(action==0){arc_riscpc_preparation_cancel(p);assert(arc_riscpc_preparation_state(p)==ARC_RPC_PREP_CANCELLED);}
            else if(action==1){unsigned calls=stopped_calls,clear_before=clears;assert(arc_riscpc_preparation_reset(p));assert(stopped_calls==calls+1 && clears==clear_before);assert(arc_riscpc_preparation_state(p)==ARC_RPC_PREP_VERIFY);assert(!arc_engine_active());}
            finish(p);
            /* Reset and unload preserve completed publications, and recovery
             * cleans only the offline candidate; no partial root can attach. */
            char error[512];ArcRpcHdStore *disc=arc_riscpc_hd_open_begin("/save",id,rpc_store_test_owner(),error,sizeof(error));assert(disc);
            ArcRpcHdOutcome outcome;do{outcome=arc_riscpc_hd_open_step(disc);}while(outcome==ARC_RPC_HD_OPEN_PENDING);
            assert(outcome==ARC_RPC_HD_UNPUBLISHED || outcome==ARC_RPC_HD_RECEIPTED);arc_riscpc_hd_close(disc,false);
            if(sanitizer && target==ARC_RPC_PREP_SAVE_DEFAULT)fprintf(stdout,"sanitizer: generation publication boundary exercised\n");
        }
}
/* Delta 22 adds isolated lock units ahead of the existing scheduler. Preserve
 * all previous batching oracles after admission; test admission separately. */
static void admit_content(ArcRpcPreparation *p)
{
    for(unsigned i=0;!arc_riscpc_preparation_owned(p);++i){
        assert(i<32);frame(p);ArcRpcPreparationStats s;arc_riscpc_preparation_stats(p,&s);
        assert(s.steps==1 && s.unit_kind==ARC_RPC_PREP_LOCK_KIND);
    }
    observations=clock_reads=0;
}
static void scheduler_matrix(const uint8_t *rom,size_t size,ArcRpcPreparationHost host)
{
    uint8_t gold[32];attachment_io=true;
    for(unsigned index=0;index<4;++index){
        unsigned mode=index==3?10:index==2?9:index+4;
        restore();clock_mode=mode;clock_value=0;attach_calls=terminal_notices=0;
        ArcRpcPreparation *p=new_request(rom,size,true,false,host);
        unsigned max_steps=0,max_namespaces=0;bool ordinary_above_old=false,namespace_above_old=false;
        for(unsigned i=0;arc_riscpc_preparation_state(p)<ARC_RPC_PREP_RUNNING;++i){
            assert(i<100000);frame(p);ArcRpcPreparationStats s;arc_riscpc_preparation_stats(p,&s);
            /* Identity derivation loads the boot and map chunks once at attach:
             * each verified chunk is open/size/three reads/close, 128-byte
             * header + 256 KiB payload + 4-byte trailer. Frame queries do no I/O. */
            if(s.attachment_units)assert(s.steps==1 && s.attachment_operations==8+2*6 &&
                s.attachment_bytes==10+2*(128+ARC_RPC_HD_CHUNK+4) && s.attachment_namespace==1);
            if(s.steps>max_steps)max_steps=s.steps;if(s.namespace_attempts>max_namespaces)max_namespaces=(unsigned)s.namespace_attempts;
            if(!s.namespace_units && !s.attachment_units && s.elapsed_ns>UINT64_C(3333333))ordinary_above_old=true;
            if(s.namespace_attempts && s.elapsed_ns>UINT64_C(8333333))namespace_above_old=true;
        }
        assert(arc_riscpc_preparation_state(p)==ARC_RPC_PREP_RUNNING);
        if(mode==4)assert(max_steps>=50 && max_namespaces>=2);
        else if(mode>=9)assert(max_steps>=50);
        else assert(max_steps==16 && ordinary_above_old && namespace_above_old); /* floor(12.5 ms / 0.75 ms) */
        uint8_t hash[32];disk_hash(hash);if(mode==4)memcpy(gold,hash,32);else assert(!memcmp(gold,hash,32));
        finish(p);
    }
    attachment_io=false;
    restore();clock_mode=9;clock_value=0;
    int cap=find("/system/cap.zip",false);assert(cap>=0);
    file_bytes("/system/arcdroid/riscpc/boot/HardDisc4.zip",files[cap].data,files[cap].size);
    ArcRpcPreparation *large=new_request(rom,size,true,false,host);bool capped=false;
    for(unsigned i=0;i<128 && !capped;++i){
        assert(frame(large)<ARC_RPC_PREP_RUNNING);ArcRpcPreparationStats stats;arc_riscpc_preparation_stats(large,&stats);
        capped=stats.steps==ARC_RPC_PREP_STEPS;
        if(capped)assert(stats.elapsed_ns==ARC_RPC_PREP_STEPS);
    }
    assert(capped);arc_riscpc_preparation_cancel(large);finish(large);
    restore();clock_mode=6;clock_value=0;terminal_notices=0;
    ArcRpcPreparation *p=new_request(rom,size,true,false,host);admit_content(p);
    ArcRpcPreparationStats s;
    do{frame(p);arc_riscpc_preparation_stats(p,&s);}while(!s.namespace_units);
    /* Delta 22: lock costs have their own kind. They no longer warm the
     * VERIFY/RECOVER namespace estimator; assert the exact cold batch. */
    assert(s.namespace_units==7 && s.steps==121 && s.elapsed_ns==12500000 && s.budget_ns==UINT64_C(13333333));
    arc_riscpc_preparation_cancel(p);finish(p);
    /* The lock kind no longer warms RECOVER. Warm its actual namespace
     * estimator, then inject a below-ceiling spike with ample frame budget.
     * The underestimated namespace unit must be the frame's last unit. */
    restore();clock_mode=16;clock_value=0;spike_at=0;p=new_request(rom,size,true,false,host);admit_content(p);
    for(unsigned i=0;!spike_at;++i){assert(i<1000);frame(p);arc_riscpc_preparation_stats(p,&s);}
    assert(s.steps==spike_at && s.estimated_ns<=100000);
    arc_riscpc_preparation_cancel(p);finish(p);
    /* The declared-cost fallback caps a stationary clock at twelve ordinary
     * units. A backward clock admits at most the first indivisible unit. */
    for(unsigned mode=1;mode<=2;++mode){
        restore();clock_mode=mode;clock_value=1000000;
        p=new_request(rom,size,true,false,host);admit_content(p);frame(p);arc_riscpc_preparation_stats(p,&s);
        assert(s.steps==(mode==1?12u:1u));arc_riscpc_preparation_cancel(p);finish(p);
    }
    restore();clock_mode=3;clock_value=0;
    p=new_request(rom,size,true,false,host);admit_content(p);
    while(arc_riscpc_preparation_state(p)!=ARC_RPC_PREP_RECOVER)frame(p);
    while(!arc_riscpc_preparation_disc(p))frame(p);
    clock_mode=1;frame(p);arc_riscpc_preparation_stats(p,&s);
    assert(s.namespace_units==1 && s.reserved_ns==UINT64_C(8333333)+(s.steps-1)*UINT64_C(1000000));
    arc_riscpc_preparation_cancel(p);finish(p);
    for(unsigned mode=12;mode<=15;++mode){
        restore();clock_mode=mode;clock_value=0;observations=0;stall_after=1;
        memset(oracle,0,sizeof(oracle));memset(oracle_at,0,sizeof(oracle_at));memset(work_seen,0,sizeof(work_seen));
        memset(phase_seen,0,sizeof(phase_seen));
        p=new_request(rom,size,true,false,host);admit_content(p);frame(p);arc_riscpc_preparation_stats(p,&s);
        if(mode==12){assert(s.steps==1 && s.elapsed_ns==2000000);frame(p);assert(observations>1);}
        if(mode==13)assert(observations>=18);
        if(mode==14){settle(p);for(unsigned work=ARC_RPC_BOOT_WORK_INFLATE;work<ARC_RPC_BOOT_WORK_COUNT;++work)assert(work_seen[work]);
            assert(phase_seen[ARC_RPC_BOOT_FORMAT] && phase_seen[ARC_RPC_BOOT_WRITE] && phase_seen[ARC_RPC_BOOT_VALIDATE] && phase_seen[ARC_RPC_BOOT_SEAL]);}
        if(mode==15){assert(s.steps==13 && s.elapsed_ns==50000 && s.reserved_ns==12050000);
        }
        arc_riscpc_preparation_cancel(p);finish(p);
    }
    restore();clock_mode=12;observations=0;clock_value=0;
    p=new_request(rom,size,true,false,host);admit_content(p);frame(p);
    clock_mode=15;observations=0;stall_after=20;frame(p);arc_riscpc_preparation_stats(p,&s);
    assert(s.steps==31 && s.elapsed_ns==1000000 && s.reserved_ns==12000000);
    arc_riscpc_preparation_cancel(p);finish(p);
    restore();clock_mode=4;attachment_fail=true;terminal_notices=0;
    p=new_request(rom,size,true,false,host);settle(p);arc_riscpc_preparation_stats(p,&s);
    assert(arc_riscpc_preparation_state(p)==ARC_RPC_PREP_FAILED && terminal_notices==1 && s.attachment_operations==1+2*6 && s.attachment_bytes==2*(128+ARC_RPC_HD_CHUNK+4) && !s.attachment_namespace && s.steps==1);
    finish(p);attachment_fail=false;
    for(unsigned index=0;index<3;++index)for(unsigned one=0;one<2;++one){
        unsigned mode=index==2?11:7+index;
        restore();clock_mode=3;clock_value=0;terminal_notices=0;
        p=new_request(rom,size,false,false,host);
        if(one)assert(arc_riscpc_preparation_pacing(p,1));
        while(arc_riscpc_preparation_state(p)!=ARC_RPC_PREP_GENERATE)frame(p);
        clock_mode=mode;
        while(!arc_engine_active())frame(p);
        frame(p);arc_riscpc_preparation_stats(p,&s);
        assert(s.generation_quanta==(one?1u:mode==8?8u:4u));
        assert(s.elapsed_ns<=ARC_RPC_GENERATION_BUDGET_NS);
        if(mode==8 && !one){unsigned before=notices;
            for(unsigned i=0;i<59;++i)frame(p);
            assert(notices-before<=1); /* Cadence is frames, never unit count. */
        }
        arc_riscpc_preparation_cancel(p);finish(p);
    }
}
static struct retro_vfs_file_handle *unavailable_manifest(const char *path,unsigned mode,unsigned hint)
{ if(strstr(path,"hd-riscpc") && strstr(path,"/manifest."))return NULL;return test_open(path,mode,hint); }
static int64_t unavailable_hd_write(struct retro_vfs_file_handle *f,const void *data,uint64_t n)
{if(strstr(files[f->file].path,"/hd-riscpc/"))return -1;return test_write(f,data,n);}
static void hd_notices(const uint8_t *rom,size_t size,ArcRpcPreparationHost host)
{
    for(unsigned ro=0;ro<2;++ro){
        restore();clock_mode=4;clock_value=0;terminal_notices=0;
        ArcRpcPreparation *p=new_request(rom,size,true,false,host);settle(p);finish(p);
        unsigned journals=0;
        for(unsigned i=0;i<TEST_FILES;++i)if(files[i].present && strstr(files[i].path,"/journal.")){
            size_t n=files[i].size;files[i].data=realloc(files[i].data,n+7);assert(files[i].data);
            memset(files[i].data+n,0x55,7);files[i].capacity=files[i].size=n+7;++journals;
        }
        assert(journals);refuse_truncate=ro!=0;
        p=new_request(rom,size,true,false,host);settle(p);
        assert(arc_riscpc_preparation_state(p)==ARC_RPC_PREP_RUNNING);
        ArcRpcHdStats s;arc_riscpc_hd_stats(arc_riscpc_preparation_disc(p),&s);assert(s.read_only==(ro!=0));
        unsigned n=notices;arc_riscpc_preparation_maintenance(p);
        assert(notices==n+1 && !terminal_notices && last_kind==1 && strstr(last_notice,"interrupted hard-disc write was recovered"));
        if(!ro){
            vfs.write=unavailable_hd_write;uint8_t sector[512]={0x42};
            assert(!arc_riscpc_hd_write(arc_riscpc_preparation_disc(p),100,1,sector));vfs.write=test_write;
            arc_riscpc_preparation_maintenance(p);
            assert(terminal_notices==1 && last_kind==2 && strstr(last_notice,"can no longer be saved"));
            n=notices;arc_riscpc_preparation_maintenance(p);assert(notices==n);
        }
        refuse_truncate=false;finish(p);
    }
}
static void store_refusals(const uint8_t *rom,size_t size,ArcRpcPreparationHost host)
{
    assert(strstr(arc_riscpc_hd_outcome_notice(ARC_RPC_HD_LEGACY),"without a creation receipt"));
    const char *messages[]={"newer version","damaged","unavailable","without a creation receipt"};
    for(unsigned kind=0;kind<4;++kind){
        restore();clock_mode=4;clock_value=0;terminal_notices=attach_calls=0;
        if(kind==3){
            char e[512];ArcRpcHdStore *disc=arc_riscpc_hd_open("/save",id,true,rpc_store_test_owner(),e,sizeof(e));assert(disc);
            assert(!arc_riscpc_hd_receipt(disc,NULL));arc_riscpc_hd_close(disc,false);
        }else{
            ArcRpcPreparation *good=new_request(rom,size,true,false,host);settle(good);finish(good);
        }
        if(kind<2)for(unsigned i=0;i<TEST_FILES;++i)if(files[i].present && strstr(files[i].path,"/manifest.")){
            if(kind==0){files[i].data[8]=3;uint32_t crc=arc_crc32(files[i].data,files[i].size-4);
                for(unsigned j=0;j<4;++j)files[i].data[files[i].size-4+j]=(uint8_t)(crc>>(8*j));}
            else files[i].data[files[i].size-1]^=1;
        }
        if(kind==2)vfs.open=unavailable_manifest;
        terminal_notices=attach_calls=0;uint8_t before[32],after[32];disk_hash(before);
        ArcRpcPreparation *p=new_request(rom,size,true,false,host);settle(p);
        assert(arc_riscpc_preparation_state(p)==ARC_RPC_PREP_FAILED && terminal_notices==1 && !attach_calls && !arc_engine_active());
        assert(last_kind==2 && strstr(last_notice,messages[kind]));
        disk_hash(after);assert(!memcmp(before,after,32));unsigned n=notices;
        for(unsigned i=0;i<180;++i)arc_riscpc_preparation_frame(p);assert(notices==n);
        finish(p);vfs.open=test_open;
    }
    /* Corrupt per-content CMOS is preserved, read-only, and its terminal
     * notice must survive the transition to a running requested session. */
    restore();clock_mode=4;ArcRpcPreparation *p=new_request(rom,size,true,false,host);settle(p);finish(p);
    bool altered=false;for(unsigned i=0;i<TEST_FILES;++i)if(files[i].present && strstr(files[i].path,"/cmos-riscpc/") && files[i].size==512){
        char corrupt[1100];snprintf(corrupt,sizeof(corrupt),"%s",files[i].path);corrupt[strlen(corrupt)-1]='0';
        file_bytes(corrupt,files[i].data,512);int bad=find(corrupt,false);assert(bad>=0);
        files[bad].data[511]^=1;altered=true;break;
    }
    assert(altered);uint8_t before[32],after[32];disk_hash(before);terminal_notices=0;
    p=new_request(rom,size,true,false,host);settle(p);
    assert(arc_riscpc_preparation_state(p)==ARC_RPC_PREP_RUNNING && terminal_notices==1 && last_kind==2);
    char e[512];assert(!arc_riscpc_preparation_free(p,e,sizeof(e)));assert(last_kind==1);current=NULL;
    disk_hash(after);assert(!memcmp(before,after,32));test_vfs_no_open_handles();
}
static void construction_and_attachments(const uint8_t *rom,size_t size)
{
    const unsigned cuts[]={0,1,2,3,4,5,36,68,70,72,328,329,330};
    uint8_t seed[256]={0},raw[1024]={0};char error[512];
    for(unsigned model=0;model<4;++model)
        for(unsigned c=0;c<sizeof(cuts)/sizeof(*cuts);++c){
            assert(arc_engine_rpc_start_begin(model,rom,size,seed,946684800,false,error,sizeof(error)));
            bool done=false;for(unsigned j=0;j<cuts[c]&&!done;++j)assert(arc_engine_rpc_start_step(&done,error,sizeof(error)));
            uint8_t b[256],m[32];uint64_t w,l,now;unsigned state;
            arc_engine_rpc_evidence(b,m,&w,&l,&now,&state);assert(now==0 && state==0);
            arc_engine_stop();assert(!arc_engine_active() && !arc_engine_is_rpc());
            ArcEngineCapabilities caps=arc_engine_capabilities(),zero_caps={0};assert(!memcmp(&caps,&zero_caps,sizeof(caps)));
            const char *fault="stale";assert(!arc_engine_faulted(&fault)&&!fault);test_vfs_no_open_handles();
        }
    ArcEngineMedia good={.kind=ARC_MEDIA_ADF_800K,.geometry=ARC_GEOMETRY_800K_E,.data=raw,.size=sizeof(raw)},bad={.kind=ARC_MEDIA_JFD};
    for(unsigned stage=0;stage<3;++stage){
        assert(arc_engine_rpc_start(0,rom,size,seed,946684800,false,error,sizeof(error)));
        if(stage==0)assert(!arc_engine_rpc_attach(&bad,NULL,NULL,error,sizeof(error)));
        if(stage==1)assert(!arc_engine_rpc_attach(&good,&bad,NULL,error,sizeof(error)));
        if(stage==2){assert(arc_engine_rpc_attach(&good,&good,NULL,error,sizeof(error)));arc_engine_stop();}
        assert(!arc_engine_active());test_vfs_no_open_handles();
    }
    assert(!arc_engine_rpc_start_begin(4,rom,size,seed,946684800,false,error,sizeof(error)));
    assert(!arc_engine_active());
}
static void helper_exclusion(bool attached)
{
    /* Real generated/mounted helper identity, production qualified cells, no
     * test override. Synthesize only authored SWI metadata at the adapter. */
    arc_rpc_observers_bind(ARC_RPC_OS_350,0);
    assert(arc_rpc_observers_capability(ARC_RPC_CELL_DISC_A));
    assert(arc_rpc_observers_capability(ARC_RPC_CELL_REPLACE_A));
    arc_rpc_observers_disc_configure(true,"Other",NULL,NULL,NULL);
    ARMState saved=arm;
    strcpy((char *)ram00+0x20000,"ArcDroid");
    arm.mode=19;arm.reg[16]=19;arm.reg[15]=0x108;arm.reg[0]=2;arm.reg[1]=8;
    arm.reg[2]=0x10020000;arm.reg[3]=UINT32_MAX;arm.reg[13]=0x10021000;
    assert(!arc_rpc_observe_swi_at(0xef020033,0x100,rom));
    ArcDiscRequest request;arc_rpc_observers_disc_snapshot(&request);
    ArcRpcDiscStatus status;arc_rpc_observers_disc_status(&status);
    assert(request.a_active==!attached);
    assert(!strcmp(status.gate,attached?"helper-excluded":"seen"));
    assert(arc_rpc_observers_replace(ARC_DISC_SIGNAL_A)==!attached);
    strcpy((char *)ram00+0x20000,"Unrelated");
    assert(!arc_rpc_observe_swi_at(0xef020033,0x100,rom));
    arc_rpc_observers_disc_snapshot(&request);assert(request.a_active);
    assert(arc_rpc_observers_replace(ARC_DISC_SIGNAL_A));
    arm=saved;arc_rpc_observers_bind(0,0);
}
static void launch_marker(const ArcRpcLaunchController *c,unsigned event,unsigned sequence,unsigned detail)
{
    uint32_t *w=ram00+(0x20000+c->status)/4;w[8]=event;w[9]=sequence;w[10]=detail;
    arm.mode=16;arm.reg[16]=16;arm.reg[15]=0x10020000+c->query+8;
    arm.reg[0]=0;arm.reg[1]=1;arm.reg[2]=0;arm.reg[3]=0x10020000+c->status;
    arm.reg[4]=0x53435241;arm.reg[5]=64;arm.reg[6]=1;arm.reg[13]=0x10008000;
    assert(!arc_rpc_observe_swi_at(0xef020006,0x10020000+c->query,ram00+0x20000/4+(c->query&~0x3ffu)/4));
}
/* Read the actual mounted drive through the production controller. This also
 * warms the drive/cache before replacement, rather than inspecting a builder. */
static void helper_sector(unsigned sector,uint8_t out[256])
{
    ARMState saved=arm;arm.mode=19;arm.reg[16]=0xd3;arm.reg[15]=0x200004;
    fdc_write(0x3f2,0x3d);assert(arc_engine_rpc_advance(1000000));
    fdc_write(0x3f5,3);fdc_write(0x3f5,0xf1);fdc_write(0x3f5,3);fdc_write(0x3f7,2);
    const uint8_t command[]={0x46,1,0,0,(uint8_t)sector,1,(uint8_t)sector,0x35,255};
    for(unsigned i=0;i<sizeof(command);++i)fdc_write(0x3f5,command[i]);
    unsigned count=0,steps=0;
    while(count<256){
        assert(++steps<40000);
        unsigned msr=fdc_read(0x3f4)&0xf0;
        assert(msr!=0xd0);
        if(msr==0xf0)out[count++]=fdc_read(0x3f5);
        else assert(arc_engine_rpc_advance(16000));
    }
    for(steps=0;(fdc_read(0x3f4)&0xf0)!=0xd0;++steps){assert(steps<1000);assert(arc_engine_rpc_advance(16000));}
    for(unsigned i=0;i<7;++i){unsigned b=fdc_read(0x3f5);if(i<3)assert(b==(i==0?0x41u:i==1?0x80u:0u));}
    arm=saved;
}
static void mounted_noop(void)
{
    uint8_t actual[256],expected[ARC_RPC_HELPER_SIZE];
    assert(arc_riscpc_helper_build(expected,sizeof(expected)));
    helper_sector(7,actual);assert(!memcmp(actual,expected+7*256,256));
    /* Run the bytes reloaded from drive 1, not a retained builder image. */
    ARMState saved=arm;memcpy((uint8_t *)ram00+0x40000,actual,144);
    updatemode(16);arm.reg[16]=0xd0;arm.reg[15]=0x10040048;pccache=UINT32_MAX;
    arm.reg[0]=0x10040000;arm.reg[1]=144;arm.reg[2]=0x10050000;arm.reg[14]=0x200000;
    ram00[0x5000c/4]=0;extern int arc_rpc_arm_exec_count(unsigned);
    assert(arc_engine_rpc_advance(10000));assert(ram00[0x5000c/4]==2);arm=saved;pccache=UINT32_MAX;
}
static void retirement_cases(const uint8_t *rom,size_t size,ArcRpcPreparationHost host,bool injection)
{
    /* Cells are disabled in this flag-free authored session. Neither expiry
     * nor CLOSED/filter removal owns the retirement obligation. */
    for(unsigned kind=0;kind<(injection?1u:7u);++kind){
        restore();clock_mode=1;launch_test=true;
        ArcRpcPreparation *p=new_request(rom,size,true,false,host);settle(p);
        ArcRpcLaunchAttempt a;arc_riscpc_preparation_attempt(p,&a);
        ArcRpcLaunchPlan plan={.action=ARC_RPC_PLAN_BOOT,.media={1}};strcpy(plan.target,"ADFS::0.$.!Boot");
        ArcRpcLaunchController c;assert(arc_riscpc_controller_build(&plan,&a.token,&c));
        uint8_t controller_sector[256],id_before[256],id_after[256];
        helper_sector(7,controller_sector);assert(!memcmp(controller_sector,c.image,256));
        helper_sector(1,id_before);
        assert(id_before[251]==0x45 && id_before[252]==0x41);
        assert(!arc_rpc_observer_boundary_pending); /* never retire PREPARED */
        memcpy((uint8_t *)ram00+0x20000,c.image,c.size);
        if(kind==1)arc_rpc_observers_frame(UINT64_C(120000000));
        uint8_t *drive0_bytes=NULL;
        if(kind==2){
            drive0_bytes=malloc(ARC_RPC_HELPER_SIZE);assert(drive0_bytes);memset(drive0_bytes,0xa5,ARC_RPC_HELPER_SIZE);
            ArcEngineMedia media={.kind=ARC_MEDIA_ADL_640K,.geometry=ARC_GEOMETRY_640K_L,
                .data=drive0_bytes,.size=ARC_RPC_HELPER_SIZE,.effective_extent=ARC_MEDIA_EXTENT_UNRESTRICTED};
            char error[256];assert(arc_engine_rpc_mount(0,&media,error,sizeof(error)));arc_engine_rpc_launch_media_changed();
            fdc_write(0x3f2,0x3c);fdc_write(0x3f5,3);fdc_write(0x3f5,0xf1);fdc_write(0x3f5,3);fdc_write(0x3f7,2);
            const uint8_t command[]={0x46,0,0,0,7,1,7,0x35,255};
            for(unsigned i=0;i<sizeof(command);++i)fdc_write(0x3f5,command[i]);
            assert(!arc_rpc_fdc_idle(0));
        }
        if(kind==4){
            ArcRpcObserveFilter mode={.swi=0xffffff,.swi_mask=0xffffff,.modes=~0u,.kinds=ARC_RPC_OBS_MODE};
            assert(arc_rpc_observer_filter(ARC_RPC_OBS_ERROR,&mode));assert(arc_rpc_observer_context);
        }
        if((kind==0 || kind==4) && !injection){
            uint32_t *w=ram00+(0x20000+c.status)/4;w[8]=1;w[9]=1;w[10]=0;
            updatemode(16);arm.reg[16]=0xd0;arm.reg[15]=0x10020000+c.query+8;pccache=UINT32_MAX;
            arm.reg[0]=0;arm.reg[1]=1;arm.reg[2]=0;arm.reg[3]=0x10020000+c.status;
            arm.reg[4]=0x53435241;arm.reg[5]=64;arm.reg[6]=1;arm.reg[13]=0x10008000;
            /* Worst position: first instruction of a maximum CPU batch. No
             * kernel/reset/helper-load instruction may follow before service. */
            extern int arc_rpc_arm_exec_count(unsigned);assert(arc_rpc_arm_exec_count(200)==1);
        }else launch_marker(&c,1,1,0);
        assert(arc_rpc_observer_boundary_pending);
        if(kind==5 || kind==6){
            if(kind==5)arc_riscpc_preparation_cancel(p);
            else arc_engine_rpc_launch_interrupt(ARC_RPC_INTERRUPT_SWAP_ROLLBACK);
            assert(!arc_rpc_observer_boundary_pending && !arc_engine_active());
            assert(arc_riscpc_preparation_state(p)==(kind==5?ARC_RPC_PREP_CANCELLED:ARC_RPC_PREP_FAILED));
            assert(arc_rpc_observers_boundary());finish(p);continue;
        }
        if(kind==3){launch_marker(&c,2,2,42);launch_marker(&c,3,3,0);launch_marker(&c,4,4,0);launch_marker(&c,5,5,0);launch_marker(&c,8,6,0);
            arc_rpc_observers_frame(1000); /* same-frame CLOSED removes launch filter */
            assert(arc_rpc_observer_boundary_pending);
        }
        arc_riscpc_preparation_attempt(p,&a);ArcRpcLaunchAttempt before=a;
        uint64_t drive0=arc_rpc_fdc_media_generation(0),drive1=arc_rpc_fdc_media_generation(1);
        unsigned restages=reset_steps;
        bool ok=arc_rpc_observers_boundary();
        if(injection){assert(!ok && arc_riscpc_preparation_state(p)==ARC_RPC_PREP_FAILED);
            assert(!arc_riscpc_preparation_execution_allowed(p) && !arc_engine_active());
            assert(!arc_engine_rpc_advance(1000));assert(arc_rpc_observers_boundary());
            assert(strstr(last_notice,"retirement failed") && last_kind==2);finish(p);continue;}
        assert(ok && !arc_rpc_observer_boundary_pending);
        assert(arc_rpc_fdc_media_generation(0)==drive0 && arc_rpc_fdc_media_generation(1)>drive1 && reset_steps==restages);
        arc_riscpc_preparation_attempt(p,&a);assert(!memcmp(&before,&a,sizeof(a)));
        if(kind==2){
            assert(!arc_rpc_fdc_idle(0));ARMState saved=arm;updatemode(19);arm.reg[16]=0xd3;arm.reg[15]=0x200004;pccache=UINT32_MAX;
            unsigned count=0;
            for(unsigned i=0;count<256;++i){assert(i<40000);
                if((fdc_read(0x3f4)&0xf0)==0xf0){assert(fdc_read(0x3f5)==0xa5);++count;}
                else assert(arc_engine_rpc_advance(16000));}
            for(unsigned i=0;(fdc_read(0x3f4)&0xf0)!=0xd0;++i){assert(i<1000);assert(arc_engine_rpc_advance(16000));}
            for(unsigned i=0;i<7;++i)(void)fdc_read(0x3f5);
            arm=saved;pccache=UINT32_MAX;
        }
        mounted_noop();helper_sector(1,id_after);assert(memcmp(id_before,id_after,256));
        assert(id_after[251]==0x44 && id_after[252]==0x41);
        /* Status validation keeps the controller object even after remount. */
        if(kind!=1 && kind!=3){launch_marker(&c,2,2,42);launch_marker(&c,3,3,0);
            arc_riscpc_preparation_attempt(p,&a);assert(a.delivery==ARC_RPC_DELIVERY_DISPATCH);}
        finish(p);free(drive0_bytes);
    }
    launch_test=false;
}
static void guest_call(const uint8_t *image,size_t size,bool reset_after)
{
    memcpy((uint8_t *)ram00+0x20000,image,size);
    ram00[0x60000/4]=reset_after?0xef00006a:0xeafffffe;
    ram00[0x60004/4]=0xeafffffe;
    updatemode(19);arm.reg[16]=0xd3;arm.reg[13]=0x10070000;
    updatemode(16);arm.reg[16]=0xd0;arm.reg[13]=0x10068000;
    arm.reg[15]=0x10020048;arm.reg[0]=0x10020000;arm.reg[1]=(uint32_t)size;
    arm.reg[2]=0x10050000;arm.reg[3]=1;arm.reg[14]=0x10060000;
    arm.event=0;pccache=UINT32_MAX;
    assert(arc_engine_rpc_advance(1000000));
}
static void guest_reboots(ArcRpcPreparationHost host)
{
    for(unsigned cells=0;cells<2;++cells)for(unsigned kind=0;kind<3;++kind){
        restore();clock_mode=1;launch_test=guest_subject=true;guest_cells=cells!=0;
        const char *name=kind==1?"/system/guest-reset.rom":"/system/guest-return.rom";
        int index=find(name,false);assert(index>=0);
        ArcRpcPreparation *p=new_request(files[index].data,files[index].size,true,false,host);settle(p);
        ArcRpcLaunchAttempt a;arc_riscpc_preparation_attempt(p,&a);
        ArcRpcLaunchPlan plan={.action=ARC_RPC_PLAN_BOOT,.media={1}};strcpy(plan.target,"ADFS::0.$.!Boot");
        ArcRpcLaunchController c;assert(arc_riscpc_controller_build(&plan,&a.token,&c));
        if(kind==2){ /* A guest reboot while PREPARED leaves the token intact. */
            ram00[0x60000/4]=0xef00006a;updatemode(19);arm.reg[13]=0x10070000;
            updatemode(16);arm.reg[16]=0xd0;arm.reg[15]=0x10060008;pccache=UINT32_MAX;
            assert(arc_engine_rpc_advance(1000000));
            ArcRpcLaunchAttempt before=a;arc_riscpc_preparation_attempt(p,&a);assert(!memcmp(&before,&a,sizeof(a)));
        }
        guest_call(c.image,c.size,kind==0);
        assert(ram00[0x3101c/4]==1 && ram00[0x31004/4]==1); /* consume and execution */
        assert(ram00[0x3102c/4]==1 && ram00[0x32000/4]==(kind==2?1u:0u)); /* guest reset cleared latch */
        arc_riscpc_preparation_attempt(p,&a);ArcRpcLaunchAttempt before=a;
        if(kind==0)assert(a.start_returned && a.outcome==(cells?ARC_RPC_OUTCOME_RETURNED:ARC_RPC_OUTCOME_PENDING));
        if(kind==1)assert(!a.start_returned && a.delivery==ARC_RPC_DELIVERY_DISPATCH);
        uint8_t image[256];helper_sector(7,image);guest_call(image,144,false);
        assert(ram00[0x5000c/4]==2); /* no-op actually executed after reboot */
        assert(ram00[0x3101c/4]==1 && ram00[0x31004/4]==1 && ram00[0x3102c/4]==1);
        arc_riscpc_preparation_attempt(p,&a);assert(!memcmp(&before,&a,sizeof(a)));
        if(kind==1){arc_rpc_observers_frame(UINT64_C(120000000));arc_riscpc_preparation_attempt(p,&a);assert(a.outcome==ARC_RPC_OUTCOME_UNKNOWN);}
        finish(p);
    }
    guest_subject=guest_cells=launch_test=false;
}
static bool rollback_write_failure;
static int64_t rollback_cmos_write(struct retro_vfs_file_handle *file,const void *bytes,uint64_t n)
{
    if(strstr(files[file->file].path,"/cmos-riscpc/")){
        assert(arc_engine_active() && arc_engine_is_rpc());
        if(rollback_write_failure)return -1;
    }
    return test_write(file,bytes,n);
}
static void launch_reset_matrix(const uint8_t *rom,size_t size,ArcRpcPreparationHost host)
{
    for(unsigned recovery=0;recovery<2;++recovery){
    restore();clock_mode=recovery?3:1;launch_test=true;
    ArcRpcPreparation *pending=new_request(rom,size,true,false,host);
    if(recovery){
        while(arc_riscpc_preparation_state(pending)!=ARC_RPC_PREP_RECOVER)frame(pending);
        frame(pending); /* An open cache scan is owned by this preparation. */
    }
    assert(arc_riscpc_preparation_reset(pending));settle(pending);
    assert(arc_riscpc_preparation_state(pending)==ARC_RPC_PREP_RUNNING && arc_riscpc_preparation_execution_allowed(pending));
    ArcRpcLaunchAttempt suppressed;arc_riscpc_preparation_attempt(pending,&suppressed);
    assert(!suppressed.live && suppressed.delivery==ARC_RPC_DELIVERY_INTERRUPTED);finish(pending);
    }
    for(unsigned present=0;present<2;++present){
        restore();clock_mode=1;
        if(present){int product=find("/system/product.zip",false);assert(product>=0);
            file_bytes("/system/arcdroid/riscpc/boot/HardDisc4.zip",files[product].data,files[product].size);}
        launch_product="ADFS::4.$.Apps.!Example";
        ArcRpcPreparation *product=new_request(rom,size,true,false,host);settle(product);
        if(arc_riscpc_preparation_state(product)!=(present?ARC_RPC_PREP_RUNNING:ARC_RPC_PREP_FAILED))fprintf(stderr,"product present=%u: %s\n",present,arc_riscpc_preparation_error(product));
        assert(arc_riscpc_preparation_state(product)==(present?ARC_RPC_PREP_RUNNING:ARC_RPC_PREP_FAILED));
        if(present){ArcRpcLaunchAttempt a;arc_riscpc_preparation_attempt(product,&a);assert(a.delivery==ARC_RPC_DELIVERY_PREPARED);
            ArcRpcLaunchPlan plan={.action=ARC_RPC_PLAN_PRODUCT,.media={1}};strcpy(plan.target,launch_product);
            ArcRpcLaunchController c;assert(arc_riscpc_controller_build(&plan,&a.token,&c));memcpy((uint8_t *)ram00+0x20000,c.image,c.size);
            launch_marker(&c,1,1,0);assert(arc_rpc_observers_boundary());mounted_noop();}
        else assert(!arc_engine_active());
        finish(product);launch_product=NULL;
    }

    for(unsigned cut=0;cut<4;++cut)for(unsigned failure=0;failure<3;++failure){
        restore();clock_mode=1;launch_test=true;ArcRpcPreparation *p=new_request(rom,size,true,false,host);settle(p);
        assert(arc_riscpc_preparation_state(p)==ARC_RPC_PREP_RUNNING && arc_riscpc_preparation_execution_allowed(p));
        ArcRpcLaunchAttempt a;arc_riscpc_preparation_attempt(p,&a);assert(a.delivery==ARC_RPC_DELIVERY_PREPARED);
        ArcRpcLaunchPlan plan={.action=ARC_RPC_PLAN_BOOT,.media={1}};strcpy(plan.target,"ADFS::0.$.!Boot");
        ArcRpcLaunchController c;assert(arc_riscpc_controller_build(&plan,&a.token,&c));memcpy((uint8_t *)ram00+0x20000,c.image,c.size);
        if(cut>=1){launch_marker(&c,1,1,0);arc_riscpc_preparation_attempt(p,&a);assert(a.delivery==ARC_RPC_DELIVERY_CONSUMED);}
        if(cut>=2){launch_marker(&c,2,2,42);launch_marker(&c,3,3,0);arc_riscpc_preparation_attempt(p,&a);assert(a.delivery==ARC_RPC_DELIVERY_DISPATCH);}
        if(cut>=3){launch_marker(&c,4,4,99);launch_marker(&c,6,5,99);launch_marker(&c,8,6,0);
            arc_riscpc_preparation_attempt(p,&a);assert(a.start_returned && !arc_riscpc_attempt_established(&a));}
        uint64_t epoch=a.epoch;reset_probe=true;reset_failure=failure;reset_steps=0;
        assert(arc_riscpc_preparation_reset(p)==!failure);reset_probe=false;
        assert(!arc_engine_active()==(failure!=0));assert(reset_steps==(failure==1?1:2));
        arc_riscpc_preparation_attempt(p,&a);assert(a.delivery==ARC_RPC_DELIVERY_INTERRUPTED && a.epoch>epoch && a.live==!failure);
        assert(arc_riscpc_preparation_execution_allowed(p)==!failure);
        assert(a.resume_allowed==!failure);
        if(failure)assert(strstr(arc_riscpc_preparation_error(p),"latest CMOS snapshot"));
        else {
            mounted_noop();
            ArcRpcLaunchAttempt before=a;memcpy((uint8_t *)ram00+0x20000,c.image,c.size);launch_marker(&c,1,1,0);
            arc_riscpc_preparation_attempt(p,&a);assert(!memcmp(&before,&a,sizeof(a))); /* stale after reset */
        }
        finish(p);launch_test=false;
    }
    restore();clock_mode=1;launch_test=false;
    ArcRpcPreparation *stopped=new_request(rom,size,true,false,host);settle(stopped);
    arc_engine_stop();assert(!arc_riscpc_preparation_execution_allowed(stopped));
    assert(!arc_riscpc_preparation_reset(stopped) && arc_riscpc_preparation_state(stopped)==ARC_RPC_PREP_FAILED);
    assert(strstr(arc_riscpc_preparation_error(stopped),"latest CMOS snapshot unavailable"));finish(stopped);
    for(unsigned save_failure=0;save_failure<2;++save_failure){
        restore();clock_mode=1;launch_test=true;
        ArcRpcPreparation *aborted=new_request(rom,size,true,false,host);settle(aborted);
        uint8_t latest[256];arc_rpc_cmos_snapshot(latest);latest[69]^=4;arc_rpc_cmos_seed(latest);
        rollback_write_failure=save_failure;vfs.write=rollback_cmos_write;
        arc_engine_rpc_launch_interrupt(ARC_RPC_INTERRUPT_SWAP_ROLLBACK);
        vfs.write=test_write;
        assert(arc_riscpc_preparation_state(aborted)==ARC_RPC_PREP_FAILED && !arc_engine_active());
        assert(!arc_riscpc_preparation_execution_allowed(aborted));
        assert(strstr(arc_riscpc_preparation_error(aborted),save_failure?"latest CMOS snapshot could not be saved":"latest CMOS snapshot saved"));
        reset_fault();
        if(!save_failure){bool saved=false;for(unsigned i=0;i<TEST_FILES;++i)
            if(files[i].present && files[i].size==512 && strstr(files[i].path,"/cmos-riscpc/") && !memcmp(files[i].data+104,latest+16,240))saved=true;
            assert(saved);}
        finish(aborted);launch_test=false;
    }
}
static void helper_ownership(const uint8_t *rom,size_t size,ArcRpcPreparationHost host)
{
    ArcEngineMedia empty={0};
    assert(!arc_riscpc_preparation_helper_override(NULL,&empty));
    assert(!arc_riscpc_preparation_helper_write_policy(NULL,false));
    for(unsigned explicit_empty=0;explicit_empty<2;++explicit_empty){
        restore();helper_override=explicit_empty;clock_mode=1;helper_construction_bytes=0;revision1_notices=0;
        ArcRpcPreparation *p=new_request(rom,size,true,false,host);
        assert(!arc_riscpc_preparation_helper_override(p,&empty));
        assert(!arc_riscpc_preparation_helper_write_policy(p,false));
        for(unsigned i=0;arc_riscpc_preparation_state(p)!=ARC_RPC_PREP_START;++i){assert(i<100000);frame(p);}
        assert(!arc_riscpc_preparation_helper_override(p,NULL));
        if(explicit_empty){
            char oversized[2048];memset(oversized,'x',sizeof(oversized)-1);oversized[sizeof(oversized)-1]=0;
            ArcEngineMedia view={.path=oversized};assert(!arc_riscpc_preparation_helper_override(p,&view));
            assert(arc_riscpc_preparation_helper_override(p,&empty));
        }else assert(!arc_riscpc_preparation_helper_override(p,&empty));
        finish(p);restore();clock_mode=1;helper_construction_bytes=0;revision1_notices=0;
        p=new_request(rom,size,true,false,host);
        settle(p);
        assert(arc_riscpc_preparation_state(p)==ARC_RPC_PREP_RUNNING);
        assert(!arc_riscpc_preparation_helper_override(p,&empty));
        assert(!arc_riscpc_preparation_helper_write_policy(p,false));
        assert(helper_construction_bytes==(explicit_empty?0:ARC_RPC_HELPER_SIZE));
        helper_exclusion(!explicit_empty);
        char h[11],d[11];assert(arc_rpc_observers_identities(h,d));assert(!strcmp(h,explicit_empty?"":"ArcDroid"));
        uint64_t generation=arc_rpc_fdc_media_generation(1);
        assert(arc_riscpc_preparation_reset(p));assert(arc_rpc_observers_identities(h,d));assert(!strcmp(h,explicit_empty?"":"ArcDroid"));
        assert(arc_rpc_fdc_media_generation(1)==generation+(explicit_empty?1:2));
        helper_exclusion(!explicit_empty);
        assert(revision1_notices==0);
        finish(p);assert(!arc_engine_active());
    }
    helper_override=false;
    /* An explicit image must reach drive 1, rather than merely suppress a
     * budget counter. Give it a distinct, valid FileCore identity. */
    uint8_t *own=malloc(ARC_RPC_HELPER_SIZE);assert(own);
    assert(arc_riscpc_helper_build(own,ARC_RPC_HELPER_SIZE));
    memcpy(own+247,"Epii\r",5);memcpy(own+502,"xlct\r",5);
    for(unsigned sector=0;sector<2;++sector){unsigned sum=255;
        for(unsigned at=255;at-->0;){if(sum>255)sum=(sum+1)&255;sum+=own[sector*256+at];}
        own[sector*256+255]=(uint8_t)sum;
    }
    ArcEngineMedia own_media={.kind=ARC_MEDIA_ADL_640K,.geometry=ARC_GEOMETRY_640K_L,
        .data=own,.size=ARC_RPC_HELPER_SIZE,.writable=false};
    restore();explicit_helper=&own_media;clock_mode=1;helper_construction_bytes=0;revision1_notices=0;
    ArcRpcPreparation *explicit_request=new_request(rom,size,true,false,host);settle(explicit_request);
    assert(arc_riscpc_preparation_state(explicit_request)==ARC_RPC_PREP_RUNNING);
    char own_name[11],own_hd[11];assert(arc_rpc_observers_identities(own_name,own_hd));
    assert(!strcmp(own_name,"Explicit") && helper_construction_bytes==0 && revision1_notices==0);
    uint64_t generation=arc_rpc_fdc_media_generation(1);
    assert(arc_riscpc_preparation_reset(explicit_request));
    assert(arc_rpc_fdc_media_generation(1)==generation+2);
    assert(arc_rpc_observers_identities(own_name,own_hd) && !strcmp(own_name,"Explicit"));
    finish(explicit_request);explicit_helper=NULL;free(own);
    /* Reopen that revision-2 root both with and without population requested.
     * Neither open emits the revision-1 notice; only population gets a helper. */
    for(unsigned population=0;population<2;++population){
        populate_request=population;helper_construction_bytes=0;revision1_notices=0;
        ArcRpcPreparation *reopen=new_request(rom,size,true,false,host);settle(reopen);
        assert(arc_riscpc_preparation_state(reopen)==ARC_RPC_PREP_RUNNING);
        assert(arc_rpc_observers_identities(own_name,own_hd));
        assert(!strcmp(own_name,population?"ArcDroid":""));
        assert(helper_construction_bytes==(population?ARC_RPC_HELPER_SIZE:0) && revision1_notices==0);
        finish(reopen);
    }
    populate_request=true;
    /* A revision-1 root remains manual without opening even invalid packages. */
    restore();clock_mode=1;ArcRpcPreparation *p=new_request(rom,size,true,false,host);settle(p);finish(p);
    unsigned changed=0;
    for(unsigned i=0;i<TEST_FILES;++i)if(files[i].present && strstr(files[i].path,"/manifest.")){
        assert(files[i].size==24452);files[i].data[24336]=1;
        uint32_t crc=arc_crc32(files[i].data,files[i].size-4);
        for(unsigned b=0;b<4;++b)files[i].data[files[i].size-4+b]=(uint8_t)(crc>>(8*b));++changed;
    }
    assert(changed);
    for(unsigned i=0;i<TEST_FILES;++i)if(files[i].present && strstr(files[i].path,".zip"))files[i].data[0]^=1;
    uint8_t before[32],after[32];disk_hash(before);revision1_notices=0;
    helper_construction_bytes=0;p=new_request(rom,size,true,false,host);settle(p);
    assert(arc_riscpc_preparation_state(p)==ARC_RPC_PREP_RUNNING && revision1_notices==1);
    assert(helper_construction_bytes==0);
    char h[11],d[11];assert(arc_rpc_observers_identities(h,d)&&!h[0]);finish(p);
    disk_hash(after);assert(!memcmp(before,after,32));
    uint8_t *image=malloc(ARC_RPC_HELPER_SIZE);assert(image);assert(arc_riscpc_helper_build(image,ARC_RPC_HELPER_SIZE));
    ArcEngineMedia helper={.kind=ARC_MEDIA_ADL_640K,.geometry=ARC_GEOMETRY_640K_L,
        .data=image,.size=ARC_RPC_HELPER_SIZE,.writable=false};
    ArcRpcFloppy f;arc_rpc_floppy_init(&f,1);char error[256];assert(arc_rpc_floppy_mount(&f,1,&helper,error,sizeof(error)));
    assert(arc_rpc_floppy_signals(&f,1,0).write_protected);
    arc_rpc_floppy_motor(&f,1,true,0);
    ArcRpcFloppyRequest req={.drive=1,.cylinder=0,.head=0,.c=0,.h=0,.r=0,.n=1,.eot=15,
        .encoding=ARC_RPC_FLOPPY_MFM,.rate_kbps=250,.operation=ARC_RPC_FLOPPY_WRITE};
    ArcRpcFloppySector sector;assert(arc_rpc_floppy_select(&f,&req,0,&sector)==ARC_RPC_FLOPPY_WRITE_PROTECT);
    arc_rpc_floppy_close(&f);free(image);
}
static bool fail_final_cmos_read;
static char writer_firmware[65];
static int64_t pending_read(struct retro_vfs_file_handle *f,void *out,uint64_t n)
{
    const char *p=files[f->file].path;
    bool fail=fail_final_cmos_read && strstr(p,writer_firmware) && p[strlen(p)-1]=='0';
    if(fail){fail_final_cmos_read=false;fault=5;fail_at=steps+1;}
    int64_t result=test_read(f,out,n);
    if(fail){fault=fail_at=0;}
    return result;
}
static void pending_unload(const char *source,ArcRpcPreparationHost host)
{
    file_copy("/system/writer.rom",source);int i=find("/system/writer.rom",false);assert(i>=0);
    uint8_t *rom=malloc(files[i].size);assert(rom);memcpy(rom,files[i].data,files[i].size);
    arc_sha256_hex(rom,files[i].size,writer_firmware);
    ArcRpcPreparation *p=new_request(rom,files[i].size,true,false,host);settle(p);
    assert(arc_riscpc_preparation_state(p)==ARC_RPC_PREP_RUNNING);
    ArcInputState input_state;arc_input_reset(&input_state);ArcEngineFrame f;
    assert(arc_engine_run_frame(&input_state,&f));
    uint8_t bytes[256],mask[32];uint64_t writes,last,now;unsigned state;
    arc_engine_rpc_evidence(bytes,mask,&writes,&last,&now,&state);assert(writes && now);
    vfs.read=pending_read;fail_final_cmos_read=true;char error[512]={0};
    assert(!arc_riscpc_preparation_persist(p,error,sizeof(error)) && !fail_final_cmos_read && error[0]);
    vfs.read=test_read;
    /* Publication has already renamed, but its readback failed. Normal unload
     * must reconcile the pending slot and save the latest live snapshot. */
    finish(p);
    bool newest=false;
    for(unsigned j=0;j<TEST_FILES;++j)if(files[j].present && strstr(files[j].path,writer_firmware) && files[j].path[strlen(files[j].path)-1]=='0'){
        assert(files[j].size==512 && files[j].data[24]==2 && !memcmp(files[j].data+104,bytes+16,240));newest=true;
    }
    assert(newest);free(rom);
}
static unsigned video_calls,audio_calls;
static bool environment(unsigned command,void *data)
{
    switch(command){
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:*(const char **)data="/system";return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:*(const char **)data="/save";return true;
    case RETRO_ENVIRONMENT_GET_VFS_INTERFACE:((struct retro_vfs_interface_info *)data)->iface=&vfs;return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:return *(enum retro_pixel_format *)data==RETRO_PIXEL_FORMAT_XRGB8888;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:*(bool *)data=true;return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE:{struct retro_variable *v=data;
        if(!strcmp(v->key,"arcdroid_machine"))v->value="A3000";
        else if(!strcmp(v->key,"arcdroid_launch"))v->value="off";
        else if(!strcmp(v->key,"arcdroid_hard_disc"))v->value="off";
        else return false;return true;}
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:*(bool *)data=false;return true;
    default:return false;
    }
}
static void video(const void *p,unsigned w,unsigned h,size_t pitch)
{assert(w && h && pitch>=w*4);(void)p;++video_calls;}
static size_t audio(const int16_t *p,size_t n){assert(p&&n);++audio_calls;return n;}
static void poll(void){}
static int16_t input(unsigned p,unsigned d,unsigned i,unsigned id_){(void)p;(void)d;(void)i;(void)id_;return 0;}
static uint64_t content_generation(const uint8_t payload[240])
{
    uint64_t generation=0;int latest=-1;
    for(unsigned i=0;i<TEST_FILES;++i)if(files[i].present && files[i].size==512 &&
        strstr(files[i].path,"/cmos-riscpc/") && !strstr(files[i].path,"/defaults/") &&
        !strstr(files[i].path,".tmp")){
        uint64_t value=0;for(unsigned j=0;j<8;++j)value|=(uint64_t)files[i].data[24+j]<<(j*8);
        if(value>generation){generation=value;latest=(int)i;}
    }
    assert(latest>=0);
    if(payload)assert(!memcmp(files[latest].data+104,payload,240));
    return generation;
}
static void host_fault_cases(const uint8_t *rom,size_t size,ArcRpcPreparationHost callbacks)
{
    uint8_t *bad=malloc(size);assert(bad);memcpy(bad,rom,size);
    const uint32_t invalid[]={0xe3a00070,0xee010f10,0xe3a00014,0xe121f000,0xeafffffe};
    memcpy(bad,invalid,sizeof(invalid));test_model=2;test_os=ARC_RPC_OS_371;
    for(unsigned boundary=0;boundary<10;++boundary){
        restore();clock_mode=0;clock_value=1000000;clock_delta=1000;
        use_frontend_host=true;host_extended=true;
        ArcRpcPreparation *p=new_request(bad,size,true,true,callbacks);
        while(arc_riscpc_host_pending(frontend_test_host)){
            arc_riscpc_host_frame_begin(frontend_test_host);arc_riscpc_host_pending_frame(frontend_test_host);arc_riscpc_host_frame_end(frontend_test_host);
            assert(arc_riscpc_preparation_state(p)<ARC_RPC_PREP_FAILED);
        }
        uint64_t generation=content_generation(NULL);
        uint8_t latest[256];arc_rpc_cmos_snapshot(latest);latest[100]^=1;arc_rpc_cmos_seed(latest);
        for(unsigned i=0;i<boundary;++i)arc_riscpc_host_maintenance(frontend_test_host);
        uint8_t captured[256];arc_rpc_cmos_snapshot(captured);captured[101]^=1;arc_rpc_cmos_seed(captured);
        assert(!arc_engine_rpc_advance(10000));
        const char *fault_text=NULL;assert(arc_engine_faulted(&fault_text));
        char expected[512];snprintf(expected,sizeof(expected),"%s",fault_text);size_t length=strlen(expected);while(length && (expected[length-1]=='\n' || expected[length-1]=='\r'))expected[--length]=0;
        arc_riscpc_host_maintenance(frontend_test_host);
        assert(arc_riscpc_preparation_state(p)==ARC_RPC_PREP_FAILED && arc_riscpc_preparation_draining(p));
        assert(!strcmp(host_display,expected));
        assert(!arc_engine_active());
        if(boundary==8){char error[512]={0};assert(arc_riscpc_host_free(frontend_test_host,error,sizeof(error)));}
        else{
            if(boundary==9)assert(arc_riscpc_host_reset(frontend_test_host));
            unsigned frames=0;
            while(arc_riscpc_preparation_draining(p)){
                uint64_t ns=namespace_calls;arc_riscpc_host_frame_begin(frontend_test_host);
                arc_riscpc_host_pending_frame(frontend_test_host);arc_riscpc_host_frame_end(frontend_test_host);
                assert(namespace_calls-ns<=1 && ++frames<=60);
            }
            assert(frames>1);
            if(boundary==9)assert(arc_riscpc_preparation_state(p)==ARC_RPC_PREP_VERIFY);
            else assert(strstr(host_display,"latest CMOS snapshot saved"));
            char error[512]={0};assert(arc_riscpc_host_free(frontend_test_host,error,sizeof(error)));
        }
        assert(content_generation(captured+16)==generation+(boundary?2:1));
        frontend_test_host=NULL;current=NULL;use_frontend_host=false;test_vfs_no_open_handles();
        for(unsigned i=0;i<16;++i)assert(!rpc_fixture_locks[i].used);
    }
    restore();clock_mode=0;clock_value=1000000;clock_delta=1000;use_frontend_host=true;host_extended=true;
    new_request(bad,size,true,true,callbacks);
    while(arc_riscpc_host_pending(frontend_test_host))arc_riscpc_host_pending_frame(frontend_test_host);
    char error[512]={0};assert(arc_riscpc_host_free(frontend_test_host,error,sizeof(error)));frontend_test_host=NULL;current=NULL;
    int slot=-1;for(unsigned i=0;i<TEST_FILES;++i)if(files[i].present && files[i].size==512 && strstr(files[i].path,"/cmos-riscpc/") && !strstr(files[i].path,"/defaults/")){slot=(int)i;break;}
    assert(slot>=0);char other[1100];snprintf(other,sizeof(other),"%s",files[slot].path);size_t length=strlen(other);other[length-1]=other[length-1]=='0'?'1':'0';
    file_bytes(other,files[slot].data,512);files[find(other,false)].data[104]^=1;
    ArcRpcPreparation *refused=new_request(bad,size,true,true,callbacks);
    while(arc_riscpc_host_pending(frontend_test_host))arc_riscpc_host_pending_frame(frontend_test_host);
    uint64_t mutations=namespace_calls,generation=content_generation(NULL);
    assert(!arc_engine_rpc_advance(10000));arc_riscpc_host_maintenance(frontend_test_host);
    assert(arc_riscpc_preparation_draining(refused));arc_riscpc_host_pending_frame(frontend_test_host);
    assert(!arc_riscpc_preparation_draining(refused) && strstr(host_display,"latest CMOS snapshot could not be saved"));
    assert(namespace_calls==mutations && content_generation(NULL)==generation);
    assert(!arc_riscpc_host_free(frontend_test_host,error,sizeof(error)));
    for(unsigned i=0;i<TEST_FILES;++i)assert(!files[i].present || !strstr(files[i].path,".unsaved"));
    frontend_test_host=NULL;current=NULL;use_frontend_host=false;test_vfs_no_open_handles();
    test_model=0;test_os=ARC_RPC_OS_350;free(bad);
    puts("production host: real guest fault, capture before stop, every in-flight CMOS boundary, bounded one-unit drain, queued Restart and unload-mid-drain passed");
}
static void host_reset_inflight(const uint8_t *rom,size_t size,ArcRpcPreparationHost callbacks)
{
    for(unsigned rollback=0;rollback<2;++rollback)for(unsigned boundary=0;boundary<9;++boundary){
        restore();clock_mode=0;clock_value=0;clock_delta=1000;use_frontend_host=true;host_extended=true;
        launch_test=rollback!=0;
        ArcRpcPreparation *p=new_request(rom,size,true,true,callbacks);
        while(arc_riscpc_host_pending(frontend_test_host))arc_riscpc_host_pending_frame(frontend_test_host);
        uint64_t generation=content_generation(NULL);uint8_t prior[256],latest[256];
        arc_rpc_cmos_snapshot(prior);prior[100]^=1;arc_rpc_cmos_seed(prior);
        for(unsigned i=0;i<=boundary;++i)arc_riscpc_host_maintenance(frontend_test_host);
        arc_rpc_cmos_snapshot(latest);latest[101]^=1;arc_rpc_cmos_seed(latest);
        reset_probe=true;reset_failure=2;reset_steps=0;
        if(rollback)arc_engine_rpc_launch_interrupt(ARC_RPC_INTERRUPT_SWAP_ROLLBACK);
        else assert(!arc_riscpc_host_reset(frontend_test_host));reset_probe=false;reset_failure=0;
        assert(reset_steps==(rollback?0:2) && arc_riscpc_preparation_state(p)==ARC_RPC_PREP_FAILED && !arc_engine_active());
        assert(strstr(host_display,"latest CMOS snapshot saved") && arc_riscpc_preparation_owned(p));
        assert(content_generation(latest+16)==generation+2);
        bool previous=false;for(unsigned i=0;i<TEST_FILES;++i)if(files[i].present && files[i].size==512 &&
            strstr(files[i].path,"/cmos-riscpc/") && !strstr(files[i].path,".tmp") && files[i].data[24]==generation+1){
            assert(!memcmp(files[i].data+104,prior+16,240));previous=true;}
        assert(previous);char error[512]={0};assert(arc_riscpc_host_free(frontend_test_host,error,sizeof(error)));
        frontend_test_host=NULL;current=NULL;use_frontend_host=false;launch_test=false;test_vfs_no_open_handles();
    }
    puts("production host: no-attempt RUNNING reset barrier/drive-1 eject order, all stepped-publication boundaries completed before frozen reset failure and swap rollback, exact prior/latest generations passed");
}
static bool fail_unload_flush,fail_marker_open;
static unsigned unload_identity_countdown;
static bool unload_identity(void *opaque,intptr_t handle,const char *path)
{
    if(unload_identity_countdown && !--unload_identity_countdown)rpc_fixture_lost=true;
    return rpc_fixture_identity(opaque,handle,path);
}
static unsigned reset_publication_failure;
static uint64_t reset_publication_generation;
static bool reset_candidate_seen;
static bool content_cmos_path(const char *path)
{return strstr(path,"/cmos-riscpc/") && !strstr(path,"/defaults/");}
static void outside_content_cmos_hash(uint8_t output[32])
{
    ArcSha256 hash;arc_sha256_init(&hash);
    for(unsigned i=0;i<TEST_FILES;++i)if(files[i].present && !content_cmos_path(files[i].path)){
        arc_sha256_update(&hash,files[i].path,strlen(files[i].path)+1);arc_sha256_update(&hash,&files[i].size,sizeof(files[i].size));
        arc_sha256_update(&hash,files[i].data,files[i].size);
    }
    arc_sha256_final(&hash,output);
}
static uint64_t record_generation(const uint8_t *bytes)
{uint64_t value=0;for(unsigned i=0;i<8;++i)value|=(uint64_t)bytes[24+i]<<(8*i);return value;}
static int64_t reset_publication_write(struct retro_vfs_file_handle *file,const void *data,uint64_t size)
{
    if(content_cmos_path(files[file->file].path) && size==512 && record_generation(data)==reset_publication_generation){
        reset_candidate_seen=true;
        if(reset_publication_failure==5)return test_write(file,data,size/2);
    }
    return test_write(file,data,size);
}
static int64_t reset_publication_read(struct retro_vfs_file_handle *file,void *data,uint64_t size)
{
    const char *path=files[file->file].path;
    if(reset_candidate_seen && content_cmos_path(path) &&
        ((reset_publication_failure==1 && strstr(path,".tmp")) ||
         (reset_publication_failure==3 && !strstr(path,".tmp") && path[strlen(path)-1]==('0'+(reset_publication_generation&1)))))return -1;
    return test_read(file,data,size);
}
static int reset_publication_flush(struct retro_vfs_file_handle *file)
{return reset_candidate_seen && reset_publication_failure==6 && content_cmos_path(files[file->file].path) && strstr(files[file->file].path,".tmp")?-1:test_flush(file);}
static int reset_publication_close(struct retro_vfs_file_handle *file)
{
    bool fail=reset_candidate_seen && reset_publication_failure==7 && content_cmos_path(files[file->file].path) && strstr(files[file->file].path,".tmp");
    int result=test_close(file);return fail?-1:result;
}
static int reset_publication_rename(const char *from,const char *to)
{return reset_candidate_seen && reset_publication_failure==2 && content_cmos_path(from)?-1:test_rename(from,to);}
static void expected_content_record(uint8_t out[512],const uint8_t prototype[512],uint64_t generation,const uint8_t payload[240])
{
    memcpy(out,prototype,512);for(unsigned i=0;i<8;++i)out[24+i]=(uint8_t)(generation>>(8*i));memcpy(out+104,payload,240);
    uint32_t crc=arc_crc32(out,508);for(unsigned i=0;i<4;++i)out[508+i]=(uint8_t)(crc>>(8*i));
}
static void host_reset_publication_failures(const uint8_t *rom,size_t size,ArcRpcPreparationHost callbacks)
{
    for(unsigned prefix=0;prefix<2;++prefix)for(unsigned failure=0;failure<8;++failure){
        restore();clock_mode=0;clock_value=0;clock_delta=1000;use_frontend_host=true;host_extended=true;
        ArcRpcPreparation *p=new_request(rom,size,true,true,callbacks);
        while(arc_riscpc_host_pending(frontend_test_host))arc_riscpc_host_pending_frame(frontend_test_host);
        uint8_t initial[256],prior[256],latest[256];arc_rpc_cmos_snapshot(initial);initial[100]^=1;arc_rpc_cmos_seed(initial);
        while(content_generation(NULL)<2)arc_riscpc_host_maintenance(frontend_test_host);
        char error[512]={0};assert(arc_riscpc_preparation_complete_publication(p,error,sizeof(error)));
        uint8_t prototype[512],older[512];char base[1100]={0};unsigned slots=0;
        for(unsigned i=0;i<TEST_FILES;++i)if(files[i].present && files[i].size==512 && content_cmos_path(files[i].path) && !strstr(files[i].path,".tmp")){
            ++slots;if(record_generation(files[i].data)==2){memcpy(prototype,files[i].data,512);snprintf(base,sizeof(base),"%s",files[i].path);base[strlen(base)-2]=0;}else memcpy(older,files[i].data,512);
        }
        assert(slots==2 && *base);memcpy(prior,initial,256);
        if(prefix){prior[102]^=1;arc_rpc_cmos_seed(prior);arc_riscpc_host_maintenance(frontend_test_host);}
        memcpy(latest,prior,256);if(failure)latest[101]^=1;arc_rpc_cmos_seed(latest);
        uint8_t outside_before[32],outside_after[32];outside_content_cmos_hash(outside_before);
        reset_publication_generation=3+prefix;reset_publication_failure=failure;reset_candidate_seen=false;
        vfs.write=reset_publication_write;vfs.read=reset_publication_read;vfs.flush=reset_publication_flush;vfs.close=reset_publication_close;vfs.rename=reset_publication_rename;
        reset_probe=true;reset_failure=2;reset_steps=0;assert(!arc_riscpc_host_reset(frontend_test_host));reset_probe=false;reset_failure=0;
        vfs.write=test_write;vfs.read=test_read;vfs.flush=test_flush;vfs.close=test_close;vfs.rename=test_rename;
        assert(reset_steps==2 && !arc_engine_active() && !arc_riscpc_preparation_execution_allowed(p));
        assert(strstr(host_display,failure==0 || failure==4?"latest CMOS snapshot saved":"latest CMOS snapshot could not be saved"));
        uint8_t current_bytes[512],latest_bytes[512],previous_bytes[512];
        expected_content_record(current_bytes,prototype,2+prefix,prior+16);
        expected_content_record(latest_bytes,prototype,3+prefix,latest+16);
        memcpy(previous_bytes,prefix?prototype:older,512);
        assert(arc_riscpc_host_free(frontend_test_host,error,sizeof(error)));frontend_test_host=NULL;current=NULL;
        outside_content_cmos_hash(outside_after);assert(!memcmp(outside_before,outside_after,32));
        unsigned seen=0;for(unsigned i=0;i<TEST_FILES;++i)if(files[i].present && content_cmos_path(files[i].path)){
            ++seen;const char *suffix=files[i].path+strlen(base);assert(!strncmp(files[i].path,base,strlen(base)));
            if(!strcmp(suffix,".tmp")){assert(failure==1 || failure==2 || failure>=5);size_t bytes=failure==5?256:512;assert(files[i].size==bytes && !memcmp(files[i].data,latest_bytes,bytes));}
            else{
                assert(files[i].size==512 && (suffix[1]=='0'||suffix[1]=='1') && !suffix[2]);
                bool destination=(unsigned)(suffix[1]-'0')==((3+prefix)&1);
                assert(!(destination && failure==2));
                const uint8_t *expected=destination?(failure==3 || failure==4?latest_bytes:previous_bytes):current_bytes;
                assert(!memcmp(files[i].data,expected,512));
            }
        }
        assert(seen==(failure==1 || failure>=5?3u:2u));
        ArcRpcCmosKey key={.model=0,.layout=1};strcpy(key.content,id);arc_sha256_hex(rom,size,key.firmware);
        ArcRpcCmosStore recovered;assert(arc_riscpc_cmos_store_load(&recovered,"/save",&key,rpc_store_test_owner(),error,sizeof(error))==ARC_RPC_CMOS_FOUND && !recovered.refused);
        assert(recovered.generation==(failure==3 || failure==4?3+prefix:2+prefix));
        assert(!memcmp(recovered.payload,failure==3 || failure==4?latest+16:prior+16,240));arc_riscpc_cmos_store_close(&recovered);
        use_frontend_host=false;test_vfs_no_open_handles();
    }
    puts("production host: reset CMOS no-op, candidate-only/short/flush/close failure, older destination retired, published-verification failure and completion; exact slot/tmp bytes and generations, with/without preceding publication, next-load recovery passed");
}
static int unload_flush(struct retro_vfs_file_handle *file)
{
    if(fail_unload_flush && strstr(files[file->file].path,"/cmos-riscpc/") && strstr(files[file->file].path,".tmp")){
        fail_unload_flush=false;return -1;
    }
    return test_flush(file);
}
static struct retro_vfs_file_handle *marker_open(const char *path,unsigned mode,unsigned hints)
{if(fail_marker_open && strstr(path,".unsaved"))return NULL;return test_open(path,mode,hints);}
static void host_unload_cases(const uint8_t *rom,size_t size,ArcRpcPreparationHost callbacks)
{
    for(unsigned marker_failure=0;marker_failure<2;++marker_failure){
        restore();clock_mode=0;clock_value=1000000;clock_delta=1000;
        use_frontend_host=true;host_extended=true;host_n13=host_unload=host_marker=0;
        ArcRpcPreparation *p=new_request(rom,size,true,true,callbacks);
        while(arc_riscpc_host_pending(frontend_test_host))arc_riscpc_host_pending_frame(frontend_test_host);
        uint8_t latest[256];arc_rpc_cmos_snapshot(latest);latest[100]^=1;arc_rpc_cmos_seed(latest);
        fail_unload_flush=true;fail_marker_open=marker_failure;vfs.flush=unload_flush;vfs.open=marker_open;
        char error[512]={0};assert(!arc_riscpc_host_free(frontend_test_host,error,sizeof(error)));
        assert(!fail_unload_flush && host_unload==1 && host_marker==marker_failure);
        assert(!strcmp(host_display,"RiscPC settings could not be saved on unload"));
        frontend_test_host=NULL;current=NULL;vfs.flush=test_flush;vfs.open=test_open;fail_marker_open=false;
        unsigned markers=0;for(unsigned i=0;i<TEST_FILES;++i)if(files[i].present && strstr(files[i].path,".unsaved")){assert(!files[i].size);++markers;}
        assert(markers==!marker_failure);test_vfs_no_open_handles();
        p=new_request(rom,size,true,true,callbacks);
        while(arc_riscpc_host_pending(frontend_test_host))arc_riscpc_host_pending_frame(frontend_test_host);
        assert(host_n13==!marker_failure);
        for(unsigned i=0;i<2;++i)arc_riscpc_host_maintenance(frontend_test_host);
        for(unsigned i=0;i<TEST_FILES;++i)assert(!files[i].present || !strstr(files[i].path,".unsaved"));
        assert(arc_riscpc_host_free(frontend_test_host,error,sizeof(error)));
        frontend_test_host=NULL;current=NULL;use_frontend_host=false;test_vfs_no_open_handles();
    }
    restore();clock_mode=0;clock_value=0;clock_delta=1000;use_frontend_host=true;host_extended=true;host_marker=0;
    new_request(rom,size,true,true,callbacks);
    while(arc_riscpc_host_pending(frontend_test_host))arc_riscpc_host_pending_frame(frontend_test_host);
    uint8_t latest[256];arc_rpc_cmos_snapshot(latest);latest[100]^=1;arc_rpc_cmos_seed(latest);
    uint64_t mutations=namespace_calls;rpc_fixture_lost=true;char lost_error[512]={0};
    assert(!arc_riscpc_host_free(frontend_test_host,lost_error,sizeof(lost_error)));
    assert(namespace_calls==mutations && !host_marker && strstr(host_display,"Another app changed") && host_duration==5000);
    for(unsigned i=0;i<TEST_FILES;++i)assert(!files[i].present || !strstr(files[i].path,".unsaved"));
    rpc_fixture_lost=false;frontend_test_host=NULL;current=NULL;use_frontend_host=false;test_vfs_no_open_handles();
    restore();clock_mode=0;clock_value=0;clock_delta=1000;use_frontend_host=true;host_extended=true;host_marker=0;
    ArcLockBackend close_backend=rpc_preparation_test_backend;close_backend.identity=unload_identity;callbacks.lock_backend=&close_backend;
    new_request(rom,size,true,true,callbacks);
    while(arc_riscpc_host_pending(frontend_test_host))arc_riscpc_host_pending_frame(frontend_test_host);
    mutations=namespace_calls;unload_identity_countdown=3; /* teardown identity, unchanged CMOS begin, then HD close */
    assert(!arc_riscpc_host_free(frontend_test_host,lost_error,sizeof(lost_error)) && !unload_identity_countdown);
    assert(namespace_calls==mutations && !host_marker && strstr(host_display,"Another app changed") && host_duration==5000);
    rpc_fixture_lost=false;frontend_test_host=NULL;current=NULL;use_frontend_host=false;test_vfs_no_open_handles();
    puts("production host: unload publication failure after clear, marker success/failure, exact N-13 next-load rule, successful no-op persist removes marker passed");
}
static bool stale_directory_stat;
static int close_target=-1;
static unsigned close_injection,hd_flushes,hd_closes;
static int hd_close_flush(struct retro_vfs_file_handle *file)
{
    if((int)file->file==close_target){++hd_flushes;assert(arc_riscpc_preparation_owned(current));if(close_injection==1)return -1;}
    return test_flush(file);
}
static int hd_close_result(struct retro_vfs_file_handle *file)
{
    bool target=(int)file->file==close_target;if(target)++hd_closes;
    int result=test_close(file);return target && close_injection==2?-1:result;
}
static void host_hd_close_cases(const uint8_t *rom,size_t size,ArcRpcPreparationHost callbacks)
{
    for(unsigned operation=0;operation<3;++operation)for(unsigned injection=1;injection<=2;++injection){
        restore();clock_mode=0;clock_value=1000000;clock_delta=1000;use_frontend_host=true;host_extended=true;host_n17=0;
        ArcRpcPreparation *p=new_request(rom,size,true,true,callbacks);
        while(arc_riscpc_host_pending(frontend_test_host))arc_riscpc_host_pending_frame(frontend_test_host);
        ArcRpcHdStore *disc=arc_riscpc_preparation_disc(p);uint8_t *payload=calloc(256,512);assert(payload);
        for(unsigned i=0;i<8;++i)assert(arc_riscpc_hd_write(disc,ARC_RPC_HD_SECTORS-256,256,payload));free(payload);
        ArcRpcHdStats stats;
        for(unsigned i=0;;++i){assert(i<10000);arc_riscpc_hd_stats(disc,&stats);
            if(stats.state==ARC_RPC_HD_ROTATE && stats.phase_step==1)break;arc_riscpc_hd_tick(disc);}
        test_vfs_rotation_writer();close_target=-1;
        for(unsigned i=0;i<16;++i)if(handles[i].used)close_target=(int)handles[i].file;
        assert(close_target>=0);hd_flushes=hd_closes=0;close_injection=injection;
        vfs.flush=hd_close_flush;vfs.close=hd_close_result;char error[512]={0};
        if(operation==0){assert(!arc_riscpc_host_free(frontend_test_host,error,sizeof(error)));assert(host_n17==1);}
        else if(operation==1){
            arc_engine_stop();assert(!arc_riscpc_host_reset(frontend_test_host));
            assert(arc_riscpc_preparation_state(p)==ARC_RPC_PREP_FAILED && !host_n17);
            assert(arc_riscpc_host_reset(frontend_test_host));assert(host_n17==1 && arc_riscpc_preparation_hd_close_ok(p));
            close_injection=0;while(arc_riscpc_host_pending(frontend_test_host))arc_riscpc_host_pending_frame(frontend_test_host);
            reset_probe=true;reset_steps=0;assert(arc_riscpc_host_reset(frontend_test_host));reset_probe=false;assert(host_n17==1);
            assert(arc_riscpc_host_free(frontend_test_host,error,sizeof(error)));assert(host_n17==1);
        }else{
            rpc_fixture_lost=true;arc_riscpc_host_maintenance(frontend_test_host);
            uint64_t mutations=namespace_calls;assert(!arc_riscpc_host_free(frontend_test_host,error,sizeof(error)));
            assert(namespace_calls==mutations && !hd_flushes);rpc_fixture_lost=false;
        }
        assert(hd_closes==1 && hd_flushes==(operation==2?0:1));
        vfs.flush=test_flush;vfs.close=test_close;close_target=-1;close_injection=0;
        frontend_test_host=NULL;current=NULL;use_frontend_host=false;test_vfs_no_open_handles();
        for(unsigned i=0;i<16;++i)assert(!rpc_fixture_locks[i].used);
    }
    puts("production host: HD flush/close failures retained through stopped Restart and unload, N-17 after clear, discard never flushes or mutates, exactly one close passed");
}
static bool fail_observation_remove;
static int observation_remove(const char *path)
{
    if(fail_observation_remove && strstr(path,"/launch.observation") && !strstr(path,".tmp")){
        fail_observation_remove=false;return -1;
    }
    return test_remove(path);
}
static bool observation_media_identity(void *opaque,uint8_t output[32])
{(void)opaque;memset(output,1,32);return true;}
static void host_observation_fault(const uint8_t *rom,size_t size,ArcRpcPreparationHost callbacks)
{
    (void)rom;(void)size;
    callbacks.media_identity=observation_media_identity;
    int firmware=find("/system/guest-return.rom",false);assert(firmware>=0);
    for(unsigned action=0;action<3;++action){
        restore();clock_mode=0;clock_value=1000000;clock_delta=1000;use_frontend_host=true;host_extended=true;
        launch_test=guest_subject=guest_cells=true;
        ArcRpcPreparation *p=new_request(files[firmware].data,files[firmware].size,true,true,callbacks);
        while(arc_riscpc_host_pending(frontend_test_host))arc_riscpc_host_pending_frame(frontend_test_host);
        ArcRpcLaunchAttempt attempt;arc_riscpc_preparation_attempt(p,&attempt);
        ArcRpcLaunchPlan plan={.action=ARC_RPC_PLAN_BOOT,.media={1}};strcpy(plan.target,"ADFS::0.$.!Boot");
        ArcRpcLaunchController controller;assert(arc_riscpc_controller_build(&plan,&attempt.token,&controller));
        guest_call(controller.image,controller.size,false);assert(arc_rpc_observers_boundary());
        arc_riscpc_preparation_attempt(p,&attempt);assert(arc_riscpc_attempt_established(&attempt));
        char path[1024];snprintf(path,sizeof(path),"/save/arcdroid/hd-riscpc/%s/launch.observation",id);
        for(unsigned i=0;find(path,false)<0;++i){assert(i<32);arc_riscpc_host_maintenance(frontend_test_host);}
        uint64_t generation=content_generation(NULL);uint8_t prior[256],captured[256];
        arc_rpc_cmos_snapshot(prior);prior[100]^=1;arc_rpc_cmos_seed(prior);
        arc_rpc_cmos_snapshot(captured);captured[101]^=1;arc_rpc_cmos_seed(captured);
        fail_observation_remove=true;vfs.remove=observation_remove;
        arc_riscpc_host_maintenance(frontend_test_host);
        vfs.remove=test_remove;assert(!fail_observation_remove);
        assert(arc_riscpc_preparation_state(p)==ARC_RPC_PREP_FAILED && arc_riscpc_preparation_draining(p));
        assert(strstr(host_display,"invalidation failed") && !arc_engine_active());
        if(action==1)assert(arc_riscpc_host_reset(frontend_test_host));
        if(action==2){char error[512]={0};assert(arc_riscpc_host_free(frontend_test_host,error,sizeof(error)));}
        else{unsigned frames=0;while(arc_riscpc_preparation_draining(p)){
            uint64_t ns=namespace_calls;arc_riscpc_host_pending_frame(frontend_test_host);assert(namespace_calls-ns<=1 && ++frames<=60);
        }
        if(!action)assert(strstr(host_display,"latest CMOS snapshot saved"));
        else assert(arc_riscpc_preparation_state(p)==ARC_RPC_PREP_VERIFY);
        char error[512]={0};assert(arc_riscpc_host_free(frontend_test_host,error,sizeof(error)));
        }
        assert(content_generation(captured+16)==generation+1);
        frontend_test_host=NULL;current=NULL;use_frontend_host=false;launch_test=guest_subject=guest_cells=false;test_vfs_no_open_handles();
    }
    puts("production host: real established observation, injected invalidation failure, immediate terminal, captured snapshot and bounded drain passed");
}
static int32_t raced_stat(const char *path,int32_t *size)
{
    if(stale_directory_stat && !strcmp(path,"/save/arcdroid")){stale_directory_stat=false;return 0;}
    return test_stat(path,size);
}
static unsigned losing_attempts;
static void loser_waits(ArcLock *loser)
{
    uint64_t before=namespace_calls;
    assert(arc_lock_acquire(loser)==ARC_LOCK_CONTENDED);++losing_attempts;
    assert(namespace_calls==before && !arc_lock_held(loser));
}
static void store_interleavings(void)
{
    restore();rpc_fixture_create_sentinel=create_sentinel;
    ArcLock owner,loser;arc_lock_content(&owner,"/save",id,&rpc_preparation_test_backend,NULL);
    ArcLockResult result;do{result=arc_lock_acquire(&owner);}while(result==ARC_LOCK_TRANSIENT);assert(result==ARC_LOCK_HELD);
    arc_lock_content(&loser,"/save",id,&rpc_preparation_test_backend,NULL);
    while(arc_lock_next_namespace(&loser))assert(arc_lock_acquire(&loser)==ARC_LOCK_TRANSIENT);
    char error[512]={0};loser_waits(&loser);
    ArcRpcHdStore *disc=arc_riscpc_hd_open("/save",id,true,&owner,error,sizeof(error));assert(disc);
    for(unsigned i=0;i<100;++i){arc_riscpc_hd_tick(disc);ArcRpcHdStats hd;arc_riscpc_hd_stats(disc,&hd);if(hd.state==ARC_RPC_HD_IDLE)break;assert(i<99);}
    assert(arc_riscpc_hd_close(disc,false));
    disc=arc_riscpc_hd_open_begin("/save",id,&owner,error,sizeof(error));assert(disc);
    do{
        loser_waits(&loser);uint64_t before=namespace_calls;
        assert(!arc_riscpc_hd_open_begin("/save",id,&loser,error,sizeof(error)) && namespace_calls==before);
    }while(arc_riscpc_hd_open_step(disc)==ARC_RPC_HD_OPEN_PENDING);
    assert(arc_riscpc_hd_close(disc,false));
    ArcRpcCmosKey key={.model=0,.layout=1};strcpy(key.content,id);memset(key.firmware,'a',64);
    ArcRpcCmosStore settings,blocked;
    assert(arc_riscpc_cmos_store_begin(&settings,"/save",&key,&owner,error,sizeof(error))==ARC_RPC_CMOS_INSPECTING);
    ArcRpcCmosResult state;do{
        loser_waits(&loser);uint64_t before=namespace_calls;
        assert(arc_riscpc_cmos_store_begin(&blocked,"/save",&key,&loser,error,sizeof(error))==ARC_RPC_CMOS_REFUSED && namespace_calls==before);
        state=arc_riscpc_cmos_store_step(&settings,error,sizeof(error));
    }while(state==ARC_RPC_CMOS_INSPECTING);
    assert(state==ARC_RPC_CMOS_MISSING);uint8_t payload[240]={0};payload[47]=1;
    assert(arc_riscpc_cmos_store_put_begin(&settings,payload,NULL,error,sizeof(error)));bool done=false;
    while(!done){
        loser_waits(&loser);uint64_t before=namespace_calls;bool blocked_done=false;
        assert(!arc_riscpc_cmos_store_put_begin(&blocked,payload,NULL,error,sizeof(error)));
        assert(!arc_riscpc_cmos_store_put_step(&blocked,&blocked_done,error,sizeof(error)) && namespace_calls==before);
        assert(arc_riscpc_cmos_store_put_step(&settings,&done,error,sizeof(error)));
    }
    assert(arc_riscpc_cmos_store_mark_unsaved(&settings));loser_waits(&loser);uint64_t before=namespace_calls;
    assert(!arc_riscpc_cmos_store_mark_unsaved(&blocked) && namespace_calls==before);
    assert(arc_riscpc_cmos_store_put(&settings,payload,NULL,error,sizeof(error)));
    ArcRpcLaunchRecordStore observation,blocked_observation={.owner=&loser,.phase=10};
    assert(arc_riscpc_launch_record_begin(&observation,"/save",id,&owner));done=false;
    while(!done){
        loser_waits(&loser);before=namespace_calls;bool other_done=false;
        assert(!arc_riscpc_launch_record_recover(&blocked_observation,&other_done) && namespace_calls==before);
        assert(arc_riscpc_launch_record_recover(&observation,&done));
    }
    ArcRpcBootRecord record={.observation=ARC_RPC_OUTCOME_RETURNED,.key={.cpu=1,.plan_revision=2,.profile_revision=2,.action=1}};
    memset(record.key.firmware,1,32);memset(record.key.configuration,2,32);memset(record.key.content,3,32);memset(record.key.media,4,32);
    memset(record.key.store,5,32);memset(record.key.plan,6,32);memset(record.key.helper,7,32);
    assert(arc_riscpc_launch_record_publish_begin(&observation,&record));done=false;
    while(!done){
        loser_waits(&loser);before=namespace_calls;bool other_done=false;
        assert(!arc_riscpc_launch_record_publish_step(&blocked_observation,true,&other_done) && namespace_calls==before);
        assert(arc_riscpc_launch_record_publish_step(&observation,true,&done));
    }
    blocked_observation.owner=&loser;blocked_observation.published=true;strcpy(blocked_observation.path,observation.path);
    loser_waits(&loser);before=namespace_calls;
    assert(!arc_riscpc_launch_record_invalidate(&blocked_observation) && namespace_calls==before && find(observation.path,false)>=0);
    assert(arc_riscpc_launch_record_invalidate(&observation));arc_riscpc_launch_record_close(&observation);
    arc_riscpc_cmos_store_close(&settings);arc_riscpc_cmos_store_close(&blocked);
    arc_lock_release(&loser);arc_lock_release(&owner);
    ArcLock defaults;arc_lock_defaults(&defaults,"/save",key.firmware,0,4,1,&rpc_preparation_test_backend,NULL);
    do{result=arc_lock_acquire(&defaults);}while(result==ARC_LOCK_TRANSIENT);assert(result==ARC_LOCK_HELD);
    key.defaults=true;key.procedure=4;key.timing=1;
    assert(arc_riscpc_cmos_store_load(&settings,"/save",&key,&defaults,error,sizeof(error))==ARC_RPC_CMOS_MISSING);
    payload[183]=2;payload[47]=3;ArcRpcCmosEvidence evidence={.rate=20,.epoch=ARC_RPC_CMOS_GENERATION_EPOCH,.writes=240,.last_write=1};
    arc_riscpc_cmos_required(1,evidence.mask);assert(arc_riscpc_cmos_oracle(1,payload,&evidence));
    assert(arc_riscpc_cmos_store_put(&settings,payload,&evidence,error,sizeof(error)));
    payload[0]=7;payload[47]+=7;assert(arc_riscpc_cmos_oracle(1,payload,&evidence));
    arc_lock_release(&defaults);before=namespace_calls;
    assert(!arc_riscpc_cmos_store_put_begin(&settings,payload,&evidence,error,sizeof(error)) && namespace_calls==before);
    arc_riscpc_cmos_store_close(&settings);test_vfs_no_open_handles();assert(losing_attempts>20);printf("interleaved native loser attempts=%u\n",losing_attempts);
    /* Two directory creators: the first saw absence before its mkdir raced
     * with the second's successful creation. Re-stat of -2 must accept it. */
    stale_directory_stat=true;vfs.stat=raced_stat;size_t cursor1=0,cursor2=0;
    uint64_t operations1=0,operations2=0,namespaces1=0,namespaces2=0;bool done1=false,done2=false;
    while(!done1)assert(arc_io_make_path_step("/save/arcdroid",&cursor1,&done1,&operations1,&namespaces1));
    while(!done2)assert(arc_io_make_path_step("/save/arcdroid",&cursor2,&done2,&operations2,&namespaces2));
    assert(operations1==4 && operations2==2 && namespaces1==1 && namespaces2==0);vfs.stat=test_stat;
    puts("ownership: two handles interleaved at every HD recovery, CMOS inspect/publish, observation recover/publish/invalidate unit; loser never mutates or truncates; defaults mutation after release refused");
}
static void ownership_refusals(const uint8_t *rom,size_t size,ArcRpcPreparationHost callbacks)
{
    for(unsigned reason=0;reason<7;++reason){
        restore();clock_mode=0;clock_value=0;clock_delta=1000000000;
        uint8_t seed[256]={0};char error[512]={0};
        ArcRpcPreparationRequest request={.firmware=rom,.firmware_size=size,.save=reason==1?"relative":reason==2?"scheme://authored":"/save",.system="/system",.seed=seed};
        request.key.model=0;request.key.layout=1;strcpy(request.key.content,id);
        rpc_fixture_open_error=reason==4?EACCES:reason==5?EINTR:0;
        rpc_fixture_try_error=reason==0?EINVAL:reason==6?EACCES:0;
        current=arc_riscpc_preparation_low_level(&request,&callbacks,error,sizeof(error));assert(current);
        if(reason==3){fault=11;fail_at=steps+1;}
        for(unsigned i=0;arc_riscpc_preparation_state(current)<ARC_RPC_PREP_RUNNING;++i){assert(i<64);frame(current);}
        assert(arc_riscpc_preparation_state(current)==ARC_RPC_PREP_FAILED && !arc_engine_active());
        assert(!arc_riscpc_preparation_disc(current) && !strncmp(last_notice_id,"N-15",4));
        for(unsigned i=0;i<TEST_FILES;++i)if(files[i].present)assert(!strstr(files[i].path,"/cmos-riscpc/") && !strstr(files[i].path,"/hd-riscpc/"));
        reset_fault();rpc_fixture_open_error=rpc_fixture_try_error=0;finish(current);
        if(reason==2){
            request.hard_disc=true;uint64_t calls=namespace_calls;
            assert(!arc_riscpc_preparation_low_level(&request,&callbacks,error,sizeof(error)) && namespace_calls==calls);
        }
    }
    restore();clock_mode=0;clock_value=0;clock_delta=1000;rpc_fixture_try_error=EAGAIN;
    uint8_t seed[256]={0};char error[512]={0};
    ArcRpcPreparationRequest request={.firmware=rom,.firmware_size=size,.save="/save",.system="/system",.seed=seed};
    request.key.model=0;request.key.layout=1;strcpy(request.key.content,id);
    current=arc_riscpc_preparation_low_level(&request,&callbacks,error,sizeof(error));assert(current);
    ArcRpcPreparationStats stats={0};
    while(!stats.lock_attempts){frame(current);arc_riscpc_preparation_stats(current,&stats);}
    assert(!arc_riscpc_preparation_owned(current));clock_value+=UINT64_C(20000000000);rpc_fixture_try_error=0;
    frame(current);assert(arc_riscpc_preparation_owned(current) && arc_riscpc_preparation_state(current)==ARC_RPC_PREP_VERIFY);
    assert(!*arc_riscpc_preparation_error(current));arc_riscpc_preparation_cancel(current);finish(current);
    puts("ownership: N-15 unsupported, relative/URI non-HD roots, mkdir/open/lock error and transient expiry; hard-disc URI remains immediate admission refusal passed");
}
static void host_cases(const uint8_t *rom,size_t size,ArcRpcPreparationHost callbacks)
{
    for(unsigned fallback=0;fallback<2;++fallback){
        restore();clock_mode=0;clock_value=1000000;clock_delta=1000;attach_calls=0;
        use_frontend_host=true;host_extended=!fallback;host_messages=host_clears=host_logs=0;rpc_fixture_lost=false;
        ArcRpcPreparation *p=new_request(rom,size,true,true,callbacks);
        for(unsigned i=0;arc_riscpc_host_pending(frontend_test_host);++i){assert(i<10000);
            arc_riscpc_host_frame_begin(frontend_test_host);arc_riscpc_host_pending_frame(frontend_test_host);arc_riscpc_host_frame_end(frontend_test_host);
            assert(arc_riscpc_preparation_state(p)<ARC_RPC_PREP_FAILED);
        }
        assert(attach_calls==1 && !strcmp(host_display,"Starting RiscPC") && host_logs);
        arc_riscpc_host_advisory(frontend_test_host,"N-8","Authored advisory");
        for(unsigned i=0;i<299;++i){arc_riscpc_host_frame_begin(frontend_test_host);arc_riscpc_host_frame_end(frontend_test_host);assert(!strcmp(host_display,"Authored advisory"));}
        arc_riscpc_host_frame_begin(frontend_test_host);arc_riscpc_host_frame_end(frontend_test_host);
        assert(!strcmp(host_display,"Starting RiscPC"));
        arc_riscpc_host_guest_time(frontend_test_host,24999999);assert(*host_display);
        arc_riscpc_host_guest_time(frontend_test_host,25000000);assert(!*host_display);
        unsigned clears_before=host_clears;reset_probe=true;reset_steps=0;
        assert(arc_riscpc_host_reset(frontend_test_host));assert(reset_steps==2);reset_probe=false;
        assert(host_clears==clears_before+1 && !strcmp(host_display,"Starting RiscPC"));
        uint64_t mutations=namespace_calls;rpc_fixture_lost=true;arc_riscpc_host_maintenance(frontend_test_host);
        assert(arc_riscpc_preparation_state(p)==ARC_RPC_PREP_FAILED && namespace_calls==mutations);
        assert(strstr(host_display,"Another app changed") && !strstr(host_display,"snapshot"));
        assert(arc_riscpc_host_pending(frontend_test_host));
        for(unsigned i=0;i<120;++i){arc_riscpc_host_frame_begin(frontend_test_host);arc_riscpc_host_pending_frame(frontend_test_host);arc_riscpc_host_frame_end(frontend_test_host);}
        assert(namespace_calls==mutations && strstr(host_display,"Another app changed"));
        char error[512]={0};assert(!arc_riscpc_host_free(frontend_test_host,error,sizeof(error)));
        frontend_test_host=NULL;current=NULL;use_frontend_host=false;rpc_fixture_lost=false;test_vfs_no_open_handles();
    }
    restore();clock_mode=0;clock_value=1000000;clock_delta=1000;
    use_frontend_host=true;host_extended=true;
    ArcRpcPreparation *stopped=new_request(rom,size,true,true,callbacks);
    while(!arc_riscpc_preparation_owned(stopped))arc_riscpc_host_pending_frame(frontend_test_host);
    stopped_failure=true;uint64_t before_failure=namespace_calls;
    assert(!arc_riscpc_host_reset(frontend_test_host));
    assert(arc_riscpc_preparation_state(stopped)==ARC_RPC_PREP_FAILED && !arc_engine_active());
    assert(arc_riscpc_preparation_owned(stopped) && namespace_calls==before_failure);
    stopped_failure=false;assert(arc_riscpc_host_reset(frontend_test_host));
    assert(arc_riscpc_preparation_state(stopped)==ARC_RPC_PREP_VERIFY && arc_riscpc_preparation_owned(stopped));
    char stopped_error[512]={0};assert(arc_riscpc_host_free(frontend_test_host,stopped_error,sizeof(stopped_error)));
    frontend_test_host=NULL;current=NULL;use_frontend_host=false;
    ArcLock absent={0};uint64_t mutations=namespace_calls;char error[512]={0};
    assert(!arc_riscpc_hd_open_begin("/save",id,&absent,error,sizeof(error)));
    ArcRpcCmosStore cmos;ArcRpcCmosKey key={.model=0,.layout=1};strcpy(key.content,id);memset(key.firmware,'a',64);
    assert(arc_riscpc_cmos_store_begin(&cmos,"/save",&key,&absent,error,sizeof(error))==ARC_RPC_CMOS_REFUSED);
    uint8_t payload[240]={0};assert(!arc_riscpc_cmos_store_put_begin(&cmos,payload,NULL,error,sizeof(error)));
    ArcRpcLaunchRecordStore observation;assert(!arc_riscpc_launch_record_begin(&observation,"/save",id,&absent));
    assert(namespace_calls==mutations);
    puts("production host: authored production begin, both message paths, hold/advisory lifetime, reset clear, N-18 final, lost-owner zero mutation, token-required stores and clean teardown passed");
}
static void host_measurements(const uint8_t *rom,size_t size,ArcRpcPreparationHost callbacks)
{
    for(unsigned fast=0;fast<3;++fast){
        restore();clock_mode=0;clock_value=100000000;clock_delta=0;use_frontend_host=true;host_extended=true;
        fast_answer=fast==1?1:-1;measure_summaries=true;summary_count=0;
        new_request(rom,size,true,true,callbacks);uint64_t entry=clock_value;
        if(fast==2){arc_riscpc_host_first_field(frontend_test_host,640,480,50,false);assert(arc_riscpc_host_reset(frontend_test_host));}
        for(unsigned i=0;i<100;++i){
            if(i)entry+=i==99?600000000:i==98?50000000:20000000;
            clock_value=entry;arc_riscpc_host_frame_begin(frontend_test_host);
            clock_value+=i==99?150000000:i==98?40000000:1000000;
            arc_riscpc_host_frame_end(frontend_test_host);
        }
        char error[512]={0};assert(arc_riscpc_host_free(frontend_test_host,error,sizeof(error)) && summary_count==2);
        for(unsigned i=0;i<2;++i){const char *line=measured_summaries[i];
            assert(!strchr(line,'\n') && strstr(line,"frames=100") && strstr(line,"state=0") && strstr(line,"frontend_av=0 guest_av=0"));
            assert(strstr(line,fast==1?"fast_forward=yes":"fast_forward=unknown"));
            assert(strstr(line,fast==2?"first_pending_ms=1 running_ms=-1 first_field_ms=0 desktop=unavailable":"first_pending_ms=1 running_ms=-1 first_field_ms=-1 desktop=unavailable"));
            assert(strstr(line,"offline={frames:100,work_max_ns:0,run_max_ns:150000000,run_p99_005T:49,interval_max_ns:50000000,interval_p99_005T:61,over_2T:1,frontend_gaps:1}"));
            for(unsigned phase=0;phase<4;++phase)assert(strstr(line,(const char *[]){"generation={frames:0","attach={frames:0","live={frames:0","drain={frames:0"}[phase]));
            assert(strstr(line,"D169=0 D162=0 reset_exceptions=0") && strstr(line,"lock_attempts=0 lock_wait_frames=0 identity_checks=0"));
        }
        measure_summaries=false;fast_answer=-1;frontend_test_host=NULL;current=NULL;use_frontend_host=false;test_vfs_no_open_handles();
    }
    static const unsigned models[]={0,1,3,2,1,3,2};
    static const ArcRpcSelectionOs operating_systems[]={ARC_RPC_OS_350,ARC_RPC_OS_360,ARC_RPC_OS_360,ARC_RPC_OS_370,ARC_RPC_OS_371,ARC_RPC_OS_371,ARC_RPC_OS_371};
    for(unsigned pair=0;pair<7;++pair){
        restore();test_model=models[pair];test_os=operating_systems[pair];clock_mode=0;clock_value=1000000;clock_delta=1000;use_frontend_host=true;host_extended=true;
        for(unsigned receipt=0;receipt<2;++receipt){
            new_request(rom,size,true,true,callbacks);
            while(arc_riscpc_host_pending(frontend_test_host))arc_riscpc_host_pending_frame(frontend_test_host);
            assert(!strcmp(host_display,"Starting RiscPC"));arc_riscpc_host_guest_time(frontend_test_host,24999999);assert(*host_display);
            arc_riscpc_host_guest_time(frontend_test_host,25000000);assert(!*host_display);
            char error[512]={0};assert(arc_riscpc_host_free(frontend_test_host,error,sizeof(error)));frontend_test_host=NULL;current=NULL;
        }
        use_frontend_host=false;test_vfs_no_open_handles();
    }
    test_model=0;test_os=ARC_RPC_OS_350;
    puts("production host: nearest-rank p99, exact maxima, gap exclusion/over-2T, fast-forward known/unknown, two one-line summaries/endpoints, all seven authored pair holds on first-use/reopen passed");
}
typedef struct {uint8_t *bytes;size_t size,start;} BufferedWrite;
static BufferedWrite buffered_writes[16];
static unsigned explicit_buffer_flushes,implicit_buffer_writes;
static bool buffer_commit(struct retro_vfs_file_handle *file,bool implicit)
{
    BufferedWrite *buffer=&buffered_writes[file-handles];if(!buffer->size)return true;
    assert(!rpc_fixture_lost);if(implicit)++implicit_buffer_writes;else ++explicit_buffer_flushes;
    file->at=buffer->start;bool ok=test_write(file,buffer->bytes,buffer->size)==(int64_t)buffer->size;
    free(buffer->bytes);memset(buffer,0,sizeof(*buffer));return ok;
}
static int64_t buffered_write(struct retro_vfs_file_handle *file,const void *data,uint64_t size)
{
    BufferedWrite *buffer=&buffered_writes[file-handles];assert(size<=SIZE_MAX-buffer->size);
    if(!buffer->size)buffer->start=file->at;
    else assert(file->at==buffer->start+buffer->size);
    uint8_t *bytes=realloc(buffer->bytes,buffer->size+(size_t)size);assert(bytes);buffer->bytes=bytes;
    memcpy(bytes+buffer->size,data,(size_t)size);buffer->size+=(size_t)size;file->at+=(size_t)size;
    if(file->at>file->cached_size)file->cached_size=file->at;return (int64_t)size;
}
static int buffered_flush(struct retro_vfs_file_handle *file)
{return buffer_commit(file,false)?test_flush(file):-1;}
static int buffered_close(struct retro_vfs_file_handle *file)
{bool flushed=buffer_commit(file,true);int result=test_close(file);return flushed?result:-1;}
static bool buffered_identity(void *opaque,intptr_t handle,const char *path)
{
    /* A boundary identity check must never coexist with buffered dirty data.
     * Empty retained descriptors remain legal, including discard close. */
    for(unsigned i=0;i<16;++i)assert(!buffered_writes[i].size);
    return rpc_fixture_identity(opaque,handle,path);
}
static void buffered_ownership(void)
{
    ArcLockBackend backend=rpc_preparation_test_backend;backend.identity=buffered_identity;
    for(unsigned kind=0;kind<3;++kind)for(unsigned boundary=0;boundary<(kind==0?1u:kind==1?10u:6u);++boundary){
        restore();rpc_fixture_create_sentinel=create_sentinel;rpc_fixture_lost=false;
        vfs.write=buffered_write;vfs.flush=buffered_flush;vfs.close=buffered_close;
        if(!kind){
            struct retro_vfs_file_handle *proof=test_open("/save/buffering-close-proof",2,0);assert(proof);
            assert(buffered_write(proof,"abc",3)==3 && files[proof->file].size==0);
            unsigned file=proof->file;assert(!buffered_close(proof) && files[file].size==3 && !memcmp(files[file].data,"abc",3));
            assert(implicit_buffer_writes==1);implicit_buffer_writes=0;
        }
        ArcLock owner;assert(arc_lock_content(&owner,"/save",id,&backend,NULL)==ARC_LOCK_TRANSIENT);
        ArcLockResult result;do{result=arc_lock_acquire(&owner);}while(result==ARC_LOCK_TRANSIENT);assert(result==ARC_LOCK_HELD);
        char error[512]={0};bool done=false;ArcRpcHdStore *disc=NULL;ArcRpcCmosStore cmos={0};ArcRpcLaunchRecordStore record={0};
        if(kind==0){
            disc=arc_riscpc_hd_open("/save",id,true,&owner,error,sizeof(error));assert(disc);
            while(true){ArcRpcHdStats stats;arc_riscpc_hd_stats(disc,&stats);if(stats.state==ARC_RPC_HD_IDLE)break;arc_riscpc_hd_tick(disc);}
            uint8_t *payload=calloc(256,512);assert(payload);
            for(unsigned i=0;i<8;++i)assert(arc_riscpc_hd_write(disc,8192,256,payload));free(payload);
            while(true){ArcRpcHdStats stats;arc_riscpc_hd_stats(disc,&stats);if(stats.state==ARC_RPC_HD_ROTATE && stats.phase_step==1)break;arc_riscpc_hd_tick(disc);}
        }else if(kind==1){
            ArcRpcCmosKey key={.model=0,.layout=1};strcpy(key.content,id);memset(key.firmware,'a',64);uint8_t payload[240]={0};
            assert(arc_riscpc_cmos_store_load(&cmos,"/save",&key,&owner,error,sizeof(error))==ARC_RPC_CMOS_MISSING);
            assert(arc_riscpc_cmos_store_put(&cmos,payload,NULL,error,sizeof(error)));payload[84]=1;
            assert(arc_riscpc_cmos_store_put_begin(&cmos,payload,NULL,error,sizeof(error)));
            for(unsigned i=0;i<boundary;++i)assert(arc_riscpc_cmos_store_put_step(&cmos,&done,error,sizeof(error)));
        }else{
            assert(arc_riscpc_launch_record_begin(&record,"/save",id,&owner));while(!done)assert(arc_riscpc_launch_record_recover(&record,&done));
            ArcRpcBootRecord observation={.observation=ARC_RPC_OUTCOME_RETURNED,.key={.cpu=1,.plan_revision=2,.profile_revision=2,.action=1}};
            memset(observation.key.firmware,1,32);memset(observation.key.configuration,2,32);memset(observation.key.content,3,32);
            memset(observation.key.media,4,32);memset(observation.key.store,5,32);memset(observation.key.plan,6,32);memset(observation.key.helper,7,32);
            assert(arc_riscpc_launch_record_publish_begin(&record,&observation));
            for(unsigned i=0;i<boundary;++i)assert(arc_riscpc_launch_record_publish_step(&record,true,&done));
        }
        uint64_t mutations=namespace_calls;rpc_fixture_lost=true;assert(arc_lock_verify(&owner)==ARC_LOCK_IDENTITY_LOST);
        if(kind==0)assert(arc_riscpc_hd_close(disc,true));
        else if(kind==1){assert(!arc_riscpc_cmos_store_put_step(&cmos,&done,error,sizeof(error)));arc_riscpc_cmos_store_close(&cmos);}
        else{assert(!arc_riscpc_launch_record_publish_step(&record,true,&done));arc_riscpc_launch_record_close(&record);}
        assert(namespace_calls==mutations && !implicit_buffer_writes);arc_lock_release(&owner);rpc_fixture_lost=false;test_vfs_no_open_handles();
        vfs.write=test_write;vfs.flush=test_flush;vfs.close=test_close;
    }
    assert(explicit_buffer_flushes);puts("buffering VFS: close demonstrably flushes pending bytes; HD/CMOS/observation ownership loss at publication boundaries leaves no buffered writes or namespace mutation");
}
#include "rpc_host_run_b.inc"
int main(int argc,char **argv)
{
    assert(argc==5);bool sanitizer=!strcmp(argv[4],"sanitizer") || !strcmp(argv[4],"sanitizer-idle");
    if(!strcmp(argv[4],"sanitizer-idle") || !strcmp(argv[4],"mutant-idle")){test_model=2;test_os=ARC_RPC_OS_371;}
    fresh_files();
    char source[4096];snprintf(source,sizeof(source),"%s/arcdroid/riscpc/boot/HardDisc4.zip",argv[1]);
    file_copy("/system/arcdroid/riscpc/boot/HardDisc4.zip",source);
    snprintf(source,sizeof(source),"%s/arcdroid/riscpc/boot/PlingSystem.zip",argv[1]);
    file_copy("/system/arcdroid/riscpc/boot/PlingSystem.zip",source);
    snprintf(source,sizeof(source),"%s/cap.zip",argv[1]);file_copy("/system/cap.zip",source);
    snprintf(source,sizeof(source),"%s/product.zip",argv[1]);file_copy("/system/product.zip",source);
    snprintf(source,sizeof(source),"%s/guest-return.rom",argv[1]);file_copy("/system/guest-return.rom",source);
    snprintf(source,sizeof(source),"%s/guest-reset.rom",argv[1]);file_copy("/system/guest-reset.rom",source);
    file_copy("/system/full.rom",argv[2]);int i=find("/system/full.rom",false);assert(i>=0);
    size_t size=files[i].size;uint8_t *rom=malloc(size);assert(rom);memcpy(rom,files[i].data,size);
    construction_and_attachments(rom,size);
    file_bytes("/system/attachment.raw","media",5);
    snapshot();ArcRpcPreparationHost host={.lock_backend=&rpc_preparation_test_backend,.clock=clock_read,.notice=notice,.attach=attach,.attached=observer_attached};
    if(!strcmp(argv[4],"host-privacy")){
        for(unsigned fallback=0;fallback<2;++fallback){
            restore();clock_mode=0;clock_value=0;clock_delta=1000;use_frontend_host=true;host_extended=!fallback;
            ArcRpcPreparation *p=new_request(rom,size,true,true,host);
            for(unsigned frame=0;arc_riscpc_preparation_state(p)<ARC_RPC_PREP_RUNNING;++frame){assert(frame<10000);arc_riscpc_host_pending_frame(frontend_test_host);}
            assert(arc_riscpc_preparation_state(p)==ARC_RPC_PREP_FAILED);
            assert(strstr(host_display,"a file in the boot archives") && !strstr(host_display,"PRIVATE-SENTINEL"));
            char error[512]={0};assert(arc_riscpc_host_free(frontend_test_host,error,sizeof(error)));
            frontend_test_host=NULL;current=NULL;use_frontend_host=false;test_vfs_no_open_handles();
        }
        free(rom);fresh_files();puts("production host privacy: boot-archive member is redacted before module error buffer on both message paths and every host log level");return 0;
    }
    if(!strcmp(argv[4],"scheduler")){scheduler_matrix(rom,size,host);free(rom);fresh_files();return 0;}
    if(!strcmp(argv[4],"host-b")){host_run_b(rom,size,host);free(rom);fresh_files();return 0;}
    if(!strcmp(argv[4],"host")){
        host_cases(rom,size,host);host_fault_cases(rom,size,host);host_reset_inflight(rom,size,host);host_reset_publication_failures(rom,size,host);host_unload_cases(rom,size,host);host_hd_close_cases(rom,size,host);host_observation_fault(rom,size,host);host_measurements(rom,size,host);ownership_refusals(rom,size,host);store_interleavings();buffered_ownership();free(rom);fresh_files();return 0;
    }
    if(!strcmp(argv[4],"retirement-failure")){
        retirement_cases(rom,size,host,true);free(rom);fresh_files();return 0;
    }
    if(!strcmp(argv[4],"launch")){
        guest_reboots(host);retirement_cases(rom,size,host,false);
        helper_ownership(rom,size,host);launch_reset_matrix(rom,size,host);free(rom);fresh_files();
        for(unsigned j=0;j<TEST_FILES;++j){free(baseline[j].data);baseline[j].data=NULL;}
        puts("RPC preparation: reset at four delivery points, callback failures, barrier and stale markers passed");return 0;
    }
    /* Initial package validation precedes either CMOS generation or seed. */
    state_matrix(rom,size,host,sanitizer,!strcmp(argv[4],"mutant") || !strcmp(argv[4],"mutant-idle"));
    helper_ownership(rom,size,host);launch_reset_matrix(rom,size,host);
    scheduler_matrix(rom,size,host);hd_notices(rom,size,host);store_refusals(rom,size,host);
    uint8_t gold[32];
    for(unsigned mode=0;mode<3;++mode){
        restore();clock_mode=mode;clock_value=UINT64_C(1000000000000);clock_delta=1000;attach_calls=terminal_notices=0;
        ArcRpcPreparation *p=new_request(rom,size,true,mode==0,host);settle(p);
        assert(arc_riscpc_preparation_state(p)==ARC_RPC_PREP_RUNNING && attach_calls==1 && !terminal_notices);
        uint8_t b[256],m[32];uint64_t w,l,now;unsigned capture;
        arc_engine_rpc_evidence(b,m,&w,&l,&now,&capture);
        for(unsigned i=0;i<16;++i)assert(!b[i]);
        assert(now==0 && b[69]==5 && b[75]==2 && !(b[80]&0x10) && b[19]==0 && b[253]==9 && b[199]==0x95 && b[63]==0x51);
        assert(arc_riscpc_preparation_reset(p));arc_engine_rpc_evidence(b,m,&w,&l,&now,&capture);
        for(unsigned i=0;i<16;++i)assert(!b[i]);
        assert(now==0 && b[69]==5 && b[75]==2 && !(b[80]&0x10) && b[19]==0 && b[253]==9 && b[199]==0x95 && b[63]==0x51);
        ArcRpcHdStats before_tick,after_tick;
        arc_riscpc_hd_stats(arc_riscpc_preparation_disc(p),&before_tick);
        unsigned reads=all_clock_reads;
        arc_riscpc_preparation_maintenance(p);
        assert(all_clock_reads==reads+4); /* Maintenance plus CMOS publication-begin identity, both measured. */
        arc_riscpc_hd_stats(arc_riscpc_preparation_disc(p),&after_tick);
        assert(before_tick.state==ARC_RPC_HD_RETIRE && after_tick.phase_step==1);
        uint8_t hash[32];disk_hash(hash);if(!mode)memcpy(gold,hash,32);else assert(!memcmp(gold,hash,32));
        finish(p);
    }
    /* Reverify the selected family before opening any store or generation. */
    restore();clock_mode=1;terminal_notices=attach_calls=0;
    ArcDigest original=arc_digest(rom,size);ArcRpcFirmwareFingerprint fingerprint={test_os,size,original.crc32,original.sha1};
    ArcRpcPreparationRequest request={.firmware=rom,.firmware_size=size,.fingerprint=&fingerprint,.save="/save",.system="/system",.epoch=946684800,.hard_disc=true,.populate=true};
    request.selection=(ArcRpcSelection){.reason=ARC_RPC_SELECTION_SELECTED,.model=(ArcRpcSelectionModel)(test_model+1),.os=test_os};
    request.key.model=test_model;request.key.layout=test_os==ARC_RPC_OS_350?1:3;strcpy(request.key.content,id);
    uint8_t *bad_rom=malloc(size);assert(bad_rom);memcpy(bad_rom,rom,size);bad_rom[4096]^=1;request.firmware=bad_rom;
    char verify_error[512];ArcRpcPreparation *unverified=arc_riscpc_preparation_begin(&request,&host,verify_error,sizeof(verify_error));
    assert(unverified);settle(unverified);
    assert(arc_riscpc_preparation_state(unverified)==ARC_RPC_PREP_FAILED && terminal_notices==1 && !attach_calls && !arc_engine_active());
    assert(strstr(last_notice,"firmware/") && strstr(last_notice,arc_riscpc_selection_os_name(test_os)));
    assert(!arc_riscpc_preparation_disc(unverified));finish(unverified);free(bad_rom);
    /* A later pinned-package failure preserves the completed CMOS seed and
     * cannot start a requested guest. */
    restore();clock_mode=0;clock_delta=2500000;attach_calls=terminal_notices=0;
    ArcRpcPreparation *later=new_request(rom,size,true,false,host);
    for(unsigned j=0;arc_riscpc_preparation_state(later)!=ARC_RPC_PREP_POPULATE;++j){assert(j<100000);frame(later);}
    int package=find("/system/arcdroid/riscpc/boot/HardDisc4.zip",false);assert(package>=0);
    files[package].data[files[package].size-1]^=1;
    settle(later);assert(arc_riscpc_preparation_state(later)==ARC_RPC_PREP_FAILED && !attach_calls && terminal_notices==1);
    bool saved=false;for(unsigned j=0;j<TEST_FILES;++j)if(files[j].present && strstr(files[j].path,"/cmos-riscpc/") && !strstr(files[j].path,".tmp"))saved=true;
    assert(saved);finish(later);
    /* Restore a complete root from the last successful preparation for reopen. */
    restore();clock_mode=1;later=new_request(rom,size,true,false,host);settle(later);finish(later);
    /* Receipted reopen performs no package reads, even when files disappear. */
    int a=find("/system/arcdroid/riscpc/boot/HardDisc4.zip",false),b=find("/system/arcdroid/riscpc/boot/PlingSystem.zip",false);
    files[a].present=files[b].present=false;clock_mode=1;
    reopen_expected=true;
    ArcRpcPreparation *p=new_request(rom,size,true,false,host);settle(p);assert(arc_riscpc_preparation_state(p)==ARC_RPC_PREP_RUNNING);
    uint8_t saved_bytes[256],saved_mask[32];uint64_t saved_writes,saved_last,saved_now;unsigned saved_state;
    arc_engine_rpc_evidence(saved_bytes,saved_mask,&saved_writes,&saved_last,&saved_now,&saved_state);
    for(unsigned i=0;i<16;++i)assert(!saved_bytes[i]);
    assert(saved_bytes[69]==5 && saved_bytes[75]==2 && !(saved_bytes[80]&0x10) && saved_bytes[19]==0 && saved_bytes[253]==9 && saved_bytes[199]==0x95 && saved_bytes[63]==0x51);finish(p);reopen_expected=false;
    /* A receipt remains a reopen even if its per-content settings vanished. */
    for(unsigned j=0;j<TEST_FILES;++j)if(files[j].present && strstr(files[j].path,"/cmos-riscpc/"))files[j].present=false;
    reopen_expected=true;clock_mode=3;p=new_request(rom,size,false,false,host);
    while(arc_riscpc_preparation_state(p)!=ARC_RPC_PREP_GENERATE)frame(p);
    frame(p);arc_riscpc_preparation_cancel(p);finish(p);reopen_expected=false;
    char writer[4096];snprintf(writer,sizeof(writer),"%s",argv[2]);char *leaf=strrchr(writer,'/'),*backslash=strrchr(writer,'\\');
    if(backslash && (!leaf || backslash>leaf))leaf=backslash;assert(leaf);strcpy(leaf+1,"writer.rom");
    pending_unload(writer,host);
    /* Missing packages terminalize before the isolated engine starts; failed
     * preparation never refreshes progress. */
    restore();files[a].present=false;terminal_notices=0;attach_calls=0;
    p=new_request(rom,size,false,false,host);settle(p);
    assert(arc_riscpc_preparation_state(p)==ARC_RPC_PREP_FAILED && !attach_calls && !arc_engine_active());
    assert(terminal_notices==1 && strstr(last_notice,"boot/"));unsigned count=notices;
    for(unsigned j=0;j<180;++j)arc_riscpc_preparation_frame(p);assert(notices==count);
    assert(arc_riscpc_preparation_reset(p));assert(arc_riscpc_preparation_state(p)==ARC_RPC_PREP_VERIFY);settle(p);
    assert(arc_riscpc_preparation_state(p)==ARC_RPC_PREP_FAILED && terminal_notices==2 && !arc_engine_active());finish(p);
    /* Follow the production module with an ordinary libretro Arculator load. */
    fresh_files();snprintf(source,sizeof(source),"%s/arcdroid-test.rom",argv[3]);
    file_copy("/system/arcdroid/firmware/authored.data",source);
    snprintf(source,sizeof(source),"%s/arcdroid-test.adf",argv[3]);file_copy("/content.adf",source);
    retro_set_environment(environment);retro_set_video_refresh(video);retro_set_audio_sample_batch(audio);
    retro_set_input_poll(poll);retro_set_input_state(input);retro_init();
    struct retro_game_info content={.path="/content.adf"};assert(retro_load_game(&content));assert(!arc_engine_is_rpc());
    for(unsigned j=0;j<4;++j)retro_run();assert(video_calls==4&&audio_calls==4);
    retro_unload_game();
    int medium=find("/content.adf",false);assert(medium>=0);char identity[41];arc_sha1_hex(files[medium].data,files[medium].size,identity);
    bool own_cmos=false;for(unsigned j=0;j<TEST_FILES;++j)if(files[j].present && strstr(files[j].path,"/arcdroid/cmos/")){
        assert(strstr(files[j].path,identity) && !strstr(files[j].path,"cmos-riscpc"));
        assert(files[j].size==256 && !memcmp(files[j].data,"ARCCMOS2",8));own_cmos=true;
    }
    assert(own_cmos);assert(retro_load_game(&content));assert(!arc_engine_is_rpc());retro_run();retro_unload_game();
    retro_deinit();assert(!arc_engine_active());test_vfs_no_open_handles();
    free(rom);fresh_files();for(unsigned j=0;j<TEST_FILES;++j){free(baseline[j].data);baseline[j].data=NULL;}
    puts("production orchestration: states/reset/cancel, clock confinement, authoritative seed, notices, receipt reopen, then ordinary Arculator passed");
    return 0;
}
