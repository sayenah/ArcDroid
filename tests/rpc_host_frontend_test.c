/* Real retro_* host and shipping projection. Authored inputs. GPL-2.0-only. */
#define main legacy_cmos_fixture_main
#include "rpc_cmos_plan_test.c"
#undef main
#include "engine_test.h"
#include "rpc_preparation.h"
#include <stdarg.h>
#include <math.h>
static struct retro_disk_control_ext_callback disks;
static struct retro_keyboard_callback keyboard;
static unsigned videos,audios,polls,av_calls,controllers,descriptors,summary_lines,freeze_notices[4];
static uint64_t audio_frames,pixel_hash=1469598103934665603ULL,sound_hash=1469598103934665603ULL;
static bool projection_override;
static bool shipping,failed,changed,write_off,last_catalogue_message,matrix_mode,following_arculator,direct_start_compat;
static char terminal[512];
static unsigned privacy_ext,privacy_legacy,privacy_notice,privacy_ext_notice,privacy_legacy_notice;
static void hash(uint64_t *value,const void *data,size_t size)
{const uint8_t *p=data;for(size_t i=0;i<size;++i){*value^=p[i];*value*=1099511628211ULL;}}
static unsigned no_phase(void){return 0;}
static void capture_log(enum retro_log_level level,const char *format,...)
{
    char text[8192];va_list args;va_start(args,format);vsnprintf(text,sizeof(text),format,args);va_end(args);
    if(!shipping && !following_arculator)assert(!strstr(text,"HOST-PRIVATE-SENTINEL"));
    if(strstr(text,"RPC privacy probe: SET_MESSAGE_EXT accepted"))++privacy_ext;
    if(strstr(text,"RPC privacy probe: SET_MESSAGE accepted"))++privacy_legacy;
    if(strstr(text,"N-7: Checking the RiscPC hard disc"))++privacy_notice;
    if(!strncmp(text,"ArcDroid build:",15))return; /* G-3 is the sole host-slice exception. */
    if(shipping)printf("log:%u:%s",level,text);
    else{
        if(!strncmp(text,"summary:",8))++summary_lines;
        if(!strncmp(text,"N-11:",5)){
            static const char *labels[]={"Disk Writes","Write-protect Tab","Short image tail","JFD omitted sectors"};
            bool found=false;for(unsigned i=0;i<4;++i)if(strstr(text,labels[i])){++freeze_notices[i];found=true;}assert(found);
        }
    }
}
static bool host_environment(unsigned command,void *data)
{
    if(command==RETRO_ENVIRONMENT_GET_LOG_INTERFACE){((struct retro_log_callback *)data)->log=capture_log;return true;}
    if(command==RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE){disks=*(struct retro_disk_control_ext_callback *)data;return true;}
    if(command==RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK){keyboard=*(struct retro_keyboard_callback *)data;return true;}
    if(command==RETRO_ENVIRONMENT_SET_CONTROLLER_INFO){
        struct retro_controller_info *ports=data;++controllers;
        if(!shipping && !following_arculator)for(unsigned p=0;ports[p].types;++p)for(unsigned i=0;i<ports[p].num_types;++i){
            assert(strstr(ports[p].types[i].desc,"RiscPC"));assert(!strstr(ports[p].types[i].desc,"A3010"));
        }return true;
    }
    if(command==RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS){++descriptors;return true;}
    if(command==RETRO_ENVIRONMENT_GET_INPUT_DEVICE_CAPABILITIES){*(uint64_t *)data=UINT64_C(1)<<RETRO_DEVICE_KEYBOARD;return true;}
    if(command==RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE){*(bool *)data=changed;changed=false;return true;}
    if(command==RETRO_ENVIRONMENT_GET_VARIABLE){struct retro_variable *v=data;
        if(projection_override && !strcmp(v->key,"arcdroid_machine")){v->value="A3000";return true;}
        if(!strcmp(v->key,"arcdroid_disk_writes")){v->value=write_off?"off":"shadow";return true;}
        if(!strcmp(v->key,"arcdroid_jfd_write_protect")){v->value=write_off?"off":"as-imaged";return true;}
        if(!strcmp(v->key,"arcdroid_short_tail")){v->value=write_off?"zero-filled":"missing";return true;}
        if(!strcmp(v->key,"arcdroid_jfd_omitted")){v->value=write_off?"strict":"file-data";return true;}}
    if(command==RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO || command==RETRO_ENVIRONMENT_SET_GEOMETRY){++av_calls;return true;}
    if(command==RETRO_ENVIRONMENT_SET_MESSAGE_EXT){struct retro_message_ext *m=data;
        if(!strcmp(m->msg,"Checking the RiscPC hard disc"))++privacy_ext_notice;
        if(!shipping && !following_arculator)assert(!strstr(m->msg,"HOST-PRIVATE-SENTINEL"));
        last_catalogue_message=m->type==RETRO_MESSAGE_TYPE_STATUS;
        if(!shipping && !following_arculator && !direct_start_compat && last_catalogue_message){assert(m->target==RETRO_MESSAGE_TARGET_OSD);assert(!strstr(m->msg,"HOST-PRIVATE-SENTINEL"));
            if(!*m->msg)assert(m->priority>=3);
            if(m->level==RETRO_LOG_ERROR){failed=true;snprintf(terminal,sizeof(terminal),"%s",m->msg);}
        }else if(shipping)printf("message:%u:%u:%u:%u:%s\n",m->level,m->priority,m->duration,m->target,m->msg);
        return extended_messages;
    }
    if(command==RETRO_ENVIRONMENT_SET_MESSAGE){struct retro_message *m=data;
        if(!strcmp(m->msg,"Checking the RiscPC hard disc"))++privacy_legacy_notice;
        if(!shipping && !following_arculator)assert(!strstr(m->msg,"HOST-PRIVATE-SENTINEL"));
        else if(shipping)printf("fallback:%u:%s\n",m->frames,m->msg);return true;
    }
    return environment(command,data);
}
static void host_video(const void *data,unsigned w,unsigned h,size_t pitch)
{
    ++videos;
    if(!shipping && !following_arculator && !direct_start_compat && phase()!=2){assert(data);if(matrix_mode){assert(!*(const uint32_t *)data && !*(const uint32_t *)((const uint8_t *)data+(h-1)*pitch+(w-1)*4));return;}for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)
        assert(!((const uint32_t *)((const uint8_t *)data+y*pitch))[x]);}
    hash(&pixel_hash,&w,sizeof(w));hash(&pixel_hash,&h,sizeof(h));
    if(data)for(unsigned y=0;y<h;++y)hash(&pixel_hash,(const uint8_t *)data+y*pitch,w*4);
}
static size_t host_audio(const int16_t *data,size_t frames)
{
    ++audios;audio_frames+=frames;
    if(!shipping && !following_arculator && !direct_start_compat && phase()!=2)for(size_t i=0;i<frames*2;++i)assert(!data[i]);
    hash(&sound_hash,data,frames*4);return frames;
}
static void host_poll(void){++polls;}
int main(int argc,char **argv)
{
    assert(argc==6);matrix_mode=!strncmp(argv[5],"matrix:",7);shipping=!strncmp(argv[5],"shipping",8);projection_override=strstr(argv[5],"override")!=NULL;extended_messages=strstr(argv[5],"fallback")==NULL;system_directory=argv[3];assert(canonical(argv[4],save_directory));
#ifdef _WIN32
    library=LoadLibraryA(argv[1]);
#else
    library=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
#endif
    assert(library);
    void (*set_environment)(retro_environment_t),(*init)(void),(*deinit)(void),(*run)(void),(*reset)(void),(*unload)(void);
    void (*set_video)(retro_video_refresh_t),(*set_audio)(retro_audio_sample_batch_t),(*set_poll)(retro_input_poll_t),(*set_input)(retro_input_state_t);
    bool (*load)(const struct retro_game_info *);void (*get_av)(struct retro_system_av_info *);
    LOAD(get_av,"retro_get_system_av_info");LOAD(set_environment,"retro_set_environment");LOAD(init,"retro_init");LOAD(deinit,"retro_deinit");LOAD(run,"retro_run");LOAD(reset,"retro_reset");LOAD(unload,"retro_unload_game");LOAD(load,"retro_load_game");
    LOAD(set_video,"retro_set_video_refresh");LOAD(set_audio,"retro_set_audio_sample_batch");LOAD(set_poll,"retro_set_input_poll");LOAD(set_input,"retro_set_input_state");
    phase=no_phase;if(!shipping){LOAD(phase,"arc_test_plan_cmos_phase");}
    set_environment(host_environment);init();set_video(host_video);set_audio(host_audio);set_poll(host_poll);set_input(input);
    struct retro_game_info game={.path=argv[2]};assert(load(&game));
    if(!strncmp(argv[5],"message-",8)){
        for(unsigned i=0;i<65;++i)run();
        bool legacy=!strcmp(argv[5],"message-legacy");
        assert(legacy?(privacy_legacy==1 && !privacy_ext):(privacy_ext==1 && !privacy_legacy));
        assert(privacy_notice && (legacy?(privacy_legacy_notice && !privacy_ext_notice):
            (privacy_ext_notice && !privacy_legacy_notice)));
        unload();deinit();vfs_assert_no_open_handles();
        printf("plan message interface: %s delivered through frontend\n",legacy?"legacy":"extended");return 0;
    }
    if(!strcmp(argv[5],"stage-error")){
        bool (*stage_error)(const char *,char *,size_t);LOAD(stage_error,"arc_test_host_stage_error");
        char path[4096],error[512];snprintf(path,sizeof(path),"%s/HOST-PRIVATE-SENTINEL-invalid.zip",system_directory);
        assert(!stage_error(path,error,sizeof(error)));
        assert(!strcmp(error,"The discs in this content could not be loaded. Check that the file is complete and a supported type."));
        unload();deinit();vfs_assert_no_open_handles();return 0;
    }
    if(matrix_mode){
        unsigned target=0,target_substep=0;char action[16]={0};assert(sscanf(argv[5],"matrix:%u:%u:%15s",&target,&target_substep,action)==3);
        void (*position)(unsigned *,unsigned *),(*fingerprint)(uint8_t *),(*attempt_fingerprint)(uint8_t *);
        LOAD(position,"arc_test_host_position");LOAD(fingerprint,"arc_test_host_pending_fingerprint");LOAD(attempt_fingerprint,"arc_test_host_attempt_fingerprint");
        unsigned state=0,substep=0;position(&state,&substep);
        for(unsigned frame=0;state!=target||(target_substep!=UINT_MAX && substep!=target_substep);++frame){
            assert(frame<20000 && (state<ARC_RPC_PREP_RUNNING || target==ARC_RPC_PREP_FAILED));
            unsigned v=videos,a=audios;uint64_t samples=audio_frames;
            run();position(&state,&substep);assert(videos==v+1 && audios==a+1 && audio_frames==samples+800);
        }
        if(target!=ARC_RPC_PREP_RUNNING){
            uint8_t before[32],after[32],attempt_before[32],attempt_after[32];fingerprint(before);attempt_fingerprint(attempt_before);
            assert(!disks.set_eject_state(true)&&!disks.set_image_index(0)&&!disks.replace_image_index(0,NULL)&&!disks.add_image_index());
            keyboard.callback(true,RETROK_SPACE,0,0);fingerprint(after);attempt_fingerprint(attempt_after);
            assert(!memcmp(before,after,32) && !memcmp(attempt_before,attempt_after,32));
        }
        if(!strcmp(action,"reset")){
            bool changed_backing=target==ARC_RPC_PREP_GENERATE || target==ARC_RPC_PREP_START;
            if(changed_backing)write_off=true;
            reset();position(&state,&substep);assert(state==(target==ARC_RPC_PREP_RUNNING?ARC_RPC_PREP_RUNNING:ARC_RPC_PREP_VERIFY));
            if(changed_backing){
                bool (*commit_jfd)(unsigned,size_t,uint8_t);LOAD(commit_jfd,"arc_libretro_test_jfd_commit");
                assert(!commit_jfd(0,1,0x55));settle(run);assert(phase()==2 && !commit_jfd(0,1,0x56));run();
            }
            unload();
        }
        else if(!strcmp(action,"unload"))unload();else{assert(!strcmp(action,"deinit"));deinit();set_environment(host_environment);init();set_video(host_video);set_audio(host_audio);set_poll(host_poll);set_input(input);}
        following_arculator=true;char ordinary[4096];snprintf(ordinary,sizeof(ordinary),"%s/ordinary.adf",system_directory);
        struct retro_game_info arculator={.path=ordinary};assert(load(&arculator));run();unload();deinit();vfs_assert_no_open_handles();
        assert(summary_lines==2);return 0;
    }
    if(shipping){for(unsigned i=0;i<8;++i)run();reset();run();}
    else{
        assert(controllers && descriptors && keyboard.callback && disks.set_eject_state);
        const char *(*staged)(unsigned)=NULL;bool (*commit_jfd)(unsigned,size_t,uint8_t)=NULL;char original_staged[4096]={0};
        bool freezing=!strncmp(argv[5],"freeze",6);
        if(freezing){
            LOAD(commit_jfd,"arc_libretro_test_jfd_commit");assert(commit_jfd(0,1,0x51));
            LOAD(staged,"arc_libretro_test_staged_path");assert(staged(0));snprintf(original_staged,sizeof(original_staged),"%s",staged(0));
            write_off=changed=true;run();assert(!strcmp(original_staged,staged(0)));
            changed=true;run();assert(!strcmp(original_staged,staged(0)));
            for(unsigned i=0;i<4;++i)assert(freeze_notices[i]==1);
            assert(commit_jfd(0,1,0x52)); /* Pending options remain frozen. */
        }
        for(unsigned i=0;i<8 && phase()==1;++i){unsigned v=videos,a=audios;uint64_t samples=audio_frames;
            assert(!disks.set_eject_state(true)&&!disks.set_image_index(0)&&!disks.replace_image_index(0,NULL)&&!disks.add_image_index());
            keyboard.callback(true,RETROK_SPACE,0,0);run();
            assert(videos==v+1 && audios==a+1 && audio_frames==samples+800 && !av_calls && !polls);
        }
        reset();assert(phase()==1);
        if(freezing)assert(!commit_jfd(0,1,0x53));
        settle(run);assert(phase()==2);
        for(unsigned i=0;i<8;++i)run();assert(polls);
        if(!strcmp(argv[5],"slow")){
            struct retro_system_av_info slow;get_av(&slow);
            for(unsigned i=0;fabs(slow.timing.fps-3.662109375)>0.00001;++i){assert(i<1000);run();get_av(&slow);}
        }
        if(freezing){
            write_off=false;changed=true;run();
            assert(!commit_jfd(0,1,0x54));reset();assert(phase()==2 && commit_jfd(0,1,0x55));
            unsigned count=disks.get_num_images();assert(disks.add_image_index() && disks.get_num_images()==count+1);
            char replacement[4096];snprintf(replacement,sizeof(replacement),"%s/replacement.adf",system_directory);
            struct retro_game_info replacement_game={.path=replacement};
            assert(disks.set_eject_state(true) && disks.replace_image_index(count,&replacement_game));
            assert(disks.set_image_index(count) && disks.set_eject_state(false));
            assert(disks.set_eject_state(true) && disks.replace_image_index(count,&replacement_game) && disks.set_eject_state(false));
        }
        if(!strcmp(argv[5],"mount-fail")){
            void (*fail_mount)(unsigned);LOAD(fail_mount,"arc_engine_test_fail_rpc_mount");fail_mount(1);reset();
        }else if(!strcmp(argv[5],"helper-fail")){
            void (*fail_helper)(void);LOAD(fail_helper,"arc_riscpc_test_fail_helper");fail_helper();reset();
        }else{void (*fault)(void);LOAD(fault,"arc_rpc_test_fault_next");fault();run();}
        assert(phase()==3 && failed);
        unsigned prior_av=av_calls;uint64_t initial_samples=audio_frames;
        struct retro_system_av_info av;get_av(&av);
        if(!strncmp(argv[5],"rate",4))assert(fabs(av.timing.fps-atof(argv[5]+4))<0.01);
        for(unsigned i=0;i<65;++i){unsigned v=videos,a=audios;run();assert(videos==v+1 && audios==a+1);}
        assert(av_calls==prior_av && strstr(terminal,"RiscPC settings were saved"));
        double expected=65*48000.0/av.timing.fps;
        assert(fabs((double)(audio_frames-initial_samples)-expected)<=1.0);
        reset();assert(phase()==1);
        if(fabs(av.timing.fps-60.0)>0.01)assert(av_calls==prior_av+1);
        settle(run);assert(phase()==2);
    }
    unload();
    if(!shipping){
        direct_start_compat=true;char direct[4096];snprintf(direct,sizeof(direct),"%s/direct.arcplan",system_directory);
        struct retro_game_info direct_game={.path=direct};assert(load(&direct_game) && phase()==0);
        void (*evidence)(uint8_t *,uint8_t *,uint64_t *,uint64_t *,uint64_t *,unsigned *);
        void (*hygiene)(ArcFrontendHygiene *);LOAD(evidence,"arc_engine_test_cmos_evidence");LOAD(hygiene,"arc_libretro_test_hygiene");
        ArcFrontendHygiene before,after;hygiene(&before);assert(before.engine_rpc);
        for(unsigned i=0;i<3;++i)run();uint8_t bytes[256],mask[32],saved[256];uint64_t writes,last,now;unsigned state;
        evidence(bytes,mask,&writes,&last,&now,&state);assert(now);memcpy(saved,bytes,256);
        reset();hygiene(&after);assert(after.engine_rpc && after.id==before.id);
        evidence(bytes,mask,&writes,&last,&now,&state);assert(!now && !memcmp(bytes+16,saved+16,240));
        run();unload();assert(summary_lines==2);
    }
    deinit();vfs_assert_no_open_handles();
    if(!shipping){assert(summary_lines==2);if(!strncmp(argv[5],"freeze",6))for(unsigned i=0;i<4;++i)assert(freeze_notices[i]==1);}
    printf("projection: videos=%u audios=%u samples=%llu pixels=%016llx sound=%016llx av=%u controllers=%u descriptors=%u\n",videos,audios,(unsigned long long)audio_frames,(unsigned long long)pixel_hash,(unsigned long long)sound_hash,av_calls,controllers,descriptors);
    return 0;
}
