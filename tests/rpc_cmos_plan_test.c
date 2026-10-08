/* Non-shipping, real frontend lifecycle and bounded capture harness. GPL-2.0-only. */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#endif
#include "libretro.h"
#include "rpc_session.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
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
    case RETRO_ENVIRONMENT_SET_MESSAGE_EXT:{struct retro_message_ext *m=data;++notices;if(m->level==RETRO_LOG_ERROR)++errors;return true;}
    default:return false;
    }
}
static void video(const void *p,unsigned w,unsigned h,size_t pitch)
{
    if(phase() && phase()!=2){assert(p&&w&&h);const uint32_t *first=p,*last=(const uint32_t *)((const uint8_t *)p+(h-1)*pitch);assert(!first[0]&&!last[w-1]);++black_frames;}
}
static size_t audio(const int16_t *p,size_t n){
    if(phase()==1 || phase()==3){assert(n==800);for(size_t i=0;i<n*2;++i)assert(!p[i]);}
    return n;
}
static void poll(void){}
static int16_t input(unsigned p,unsigned d,unsigned i,unsigned id){(void)p;(void)d;(void)i;(void)id;return 0;}
#define LOAD(v,name) do {void *p=symbol(name);assert(p);memcpy(&(v),&p,sizeof(v));}while(0)
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
    const char *mode=argv[5];
    if(!strcmp(mode,"nodelete")){
        LOAD(rpc_start,"arc_rpc_test_start");LOAD(payload_evidence,"arc_engine_test_cmos_evidence");
        char rom_path[4096];snprintf(rom_path,sizeof(rom_path),"%s/full.rom",system_directory);FILE *f=fopen(rom_path,"rb");assert(f);
        uint8_t *rom=malloc(2097152);assert(rom&&fread(rom,1,2097152,f)==2097152);fclose(f);uint8_t seed[256]={0};char error[256];
        assert(rpc_start((ArcRpcModel)atoi(argv[7]),rom,2097152,seed,946684800,error,sizeof(error)));free(rom);
        uint64_t remaining=UINT64_C(30000000000);while(remaining){uint64_t ns=remaining<16666667?remaining:16666667;assert(advance(ns));remaining-=ns;}
        uint8_t bytes[256],mask[32];uint64_t writes,last,now;unsigned state;payload_evidence(bytes,mask,&writes,&last,&now,&state);
        assert(!writes&&!last&&now==UINT64_C(30000000000));for(unsigned i=0;i<32;++i)assert(!mask[i]);unload();deinit();return 0;
    }
    struct retro_game_info game={argv[2],NULL,0,NULL};
    if(!strcmp(mode,"cross")){
        char arc_path[4096];snprintf(arc_path,sizeof(arc_path),"%s/arc.adf",system_directory);
        struct retro_game_info arc_game={arc_path,NULL,0,NULL};bool (*arc_write)(unsigned,uint8_t);bool (*arc_image)(uint8_t *);
        LOAD(arc_write,"arc_engine_test_cmos_write");LOAD(arc_image,"arc_engine_test_cmos_image");
        audit_mode=2;assert(load(&arc_game));assert(arc_write(100,0x22));run();
        audit_mode=0;assert(load(&game)&&phase()==2); /* existing Arc eager persistence survives the switch */
        audit_mode=1;run();unload();assert(rpc_accesses);
        audit_mode=2;assert(load(&arc_game));uint8_t live[256];assert(arc_image(live));assert(live[100]==0x22);
        run();unload();assert(arc_accesses);deinit();puts("Arculator/RPC/Arculator persistence and audited path isolation passed");return 0;
    }
    audit_mode=1;bool accepted=load(&game);
unsigned expected_rate=(unsigned)atoi(argv[6]);
    if(!strcmp(mode,"reject")){assert(!accepted&&errors);unload();deinit();return 0;}
    assert(accepted);ArcRpcTestSnapshot s;snapshot(&s);assert(s.now_ns==0);
    if(!strcmp(mode,"cache")){assert(phase()==2&&notices==0);}
    else if(!strcmp(mode,"record") || !strcmp(mode,"writer")){assert(phase()==2);}
    else {
        assert(phase()==1&&notices==1);assert(s.rate_mips==expected_rate);
        for(unsigned i=0;i<1800;++i){
            if(i==20 && !strcmp(mode,"reset"))reset();
            run();snapshot(&s);
            if(phase()!=1)break;
            assert(s.now_ns==(uint64_t)(i+1)*16666667);
        }
        if(!strcmp(mode,"negative") || !strcmp(mode,"reset")){assert(phase()==3&&errors&&!s.active);run();assert(phase()==3);unload();deinit();puts("negative oracle/reset: no requested session");return 0;}
        assert(phase()==2&&!errors);snapshot(&s);assert(s.now_ns==0&&s.executed==0&&black_frames==1799); /* clean requested session */
    }
    uint8_t before[256],after[256];cmos(before);reset();cmos(after);assert(!memcmp(before+16,after+16,240));
    if(!strcmp(mode,"writer")){
        assert(advance(1000000));cmos(before);unload();assert(load(&game)&&phase()==2);cmos(after);assert(!memcmp(before+16,after+16,240));
    }
    for(unsigned i=0;i<4;++i){
        cmos(before);unsigned publications=tmp_writes;run();cmos(after);
        assert(tmp_writes-publications==(!memcmp(before+16,after+16,240)?0u:1u));
    }
    cmos(before);unload();assert(phase()==0);assert(load(&game)&&phase()==2);cmos(after);assert(!memcmp(before+16,after+16,240));
    /* Guest work outside retro_run is persisted specifically by unload. */
    assert(advance(10000000));cmos(before);unload();assert(load(&game)&&phase()==2);cmos(after);assert(!memcmp(before+16,after+16,240));unload();
    /* Failed admission leaves persistence unarmed. */
    game.path="/missing.arcplan";assert(!load(&game)&&phase()==0);unload();deinit();
    puts("RPC plan CMOS: slices, clean restart, reset, eager/unload and failed start passed");
#ifdef _WIN32
    FreeLibrary(library);
#else
    assert(!dlclose(library));
#endif
    return 0;
}
