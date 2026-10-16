/* Plan-core retro_* switching versus an independent fresh-process trace.
 * Authored content only. GPL-2.0-only. */
#if defined(ARCDROID_SESSION_SANITIZER_PROFILE)
#if defined(__clang__)
#include <sanitizer/allocator_interface.h>
#else
#include <stddef.h>
/* GCC exports this sanitizer API but does not install allocator_interface.h. */
extern size_t __sanitizer_get_current_allocated_bytes(void);
#endif
#elif defined(__APPLE__)
#include <malloc/malloc.h>
#elif defined(__linux__)
#include <malloc.h>
#endif
#define main cmos_fixture_main
#define environment cmos_environment
#include "rpc_cmos_plan_test.c"
#undef environment
#undef main
#include "engine_test.h"
#include "arc_session.h"
#include <stdarg.h>

/* Keep every lifecycle/fault case under sanitizers; the host suite owns the
 * full 200-cycle live-heap census. ASan checks 20 repeated machine lifetimes.
 * The 10,000-ID arithmetic oracle runs separately in rpc-session-ids; avoid
 * rehashing 20 GiB of identical firmware in the sanitizer frontend stress leg. */
#if defined(ARCDROID_SESSION_SANITIZER_PROFILE)
static const unsigned h_trace_fields=1,h_id_admissions=128,h_filter_iterations=1000,h_cycle_count=20;
#else
static const unsigned h_trace_fields=4,h_id_admissions=10000,h_filter_iterations=1000000,h_cycle_count=200;
#endif
static const char *h_progress_path;
static void h_progress(const char *stage,unsigned n)
{
    if(!h_progress_path)return;
    FILE *f=fopen(h_progress_path,"a");assert(f);fprintf(f,"%s %u\n",stage,n);fclose(f);
}
static void (*h_alloc_fail)(unsigned);
static void (*h_init)(void),(*h_deinit)(void),(*h_run)(void),(*h_reset)(void),(*h_unload)(void);
static bool (*h_load)(const struct retro_game_info *);
static void (*h_env)(retro_environment_t),(*h_video)(retro_video_refresh_t),(*h_audio)(retro_audio_sample_batch_t),(*h_poll)(retro_input_poll_t),(*h_input)(retro_input_state_t);
static void (*h_av)(struct retro_system_av_info *),(*h_hygiene)(ArcFrontendHygiene *),(*h_plant)(const char *),(*h_fault)(void);
static const ArcEngineBackendOps *h_api;
static struct retro_disk_control_ext_callback h_disks;
static struct retro_keyboard_callback h_keyboard;
static const char *h_launch="off";
static bool h_l3,h_projection,h_loading_arc;
static unsigned h_preconstruction_checks;
static ArcFrontendHygiene h_state(void);
static unsigned h_pad,h_build_lines,h_logs,h_video_calls,h_audio_calls;
static uint64_t h_hash;
static unsigned h_refusal_logs,h_refusal_notices,h_storage_notices,h_flush_failures;
static bool h_fail_flush;
static bool (*h_guard)(unsigned,unsigned,const char *);
static unsigned h_vfs_reads,h_vfs_writes,h_arc_fdc;
static bool h_arc_writes_off;
static int64_t h_read(struct retro_vfs_file_handle *f,void *data,uint64_t size)
{++h_vfs_reads;return vfs_read(f,data,size);}
static int64_t h_write(struct retro_vfs_file_handle *f,const void *data,uint64_t size)
{++h_vfs_writes;return vfs_write(f,data,size);}
static int h_flush(struct retro_vfs_file_handle *f)
{if(h_fail_flush&&f->writable){++h_flush_failures;return -1;}return vfs_flush(f);}
static bool (*h_name)(unsigned,ArcGuestName *);
static uint64_t (*h_filter_cost)(unsigned);
static bool (*h_ram)(unsigned,uint32_t *);
static uint64_t h_hash_bytes(const void *data,size_t n)
{const uint8_t *p=data;for(size_t i=0;i<n;++i){h_hash^=p[i];h_hash*=UINT64_C(1099511628211);}return h_hash;}
static void h_log(enum retro_log_level level,const char *format,...)
{
    (void)level;char line[1024];va_list ap;va_start(ap,format);vsnprintf(line,sizeof(line),format,ap);va_end(ap);
    if(strstr(line,"ArcDroid build:")){
        assert(strstr(line,"version=")&&strstr(line,"commit=")&&strstr(line,"target=")&&strstr(line,"config=")&&strstr(line,"gate=closed")&&strstr(line,"substitutions="));++h_build_lines;
    }
    if(strstr(line,"ADF write refused:"))++h_refusal_logs;
    if(strstr(line,"FDC command=A0 type=A drive=0"))++h_arc_fdc;
    if(h_projection)printf("log:%u:%s",(unsigned)level,line);
    ++h_logs;
}
static bool h_environment(unsigned command,void *data)
{
    if(command==RETRO_ENVIRONMENT_SET_MESSAGE_EXT){struct retro_message_ext *m=data;
        if(h_projection)printf("notice:%u:%u:%u:%s\n",m->level,m->type,m->duration,m->msg);
        if(strstr(m->msg,"RiscPC disc write refused")){assert(m->level==RETRO_LOG_INFO && m->duration==5000 && m->type==RETRO_MESSAGE_TYPE_NOTIFICATION);++h_refusal_notices;}
        if(strstr(m->msg,"RiscPC disc writes can no longer")){assert(m->level==RETRO_LOG_WARN && m->duration==5000 && m->type==RETRO_MESSAGE_TYPE_NOTIFICATION);++h_storage_notices;}
    }
    if(command==RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY && h_loading_arc && !h_projection){
        ArcFrontendHygiene s=h_state();ArcEngineCapabilities zero={0};
        assert(s.kind==ARC_SESSION_KIND_ARCULATOR&&s.id&&!s.engine_rpc&&!s.fault&&!s.fault_text&&!memcmp(&s.capabilities,&zero,sizeof(zero)));++h_preconstruction_checks;
    }
    if(command==RETRO_ENVIRONMENT_GET_LOG_INTERFACE){((struct retro_log_callback *)data)->log=h_log;return true;}
    if(command==RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE){h_disks=*(struct retro_disk_control_ext_callback *)data;return true;}
    if(command==RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK){h_keyboard=*(struct retro_keyboard_callback *)data;return true;}
    if(command==RETRO_ENVIRONMENT_GET_VARIABLE){struct retro_variable *v=data;if(!strcmp(v->key,"arcdroid_launch")){v->value=h_launch;return true;}}
    if(command==RETRO_ENVIRONMENT_GET_VARIABLE && h_arc_writes_off){struct retro_variable *v=data;if(!strcmp(v->key,"arcdroid_disk_writes")){v->value="off";return true;}}
    return cmos_environment(command,data);
}
static void h_present(const void *data,unsigned width,unsigned height,size_t pitch)
{
    ++h_video_calls;h_hash_bytes(&width,sizeof(width));h_hash_bytes(&height,sizeof(height));
    if(data)for(unsigned y=0;y<height;++y)h_hash_bytes((const uint8_t *)data+y*pitch,width*4);
}
static size_t h_sound(const int16_t *data,size_t n)
{++h_audio_calls;h_hash_bytes(data,n*4);return n;}
static int16_t h_input_state(unsigned port,unsigned device,unsigned index,unsigned id)
{(void)index;if(port||device!=RETRO_DEVICE_JOYPAD)return 0;return id==RETRO_DEVICE_ID_JOYPAD_L3?h_l3:id==RETRO_DEVICE_ID_JOYPAD_A?(int16_t)h_pad:0;}
static unsigned h_no_phase(void) { return 0; }
static void h_bind(void)
{h_env(h_environment);h_init();h_video(h_present);h_audio(h_sound);h_poll(poll);h_input(h_input_state);}
static ArcFrontendHygiene h_state(void)
{ArcFrontendHygiene s;h_hygiene(&s);return s;}
static void h_empty(void)
{
    ArcFrontendHygiene s=h_state();assert(!s.id&&s.kind==ARC_SESSION_KIND_EMPTY&&!s.engine_rpc&&!s.fault&&!s.fault_text&&!s.retained&&!s.frontend_allocations);
    ArcEngineCapabilities zero={0};assert(!memcmp(&s.capabilities,&zero,sizeof(zero)));vfs_assert_no_open_handles();
    ArcRpcTestSnapshot r;snapshot(&r);assert(!r.live_allocations);
}
static void h_geometry(double rate)
{struct retro_system_av_info av;h_av(&av);assert(av.geometry.base_width==640&&av.geometry.base_height==480&&av.geometry.max_width==4096&&av.geometry.max_height==2048&&av.timing.fps==rate);}
static struct retro_game_info h_game(const char *path)
{return (struct retro_game_info){.path=path};}
static uint64_t h_arc_trace(const char *path)
{
    audit_mode=2;h_launch="off";struct retro_game_info g=h_game(path);h_loading_arc=true;assert(h_load(&g));h_loading_arc=false;assert(h_preconstruction_checks);
    h_geometry(50.0);ArcFrontendHygiene s=h_state();assert(s.id&&s.kind==ARC_SESSION_KIND_ARCULATOR&&!s.engine_rpc&&!s.fault&&!s.fault_text&&!s.retained&&s.capabilities.cmos_persistence);
    h_hash=UINT64_C(14695981039346656037);h_video_calls=h_audio_calls=0;
    for(unsigned i=0;i<h_trace_fields;++i)h_run();assert(h_video_calls==h_trace_fields&&h_audio_calls==h_trace_fields);
    uint64_t hash=h_hash;h_unload();h_empty();audit_mode=0;return hash;
}
static void h_persistence_guards(const char *arc_path)
{
    const char *names[]={"sync","seed","persist","boot-seed","hd-reconcile",
        "boot-publish","outcome","boot-path","binding","construct"};
    for(unsigned site=0;site<10;++site)for(unsigned control=0;control<2;++control){
        struct retro_game_info g=h_game(arc_path);assert(h_load(&g));
        char path[4096];snprintf(path,sizeof(path),"%s/arcdroid/cmos/guard-%u-%u",save_directory,site,control);
        assert(!h_guard(site,0,path)); /* Setup before freezing observable I/O. */
        unsigned reads=h_vfs_reads,writes=h_vfs_writes;
        bool changed=h_guard(site,control?2:1,path);
        bool effect=changed || reads!=h_vfs_reads || writes!=h_vfs_writes;
        if(effect!=(control!=0))fprintf(stderr,"persistence guard %s control=%u changed=%u reads=%u writes=%u\n",names[site],control,changed,h_vfs_reads-reads,h_vfs_writes-writes);
        assert(effect==(control!=0));
        printf("persistence-guard:%s:%s\n",names[site],control?"arculator-effect":"rpc-inert");
        h_unload();h_empty();
    }
}
static void h_switch_persistence(const char *arc_path,const char *chooser,const char *rpc_path)
{
    uint8_t reference[256];
    for(unsigned plan=0;plan<2;++plan)for(unsigned choice=0;choice<2;++choice){
        h_launch=choice?"auto":"off";
        struct retro_game_info outgoing=h_game(choice?chooser:arc_path);assert(h_load(&outgoing));
        char path[4096];snprintf(path,sizeof(path),"%s/arcdroid/cmos/switch-%u-%u",save_directory,plan,choice);
        assert(!h_guard(choice?3:2,0,path)); /* Pending chooser seed or dirty CMOS. */
        h_launch="off";struct retro_game_info next=h_game(plan?rpc_path:arc_path);assert(h_load(&next));
        FILE *file=fopen(path,"rb");
        if(choice)assert(!file); /* Neither load publishes a pending seed. */
        else {uint8_t bytes[256];assert(file&&fread(bytes,1,sizeof(bytes),file)==sizeof(bytes));assert(!fclose(file));
            if(!plan)memcpy(reference,bytes,sizeof(bytes));else assert(!memcmp(reference,bytes,sizeof(bytes)));}
        h_unload();h_empty();
    }
    puts("switch-persistence: ordinary/plan CMOS bytes equal; chooser seed unpublished");
}
static void h_arculator_callbacks(const char *arc_path)
{
    /* Clear callbacks while Arculator is selected, then select RPC without an
     * intervening init. This cannot inherit an already-armed Arculator callback. */
    struct retro_game_info arc=h_game(arc_path);assert(h_load(&arc));h_deinit();
    uint8_t *rom=calloc(1,2097152),seed[256]={0};assert(rom);
    rom[0]=0xfe;rom[1]=rom[2]=0xff;rom[3]=0xea;
    char error[256]={0};assert(rpc_start(ARC_RPC_SA110,rom,2097152,seed,0,error,sizeof(error)));free(rom);
    h_bind();h_empty();
    const char *previous=system_directory;char directory[4096];snprintf(directory,sizeof(directory),"%s/arc-refusal",previous);system_directory=directory;
    h_arc_writes_off=true;h_refusal_logs=h_arc_fdc=0;assert(h_load(&arc));
    for(unsigned i=0;i<150;++i)h_run();
    assert(h_refusal_logs==1&&h_arc_fdc); /* Frontend refusal and backend DEBUG. */
    h_unload();h_empty();h_arc_writes_off=false;system_directory=previous;
    puts("arculator callbacks: refusal and FDC log after RPC-current init");
}
static size_t h_heap(void)
{
#if defined(ARCDROID_SESSION_SANITIZER_PROFILE)
    return __sanitizer_get_current_allocated_bytes();
#elif defined(__APPLE__)
    malloc_statistics_t s;malloc_zone_statistics(NULL,&s);return s.size_in_use;
#elif defined(__linux__)
    return (size_t)mallinfo2().uordblks;
#else
    /* Native Windows heap walk counts busy allocations, not working-set pages. */
    HANDLE heaps[128];DWORD n=GetProcessHeaps(128,heaps);assert(n<=128);size_t used=0;
    for(DWORD i=0;i<n;++i){PROCESS_HEAP_ENTRY e={0};assert(HeapLock(heaps[i]));
        while(HeapWalk(heaps[i],&e))if(e.wFlags&PROCESS_HEAP_ENTRY_BUSY)used+=e.cbData;
        assert(HeapUnlock(heaps[i]));}return used;
#endif
}
int main(int argc,char **argv)
{
    assert(argc==8);h_progress_path=getenv("ARCDROID_HYGIENE_PROGRESS");h_progress("begin",0);system_directory=argv[3];assert(canonical(argv[4],save_directory));
#ifdef _WIN32
    library=LoadLibraryA(argv[1]);
#else
    library=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
#endif
    assert(library);
    LOAD(h_env,"retro_set_environment");LOAD(h_init,"retro_init");LOAD(h_deinit,"retro_deinit");LOAD(h_load,"retro_load_game");LOAD(h_run,"retro_run");LOAD(h_reset,"retro_reset");LOAD(h_unload,"retro_unload_game");LOAD(h_av,"retro_get_system_av_info");LOAD(h_video,"retro_set_video_refresh");LOAD(h_audio,"retro_set_audio_sample_batch");LOAD(h_poll,"retro_set_input_poll");LOAD(h_input,"retro_set_input_state");
    if(!strcmp(argv[5],"projection") || !strcmp(argv[5],"switch-projection")){
        h_projection=true;phase=h_no_phase;h_bind();
        if(!strcmp(argv[5],"switch-projection")){
            LOAD(phase,"arc_test_plan_cmos_phase");
            const char *rpc_paths[]={argv[2],argv[6],argv[7]};
            for(unsigned scenario=0;scenario<3;++scenario){
                struct retro_game_info rpc=h_game(rpc_paths[scenario]);assert(h_load(&rpc));
                if(!scenario){settle(h_run);h_run();}
                struct retro_system_av_info av;h_av(&av);
                printf("switch-projection:%u:rpc base=%ux%u max=%ux%u fps=%.0f\n",scenario,av.geometry.base_width,av.geometry.base_height,av.geometry.max_width,av.geometry.max_height,av.timing.fps);
                char path[4096];snprintf(path,sizeof(path),"%s/arc.adf",system_directory);struct retro_game_info arc=h_game(path);assert(h_load(&arc));
                h_av(&av);printf("switch-projection:%u:arc base=%ux%u max=%ux%u fps=%.0f\n",scenario,av.geometry.base_width,av.geometry.base_height,av.geometry.max_width,av.geometry.max_height,av.timing.fps);
                h_hash=UINT64_C(14695981039346656037);h_video_calls=h_audio_calls=0;
                for(unsigned i=0;i<4;++i)h_run();
                printf("switch-projection:%u:arc hash=%016" PRIx64 " video=%u audio=%u discs=%u\n",scenario,h_hash,h_video_calls,h_audio_calls,h_disks.get_num_images());
                h_unload();
            }
            h_deinit();vfs_assert_no_open_handles();
#ifdef _WIN32
            FreeLibrary(library);
#else
            assert(!dlclose(library));
#endif
            return 0;
        }
        char path[4096];snprintf(path,sizeof(path),"%s/arc.adf",system_directory);struct retro_game_info g=h_game(path);
        assert(h_load(&g));h_geometry(50.0);h_hash=UINT64_C(14695981039346656037);
        for(unsigned i=0;i<4;++i)h_run();
        printf("ordinary-projection=%016" PRIx64 " video=%u audio=%u discs=%u\n",h_hash,h_video_calls,h_audio_calls,h_disks.get_num_images());
        h_unload();h_deinit();vfs_assert_no_open_handles();
#ifdef _WIN32
        FreeLibrary(library);
#else
        assert(!dlclose(library));
#endif
        return 0;
    }
    LOAD(h_hygiene,"arc_libretro_test_hygiene");LOAD(h_plant,"arc_libretro_test_plant_cmos");LOAD(h_fault,"arc_rpc_test_fault_next");LOAD(h_alloc_fail,"arc_rpc_test_fail_alloc");LOAD(phase,"arc_test_plan_cmos_phase");LOAD(snapshot,"arc_rpc_test_snapshot");
    const ArcEngineBackendOps *(*get_api)(void);LOAD(get_api,"arc_rpc_test_engine_api");h_api=get_api();
    LOAD(h_name,"arc_libretro_test_disc_name");LOAD(h_filter_cost,"arc_libretro_test_journal_filter_cost");LOAD(h_ram,"arc_rpc_test_read_ram");
    LOAD(h_guard,"arc_libretro_test_persistence_guard");LOAD(rpc_start,"arc_rpc_test_start");
    vfs.flush=h_flush;vfs.read=h_read;vfs.write=h_write;
    h_bind();h_empty();char arc_path[4096],chooser[4096],planted[4096];
    snprintf(arc_path,sizeof(arc_path),"%s/arc.adf",system_directory);snprintf(chooser,sizeof(chooser),"%s/chooser.adl",system_directory);snprintf(planted,sizeof(planted),"%s/arcdroid/cmos/planted",save_directory);
    FILE *plant=fopen(planted,"wb");assert(plant);uint8_t bytes[256];memset(bytes,0xa5,sizeof(bytes));assert(fwrite(bytes,1,sizeof(bytes),plant)==sizeof(bytes));assert(!fclose(plant));
    uint64_t gold=h_arc_trace(arc_path);
    printf("frontend-arculator-trace=%016" PRIx64 "\n",gold);
    if(!strcmp(argv[5],"guards")){
        h_persistence_guards(arc_path);
    }else if(!strcmp(argv[5],"qa-lows")){
        h_persistence_guards(arc_path);
        h_switch_persistence(arc_path,chooser,argv[7]);
        h_arculator_callbacks(arc_path);
    }else if(strcmp(argv[5],"fresh")){
        const char *paths[]={argv[2],argv[6],argv[7]};uint64_t last_id=h_state().id;
        for(unsigned scenario=0;scenario<8;++scenario){
            h_progress("scenario",scenario);
            audit_mode=1;h_launch="off";struct retro_game_info g=h_game(paths[scenario==1?1:scenario==2||scenario==3?2:0]);
            char error_path[4096];snprintf(error_path,sizeof(error_path),"%s/error.arcplan",system_directory);
            if(scenario==7)g=h_game(error_path);
            if(scenario==6)h_alloc_fail(1);
            assert(h_load(&g));ArcFrontendHygiene s=h_state();assert(s.id>last_id&&s.kind==ARC_SESSION_KIND_RISCPC);last_id=s.id;h_geometry(60.0);
            if(scenario==1||scenario==6){settle(h_run);assert(phase()==3&&terminal_active&&(h_state().retained&32u));
                if(scenario==6){s=h_state();assert(!s.engine_rpc&&!s.fault&&!s.fault_text);h_alloc_fail(0);}}
            else if(scenario==7){for(unsigned i=0;i<4;++i)h_run();assert(h_state().retained&2u);}

            else if(scenario==2||scenario==3){for(unsigned i=0;i<8;++i)h_run();assert(phase()==1);}
            else {settle(h_run);assert(phase()==0||phase()==2);h_run();if(scenario==4){h_fault();h_run();assert(h_state().fault);}}
            if(scenario==0){uint64_t kept=h_state().id;h_reset();assert(h_state().id==kept);h_run();}
            if(scenario==0||scenario==7){
                h_keyboard.callback(true,RETROK_a,0,0);h_pad=1;h_run();
                h_l3=true;h_run();h_l3=false;
                assert(h_state().retained&1u);h_pad=0;
            }
            h_plant(planted); /* Even a planted former destination must be inert. */
            if(scenario==5){h_deinit();h_bind();}else h_unload();
            assert(!terminal_active&&!fallback_lifetime);h_empty();
            audit_mode=0;plant=fopen(planted,"rb");assert(plant&&fread(bytes,1,256,plant)==256);fclose(plant);for(unsigned i=0;i<256;++i)assert(bytes[i]==0xa5);
            if(scenario%3==0){
                h_launch="auto";struct retro_game_info c=h_game(chooser);assert(h_load(&c));h_geometry(50.0);s=h_state();assert(s.kind==ARC_SESSION_KIND_ARCULATOR&&!s.engine_rpc&&!s.fault&&!s.fault_text&&!s.capabilities.av);
                /* Chooser disk control stays unavailable even when the last
                 * constructed backend was RPC. */
                assert(!h_disks.set_eject_state(true));
                h_unload();h_empty();h_launch="off";
            }
            struct retro_game_info bad=h_game("/missing-content.adf");assert(!h_load(&bad));h_empty();h_geometry(50.0);
            assert(h_arc_trace(arc_path)==gold);
        }
        /* Real guest raw refusals, callback arming after an RPC-current init,
         * and a later flush failure on the very same sector. */
        for(unsigned repeat=0;repeat<2;++repeat){
            h_progress("refusal",repeat);
            char path[4096];snprintf(path,sizeof(path),"%s/refusal.arcplan",system_directory);
            struct retro_game_info g=h_game(path);h_refusal_logs=h_refusal_notices=h_storage_notices=h_flush_failures=0;
            assert(h_load(&g));settle(h_run);uint32_t marker=0;
            for(unsigned i=0;!h_refusal_notices;++i){assert(i<200);h_run();}
            assert(h_refusal_logs==1&&h_refusal_notices==1&&!h_storage_notices);
            unsigned refusal_frames=0;
            while(marker!=3){assert(refusal_frames++<200);h_run();assert(h_ram(0x20000,&marker));
                assert(h_refusal_logs==1&&h_refusal_notices==1&&!h_storage_notices);}
            assert(refusal_frames>=3); /* Ordinary refusals cross frame boundaries. */
            h_fail_flush=true;
            for(unsigned i=0;!h_storage_notices;++i){assert(i<200);h_run();}
            h_fail_flush=false;assert(h_flush_failures==1&&h_refusal_logs==1&&h_storage_notices==1);
            for(unsigned i=0;i<4;++i)h_run();assert(h_ram(0x20000,&marker)&&marker==4);
            assert(h_refusal_logs==1&&h_refusal_notices==1&&h_storage_notices==1);
            if(!repeat){h_bind();h_empty();}else{h_unload();h_empty();}
        }
        /* Guest FORMAT changes the cached name used by signal-A automatic
         * replacement. The cache was populated before any guest instruction. */
        h_progress("format",0);
        char format_path[4096];snprintf(format_path,sizeof(format_path),"%s/format.arcplan",system_directory);
        struct retro_game_info format_game=h_game(format_path);assert(h_load(&format_game));settle(h_run);
        assert(h_disks.get_num_images()==2);ArcGuestName before={0},after={0};assert(h_name(0,&before)&&before.length);
        h_progress("filter-begin",h_filter_iterations);uint64_t cost=h_filter_cost(h_filter_iterations);h_progress("filter-end",h_filter_iterations);assert(h_name(0,&after)&&!memcmp(&before,&after,sizeof(before)));
        printf("journal-write-observer: comparisons=%u ns=%" PRIu64 " ns_per_write=%.3f\n",h_filter_iterations,cost,(double)cost/h_filter_iterations);
        uint32_t marker=0;for(unsigned i=0;marker!=2;++i){assert(i<200);if(i%25==0)h_progress("format-frame",i);h_run();assert(h_ram(0x20000,&marker));}
        bool named=h_name(0,&after);assert(!named||after.length!=before.length||memcmp(after.bytes,before.bytes,before.length));
        h_unload();h_empty();
        if(strcmp(argv[5],"probe")){
        /* Repeated full sessions, teardown allocation/handle and live heap census.
         * Warm both allocators before freezing the live-allocation baseline. */
        struct retro_game_info g=h_game(argv[2]);
        size_t floor=0,ceiling=0;
        for(unsigned i=0;i<h_cycle_count+2;++i){
            if(i%25==0)h_progress("switch-cycle",i);
            assert(h_load(&g));assert(h_state().id>last_id);last_id=h_state().id;settle(h_run);h_run();h_unload();h_empty();
            assert(h_arc_trace(arc_path)==gold);
            size_t used=h_heap();if(i==1)floor=ceiling=used;else if(i>1){if(used<floor)floor=used;if(used>ceiling)ceiling=used;}
        }
        /* After warming both routes the live heap must return exactly. */
        printf("switch-cycles=%u heap-live-min=%zu max=%zu delta=%zu handles=0 rpc-allocations=0 frontend-allocations=0\n",h_cycle_count,floor,ceiling,ceiling-floor);fflush(stdout);assert(ceiling==floor);
        /* 10,000 actual frontend admissions; reset/recovery preserves the ID;
         * init/deinit deliberately occur in the same loaded library. */
        g=h_game(argv[7]);
        for(unsigned i=0;i<h_id_admissions;++i){
            if(i%1000==0)h_progress("id-admission",i);
            assert(h_load(&g));ArcFrontendHygiene s=h_state();assert(s.id>last_id&&s.kind==ARC_SESSION_KIND_RISCPC);last_id=s.id;
            h_reset();assert(h_state().id==last_id);h_unload();h_empty();
            if(i%97==0){h_deinit();h_bind();}
        }
        assert(h_build_lines>=3);printf("frontend IDs: %u load/reset/unload admissions plus init/deinit; unique and nonzero\n",h_id_admissions);
        }
    }
    h_progress("done",0);h_deinit();h_empty();
#ifdef _WIN32
    FreeLibrary(library);
#else
    assert(!dlclose(library));
#endif
    return 0;
}
