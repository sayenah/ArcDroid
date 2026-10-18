/* S68 Run B behavioural regressions. Included by the flag-free harness.
 * GPL-2.0-only. */
static unsigned b_identity_calls;
static bool b_identity(void *opaque,intptr_t handle,const char *path)
{++b_identity_calls;clock_value+=123;return rpc_fixture_identity(opaque,handle,path);}
static bool b_fail_candidate;
static struct retro_vfs_file_handle *b_candidate_open(const char *path,unsigned mode,unsigned hints)
{
    if(b_fail_candidate && strstr(path,"/cmos-riscpc/") && strstr(path,".tmp") && (mode&RETRO_VFS_FILE_ACCESS_WRITE)){b_fail_candidate=false;return NULL;}
    return test_open(path,mode,hints);
}
static struct retro_vfs_file_handle *b_defaults_open(const char *path,unsigned mode,unsigned hints)
{if(strstr(path,"/defaults/") && strstr(path,".tmp") && (mode&RETRO_VFS_FILE_ACCESS_WRITE))return NULL;return test_open(path,mode,hints);}
static uint64_t b_number(const char *line,const char *name)
{const char *at=strstr(line,name);assert(at);return strtoull(at+strlen(name),NULL,10);}
static void b_ready(void)
{
    for(unsigned i=0;arc_riscpc_host_pending(frontend_test_host);++i){assert(i<20000);arc_riscpc_host_pending_frame(frontend_test_host);}
}
static void b_setup(void)
{
    restore();clock_mode=0;clock_value=1000000;clock_delta=1000;test_model=2;test_os=ARC_RPC_OS_371;
    use_frontend_host=true;host_extended=true;host_n13=host_unload=host_marker=host_n17=0;
}
static void b_free(bool expected)
{
    char error[512]={0};assert(arc_riscpc_host_free(frontend_test_host,error,sizeof(error))==expected);
    frontend_test_host=NULL;current=NULL;test_vfs_no_open_handles();
    for(unsigned i=0;i<16;++i)assert(!rpc_fixture_locks[i].used);
}
static void b_refuse_record(const uint8_t *rom,size_t size,ArcRpcPreparationHost host)
{
    new_request(rom,size,true,true,host);b_ready();b_free(true);
    int slot=-1;for(unsigned i=0;i<TEST_FILES;++i)if(files[i].present && files[i].size==512 && strstr(files[i].path,"/cmos-riscpc/") && !strstr(files[i].path,"/defaults/")){slot=(int)i;break;}
    assert(slot>=0);char other[1100];snprintf(other,sizeof(other),"%s",files[slot].path);size_t length=strlen(other);other[length-1]=other[length-1]=='0'?'1':'0';
    file_bytes(other,files[slot].data,512);files[find(other,false)].data[104]^=1;
    new_request(rom,size,true,true,host);b_ready();assert(strstr(host_display,"refused"));
}
static void b_defaults_available(void)
{
    /* A real second file description on the same defaults key must acquire. */
    char firmware[65];int found=-1;
    for(unsigned i=0;i<TEST_FILES;++i)if(files[i].present && strstr(files[i].path,"/locks-riscpc/d-")){found=(int)i;break;}
    assert(found>=0);const char *leaf=strrchr(files[found].path,'/');memcpy(firmware,leaf+3,64);firmware[64]=0;
    ArcLock other;arc_lock_defaults(&other,"/save",firmware,2,ARC_RPC_CMOS_PROCEDURE_REVISION,ARC_RPC_CMOS_TIMING_REVISION,&rpc_preparation_test_backend,NULL);
    ArcLockResult result;do{result=arc_lock_acquire(&other);}while(result==ARC_LOCK_TRANSIENT);
    assert(result==ARC_LOCK_HELD);arc_lock_release(&other);
}
static void host_run_b(const uint8_t *rom,size_t size,ArcRpcPreparationHost host)
{
    uint8_t *bad=malloc(size);assert(bad);memcpy(bad,rom,size);
    const uint32_t invalid[]={0xe3a00070,0xee010f10,0xe3a00014,0xe121f000,0xeafffffe};memcpy(bad,invalid,sizeof(invalid));
    /* F1: refused drain candidate create, Restart, healthy CMOS edit/unload, no false N-13. */
    b_setup();ArcRpcPreparation *p=new_request(bad,size,true,true,host);b_ready();
    uint8_t changed[256];arc_rpc_cmos_snapshot(changed);changed[100]^=1;arc_rpc_cmos_seed(changed);
    assert(!arc_engine_rpc_advance(10000));arc_riscpc_host_maintenance(frontend_test_host);assert(arc_riscpc_preparation_draining(p));
    b_fail_candidate=true;vfs.open=b_candidate_open;
    while(arc_riscpc_preparation_draining(p))arc_riscpc_host_pending_frame(frontend_test_host);
    assert(!b_fail_candidate && strstr(host_display,"could not be saved"));vfs.open=test_open;
    assert(arc_riscpc_host_reset(frontend_test_host));b_ready();uint64_t generation=content_generation(NULL);
    uint8_t latest[256];arc_rpc_cmos_snapshot(latest);latest[102]^=1;arc_rpc_cmos_seed(latest);b_free(true);
    assert(content_generation(latest+16)==generation+1 && !host_unload && !host_marker);
    for(unsigned i=0;i<TEST_FILES;++i)assert(!files[i].present || !strstr(files[i].path,".unsaved"));
    new_request(bad,size,true,true,host);b_ready();assert(!host_n13);b_free(true);
    puts("Run B F1: failed drain then healthy Restart/unload persists byte 102, no false advisory or marker/N-13");
    /* F3: the public module API itself cannot strand a drain behind a state. */
    for(unsigned cancel=0;cancel<2;++cancel){
        b_setup();p=new_request(bad,size,true,true,host);b_ready();assert(!arc_engine_rpc_advance(10000));arc_riscpc_host_maintenance(frontend_test_host);
        assert(arc_riscpc_preparation_draining(p));
        if(cancel)arc_riscpc_preparation_cancel(p);else assert(arc_riscpc_preparation_reset(p));
        assert(!arc_riscpc_preparation_draining(p));b_free(true);
    }
    puts("Run B F3: module cancel and module reset during drain both leave free bounded");
    /* A usable refused record means no CMOS put can mask a missing maintenance check. */
    b_setup();ArcLockBackend backend=rpc_preparation_test_backend;backend.identity=b_identity;host.lock_backend=&backend;
    b_refuse_record(rom,size,host);p=current;
    clock_delta=0;ArcRpcPreparationStats before,after;arc_riscpc_preparation_stats(p,&before);unsigned calls=b_identity_calls;
    for(unsigned i=0;i<3;++i)arc_riscpc_host_maintenance(frontend_test_host);
    arc_riscpc_preparation_stats(p,&after);assert(b_identity_calls==calls+3 && after.identity_checks==before.identity_checks+3 && after.identity_max_ns>=123);
    calls=b_identity_calls;uint64_t mutations=namespace_calls;rpc_fixture_lost=true;arc_riscpc_host_maintenance(frontend_test_host);
    assert(b_identity_calls==calls+1 && arc_riscpc_preparation_state(p)==ARC_RPC_PREP_FAILED && !arc_engine_active());
    assert(namespace_calls==mutations && strstr(host_display,"Another app changed"));b_free(false);rpc_fixture_lost=false;clock_delta=1000;
    puts("Run B F5 maintenance-identity: quiet RUNNING/refused record stops on the first lost-owner frame; every native check measured");
    /* Exception 1 must replace the non-stopping refusal before a drain outcome exists. */
    for(unsigned fallback=0;fallback<2;++fallback){
        b_setup();host_extended=!fallback;b_refuse_record(bad,size,host);p=current;
        char retained[512];snprintf(retained,sizeof(retained),"%s",host_display);unsigned messages=host_messages;
        for(unsigned i=0;i<180;++i){arc_riscpc_host_frame_begin(frontend_test_host);arc_riscpc_host_frame_end(frontend_test_host);
            assert(!strcmp(host_display,retained));assert(host_messages==messages+(fallback?(i+1)/60:0));}
        assert(!arc_engine_rpc_advance(10000));arc_riscpc_host_maintenance(frontend_test_host);
        assert(arc_riscpc_preparation_draining(p) && strcmp(host_display,retained) && !strstr(host_display,"snapshot"));
        assert(!strchr(host_display,'\n'));arc_riscpc_host_pending_frame(frontend_test_host);
        assert(strstr(host_display,"; latest CMOS snapshot could not be saved") && !strchr(host_display,'\n'));b_free(false);
    }
    puts("Run B F5 exception-1/fallback-resend, S3: fault immediately replaces refusal; exact 60/120/180 fallback refreshes, trimmed cause");
    /* Stepped S-4 has no namespace operation on begin, nor a completed generation. */
    b_setup();p=new_request(rom,size,true,true,host);b_ready();generation=content_generation(NULL);
    arc_rpc_cmos_snapshot(latest);latest[100]^=1;arc_rpc_cmos_seed(latest);mutations=namespace_calls;
    arc_riscpc_host_maintenance(frontend_test_host);assert(namespace_calls==mutations && content_generation(NULL)==generation);
    unsigned frames=0;while(content_generation(NULL)==generation){mutations=namespace_calls;arc_riscpc_host_maintenance(frontend_test_host);assert(namespace_calls-mutations<=1 && ++frames<=60);}
    assert(frames>1);b_free(true);puts("Run B F5 stepped-CMOS: changed snapshot stays unpublished on begin, then at most one namespace unit per frame within 60 frames");
    /* No identity syscalls on IDE writes or ordinary ticks, one at checkpoint begin. */
    b_setup();measure_summaries=true;summary_count=0;unsigned session_calls=b_identity_calls;
    p=new_request(rom,size,true,true,host);b_ready();ArcRpcHdStore *disc=arc_riscpc_preparation_disc(p);
    ArcRpcHdStats hd;do{arc_riscpc_hd_tick(disc);arc_riscpc_hd_stats(disc,&hd);}while(hd.state!=ARC_RPC_HD_IDLE);
    calls=b_identity_calls;uint8_t *sectors=calloc(256,512);assert(sectors);
    for(unsigned i=0;i<8;++i)assert(arc_riscpc_hd_write(disc,8192,256,sectors));free(sectors);assert(b_identity_calls==calls);
    arc_riscpc_hd_tick(disc);assert(b_identity_calls==calls+1);calls=b_identity_calls;
    for(unsigned i=0;i<8;++i)arc_riscpc_hd_tick(disc);assert(b_identity_calls==calls);b_free(true);
    assert(summary_count==2 && b_number(measured_summaries[1],"identity_checks=")==b_identity_calls-session_calls && b_number(measured_summaries[1],"identity_max_ns=")>=123);measure_summaries=false;
    puts("Run B F9/S4: eight IDE writes and non-begin checkpoint ticks make zero identity calls; checkpoint begin makes one");
    /* F8/F7: N-18 wins an active fault drain on both message paths, remains final;
     * ownership first lost during unload is a timed advisory after clear. */
    for(unsigned fallback=0;fallback<2;++fallback){
        b_setup();host_extended=!fallback;p=new_request(bad,size,true,true,host);b_ready();assert(!arc_engine_rpc_advance(10000));arc_riscpc_host_maintenance(frontend_test_host);
        assert(arc_riscpc_preparation_draining(p));host_n18_unsaved=0;rpc_fixture_lost=true;mutations=namespace_calls;
        arc_riscpc_host_pending_frame(frontend_test_host);assert(!arc_riscpc_preparation_draining(p) && namespace_calls==mutations);
        assert(strstr(host_display,"Another app changed") && !strstr(host_display,"latest CMOS") && host_n18_unsaved==1);b_free(false);assert(host_n18_unsaved==1);rpc_fixture_lost=false;
        b_setup();host_extended=!fallback;new_request(rom,size,true,true,host);b_ready();rpc_fixture_lost=true;unsigned clear_before=host_clears;b_free(false);
        assert(host_clears==clear_before+1 && strstr(host_display,"Another app changed"));assert(fallback?host_frames==300:host_duration==5000);rpc_fixture_lost=false;
        b_setup();host_extended=!fallback;p=new_request(bad,size,true,true,host);b_ready();assert(!arc_engine_rpc_advance(10000));arc_riscpc_host_maintenance(frontend_test_host);
        assert(arc_riscpc_preparation_draining(p));rpc_fixture_lost=true;clear_before=host_clears;b_free(false);
        assert(host_clears==clear_before+1 && strstr(host_display,"Another app changed"));assert(fallback?host_frames==300:host_duration==5000);rpc_fixture_lost=false;
    }
    b_setup();p=new_request(rom,size,true,true,host);b_ready();arc_rpc_cmos_snapshot(latest);latest[100]^=1;arc_rpc_cmos_seed(latest);
    arc_riscpc_host_maintenance(frontend_test_host); /* leave a publication in flight */
    mutations=namespace_calls;rpc_fixture_lost=true;b_free(false);assert(namespace_calls==mutations && strstr(host_display,"Another app changed") && host_duration==5000);rpc_fixture_lost=false;
    puts("Run B F7/F8: N-18 replaces drain cause with no mutation; unload N-18 is timed after clear on both paths");
    /* Defaults are released at phase exit and on failure; Restart completes
     * each pending store and accounts its distinct measured exception. */
    for(unsigned phase=ARC_RPC_PREP_SAVE_DEFAULT;phase<=ARC_RPC_PREP_SAVE_RECORD;++phase){
        b_setup();clock_delta=10000000;p=new_request(rom,size,false,true,host);
        for(unsigned i=0;arc_riscpc_preparation_state(p)!=phase;++i){assert(i<20000 && arc_riscpc_preparation_state(p)<ARC_RPC_PREP_FAILED);arc_riscpc_host_pending_frame(frontend_test_host);}
        if(phase==ARC_RPC_PREP_SAVE_RECORD)b_defaults_available();
        arc_riscpc_preparation_stats(p,&before);assert(arc_riscpc_host_reset(frontend_test_host));arc_riscpc_preparation_stats(p,&after);
        assert(phase==ARC_RPC_PREP_SAVE_RECORD?after.restart_record_exceptions==before.restart_record_exceptions+1 && after.restart_record_max_ns>0:after.restart_defaults_exceptions==before.restart_defaults_exceptions+1 && after.restart_defaults_max_ns>0);
        if(phase==ARC_RPC_PREP_SAVE_RECORD)assert(content_generation(NULL)==1);
        else{
            uint64_t published=0;
            for(unsigned i=0;i<TEST_FILES;++i)if(files[i].present && files[i].size==512 && strstr(files[i].path,"/cmos-riscpc/defaults/") && !strstr(files[i].path,".tmp")){
                uint64_t value=0;for(unsigned j=0;j<8;++j)value|=(uint64_t)files[i].data[24+j]<<(8*j);if(value>published)published=value;
            }
            assert(published==1);
        }
        b_defaults_available();measure_summaries=true;summary_count=0;b_free(true);
        assert(summary_count==2);
        assert(b_number(measured_summaries[1],phase==ARC_RPC_PREP_SAVE_RECORD?"restart_record_exceptions=":"restart_defaults_exceptions=")==1);
        assert(b_number(measured_summaries[1],phase==ARC_RPC_PREP_SAVE_RECORD?"restart_record_max_ns=":"restart_defaults_max_ns=")>0);measure_summaries=false;
    }
    for(unsigned phase=ARC_RPC_PREP_GENERATE;phase<=ARC_RPC_PREP_SAVE_DEFAULT;++phase){
        b_setup();clock_delta=10000000;p=new_request(rom,size,false,true,host);
        while(arc_riscpc_preparation_state(p)!=phase){assert(arc_riscpc_preparation_state(p)<ARC_RPC_PREP_FAILED);arc_riscpc_host_pending_frame(frontend_test_host);}
        if(phase==ARC_RPC_PREP_GENERATE){arc_engine_stop();for(unsigned i=0;arc_riscpc_preparation_state(p)!=ARC_RPC_PREP_FAILED;++i){assert(i<32);arc_riscpc_host_pending_frame(frontend_test_host);}assert(arc_riscpc_preparation_owned(p));}
        else{vfs.open=b_defaults_open;for(unsigned i=0;arc_riscpc_preparation_state(p)!=ARC_RPC_PREP_FAILED;++i){assert(i<32);arc_riscpc_host_pending_frame(frontend_test_host);}vfs.open=test_open;
            assert(arc_riscpc_preparation_state(p)==ARC_RPC_PREP_FAILED && arc_riscpc_preparation_owned(p));}
        b_defaults_available();b_free(true);
    }
    puts("Run B F5/F10/F11: second defaults description acquires on phase exit/failure/cancel; both Restart publication exceptions counted and timed");
    /* Publication-begin identity is independently observable without maintenance. */
    b_setup();rpc_fixture_create_sentinel=create_sentinel;ArcLock owner;arc_lock_content(&owner,"/save",id,&backend,NULL);
    ArcLockResult result;do{result=arc_lock_acquire(&owner);}while(result==ARC_LOCK_TRANSIENT);assert(result==ARC_LOCK_HELD);
    ArcRpcLaunchRecordStore store;assert(arc_riscpc_launch_record_begin(&store,"/save",id,&owner));bool done=false;
    while(!done)assert(arc_riscpc_launch_record_recover(&store,&done));
    ArcRpcBootRecord record={.observation=ARC_RPC_OUTCOME_RETURNED,.key={.cpu=1,.plan_revision=2,.profile_revision=2,.action=1}};
    memset(record.key.firmware,1,32);memset(record.key.configuration,2,32);memset(record.key.content,3,32);memset(record.key.media,4,32);
    memset(record.key.store,5,32);memset(record.key.plan,6,32);memset(record.key.helper,7,32);
    mutations=namespace_calls;rpc_fixture_lost=true;assert(!arc_riscpc_launch_record_publish_begin(&store,&record));
    assert(namespace_calls==mutations && !arc_lock_held(&owner));arc_riscpc_launch_record_close(&store);arc_lock_release(&owner);rpc_fixture_lost=false;
    puts("Run B F5 launch-publication-identity: replacement before begin refuses without a maintenance/store-step backstop");
    host.lock_backend=&rpc_preparation_test_backend;use_frontend_host=false;test_model=0;test_os=ARC_RPC_OS_350;free(bad);
}
