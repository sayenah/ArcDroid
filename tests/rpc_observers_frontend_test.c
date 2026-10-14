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
static unsigned vfs_reads;
#include "../tools/rpc_runner_vfs.h"
static int64_t counted_read(struct retro_vfs_file_handle *handle,void *bytes,uint64_t size)
{ ++vfs_reads;return vfs_read(handle,bytes,size); }
static const char *directory;
static unsigned pad_a,pad_b,polls,hints,manual_hints;
static unsigned disc_logs,busy_logs,resolved_logs;static bool saw_device_refusal;static bool saw_disc_insert,saw_identity_unknown;
static struct retro_disk_control_callback disks;
static unsigned hd_identity_case,hd_identity_reads,hd_identity_writes;
static bool hd_record_store;
static uint8_t identity_records[3][512];
static void (*override_cells)(unsigned,uint32_t,uint32_t,uint32_t);
static bool (*identity_query)(char *,char *);
static bool identity_write_fail;
static void block_replacement(void) { override_cells(20,0,0,0); }
static void log_line(enum retro_log_level level,const char *format,...)
{ va_list args;va_start(args,format);char line[512];vsnprintf(line,sizeof(line),format,args);va_end(args);
  if(strstr(line,"RPC disc:")){++disc_logs;if(strstr(line,"action=busy"))++busy_logs;if(strstr(line,"action=resolved"))++resolved_logs;if(strstr(line,"gate=device"))saw_device_refusal=true;if(strstr(line,"action=inserted"))saw_disc_insert=true;if(strstr(line,"identity-unknown"))saw_identity_unknown=true;}
  if(level>=RETRO_LOG_WARN)fputs(line,stderr); }
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
static void disc_guest(bool busy,bool modal,uint32_t rename_lba,bool rename,bool privileged_box,uint32_t device)
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
    if(rename_lba){
        /* Transfer authored FileCore records through guest ATA PIO. */
        EMIT(0xe59f9000|((0x308-at-8)&0xfff));
        unsigned first=rename_lba==UINT32_MAX?0:rename_lba==6?0:rename_lba==122368?1:2;
        unsigned copies=rename_lba==UINT32_MAX?3:1;
        for(unsigned i=first;i<first+copies;++i){
            EMIT(0xe59f1000|((0x310+4*i-at-8)&0xfff));
            EMIT(0xe3a04a01); /* ROM record base 0x1000 */
            if(i)EMIT(0xe2844c00|(2*i)); /* +512*i */
            EMIT(branch(at,0x400,14)|0x01000000); /* BL write record */
        }
    }
    EMIT(0xe3a02201); /* medium name in authored RAM */
    EMIT(0xe59f0000|((0x304-at-8)&0xfff));EMIT(0xe5820000);
    EMIT(0xe3a00002);EMIT(0xe3a01008);EMIT(device==UINT32_MAX?0xe3e03000:0xe3a03000|device);EMIT(0xef020033);
    if(privileged_box){EMIT(0xe3a01033);unsigned box_loop=at;EMIT(0xef0400df);EMIT(branch(at,box_loop,14));}
    if(modal){
        EMIT(0xe321f010);EMIT(0xe3a0d201);EMIT(0xe3a00000);EMIT(0xe3a01c01);EMIT(0xef0400df);
        put_word(8,branch(8,0x200,14));
        const uint32_t box_handler[]={0xe3500002,0x01b0f00e,0xe3500081,0x01b0f00e,0xe1a0800e,0xe14f9000,
            0xe3a00081,0xe3a01000,0xe3a02000,0xef000006,0xeafffffa};
        for(unsigned i=0;i<sizeof(box_handler)/4;++i)put_word(0x200+4*i,box_handler[i]);
    }
    EMIT(0xeafffffe);
    put_word(0x300,0x03010fc0);put_word(0x304,0x006f7754);put_word(0x308,0x030107c0); /* authored Two */
    const uint32_t map_lba=((2u*(512u*8u-32u)-480u)<<13)/512u;
    const uint32_t lbas[]={6,map_lba,map_lba+5};
    for(unsigned i=0;i<3;++i){
        put_word(0x310+4*i,lbas[i]);uint8_t *record=rom+0x1000+512*i;
        assert(arc_hd_format_range(256,13,(uint64_t)lbas[i]*512,record,512));
        if(rename){memset(record+(i?26:0x1c0+22),' ',10);memcpy(record+(i?26:0x1c0+22),"Two",3);}
        if(!i){unsigned sum=0;for(unsigned j=0;j<511;++j)sum+=record[j];while(sum>255)sum=(sum&255)+(sum>>8);record[511]=(uint8_t)sum;}
    }
    /* r1=LBA, r4=source, r9=ATA base; preserve LR until PIO completes. */
    const uint32_t writer[]={
        0xe3a000e0,0xe5c90018,0xe1a00821,0xe5c90014,0xe1a00421,0xe5c90010,
        0xe5c9100c,0xe3a00001,0xe5c90008,0xe3a00030,0xe5c9001c,0xe3a02c02,
        0xe5d9001c,0xe3100008,0x0afffffc,0xe4d40001,0xe5c90000,0xe2522001,0x1afffff8,
        0xe5d9001c,0xe3100080,0x1afffffc,0xe1a0f00e};
    for(unsigned i=0;i<sizeof(writer)/4;++i)put_word(0x400+4*i,writer[i]);
