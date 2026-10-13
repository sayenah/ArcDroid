/* Non-shipping, real frontend lifecycle and bounded capture harness. GPL-2.0-only. */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#endif
#include "libretro.h"
#include "rpc_session.h"
#include "../libretro/rpc_hd_store.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <inttypes.h>
#include <limits.h>
#if defined(_WIN32)
#include <windows.h>
#include <io.h>
#include <fcntl.h>
static HMODULE library;
static void *symbol(const char *s){return (void *)GetProcAddress(library,s);}
#else
#include <dlfcn.h>
static void *library;
static void *symbol(const char *s){return dlsym(library,s);}
#endif
static unsigned audit_mode, arc_accesses, rpc_accesses, tmp_writes;
static void audit(const char *p,unsigned mode)
{
    if(strstr(p,".tmp") && (mode&RETRO_VFS_FILE_ACCESS_WRITE))++tmp_writes;
    if(strstr(p,"/arcdroid/cmos-riscpc/")){++rpc_accesses;assert(audit_mode!=2);}
    if(strstr(p,"/arcdroid/cmos/")){++arc_accesses;assert(audit_mode!=1);}
}
#define ARC_RUNNER_VFS_AUDIT(path,mode) audit(path,mode)
#include "../tools/rpc_runner_vfs.h"
static const char *system_directory;static unsigned black_frames, errors, notices;
static bool extended_messages=true;
static unsigned progress_messages,fallback_messages,cleared_messages,fallback_lifetime;
static bool terminal_active,last_status_clear;
static enum retro_log_level message_level;
static bool (*rpc_start)(ArcRpcModel,const uint8_t *,size_t,const uint8_t *,uint64_t,char *,size_t);
static void (*payload_evidence)(uint8_t *,uint8_t *,uint64_t *,uint64_t *,uint64_t *,unsigned *);
static unsigned (*phase)(void);static bool (*advance)(uint64_t);static void (*snapshot)(ArcRpcTestSnapshot *);
static bool environment(unsigned command,void *data)
{
    switch(command){
    case RETRO_ENVIRONMENT_GET_VFS_INTERFACE:((struct retro_vfs_interface_info *)data)->iface=&vfs;return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:*(const char **)data=system_directory;return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:*(const char **)data=save_directory;return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:return true;
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:{struct retro_system_av_info *av=data;
        if(phase()==1 || phase()==3)assert(av->timing.fps==60.0 && av->timing.sample_rate==48000.0);
        return true;}
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:*(bool *)data=true;return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE:{struct retro_variable *v=data;
        if(!strcmp(v->key,"arcdroid_machine"))v->value="A3000";
        else if(!strcmp(v->key,"arcdroid_launch"))v->value="off";
        else if(!strcmp(v->key,"arcdroid_hard_disc"))v->value="off";
        else return false;
        return true;}
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:*(bool *)data=false;return true;
    case RETRO_ENVIRONMENT_SET_MESSAGE_EXT:{struct retro_message_ext *m=data;
        message_level=m->level;
        if(m->type==RETRO_MESSAGE_TYPE_STATUS && m->msg[0]){
            if(m->level==RETRO_LOG_ERROR){assert(m->duration==UINT_MAX && last_status_clear);terminal_active=true;}
            else {assert(m->duration==60000);++progress_messages;}
        }
        if(m->type==RETRO_MESSAGE_TYPE_STATUS){
            last_status_clear=!m->msg[0];
            if(last_status_clear){++cleared_messages;terminal_active=false;}
        }
        if(!extended_messages)return false;
        ++notices;if(m->level==RETRO_LOG_ERROR)++errors;return true;}
    case RETRO_ENVIRONMENT_SET_MESSAGE:{struct retro_message *m=data;
        if(message_level!=RETRO_LOG_ERROR)assert(m->frames==1 || m->frames==120 || m->frames==300);
        fallback_lifetime=m->frames;
        ++fallback_messages;++notices;if(message_level==RETRO_LOG_ERROR)++errors;return true;}
    default:return false;
    }
}
static void video(const void *p,unsigned w,unsigned h,size_t pitch)
{
    if(fallback_lifetime)--fallback_lifetime;
    if(phase() && phase()!=2){assert(p&&w&&h);const uint32_t *first=p,*last=(const uint32_t *)((const uint8_t *)p+(h-1)*pitch);assert(!first[0]&&!last[w-1]);++black_frames;}
}
static size_t audio(const int16_t *p,size_t n){
    if(phase()==1 || phase()==3){assert(n==800);for(size_t i=0;i<n*2;++i)assert(!p[i]);}
    return n;
}
static void poll(void){}
static int16_t input(unsigned p,unsigned d,unsigned i,unsigned id){(void)p;(void)d;(void)i;(void)id;return 0;}
#define LOAD(v,name) do {void *p=symbol(name);assert(p);memcpy(&(v),&p,sizeof(v));}while(0)
static void settle(void (*run)(void))
{
    for(unsigned i=0;phase()==1;++i){assert(i<100000);run();}
}
int main(int argc,char **argv)
{
    assert(argc==8);system_directory=argv[3];assert(canonical(argv[4],save_directory));
#ifdef _WIN32
    library=LoadLibraryA(argv[1]);
#else
    library=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
#endif
    assert(library);
    char original_root[4096];strcpy(original_root,save_directory);
#ifdef _WIN32
    strcpy(save_directory,"C:\\");assert(below_save("C:\\child")&&!below_save("D:\\child"));
#else
    strcpy(save_directory,"/");assert(below_save("/child")&&!below_save("relative"));
#endif
    strcpy(save_directory,original_root);
#ifdef _WIN32
    if(save_directory[1]==':'){
        char previous[4096],outside[64];assert(GetCurrentDirectoryA(sizeof(previous),previous));
        assert(SetCurrentDirectoryA(save_directory));
        snprintf(outside,sizeof(outside),"%c:\\arcdroid-vfs-outside-save",save_directory[0]);
        assert(!writable(outside)); /* drive-relative parent would resolve inside save */
        assert(SetCurrentDirectoryA(previous));
    }
#endif
    assert(!vfs_open(argv[2],RETRO_VFS_FILE_ACCESS_WRITE,0)); /* plan outside save */
    void (*set_environment)(retro_environment_t),(*init)(void),(*deinit)(void),(*set_video)(retro_video_refresh_t),(*set_audio)(retro_audio_sample_batch_t),(*set_poll)(retro_input_poll_t),(*set_input)(retro_input_state_t),(*run)(void),(*reset)(void),(*unload)(void);
    bool (*load)(const struct retro_game_info *);void (*cmos)(uint8_t *);
    LOAD(set_environment,"retro_set_environment");LOAD(init,"retro_init");LOAD(deinit,"retro_deinit");LOAD(set_video,"retro_set_video_refresh");LOAD(set_audio,"retro_set_audio_sample_batch");LOAD(set_poll,"retro_set_input_poll");LOAD(set_input,"retro_set_input_state");LOAD(run,"retro_run");LOAD(reset,"retro_reset");LOAD(unload,"retro_unload_game");LOAD(load,"retro_load_game");LOAD(phase,"arc_test_plan_cmos_phase");LOAD(snapshot,"arc_rpc_test_snapshot");LOAD(cmos,"arc_rpc_test_cmos_snapshot");LOAD(advance,"arc_rpc_test_advance");
    set_environment(environment);init();set_video(video);set_audio(audio);set_poll(poll);set_input(input);
    const char *mode=argv[5];extended_messages=strcmp(mode,"fallback") && strcmp(mode,"terminalfallback");
    if(!strcmp(mode,"nodelete")){
        LOAD(rpc_start,"arc_rpc_test_start");LOAD(payload_evidence,"arc_engine_test_cmos_evidence");
        char rom_path[4096];snprintf(rom_path,sizeof(rom_path),"%s/full.rom",system_directory);FILE *f=fopen(rom_path,"rb");assert(f);
        uint8_t *rom=malloc(2097152);assert(rom&&fread(rom,1,2097152,f)==2097152);fclose(f);uint8_t seed[256]={0};char error[256];
        assert(rpc_start((ArcRpcModel)atoi(argv[7]),rom,2097152,seed,946684800,error,sizeof(error)));free(rom);
        uint64_t remaining=UINT64_C(45000000000);while(remaining){uint64_t ns=remaining<16666667?remaining:16666667;assert(advance(ns));remaining-=ns;}
        uint8_t bytes[256],mask[32];uint64_t writes,last,now;unsigned state;payload_evidence(bytes,mask,&writes,&last,&now,&state);
        assert(!writes&&!last&&now==UINT64_C(45000000000));for(unsigned i=0;i<32;++i)assert(!mask[i]);unload();deinit();vfs_assert_no_open_handles();return 0;
    }
    struct retro_game_info game={argv[2],NULL,0,NULL};
    if(!strcmp(mode,"cross")){
        char arc_path[4096];snprintf(arc_path,sizeof(arc_path),"%s/arc.adf",system_directory);
        struct retro_game_info arc_game={arc_path,NULL,0,NULL};bool (*arc_write)(unsigned,uint8_t);bool (*arc_image)(uint8_t *);
        LOAD(arc_write,"arc_engine_test_cmos_write");LOAD(arc_image,"arc_engine_test_cmos_image");
        audit_mode=2;assert(load(&arc_game));assert(arc_write(100,0x22));run();
        audit_mode=0;assert(load(&game));settle(run);assert(phase()==2); /* existing Arc eager persistence survives the switch */
        audit_mode=1;run();unload();assert(rpc_accesses);
        audit_mode=2;assert(load(&arc_game));uint8_t live[256];assert(arc_image(live));assert(live[100]==0x22);
        run();unload();assert(arc_accesses);deinit();vfs_assert_no_open_handles();puts("Arculator/RPC/Arculator persistence and audited path isolation passed");return 0;
    }
    audit_mode=1;bool accepted=load(&game);
unsigned expected_rate=(unsigned)atoi(argv[6]);
    if(!strcmp(mode,"reject")){if(accepted)settle(run);assert((!accepted||phase()==3)&&errors);unload();deinit();vfs_assert_no_open_handles();return 0;}
    assert(accepted);ArcRpcTestSnapshot s;snapshot(&s);assert(s.now_ns==0);
    if(!strcmp(mode,"positive1")){bool (*pacing)(unsigned);LOAD(pacing,"arc_test_plan_cmos_pacing");assert(pacing(1));}
    /* V2 admission now yields black preparation frames. They execute neither
     * the requested guest nor scripted input; semantic refusals are deferred. */
    if(strcmp(mode,"positive")&&strcmp(mode,"positive1")&&strcmp(mode,"negative")&&strcmp(mode,"reset")&&strcmp(mode,"fallback")&&strcmp(mode,"terminalfallback"))settle(run);

    if(!strcmp(mode,"hd")||!strcmp(mode,"hd-frame")||!strcmp(mode,"hd-reload")){
        bool (*stats)(ArcRpcHdStats *);LOAD(stats,"arc_test_plan_hd_stats");
        ArcRpcHdStats a;assert(phase()==2&&stats(&a)&&a.published&&!a.read_only);
        for(unsigned i=0;i<8;++i)run();assert(stats(&a)&&a.commands==1&&a.sequence==(!strcmp(mode,"hd-reload")?2u:1u));
        uint32_t result;bool (*ram)(unsigned,uint32_t *);LOAD(ram,"arc_rpc_test_read_ram");
        assert(ram(24,&result)&&result==0xa5);assert(ram(20,&result)&&result==1024*0x5a);
        if(!strcmp(mode,"hd-frame")){
            bool (*write)(uint32_t,uint32_t,const uint8_t *);uint64_t (*frame_ns)(void);
            LOAD(write,"arc_test_plan_hd_write");LOAD(frame_ns,"arc_engine_test_frame_ns");
            unsigned seen[7]={0};uint64_t high_bytes[7]={0},high_ops[7]={0},high_ns[7]={0},high_hash[7]={0},high_copy[7]={0},high_metadata[7]={0};
            uint8_t bytes[131072];memset(bytes,0x61,sizeof(bytes));clock_t start=clock();
            /* D-159 sequential envelope: 16,480 journal bytes/frame. */
            for(unsigned frame=0;frame<160;++frame){assert(write(10000+(frame*32)%100000,32,bytes));
                assert(stats(&a));unsigned state=a.state;run();assert(stats(&a)&&!a.read_only);
                assert(a.tick_bytes<=9437712&&a.tick_operations<=4096&&a.allocation_high<=4889964&&a.tick_metadata_operations<=4&&a.tick_namespace_operations<=1);
                if(state<7){++seen[state];if(a.tick_bytes>high_bytes[state])high_bytes[state]=a.tick_bytes;
                    if(a.tick_operations>high_ops[state])high_ops[state]=a.tick_operations;
                    if(a.tick_metadata_operations>high_metadata[state])high_metadata[state]=a.tick_metadata_operations;
                    if(a.tick_hash_bytes>high_hash[state])high_hash[state]=a.tick_hash_bytes;
                    if(a.tick_copied_bytes>high_copy[state])high_copy[state]=a.tick_copied_bytes;
                    uint64_t elapsed=frame_ns();if(elapsed>high_ns[state])high_ns[state]=elapsed;}}
            for(unsigned state=0;state<7;++state){assert(seen[state]);printf("frame state=%u calls=%u bytes=%" PRIu64 " ops=%" PRIu64 " metadata=%" PRIu64 " hash=%" PRIu64 " copy=%" PRIu64 " host_ns=%" PRIu64 "\n",state,seen[state],high_bytes[state],high_ops[state],high_metadata[state],high_hash[state],high_copy[state],high_ns[state]);}
            printf("frame workload payload=%u journal_high=%" PRIu64 " memory=%" PRIu64 " open_bytes=%" PRIu64 " open_ops=%" PRIu64 " advisory_ops=%" PRIu64 " cpu_s=%.3f\n",160*16384,a.journal_high,a.allocation_high,a.open_bytes,a.open_operations,a.advisory_operations,(double)(clock()-start)/CLOCKS_PER_SEC);
        }
        for(unsigned frame=0;frame<1000;++frame){assert(stats(&a));if(a.state==ARC_RPC_HD_IDLE)break;run();assert(frame<999);}
        reset();assert(stats(&a)&&a.published);unload();assert(!stats(&a));deinit();vfs_assert_no_open_handles();
        assert(!errors);puts("RPC hard-disc plan: real adapter/store, guest roundtrip, reset/unload ownership pass");return 0;
    }
    if(!strcmp(mode,"refused")){
        assert(phase()==2&&errors==1);
        unsigned writes=tmp_writes,notice_baseline=notices;for(unsigned i=0;i<64;++i)run();
        assert(errors==1&&notices==notice_baseline&&tmp_writes==writes);
        unload();assert(errors==1);
        assert(load(&game));settle(run);assert(phase()==2&&errors==2);
        for(unsigned i=0;i<4;++i)run();
        unload();assert(errors==2);
        deinit();vfs_assert_no_open_handles();puts("refused store: one notice per loaded refusal, including unload");return 0;
    }
    if(!strcmp(mode,"cache")){assert(phase()==2&&!errors);}
    else if(!strcmp(mode,"record") || !strcmp(mode,"writer")){assert(phase()==2);}
    else {
        assert(phase()==1);
        bool (*pacing)(unsigned);void (*capture)(ArcRpcCapture *);
        LOAD(pacing,"arc_test_plan_cmos_pacing");LOAD(capture,"arc_rpc_test_capture_status");
        assert(!pacing(3));
        uint64_t previous=0;unsigned generation_frames=0;bool reset_done=false;
        for(unsigned i=0;phase()==1;++i){
            assert(i<100000);
            ArcRpcCapture current;capture(&current);
            if(current.state==ARC_RPC_CAPTURE_RUNNING && current.elapsed_ns){
                assert(current.delete_held);assert(!pacing(1));
                if(!strcmp(mode,"reset") && !reset_done){
                    reset();reset_done=true;previous=0;capture(&current);
                    assert(current.state!=ARC_RPC_CAPTURE_RUNNING); /* reset restarts recovery */
                }
            }
            run();snapshot(&s);capture(&current);
            if(current.state==ARC_RPC_CAPTURE_RUNNING){
                assert(s.rate_mips==expected_rate);
                assert(current.elapsed_ns>=previous);
                assert(current.elapsed_ns-previous<=UINT64_C(16666667)*8);
                previous=current.elapsed_ns;++generation_frames;
            }
        }
        if(!strcmp(mode,"negative") || !strcmp(mode,"terminalfallback")){
            assert(phase()==3&&errors&&!s.active && terminal_active);
            unsigned old=notices,progress=progress_messages,clears_before=cleared_messages;
            for(unsigned j=0;j<181;++j){run();assert(terminal_active);if(!extended_messages)assert(fallback_lifetime);}
            assert(phase()==3 && progress_messages==progress);
            if(extended_messages)assert(notices==old);else assert(notices>=old+3);
            reset();assert(!terminal_active && cleared_messages>clears_before && phase()==1);
            clears_before=cleared_messages;unload();assert(cleared_messages>clears_before && !terminal_active);
            deinit();vfs_assert_no_open_handles();return 0;
        }
        assert(phase()==2&&!errors&&generation_frames);snapshot(&s);
        assert(s.now_ns==0&&s.executed==0&&black_frames); /* clean requested session */
        if(!strcmp(mode,"reset"))assert(reset_done);
    }
    if(!strcmp(mode,"fallback"))assert(progress_messages>2 && fallback_messages>2);
    uint8_t before[256],after[256];cmos(before);reset();cmos(after);assert(!memcmp(before+16,after+16,240));
    if(!strcmp(mode,"writer")){
        assert(advance(1000000));cmos(before);unload();assert(load(&game));settle(run);assert(phase()==2);cmos(after);assert(!memcmp(before+16,after+16,240));
    }
    for(unsigned i=0;i<4;++i){
        cmos(before);unsigned publications=tmp_writes;run();cmos(after);
        assert(tmp_writes-publications==(!memcmp(before+16,after+16,240)?0u:1u));
    }
    cmos(before);unload();assert(phase()==0);assert(load(&game));settle(run);assert(phase()==2);cmos(after);assert(!memcmp(before+16,after+16,240));
    /* Guest work outside retro_run is persisted specifically by unload. */
    assert(advance(10000000));cmos(before);unload();assert(load(&game));settle(run);assert(phase()==2);cmos(after);assert(!memcmp(before+16,after+16,240));unload();
    /* Failed admission leaves persistence unarmed. */
    game.path="/missing.arcplan";assert(!load(&game)&&phase()==0);unload();deinit();vfs_assert_no_open_handles();
    puts("RPC plan CMOS: slices, clean restart, reset, eager/unload and failed start passed");
#ifdef _WIN32
    FreeLibrary(library);
#else
    assert(!dlclose(library));
#endif
    return 0;
}
