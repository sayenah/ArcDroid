/* Flag-free ordinary entry points, lifecycle and secondary refusal. GPL-2.0-only. */
#if defined(ARCDROID_TEST_FAULTS) || defined(ARCDROID_TEST_PLANS)
#error Flag-free route harness must not enable test definitions
#endif
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#endif
#include "libretro.h"
#include "../libretro/arc_session.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
static HMODULE library;
static void *symbol(const char *name){return (void *)GetProcAddress(library,name);}
#else
#include <dlfcn.h>
static void *library;
static void *symbol(const char *name){return dlsym(library,name);}
#endif
static unsigned package_stats,guest_fields;
static bool tracing,allow_arculator,raw_expected,metadata_only;
static unsigned private_lines,arculator_starts;
static bool discarded_failure,ask_chooser,chooser_seen,fault_core;
static unsigned selected_reads;
static const char *primary_library_path;
static unsigned registrations,secondary_refusals;
static bool change_locator,system_unavailable;
static bool boot_unreadable,preflight_unavailable,preflight_ready;
static uint64_t last_session;
static uint64_t trace_hash;
static void trace(const void *p,size_t n){const unsigned char *b=p;for(size_t i=0;i<n;++i){trace_hash^=b[i];trace_hash*=UINT64_C(1099511628211);}}
static unsigned writes,package_opens,notices,videos,audios,running,arculator_writes,observer_bind,drift;
static bool callbacks_ready,legacy_interface,failed;
static const char *system_directory;
static char last_notice[1024],failure_id[128];
static struct retro_disk_control_ext_callback disks;
static void audit(const char *path,unsigned mode)
{
    if(raw_expected && allow_arculator && strstr(path,"/peer.jfd"))discarded_failure=true;
    if(strstr(path,"/riscpc/boot/"))++package_opens;
    if(preflight_unavailable && strstr(path,"/ro371.rom") && ++selected_reads==2)preflight_ready=true;
    if(change_locator && strstr(path,"/ro371.rom") && ++selected_reads==2){
        FILE *out=fopen(path,"r+b");assert(out);int byte=fgetc(out);assert(byte!=EOF && !fseek(out,0,SEEK_SET) && fputc(byte^1,out)!=EOF && !fclose(out));
    }
    if(mode & RETRO_VFS_FILE_ACCESS_WRITE){++writes;if(strstr(path,"/arcdroid/cmos/")||strstr(path,"/arcdroid/hd/"))++arculator_writes;}
}
#define ARC_RUNNER_VFS_AUDIT(path,mode) audit(path,mode)
#define ARC_RUNNER_VFS_WRITE_REFUSE(path) (discarded_failure && strstr(path,"/arcdroid/media/"))
#include "../tools/rpc_runner_vfs.h"
static struct retro_vfs_file_handle *audited_open(const char *p,unsigned mode,unsigned hints)
{if(strstr(p,"/unreadable.rom"))return NULL;return vfs_open(p,mode,hints);}
static int32_t audited_stat(const char *p,int32_t *size){if(strstr(p,"/riscpc/boot/"))++package_stats;if((boot_unreadable && strstr(p,"/HardDisc4.zip")) || (preflight_ready && !strncmp(p,save_directory,strlen(save_directory))))return 0;return vfs_stat(p,size);}
static void log_capture(enum retro_log_level level,const char *format,...)
{
    (void)level;char text[8192];va_list args;va_start(args,format);vsnprintf(text,sizeof(text),format,args);va_end(args);
    if(strstr(text,"RPC secondary load refused before preparation"))++secondary_refusals;
    if(!strncmp(text,"ArcDroid build:",15)){if(!metadata_only)assert(strstr(text,"gate=open"));
        if(fault_core){assert(strstr(text,"substitutions=authored-catalogue,test-faults,test-plans"));
            if(symbol("arc_route_probe_position"))assert(strstr(text,",read-only-position,unit-clock"));}
        if(strstr(text,"target=arcdroid_rpcroute_libretro"))assert(strstr(text,"substitutions=authored-catalogue catalogue=authored"));}
    if(!strncmp(text,"N-",2))snprintf(failure_id,sizeof(failure_id),"%s",text);
    if(strstr(text,"RPC route: production-observer-cells=")){assert(strstr(text,"production-observer-cells=0"));++observer_bind;}
    if(strstr(text,"pair-changed:"))++drift;
    if((strstr(text,"chooser asked") || strstr(text,"chooser shown")))chooser_seen=true;
    if(strstr(text,"Firmware: ArcDroid test ROM"))++arculator_starts;
    if(!allow_arculator)assert(!strstr(text,"Machine: requested=") && !strstr(text,"Firmware: "));
    if(system_unavailable && strstr(text,"summary:")){const char *kind=strstr(text,"kind=");assert(kind && strtoul(kind+5,NULL,10)==ARC_SESSION_KIND_RISCPC);}
    if(strstr(text,"summary: phase=pending ") && strstr(text,"state=11 "))failed=true;
    if(strstr(text,"summary: phase=pending ") && strstr(text,"state=10 ")){const char *at=strstr(text,"session=");assert(at);uint64_t id=strtoull(at+8,NULL,10);assert(id && id>last_session);last_session=id;++running;}
    if(strstr(text,"ROUTE-PRIVATE-SENTINEL")){++private_lines;assert(raw_expected);}
}
static bool environment(unsigned command,void *data)
{
    switch(command){
    case RETRO_ENVIRONMENT_GET_LIBRETRO_PATH:*(const char **)data=primary_library_path;return true;
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:++registrations;return true;
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:if(metadata_only){*(unsigned *)data=2;return true;}return false;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:{
        ++registrations;
        if(!metadata_only)return false;
        const struct retro_core_options_v2 *options=data;
        for(const struct retro_core_option_v2_definition *d=options->definitions;d->key;++d){
            printf("option|%s|%s|%s|%s|%s|%s|%s\n",d->key,d->desc?d->desc:"",d->desc_categorized?d->desc_categorized:"",d->info?d->info:"",d->info_categorized?d->info_categorized:"",d->category_key?d->category_key:"",d->default_value?d->default_value:"");
            for(unsigned i=0;d->values[i].value;++i)printf("value|%s|%s|%s\n",d->key,d->values[i].value,d->values[i].label?d->values[i].label:"");
        }return true;
    }
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:((struct retro_log_callback *)data)->log=log_capture;return true;
    case RETRO_ENVIRONMENT_GET_VFS_INTERFACE:((struct retro_vfs_interface_info *)data)->iface=&vfs;return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:*(const char **)data=save_directory;return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:if(system_unavailable)return false;*(const char **)data=system_directory;return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:return true;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:*(bool *)data=true;return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE:{struct retro_variable *v=data;if(ask_chooser && !strcmp(v->key,"arcdroid_launch_chooser")){v->value="ask";return true;}return false;}
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:*(bool *)data=false;return true;
    case RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE:++registrations;disks=*(struct retro_disk_control_ext_callback *)data;return true;
    case RETRO_ENVIRONMENT_SET_MESSAGE_EXT:{if(legacy_interface)return false;struct retro_message_ext *m=data;
        if(m->type==RETRO_MESSAGE_TYPE_STATUS){assert(m->target==RETRO_MESSAGE_TARGET_OSD);assert(m->priority==(*m->msg && m->level!=RETRO_LOG_ERROR?2u:3u));assert(!*m->msg?m->duration==1u:m->level==RETRO_LOG_ERROR?m->duration==~0u:(m->duration==2000u || m->duration==5000u));}++notices;snprintf(last_notice,sizeof(last_notice),"%s",m->msg);return true;}
    case RETRO_ENVIRONMENT_SET_MESSAGE:{struct retro_message *m=data;++notices;snprintf(last_notice,sizeof(last_notice),"%s",m->msg);return true;}
    default:return false;
    }
}
static void video(const void *p,unsigned w,unsigned h,size_t pitch){(void)p;(void)w;(void)h;(void)pitch;++videos;if(tracing){trace(&w,sizeof(w));trace(&h,sizeof(h));if(p)for(unsigned y=0;y<h;++y)trace((const unsigned char *)p+y*pitch,w*4);}
    if(p){const unsigned char *row=p;bool nonzero=false;for(unsigned y=0;y<h && !nonzero;++y)for(unsigned x=0;x<w*4;++x)if(row[y*pitch+x]){nonzero=true;break;}if(!allow_arculator && !running)assert(!nonzero);if(nonzero)++guest_fields;}}
