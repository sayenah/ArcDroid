/* Actual libretro pad arbitration and lifecycle, authored guests only.
 * Portable native Windows/host harness. GPL-2.0-only. */
#define main session_fixture_main
#include "rpc_session_test.c"
#undef main
#include "libretro.h"
#include "install_disc.h"
#include <stdarg.h>
#include <sys/stat.h>
#include <errno.h>
#ifdef _WIN32
#include <io.h>
#endif
#include "../tools/rpc_runner_vfs.h"
static const char *directory;
static unsigned pad_a,pad_b,polls,hints,manual_hints;
static struct retro_disk_control_callback disks;
static unsigned hd_identity_case;
static void log_line(enum retro_log_level level,const char *format,...)
{ if(level>=RETRO_LOG_WARN){va_list args;va_start(args,format);vfprintf(stderr,format,args);va_end(args);} }
static bool environment(unsigned command,void *data)
{
    switch(command){
    case RETRO_ENVIRONMENT_GET_VFS_INTERFACE:{struct retro_vfs_interface_info *request=data;
        if(request->required_interface_version>3)return false;request->iface=&vfs;return true;}
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *(const char **)data=directory;return true;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:((struct retro_log_callback *)data)->log=log_line;return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:return *(enum retro_pixel_format *)data==RETRO_PIXEL_FORMAT_XRGB8888;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:*(bool *)data=true;return true;
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:case RETRO_ENVIRONMENT_SET_GEOMETRY:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
    case RETRO_ENVIRONMENT_SET_VARIABLES:case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:return true;
    case RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE:disks=*(struct retro_disk_control_callback *)data;return true;
    case RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE:{
        const struct retro_disk_control_ext_callback *ext=data;
        disks=(struct retro_disk_control_callback){ext->set_eject_state,ext->get_eject_state,ext->get_image_index,
            ext->set_image_index,ext->get_num_images,ext->replace_image_index,ext->add_image_index};return true;}
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:*(unsigned *)data=0;return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:*(bool *)data=false;return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE:return false;
    case RETRO_ENVIRONMENT_SET_MESSAGE:{const char *message=((struct retro_message *)data)->msg;
        if(strstr(message,"error") || strstr(message,"Error")){++hints;if(strstr(message,"keyboard or mouse"))++manual_hints;}return true;}
    default:return false;
    }
}
static void video(const void *p,unsigned w,unsigned h,size_t pitch) { (void)p;(void)w;(void)h;(void)pitch; }
static size_t audio(const int16_t *p,size_t n) { (void)p;return n; }
static void poll_input(void) { ++polls; }
static int16_t input(unsigned port,unsigned device,unsigned index,unsigned id)
{
    (void)index;if(port || device!=RETRO_DEVICE_JOYPAD)return 0;
    return id==RETRO_DEVICE_ID_JOYPAD_A?(int16_t)pad_a:id==RETRO_DEVICE_ID_JOYPAD_B?(int16_t)pad_b:0;
}
static void put_word(unsigned off,uint32_t value)
{ for(unsigned i=0;i<4;++i)rom[off+i]=(uint8_t)(value>>(8*i)); }
static uint32_t branch(unsigned at,unsigned to,unsigned condition)
{ return (condition<<28)|0x0a000000u|(((to-at-8)/4)&0xffffffu); }
static void observer_guest(unsigned flags)
{
    rom_words(NULL,0);
    put_word(0,branch(0,0x100,14));put_word(8,branch(8,0x180,14));
    /* SVC stack; enter User32; user ReportError; hang after its return. */
    const uint32_t boot[]={0xe3a0d201,0xe321f010,0xe3a0d201,0xe3a00000,0xe3a01000|flags,0xef0400df,0xeafffffe};
    for(unsigned i=0;i<sizeof(boot)/4;++i)put_word(0x100+4*i,boot[i]);
    /* Authored SWI handler: a nested OS_Byte returns; ReportError preserves
     * its user LR/SPSR across the repeated key reads. */
    const uint32_t handler[]={0xe3500081,0x01b0f00e,0xe1a0800e,0xe14f9000,
        0xe3a00081,0xe3a01000,0xe3a02000,0xef000006,0xe351000d,0x0a000003,
        0xe351001b,0x0a000001,0xeafffff6,0xe1a00000,
        0xe3a07201,0xe5871000,0xe169f009,0xe1b0f008};
    for(unsigned i=0;i<sizeof(handler)/4;++i)put_word(0x180+4*i,handler[i]);
}
static void disc_guest(bool busy,bool modal)
{
    rom_words(NULL,0);put_word(0,branch(0,0x100,14));put_word(8,0xe1b0f00e);
    unsigned at=0x100;
#define EMIT(word) do {put_word(at,(word));at+=4;} while(0)
    EMIT(0xe59fa000|((0x300-at-8)&0xfff)); /* FDC register base */
    EMIT(0xe3a0003c);EMIT(0xe5ca0008); /* release reset */
    EMIT(0xe3a01ffa); /* MOV r1,1000 */
    unsigned loop=at;EMIT(0xe2511001);EMIT(branch(at,loop,1));
    if(!busy)for(unsigned d=0;d<4;++d){
        EMIT(0xe3a00008);EMIT(0xe5ca0014);EMIT(0xe5da0014);EMIT(0xe5da0014);
    }
    EMIT(0xe3a02201); /* medium name in authored RAM */
    EMIT(0xe59f0000|((0x304-at-8)&0xfff));EMIT(0xe5820000);
    EMIT(0xe3a00002);EMIT(0xe3a01008);EMIT(0xe3a03000);EMIT(0xef020033);
    if(modal){
        EMIT(0xe321f010);EMIT(0xe3a0d201);EMIT(0xe3a00000);EMIT(0xe3a01c01);EMIT(0xef0400df);
        put_word(8,branch(8,0x200,14));
        const uint32_t box_handler[]={0xe3500002,0x01b0f00e,0xe3500081,0x01b0f00e,0xe1a0800e,0xe14f9000,
            0xe3a00081,0xe3a01000,0xe3a02000,0xef000006,0xeafffffa};
        for(unsigned i=0;i<sizeof(box_handler)/4;++i)put_word(0x200+4*i,box_handler[i]);
    }
    EMIT(0xeafffffe);
    put_word(0x300,0x03010fc0);put_word(0x304,0x006f7754); /* authored Two */
#undef EMIT
}
static bool hd_identity_read(void *opaque,uint32_t lba,uint32_t count,uint8_t *out)
{
    (void)opaque;if(hd_identity_case==3 || count!=1)return false;
    if(!arc_hd_format_range(256,13,(uint64_t)lba*512,out,512))return false;
    const unsigned map_lba=((2u*(512u*8u-32u)-480u)<<13)/512u;
    unsigned offset=lba==6?0x1c0+22:26;
    if((lba==6 || lba==map_lba || lba==map_lba+5) && (hd_identity_case==1 || lba==6)){
        memset(out+offset,' ',10);memcpy(out+offset,"Two",3);
        if(lba==6){unsigned sum=0;for(unsigned i=0;i<511;++i)sum+=out[i];while(sum>255)sum=(sum&255)+(sum>>8);out[511]=(uint8_t)sum;}
    }
    return true;
}
static void write_file(const char *name,const void *bytes,size_t size)
{
    char path[2048];snprintf(path,sizeof(path),"%s/%s",directory,name);
    FILE *file=fopen(path,"wb");assert(file && fwrite(bytes,1,size,file)==size && !fclose(file));
}
int main(int argc,char **argv)
{
    assert(argc==4 || argc==5);directory=argv[2];bool sanitizer=argc==5 && !strcmp(argv[4],"sanitizer");
#ifdef _WIN32
    assert(CreateDirectoryA(directory,NULL) || GetLastError()==ERROR_ALREADY_EXISTS);
#else
    assert(!mkdir(directory,0700) || errno==EEXIST);
#endif
    assert(canonical(directory,save_directory));
    open_library(argv[1]);load_api();
    void (*set_env)(retro_environment_t),(*set_video)(retro_video_refresh_t),(*set_audio)(retro_audio_sample_batch_t),
        (*set_poll)(retro_input_poll_t),(*set_input)(retro_input_state_t),(*init)(void),(*deinit)(void),(*run)(void),(*unload)(void),(*reset)(void);
    bool (*load)(const struct retro_game_info *),(*pad_level)(unsigned,unsigned);
    LOAD(pad_level,"arc_libretro_test_pad_source_level");
    LOAD(set_env,"retro_set_environment");LOAD(set_video,"retro_set_video_refresh");LOAD(set_audio,"retro_set_audio_sample_batch");
    LOAD(set_poll,"retro_set_input_poll");LOAD(set_input,"retro_set_input_state");LOAD(init,"retro_init");LOAD(deinit,"retro_deinit");
    LOAD(run,"retro_run");LOAD(unload,"retro_unload_game");LOAD(load,"retro_load_game");LOAD(reset,"retro_reset");
    set_env(environment);set_video(video);set_audio(audio);set_poll(poll_input);set_input(input);init();
    rom=calloc(1,2097152);assert(rom);write_file("seed",seed,sizeof(seed));
    const char *models[]={"arm610","arm710","sa110","arm7500"};
    char path[2048],plan[4096];snprintf(path,sizeof(path),"%s/test.arcplan",directory);
    struct retro_game_info game={path,NULL,0,NULL};
    for(unsigned model=0;model<4;++model)for(unsigned answer=0;answer<2;++answer)for(unsigned flags=3;flags<=7;flags+=4){
        if(sanitizer && model!=2)continue;
        observer_guest(flags);write_file("guest.rom",rom,2097152);
        int count=snprintf(plan,sizeof(plan),"version=1\nmodel=%s\nfirmware=%s/guest.rom\ncmos=seed\nseed=%s/seed\nrtc_epoch=0\nobserver_cells=12\nobserver_forms=0xffffffff\n",models[model],directory,directory);
        write_file("test.arcplan",plan,(size_t)count);pad_a=1;pad_b=0;hints=manual_hints=0;assert(load(&game));
        unsigned before=polls;run();assert(polls==before+1);
        ArcErrorBox box;api->error_snapshot(&box);assert(box.recognised_open && box.answer_available && word(0)==0);
        run();assert(word(0)==0 && !pad_level(0,RETROK_SPACE)); /* held A cannot answer the newly opened generation */
        pad_a=0;run();pad_a=pad_b=1;run();assert(word(0)==0);
        pad_a=pad_b=0;run();pad_a=!answer;pad_b=answer;run();
        assert(word(0)==((answer==(flags==7))?13u:27u));
        api->error_snapshot(&box);assert(!box.recognised_open && !box.answer_available && hints && !manual_hints);
        run();assert(word(0)==((answer==(flags==7))?13u:27u) && !pad_level(0,RETROK_SPACE) && !pad_level(0,RETROK_RETURN));
        pad_a=pad_b=0;run();pad_a=1;run();assert(pad_level(0,RETROK_SPACE));unload();
    }
    /* Observation-only: truthful manual hint, no completion; reset/unload
     * retire eligibility even with A held. */
    observer_guest(3);write_file("guest.rom",rom,2097152);
    int count=snprintf(plan,sizeof(plan),"version=1\nmodel=sa110\nfirmware=%s/guest.rom\ncmos=seed\nseed=%s/seed\nrtc_epoch=0\nobserver_cells=4\nobserver_forms=0xffffffff\n",directory,directory);
    write_file("test.arcplan",plan,(size_t)count);pad_a=pad_b=0;hints=manual_hints=0;assert(load(&game));run();run();
    ArcErrorBox box;api->error_snapshot(&box);assert(box.recognised_open && !box.answer_available && manual_hints);
    pad_a=1;run();assert(word(0)==0 && pad_level(0,RETROK_SPACE));reset();api->error_snapshot(&box);assert(!box.answer_available);run();api->error_snapshot(&box);assert(box.recognised_open && !box.answer_available);unload();
    /* Contained fault during a recognised generation retires all observers. */
    observer_guest(3);write_file("guest.rom",rom,2097152);
    count=snprintf(plan,sizeof(plan),"version=1\nmodel=sa110\nfirmware=%s/guest.rom\ncmos=seed\nseed=%s/seed\nrtc_epoch=0\nobserver_cells=15\nobserver_forms=0xffffffff\nobserver_launch=Run Authored\nobserver_boot=launch\n",directory,directory);
    write_file("test.arcplan",plan,(size_t)count);pad_a=pad_b=0;assert(load(&game));run();api->error_snapshot(&box);assert(box.answer_available);
    fault_next();run();api->error_snapshot(&box);assert(box.modal_unknown && !box.answer_available);
    assert(api->boot_watch_state()==ARC_BOOT_WATCH_DISABLED);ArcLaunchOutcome launch;api->launch_outcome_snapshot(&launch);assert(launch.state==ARC_OUTCOME_OFF);
    unload();
    /* The real RPC presentation/prepare route resolves authored content,
     * preflights it and commits through Disk Control. Busy, hint-only,
     * helper collision, renamed/conflicting/unknown IDE identity and failed
     * preflight remain manual. Both drives are present in helper cases. */
    unsigned (*phase)(void);void (*preflight_fail)(void);bool (*attach_ide)(const ArcIdeBackend *);
    LOAD(phase,"arc_test_plan_cmos_phase");LOAD(preflight_fail,"arc_libretro_test_fail_disc_preflight");
    LOAD(attach_ide,"arc_engine_test_ide_backend");
    for(unsigned scenario=0;scenario<11;++scenario){
        disc_guest(scenario==1,scenario==8);write_file("guest.rom",rom,2097152);
        count=snprintf(plan,sizeof(plan),"version=2\nmodel=sa110\nfirmware=%s/guest.rom\ncmos=persist\nseed=%s/seed\ncmos_layout=ro350\ncontent_id=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\nrtc_epoch=0\nhard_disc=off\nmedia=%s/disc-request/Set.m3u\nobserver_cells=%u\n%s",
            directory,directory,argv[3],scenario==2?20:scenario==9?48:52,scenario==3?"drive1=HELPER\n":"");
        if(scenario==3){char helper[2048];snprintf(helper,sizeof(helper),"%s/disc-request/Two.adf",argv[3]);
            char *mark=strstr(plan,"HELPER");assert(mark);snprintf(mark,sizeof(plan)-(size_t)(mark-plan),"%s\n",helper);count=(int)strlen(plan);}
        write_file("test.arcplan",plan,(size_t)count);pad_a=pad_b=0;assert(load(&game));
        for(unsigned step=0;phase()!=2 && step<10000;++step)run();assert(phase()==2);
        if(scenario>=4 && scenario<=6){hd_identity_case=scenario-3;
            ArcIdeBackend ide={.read=hd_identity_read,.sectors=256u*16u*63u};assert(attach_ide(&ide));}
        if(scenario==7)preflight_fail();
        for(unsigned frame=0;frame<4;++frame)run();
        assert(disks.get_image_index()==(scenario==0 || scenario==10?1u:0u));
        if(scenario!=0 && scenario!=10){assert(disks.set_eject_state(true) && disks.set_image_index(1) && disks.set_eject_state(false));assert(disks.get_image_index()==1);}
        unload();vfs_assert_no_open_handles();
    }
    deinit();free(rom);vfs_assert_no_open_handles();
#ifdef _WIN32
    assert(FreeLibrary(library));
#else
    assert(!dlclose(library));
#endif
    puts("RPC frontend: four CPUs; pad A/B, defaults, held/release gates, hints, observation-only, reset, unload, faults and automatic/manual disc commits");return 0;
}
