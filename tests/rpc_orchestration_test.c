/* Production-source harness: no plan/fault compile definitions. GPL-2.0-only. */
#if defined(ARCDROID_TEST_PLANS) || defined(ARCDROID_TEST_FAULTS)
#error Production orchestration harness must not enable test flags
#endif
#include "rpc_preparation.h"
#include "rpc_hd_vfs.h"
#include "rpc/rpc_rtc.h"
static unsigned notices,terminal_notices,clears,attach_calls;
static char last_notice[512];
static uint64_t clock_value,clock_delta;
static unsigned clock_mode,clock_reads,all_clock_reads;
static unsigned observations,stall_after;
static bool work_seen[ARC_RPC_BOOT_WORK_COUNT];
static bool phase_seen[ARC_RPC_BOOT_CANCELLED+1];
static uint64_t oracle[ARC_RPC_PREP_COST_KINDS][2][16];
static unsigned oracle_at[ARC_RPC_PREP_COST_KINDS][2],previous_ns;
static uint64_t frame_budget;
static bool last_clear,attachment_io,attachment_fail,reopen_expected;
static unsigned last_kind;
static ArcRpcPreparation *current;
static unsigned test_model;
static ArcRpcSelectionOs test_os=ARC_RPC_OS_350;
static const char *id="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
static uint64_t clock_read(void *opaque)
{
    (void)opaque;++all_clock_reads;
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
                if(s.unit_kind>=ARC_RPC_PREP_CANCELLED){
                    unsigned unit=s.unit_kind-ARC_RPC_PREP_CANCELLED;
                    unsigned work=unit%ARC_RPC_BOOT_WORK_COUNT,phase=unit/ARC_RPC_BOOT_WORK_COUNT;
                    assert(phase<=ARC_RPC_BOOT_CANCELLED);work_seen[work]=true;phase_seen[phase]=true;
                    cost=work==ARC_RPC_BOOT_WORK_HASH?800000:work==ARC_RPC_BOOT_WORK_FORMAT?900000:
                         work==ARC_RPC_BOOT_WORK_WRITE?100000:work==ARC_RPC_BOOT_WORK_READ?70000:50000;
                }
                oracle[s.unit_kind][ns][oracle_at[s.unit_kind][ns]++%16]=cost;
            }
            if(clock_mode==15 && ++observations>stall_after)cost=0;
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
static void notice(void *opaque,const char *s,bool terminal)
{
    (void)opaque;++notices;if(reopen_expected)assert(!strstr(s,"first use"));
    if(terminal){assert(last_clear);++terminal_notices;last_kind=2;}
    else last_kind=*s?1:0;
    last_clear=!s[0];if(last_clear)++clears;
    snprintf(last_notice,sizeof(last_notice),"%s",s);
}
static void observer_attached(void *opaque)
{
    (void)opaque;
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
            !strstr(files[i].path,".tmp") && strcmp(files[i].path,previous)>0 &&
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
    ArcRpcPreparationRequest r={0};r.firmware=rom;r.firmware_size=size;r.key.model=test_model;r.key.layout=test_os==ARC_RPC_OS_350?1:3;
    strcpy(r.key.content,id);r.save="/save";r.system="/system";r.epoch=1234567890;
    r.hard_disc=r.populate=true;
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
    current=fingerprint?arc_riscpc_preparation_begin(&r,&host,error,sizeof(error)):
                        arc_riscpc_preparation_low_level(&r,&host,error,sizeof(error));
    assert(current && !error[0]);return current;
}
static ArcRpcPreparationState frame(ArcRpcPreparation *p)
{
    uint64_t ns=namespace_calls;ArcRpcPreparationState old=arc_riscpc_preparation_state(p);
    clock_reads=previous_ns=0;frame_budget=old==ARC_RPC_PREP_GENERATE?UINT64_C(66666664):UINT64_C(13333333);
    ArcRpcPreparationState state=arc_riscpc_preparation_frame(p);ArcRpcPreparationStats s;
    arc_riscpc_preparation_stats(p,&s);
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
    char e[512]={0};assert(arc_riscpc_preparation_free(p,e,sizeof(e)));
    current=NULL;assert(last_clear && last_kind==0);assert(!arc_engine_active());test_vfs_no_open_handles();
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
            else if(action==1){assert(arc_riscpc_preparation_reset(p));assert(arc_riscpc_preparation_state(p)==ARC_RPC_PREP_VERIFY);assert(last_clear && last_kind==0);assert(!arc_engine_active());}
            finish(p);
            /* Reset and unload preserve completed publications, and recovery
             * cleans only the offline candidate; no partial root can attach. */
            char error[512];ArcRpcHdStore *disc=arc_riscpc_hd_open_begin("/save",id,error,sizeof(error));assert(disc);
            ArcRpcHdOutcome outcome;do{outcome=arc_riscpc_hd_open_step(disc);}while(outcome==ARC_RPC_HD_OPEN_PENDING);
            assert(outcome==ARC_RPC_HD_UNPUBLISHED || outcome==ARC_RPC_HD_RECEIPTED);arc_riscpc_hd_close(disc);
            if(sanitizer && target==ARC_RPC_PREP_SAVE_DEFAULT)fprintf(stdout,"sanitizer: generation publication boundary exercised\n");
        }
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
    ArcRpcPreparation *p=new_request(rom,size,true,false,host);
    ArcRpcPreparationStats s;
    do{frame(p);arc_riscpc_preparation_stats(p,&s);}while(!s.namespace_units);
    assert(s.namespace_units==2 && s.steps==2 && s.elapsed_ns==600000 && s.budget_ns==UINT64_C(13333333));
    arc_riscpc_preparation_cancel(p);finish(p);
    /* The declared-cost fallback caps a stationary clock at twelve ordinary
     * units. A backward clock admits at most the first indivisible unit. */
    for(unsigned mode=1;mode<=2;++mode){
        restore();clock_mode=mode;clock_value=1000000;
        p=new_request(rom,size,true,false,host);frame(p);arc_riscpc_preparation_stats(p,&s);
        assert(s.steps==(mode==1?12u:1u));arc_riscpc_preparation_cancel(p);finish(p);
    }
    restore();clock_mode=3;clock_value=0;
    p=new_request(rom,size,true,false,host);
    while(arc_riscpc_preparation_state(p)!=ARC_RPC_PREP_RECOVER)frame(p);
    while(!arc_riscpc_preparation_disc(p))frame(p);
    clock_mode=1;frame(p);arc_riscpc_preparation_stats(p,&s);
    assert(s.namespace_units==1 && s.reserved_ns==UINT64_C(8333333)+(s.steps-1)*UINT64_C(1000000));
    arc_riscpc_preparation_cancel(p);finish(p);
    for(unsigned mode=12;mode<=15;++mode){
        restore();clock_mode=mode;clock_value=0;observations=0;stall_after=1;
        memset(oracle,0,sizeof(oracle));memset(oracle_at,0,sizeof(oracle_at));memset(work_seen,0,sizeof(work_seen));
        memset(phase_seen,0,sizeof(phase_seen));
        p=new_request(rom,size,true,false,host);frame(p);arc_riscpc_preparation_stats(p,&s);
        if(mode==12){assert(s.steps==1 && s.elapsed_ns==2000000);frame(p);assert(observations>1);}
        if(mode==13)assert(observations>=18);
        if(mode==14){settle(p);for(unsigned work=ARC_RPC_BOOT_WORK_INFLATE;work<ARC_RPC_BOOT_WORK_COUNT;++work)assert(work_seen[work]);
            assert(phase_seen[ARC_RPC_BOOT_FORMAT] && phase_seen[ARC_RPC_BOOT_WRITE] && phase_seen[ARC_RPC_BOOT_VALIDATE] && phase_seen[ARC_RPC_BOOT_SEAL]);}
        if(mode==15){assert(s.steps==13 && s.elapsed_ns==50000 && s.reserved_ns==12050000);
        }
        arc_riscpc_preparation_cancel(p);finish(p);
    }
    restore();clock_mode=12;observations=0;clock_value=0;
    p=new_request(rom,size,true,false,host);frame(p);
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
            char e[512];ArcRpcHdStore *disc=arc_riscpc_hd_open("/save",id,true,e,sizeof(e));assert(disc);
            assert(!arc_riscpc_hd_receipt(disc,NULL));arc_riscpc_hd_close(disc);
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
    char e[512];assert(!arc_riscpc_preparation_free(p,e,sizeof(e)));assert(last_clear);current=NULL;
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
    file_copy("/system/full.rom",argv[2]);int i=find("/system/full.rom",false);assert(i>=0);
    size_t size=files[i].size;uint8_t *rom=malloc(size);assert(rom);memcpy(rom,files[i].data,size);
    construction_and_attachments(rom,size);
    file_bytes("/system/attachment.raw","media",5);
    snapshot();ArcRpcPreparationHost host={.clock=clock_read,.notice=notice,.attach=attach,.attached=observer_attached};
    /* Initial package validation precedes either CMOS generation or seed. */
    state_matrix(rom,size,host,sanitizer,!strcmp(argv[4],"mutant") || !strcmp(argv[4],"mutant-idle"));
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
        assert(all_clock_reads==reads); /* Live work never uses offline admission. */
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