static size_t audio(const int16_t *p,size_t n){if(!allow_arculator && !running)for(size_t i=0;i<n*2;++i)assert(!p[i]);if(tracing)trace(p,n*4);++audios;return n;}
static void poll(void){}
static int16_t input(unsigned a,unsigned b,unsigned c,unsigned d){(void)a;(void)b;(void)c;(void)d;return 0;}
static void sample(int16_t left,int16_t right){(void)left;(void)right;}
static bool (*primary_load)(const struct retro_game_info *);
static void (*set_video)(retro_video_refresh_t),(*set_sample)(retro_audio_sample_t),(*set_audio)(retro_audio_sample_batch_t),(*set_poll)(retro_input_poll_t),(*set_input)(retro_input_state_t);
static unsigned (*api_version)(void);
static void (*av_info)(struct retro_system_av_info *);
static bool load_primary(const struct retro_game_info *game)
{
    /* Both CLI and menu/intent converge here: runloop.c:4800-4821 loads
     * before core_init_libretro_cbs (:4634-4638), then queries AV (:4664).
     * In particular, the first primary load has NO frame callbacks. */
    if(!primary_load(game))return false;
    assert(api_version()==RETRO_API_VERSION);
    set_video(video);set_sample(sample);set_audio(audio);set_input(input);set_poll(poll);
    callbacks_ready=true;
    struct retro_system_av_info info={0};av_info(&info);assert(info.geometry.base_width && info.timing.fps>0);
    return true;
}
static bool probe_environment(unsigned command,void *data)
{if(command==RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME){*(bool *)data=false;return true;}return false;}
static bool ignored_environment(unsigned command,void *data){(void)command;(void)data;return false;}
static bool secondary_environment(unsigned command,void *data)
{
    /* runahead.c:390-406 forwards to the SAME primary environment; the only
     * secondary override is GET_VARIABLE[_UPDATE], not GET_LIBRETRO_PATH. */
    if(command==RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE){*(bool *)data=true;return true;}
    return environment(command,data);
}
static void check_alias(const struct retro_game_info *game,void (*env)(retro_environment_t),
    void (*init)(void),void (*reset)(void),void (*run)(void),void (*unload)(void),void (*deinit)(void))
{
    unsigned w=writes,n=notices,r=registrations,v=videos,a=audios,refused=secondary_refusals;
    unsigned packages=package_opens,stats=package_stats,bindings=observer_bind;
    struct retro_disk_control_ext_callback registered=disks;
    assert(!primary_load(game)); /* Repeated primary load itself has no effect. */
    env(secondary_environment);init();assert(!primary_load(game));
    reset();run();unload();deinit(); /* Failed secondary's SAME-module cleanup. */
    assert(secondary_refusals==refused+2 && writes==w && notices==n && registrations==r && videos==v && audios==a);
    assert(package_opens==packages && package_stats==stats && observer_bind==bindings && !memcmp(&registered,&disks,sizeof(disks)));
    env(probe_environment);env(environment); /* Legitimate live metadata probe. */
    secondary_refusals=0;
}
#define LOAD(v,n) do{void *p=symbol(n);assert(p);memcpy(&(v),&p,sizeof(v));}while(0)
int main(int argc,char **argv)
{
    assert(argc==7);primary_library_path=argv[1];boot_unreadable=!strcmp(argv[5],"refuse:N-20");preflight_unavailable=!strcmp(argv[5],"refuse:N-19");system_unavailable=!strcmp(argv[5],"system-unavailable");change_locator=!strcmp(argv[5],"refuse:N-B firmware");vfs.open=audited_open;fault_core=!strcmp(argv[5],"chooser-cycles") || !strcmp(argv[5],"metadata-fault") || !strcmp(argv[5],"calendar") || !strncmp(argv[5],"matrix:",7);metadata_only=!strncmp(argv[5],"metadata",8);vfs.stat=audited_stat;raw_expected=!strcmp(argv[5],"failure-raw") || !strcmp(argv[5],"discarded");allow_arculator=!strcmp(argv[5],"discarded");system_directory=argv[2];assert(canonical(argv[3],save_directory));
#ifdef _WIN32
    library=LoadLibraryA(argv[1]);
#else
    library=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
#endif
    assert(library);
    void (*env)(retro_environment_t),(*init)(void),(*deinit)(void),(*unload)(void),(*run)(void),(*reset)(void);
    bool (*load)(const struct retro_game_info *),(*serialize)(void *,size_t),(*unserialize)(const void *,size_t);size_t (*state_size)(void);
    LOAD(env,"retro_set_environment");LOAD(init,"retro_init");LOAD(deinit,"retro_deinit");LOAD(unload,"retro_unload_game");LOAD(run,"retro_run");LOAD(reset,"retro_reset");LOAD(primary_load,"retro_load_game");load=load_primary;
    LOAD(api_version,"retro_api_version");LOAD(av_info,"retro_get_system_av_info");LOAD(set_sample,"retro_set_audio_sample");
    LOAD(set_video,"retro_set_video_refresh");LOAD(set_audio,"retro_set_audio_sample_batch");LOAD(set_poll,"retro_set_input_poll");LOAD(set_input,"retro_set_input_state");
    LOAD(state_size,"retro_serialize_size");LOAD(serialize,"retro_serialize");LOAD(unserialize,"retro_unserialize");
    if(!fault_core)assert(!symbol("arc_libretro_test_rpc_catalogue") && !symbol("arc_rpc_test_start"));
    else assert(symbol("arc_libretro_test_rpc_catalogue"));
    void (*info)(struct retro_system_info *);LOAD(info,"retro_get_system_info");struct retro_system_info system={0};
    /* Menu LOAD_CORE does an additional metadata probe. Common init's
     * LOAD_CORE_PERSIST probes again (runloop.c:4698, retroarch.c:3334), then
     * queries system info (:4712). No frame callbacks in either sequence. */
    if(!strcmp(argv[5],"primary-menu")){info(&system);env(probe_environment);env(ignored_environment);}
    info(&system);env(probe_environment);env(ignored_environment);info(&system);
    env(environment);init();struct retro_game_info game={.path=argv[4]};
    if(metadata_only){
        printf("system|%s|%s|%s|%u|%u\n",system.library_name,system.library_version,system.valid_extensions,system.need_fullpath,system.block_extract);deinit();return 0;
    }
    assert(!callbacks_ready && !videos && !audios && !secondary_refusals);
    legacy_interface=!strcmp(argv[5],"legacy");
    if(!strcmp(argv[5],"primary-arc")){
        allow_arculator=true;assert(load(&game) && callbacks_ready && arculator_starts==1);
        check_alias(&game,env,init,reset,run,unload,deinit);
        for(unsigned f=0;f<8;++f)run();assert(videos==8 && audios && !secondary_refusals);
        unload();deinit();vfs_assert_no_open_handles();puts("rpc-route: callback-less primary Arculator load passes");return 0;
    }
    if(!strncmp(argv[5],"failure-",8) || !strcmp(argv[5],"discarded")){
        bool loaded=load(&game);assert(loaded==allow_arculator);assert(raw_expected?private_lines>0:private_lines==0);if(allow_arculator)assert(arculator_starts==1);
        if(loaded)run();unload();deinit();vfs_assert_no_open_handles();return 0;
    }
    if(system_unavailable || !strcmp(argv[5],"missing") || !strncmp(argv[5],"refuse:",7)){
        assert(!load(&game));assert(*last_notice);
        assert(!running && !arculator_writes);if(system_unavailable)assert(strstr(failure_id,"N-5:"));if(!strncmp(argv[5],"refuse:",7))assert(strstr(failure_id,argv[5]+7));
        if(!strcmp(argv[5],"refuse:N-6"))printf("N-6 text: %s\n",last_notice);
        deinit();vfs_assert_no_open_handles();puts("rpc-route: shipping refusal and secondary isolation pass");return 0;
    }
    assert(load(&game));assert(state_size()==0);unsigned char pending[8]={0};assert(!serialize(pending,sizeof(pending)) && !unserialize(pending,sizeof(pending)));
    /* R-1a: an active library is never a fresh primary. Also replay Android's
     * aliased copy: SAME functions, secondary environment, init/load/cleanup.
     * Its init and cleanup must leave the primary alive and registered. */
    check_alias(&game,env,init,reset,run,unload,deinit);
    if(!strcmp(argv[5],"primary-cli") || !strcmp(argv[5],"primary-menu")){
        assert(callbacks_ready && !secondary_refusals);run();assert(videos==1 && audios==1);
        unload();deinit();vfs_assert_no_open_handles();puts("rpc-route: RetroArch primary load/init/callback order passes");return 0;
    }
    if(!strncmp(argv[5],"matrix:",7)){
        unsigned target;char action[16];assert(sscanf(argv[5],"matrix:%u:%15s",&target,action)==2);
        void (*position)(unsigned *),(*fingerprint)(unsigned char *);LOAD(position,"arc_route_probe_position");LOAD(fingerprint,"arc_route_probe_fingerprint");
        char archive[4096],hidden[4096];
        if(target==11){snprintf(archive,sizeof(archive),"%s/arcdroid/riscpc/boot/HardDisc4.zip",system_directory);snprintf(hidden,sizeof(hidden),"%s/withheld-package",system_directory);assert(!rename(archive,hidden));}
        unsigned state;position(&state);
        char history[128]="0";size_t history_size=1;
        for(unsigned f=0;state!=target;++f){
            if(f>=20000 || (state>=10 && target!=11)){fprintf(stderr,"unreached=%u states=%s notice=%s\n",target,history,last_notice);assert(false);}
            unsigned prior=state,v=videos,a=audios;run();position(&state);assert(videos==v+1 && audios==a+1);
            if(state!=prior && history_size<sizeof(history))history_size+=(size_t)snprintf(history+history_size,sizeof(history)-history_size,",%u",state);
        }
        if(target!=10){unsigned char before[32],after[32];fingerprint(before);
            assert(!disks.set_eject_state(true) && !disks.set_image_index(0) && !disks.replace_image_index(0,NULL) && !disks.add_image_index());fingerprint(after);assert(!memcmp(before,after,32));}
        if(target==11)assert(!rename(hidden,archive));
        if(!strcmp(action,"reset")){reset();position(&state);for(unsigned f=0;state!=10;++f){assert(f<20000 && state!=11);run();position(&state);}unload();}
        else if(!strcmp(action,"unload"))unload();else{assert(!strcmp(action,"deinit"));deinit();env(environment);init();callbacks_ready=false;}
        vfs_assert_no_open_handles();allow_arculator=true;char path[4096];snprintf(path,sizeof(path),"%s/arc-control.adf",system_directory);struct retro_game_info arc={.path=path};assert(load(&arc));
        tracing=true;trace_hash=UINT64_C(1099511628211);for(unsigned f=0;f<8;++f)run();tracing=false;
        printf("matrix:%u:%s arculator-trace=%016llx\n",target,action,(unsigned long long)trace_hash);unload();deinit();vfs_assert_no_open_handles();return 0;
    }
    if(!strcmp(argv[5],"changed-packages") || !strcmp(argv[5],"namespace-file")){
        char path[4096];
        if(!strcmp(argv[5],"changed-packages")){
            snprintf(path,sizeof(path),"%s/arcdroid/riscpc/boot/HardDisc4.zip",system_directory);assert(!remove(path));
        }else{
            snprintf(path,sizeof(path),"%s/arcdroid/hd-riscpc",save_directory);FILE *out=fopen(path,"wb");assert(out);assert(fputs("authored namespace change",out)>=0 && !fclose(out));
        }
        for(unsigned f=0;f<10000 && !failed;++f)run();assert(failed && !running && !observer_bind && !arculator_writes && state_size()==0);
        unload();deinit();vfs_assert_no_open_handles();return 0;
    }
    if(!strcmp(argv[5],"empty-after-probe")){
        char parent[4096],store[4096],digest[41]={0};snprintf(parent,sizeof(parent),"%s/arcdroid/media",save_directory);
        struct retro_vfs_dir_handle *directory=vfs_opendir(parent,false);assert(directory);
        while(vfs_readdir(directory)){const char *name=vfs_dirname(directory);if(name && strlen(name)==40){memcpy(digest,name,41);break;}}
        assert(!vfs_closedir(directory) && *digest);snprintf(parent,sizeof(parent),"%s/arcdroid/hd-riscpc",save_directory);assert(!vfs_mkdir(parent));
        snprintf(store,sizeof(store),"%s/%s",parent,digest);assert(!vfs_mkdir(store));
    }
    for(unsigned f=0;f<100000 && !running;++f){unsigned v=videos,a=audios;run();assert(videos==v+1 && audios==a+1);}
    assert(running==1 && !arculator_writes && observer_bind);
    check_alias(&game,env,init,reset,run,unload,deinit);
    if(!strcmp(argv[5],"calendar")){
        bool (*ram)(unsigned,uint32_t *);LOAD(ram,"arc_rpc_test_read_ram");
        for(unsigned reopen=0;reopen<2;++reopen){
            for(unsigned resetting=0;resetting<2;++resetting){
                for(unsigned f=0;f<32;++f)run(); /* execute fresh boot after reset */
                uint32_t done=0,year=0,checksum=0;
                for(unsigned f=0;done!=0xa5;++f){assert(f<1000);run();assert(ram(0x20408,&done));}
                assert(ram(0x20400,&year) && ram(0x20404,&checksum) && checksum==1);
                time_t now=time(NULL);struct tm *local=localtime(&now);assert(local && year==(unsigned)local->tm_year+1900);
                if(!resetting)reset();
            }
            unload();
            if(!reopen){running=0;assert(load(&game));for(unsigned f=0;!running;++f){assert(f<10000);run();}assert(running==1);}
        }
        deinit();vfs_assert_no_open_handles();puts("rpc-route calendar: guest IIC year/checksum, reset and persisted reopen passed");return 0;
    }
    if(!strcmp(argv[5],"admit") || !strcmp(argv[5],"drift") || !strcmp(argv[5],"empty-after-probe")){
        if(!strcmp(argv[5],"empty-after-probe"))assert(package_opens>0);
        assert(drift==(!strcmp(argv[5],"drift")?1u:0u));for(unsigned f=0;f<30 && !guest_fields;++f)run();assert(guest_fields);unload();deinit();vfs_assert_no_open_handles();return 0;
    }
    /* Independent copied libraries: replay each RA create path while primary lives.
     * Refusal occurs before preparation can open files or execute instructions. */
    struct retro_disk_control_ext_callback primary_disks=disks;
    for(unsigned route=0;route<4;++route){
#ifdef _WIN32
        HMODULE secondary=LoadLibraryA(argv[6]);assert(secondary);library=secondary;
#else
        void *secondary=dlopen(argv[6],RTLD_NOW|RTLD_LOCAL);assert(secondary);library=secondary;
#endif
        void (*se)(retro_environment_t),(*si)(void),(*sd)(void),(*su)(void),(*sr)(void),(*sc)(void),(*ss)(unsigned,bool,const char *);
        bool (*sl)(const struct retro_game_info *);
        LOAD(se,"retro_set_environment");LOAD(si,"retro_init");LOAD(sd,"retro_deinit");LOAD(su,"retro_unload_game");LOAD(sr,"retro_run");LOAD(sl,"retro_load_game");LOAD(sc,"retro_cheat_reset");LOAD(ss,"retro_cheat_set");
        unsigned w=writes,n=notices,v=videos,a=audios,r=registrations,refused=secondary_refusals;
        se(secondary_environment);si();
        /* Initial create, failed-preempt re-entry, cheat reset, cheat set all
         * reach secondary_core_create's identical env/init/load sequence.
         * A failed load goes directly to unload/deinit (:523-525), without
         * installing callbacks. Retry from the next frontend entry uses a
         * newly loaded copy, as this loop does. */
        assert(!sl(&game));
        assert(secondary_refusals==refused+1);
        sc();ss(0,true,"00000000");sr();su();sd();
        assert(writes==w && notices==n && videos==v && audios==a && running==1 && registrations==r);
        assert(!memcmp(&disks,&primary_disks,sizeof(disks)));
#ifdef _WIN32
        FreeLibrary(secondary);
#else
        assert(!dlclose(secondary));
#endif
        run();assert(videos==v+1 && audios==a+1);
    }

    if(!strcmp(argv[5],"cycles") || !strcmp(argv[5],"chooser-cycles")){
        char from[4096],to[4096],arc_path[4096];snprintf(from,sizeof(from),"%s/arc-control.rom",system_directory);snprintf(to,sizeof(to),"%s/arcdroid/firmware/control.rom",system_directory);
        FILE *input_file=fopen(from,"rb"),*output_file=fopen(to,"wb");assert(input_file && output_file);unsigned char chunk[4096];size_t used;
        while((used=fread(chunk,1,sizeof(chunk),input_file)))assert(fwrite(chunk,1,used,output_file)==used);assert(!fclose(input_file) && !fclose(output_file));
        snprintf(arc_path,sizeof(arc_path),"%s/arc-control.adf",system_directory);struct retro_game_info arc={.path=arc_path};uint64_t baseline=0;
        /* A class-A refusal and a live chooser both precede ordinary switches. */
        unload();running=0;char hidden[4][4096],original[4][4096];
        static const char *names[]={"ro350.rom","ro360.rom","ro370.rom","ro371.rom"};
        for(unsigned i=0;i<4;++i){snprintf(original[i],sizeof(original[i]),"%s/arcdroid/firmware/%s",system_directory,names[i]);snprintf(hidden[i],sizeof(hidden[i]),"%s/hidden-%s",system_directory,names[i]);assert(!rename(original[i],hidden[i]));}
        assert(!load(&game) && !running);
        for(unsigned i=0;i<4;++i)assert(!rename(hidden[i],original[i]));
        if(fault_core){
        allow_arculator=ask_chooser=true;char chooser_path[4096];snprintf(chooser_path,sizeof(chooser_path),"%s/arc-chooser.adl",system_directory);
        struct retro_game_info chooser={.path=chooser_path};assert(load(&chooser));run();assert(chooser_seen);
        unload();ask_chooser=allow_arculator=false;
        }
        assert(load(&game));for(unsigned f=0;f<10000 && !running;++f)run();assert(running==1);
        for(unsigned cycle=0;cycle<(fault_core?2u:200u);++cycle){
            unload();allow_arculator=true;assert(load(&arc));tracing=true;trace_hash=UINT64_C(1469598103934665603);for(unsigned f=0;f<8;++f)run();tracing=false;
            if(!cycle)baseline=trace_hash;else assert(trace_hash==baseline);unload();vfs_assert_no_open_handles();
            unsigned before_arc=arculator_writes;running=0;allow_arculator=false;assert(load(&game));for(unsigned f=0;f<10000 && !running;++f)run();assert(running==1 && arculator_writes==before_arc);
            assert(state_size()==0);unsigned char untouched[8]={0};assert(!serialize(untouched,sizeof(untouched)) && !unserialize(untouched,sizeof(untouched)));
        }
        unload();deinit();vfs_assert_no_open_handles();puts(fault_core?"rpc-route: fault-test chooser/refusal transitions pass":"rpc-route: 200 ordinary RPC/Arculator cycles have identical Arculator video/audio and no cross-engine writes");return 0;
    }
    assert(disks.get_num_images()==1 && disks.add_image_index());assert(disks.get_num_images()==2);
    assert(disks.set_eject_state(true));assert(disks.set_image_index(0));assert(disks.set_eject_state(false));
    for(unsigned f=0;f<30 && !guest_fields;++f)run();assert(guest_fields);
    reset();run();assert(!arculator_writes);
    unsigned char state[64];memset(state,0x5a,sizeof(state));
    assert(state_size()==0 && !serialize(state,sizeof(state)) && !unserialize(state,sizeof(state)));
    for(unsigned i=0;i<sizeof(state);++i)assert(state[i]==0x5a);
    unload();unsigned packages=package_opens,stats=package_stats;running=0;
    char archive[4096];for(unsigned i=0;i<2;++i){snprintf(archive,sizeof(archive),"%s/arcdroid/riscpc/boot/%s",system_directory,i?"PlingSystem.zip":"HardDisc4.zip");assert(!remove(archive));}
    assert(load(&game));for(unsigned f=0;f<10000 && !running;++f)run();assert(running==1 && package_opens==packages && package_stats==stats);
    unload();deinit();vfs_assert_no_open_handles();puts("rpc-route: ordinary first use, reopen, reset, serialization and helper exclusion pass");return 0;
}