#undef EMIT
}
static bool hd_identity_read(void *opaque,uint32_t lba,uint32_t count,uint8_t *out)
{
    (void)opaque;++hd_identity_reads;if(hd_identity_case==3 || count!=1)return false;
    const uint32_t record_lbas[]={6,122368,122373};
    if(hd_record_store){for(unsigned i=0;i<3;++i)if(lba==record_lbas[i]){memcpy(out,identity_records[i],512);return true;}return false;}
    if(!arc_hd_format_range(256,13,(uint64_t)lba*512,out,512))return false;
    const unsigned map_lba=((2u*(512u*8u-32u)-480u)<<13)/512u;
    unsigned offset=lba==6?0x1c0+22:26;
    if((lba==6 || lba==map_lba || lba==map_lba+5) && (hd_identity_case==1 || (hd_identity_case==2 && lba==6))){
        memset(out+offset,' ',10);memcpy(out+offset,"Two",3);
        if(lba==6){unsigned sum=0;for(unsigned i=0;i<511;++i)sum+=out[i];while(sum>255)sum=(sum&255)+(sum>>8);out[511]=(uint8_t)sum;}
    }
    return true;
}
static bool hd_identity_write(void *opaque,uint32_t lba,uint32_t count,const uint8_t *bytes)
{
    (void)opaque;assert(count==1);
    char helper[11],hd[11];assert(!identity_query(helper,hd)); /* Unknown before backend mutation. */
    const uint32_t map_lba=((2u*(512u*8u-32u)-480u)<<13)/512u;
    assert(lba==6 || lba==map_lba || lba==map_lba+5);
    const uint32_t lbas[]={6,map_lba,map_lba+5};
    for(unsigned i=0;i<3;++i)if(lba==lbas[i])memcpy(identity_records[i],bytes,512);
    ++hd_identity_writes;return !identity_write_fail;
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
    void (*binding)(unsigned,unsigned *,unsigned *);
    LOAD(binding,"arc_engine_test_observers_binding");LOAD(override_cells,"arc_engine_test_observers_override");
    LOAD(identity_query,"arc_engine_test_observers_identities");
    vfs.read=counted_read;
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
        unsigned catalogue,cpu;binding(1,&catalogue,&cpu);
        assert(catalogue==0 && cpu==model); /* Authored image cannot inherit catalogue 1. */
        override_cells(12,0,0,UINT32_MAX);
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
    /* Drawing spans several frames before the first qualified poll. A hint
     * follows eligibility changes once within the same generation. */
    observer_guest(3);
    const uint32_t delay[]={0xe3a06701,0xe2566001,0x1afffffd}; /* authored draw delay */
    memmove(rom+0x19c,rom+0x190,14*4);
    for(unsigned i=0;i<3;++i)put_word(0x190+4*i,delay[i]);
    write_file("guest.rom",rom,2097152);
    int delayed_count=snprintf(plan,sizeof(plan),"version=1\nmodel=arm610\nfirmware=%s/guest.rom\ncmos=seed\nseed=%s/seed\nrtc_epoch=0\nobserver_cells=12\nobserver_forms=8\n",directory,directory);
    write_file("test.arcplan",plan,(size_t)delayed_count);pad_a=pad_b=0;hints=manual_hints=0;assert(load(&game));
    run();ArcErrorBox delayed;api->error_snapshot(&delayed);assert(delayed.recognised_open && !delayed.answer_available);
    uint64_t delayed_generation=delayed.generation;unsigned drawing_frames=0;
    do {run();api->error_snapshot(&delayed);++drawing_frames;assert(drawing_frames<60);
        assert(delayed.generation==delayed_generation);if(!delayed.answer_available)assert(hints==1 && manual_hints==1);
    } while(!delayed.answer_available);
    assert(drawing_frames>=3);run();assert(hints==2 && manual_hints==1);
    run();assert(hints==2);assert(api->error_request(ARC_ERROR_OK));
    api->error_snapshot(&delayed);assert(delayed.recognised_open && !delayed.answer_available);
    run();assert(hints==3 && manual_hints==2 && word(0)==13);run();assert(hints==3);unload();
    /* A later privileged nested report retires eligibility into unknown
     * modal state. Retire the pad hint even though recognised-open is gone. */
    observer_guest(3);put_word(0x10c,0xe3a06902); /* authored countdown */
    put_word(0x1b0,branch(0x1b0,0x200,14));
    put_word(0x200,0xe2566001);put_word(0x204,branch(0x204,0x190,1));
    put_word(0x208,0xe3a01033);put_word(0x20c,0xef0400df);put_word(0x210,branch(0x210,0x190,14));
    write_file("guest.rom",rom,2097152);hints=manual_hints=0;assert(load(&game));
    run();api->error_snapshot(&delayed);assert(delayed.answer_available);delayed_generation=delayed.generation;
    unsigned retired_frames=0;
    do {run();api->error_snapshot(&delayed);assert(++retired_frames<60);}while(!delayed.modal_unknown);
    assert(!delayed.recognised_open && !delayed.answer_available && delayed.generation==delayed_generation);
    run();assert(hints==2 && manual_hints==1 && word(0)==0);run();assert(hints==2);unload();
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
    void (*after_preflight)(void (*)(void),bool),(*hold_reset)(void);
    bool (*mount_helper)(unsigned,const ArcEngineMedia *,char *,size_t);
    LOAD(mount_helper,"arc_engine_test_floppy_mount");
    LOAD(after_preflight,"arc_libretro_test_disc_after_preflight");LOAD(hold_reset,"arc_rpc_test_fdc_hold_reset");
    for(unsigned scenario=0;scenario<38;++scenario){
        bool long_log_case=(scenario>=25 && scenario<=27) || scenario==37;
        bool auto_insert=scenario==0 || scenario==10 || (scenario>=19 && scenario<=21) || scenario==23 || scenario==24 || scenario==31 || scenario==34;
        disc_logs=busy_logs=resolved_logs=0;saw_disc_insert=saw_identity_unknown=saw_device_refusal=false;
        const uint32_t map_lba=((2u*(512u*8u-32u)-480u)<<13)/512u;
        uint32_t rename_lba=scenario==12?6:scenario==13?map_lba:scenario==14?map_lba+5:scenario==18?UINT32_MAX:scenario==19 || scenario==22?6:scenario==20?map_lba:scenario==21?map_lba+5:0;
        uint32_t device=scenario==28 || scenario==31?0:scenario==29 || scenario==35?1:scenario==30 || scenario==36?4:UINT32_MAX;
        disc_guest(scenario==1 || scenario==27,scenario==8,rename_lba,scenario<19,scenario==24,device);
        write_file("guest.rom",rom,2097152);
        count=snprintf(plan,sizeof(plan),"version=2\nmodel=sa110\nfirmware=%s/guest.rom\ncmos=persist\nseed=%s/seed\ncmos_layout=ro350\ncontent_id=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\nrtc_epoch=0\nhard_disc=off\nmedia=%s/disc-request/Set.m3u\nobserver_cells=%u\n%s",
            directory,directory,argv[3],(scenario==2 || scenario==11)?20:scenario==9?48:52,(scenario==3 || scenario==11)?"drive1=HELPER\n":"");
        if(scenario==23 || scenario==24){
            count=snprintf(plan,sizeof(plan),"version=2\nmodel=sa110\nfirmware=%s/guest.rom\ncmos=persist\nseed=%s/seed\ncmos_layout=ro370\ncontent_id=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\nrtc_epoch=0\nhard_disc=on\nhd_populate=on\nmedia=%s/disc-request/Set.m3u\nobserver_cells=52\n",directory,directory,argv[3]);
        }
        if(scenario==3 || scenario==11){char helper[2048];snprintf(helper,sizeof(helper),"%s/disc-request/%s.adf",argv[3],scenario==3?"Two":"One");
            char *mark=strstr(plan,"HELPER");assert(mark);snprintf(mark,sizeof(plan)-(size_t)(mark-plan),"%s\n",helper);count=(int)strlen(plan);}
        if(scenario>=25){
            unsigned cells=scenario==25 || scenario==26?0:scenario==37?20:52;
            unsigned forms=scenario==31 || scenario==32?1:scenario==33?0:scenario>=34 && scenario<=36?3:2;
            count=snprintf(plan,sizeof(plan),"version=2\nmodel=sa110\nrate_mips=1\nfirmware=%s/guest.rom\ncmos=persist\nseed=%s/seed\ncmos_layout=ro350\ncontent_id=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\nrtc_epoch=0\nhard_disc=off\n%sobserver_cells=%u\nobserver_disc_devices=%u\n",directory,directory,scenario==26?"":"MEDIA",cells,forms);
            if(scenario!=26){char *mark=strstr(plan,"MEDIA");assert(mark);char tail[128];snprintf(tail,sizeof(tail),"%s",mark+5);
                snprintf(mark,sizeof(plan)-(size_t)(mark-plan),"media=%s/disc-request/Set.m3u\n%s",argv[3],tail);count=(int)strlen(plan);}
        }
        write_file("test.arcplan",plan,(size_t)count);pad_a=pad_b=0;assert(load(&game));
        for(unsigned step=0;phase()!=2 && step<10000;++step)run();assert(phase()==2);
        hd_identity_reads=hd_identity_writes=0;
        hd_record_store=rename_lba!=0;identity_write_fail=scenario==22;
        if(hd_record_store){const uint32_t lbas[]={6,map_lba,map_lba+5};for(unsigned i=0;i<3;++i)
            assert(arc_hd_format_range(256,13,(uint64_t)lbas[i]*512,identity_records[i],512));}
        if(scenario==11){char helper_path[2048];snprintf(helper_path,sizeof(helper_path),"%s/disc-request/One.adf",argv[3]);
            ArcEngineMedia helper={.path=helper_path,.kind=ARC_MEDIA_ADF_800K,.geometry=ARC_GEOMETRY_800K_D};
            assert(mount_helper(1,&helper,error,sizeof(error)));}
        if((scenario>=4 && scenario<=6) || ((scenario>=11 && scenario<=14) || (scenario>=18 && scenario<=22))){
            hd_identity_case=scenario<=6?scenario-3:0;
            ArcIdeBackend ide={.read=hd_identity_read,.write=hd_identity_write,.writable=true,.sectors=256u*16u*63u};
            assert(attach_ide(&ide));
        }
        if(scenario==15)after_preflight(NULL,true);
        if(scenario==16)after_preflight(hold_reset,false);
        if(scenario==17)after_preflight(block_replacement,false);
        if(scenario==7)preflight_fail();
        run();unsigned reads_before=vfs_reads;
        for(unsigned frame=1;frame<((scenario>=15 && scenario<=17)?1u:long_log_case?150u:scenario==11?32u:4u);++frame)run();
        if(scenario==11){
            ArcDiscRequest request;api->disc_snapshot(&request);assert(request.a_active && request.a_notified);
            assert(hd_identity_reads==3 && vfs_reads==reads_before);
        }
        if(rename_lba){
            char helper[11],hd[11];
            assert(identity_query(helper,hd)==(scenario>=19 && scenario<=21));
            if(scenario>=19 && scenario<=21)assert(!strcmp(hd,"ArcDroidHD"));
            assert(hd_identity_writes==(scenario==18?3u:1u) && hd_identity_reads==3);
            unsigned first=scenario==12 || scenario==18?0:scenario==13?1:2;
            unsigned copies=scenario==18?3:1;
            for(unsigned i=first;i<first+copies;++i)assert(!memcmp(identity_records[i]+(i?26:0x1c0+22),scenario<19?"Two       ":"ArcDroidHD",10));
            if(scenario<19){reset();assert(hd_identity_reads==6); /* Refresh sees partial or coherent guest rename. */
            for(unsigned frame=0;frame<4;++frame)run();assert(hd_identity_reads==6);}
        }
        after_preflight(NULL,false);
        assert(disks.get_image_index()==(auto_insert?1u:0u));
        if(scenario==23 || scenario==24){char helper[11],hd[11];assert(identity_query(helper,hd) && !strcmp(hd,"ArcDroidHD") && saw_disc_insert);}
        if(scenario==6)assert(saw_identity_unknown);
        if(scenario==11)assert(disc_logs<=4);
        if(scenario==25 || scenario==26){assert(disc_logs==1);reset();for(unsigned f=0;f<5;++f)run();assert(disc_logs==1);}
        if(scenario==27)assert(busy_logs>=2 && busy_logs<=3 && resolved_logs==1 && disc_logs==busy_logs+1);
        if((scenario>=28 && scenario<=30) || scenario==32 || scenario==33 || scenario==35 || scenario==36)assert(saw_device_refusal);
        if(scenario==31){reset();for(unsigned f=0;f<4;++f)run();ArcDiscRequest request;api->disc_snapshot(&request);assert(request.a_active);}
        if(scenario==37)assert(disc_logs==2); /* One hint and one notified transition, no heartbeat. */
        if(!auto_insert && scenario!=26){assert(disks.set_eject_state(true) && disks.set_image_index(1) && disks.set_eject_state(false));assert(disks.get_image_index()==1);}
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
