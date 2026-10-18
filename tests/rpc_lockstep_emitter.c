/* SPDX-License-Identifier: GPL-2.0-only */
/* Bounded native loader: exercises the real emitter without a frame-length
 * terminal loop. Each invocation owns one library and one stream. */
#include "rpc_session.h"
#include "rpc_lockstep.h"
#include "rpc_lockstep_observers.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
static HMODULE library;
static void *find(const char *name) { return (void *)GetProcAddress(library,name); }
#else
#include <dlfcn.h>
static void *library;
static void *find(const char *name) { return dlsym(library,name); }
#endif
#define LOAD(v,n) do { void *p=find(n);if(!p){fprintf(stderr,"missing %s\n",n);return 2;}memcpy(&v,&p,sizeof(v)); } while(0)
static void revalidate(void *opaque) { (void)opaque; }
static uint64_t written;
static bool (*retirement_probe)(unsigned);
static bool retirement_changed;
static unsigned sink_control;
static bool hash_sink,discard_sink;
static ArcSha256 stream_hash;
static void print_hash(void)
{
    if(!hash_sink)return;
    uint8_t digest[32];arc_sha256_final(&stream_hash,digest);
    fputs("stream-sha256=",stderr);for(unsigned i=0;i<32;++i)fprintf(stderr,"%02x",digest[i]);
    fprintf(stderr," bytes=%" PRIu64 "\n",written);
}
static bool (*end_stream)(void);
static bool (*complete)(ArcRpcObserver,uint64_t,uint64_t,uint64_t,uint16_t,const uint32_t *,uint32_t);
static uint64_t (*owner_generation)(ArcRpcObserver);
static void consume(void *opaque,const ArcRpcObservation *event)
{ (void)opaque;if(event->kind==ARC_RPC_OBS_SWI)(void)complete(ARC_RPC_OBS_ERROR,event->order,event->epoch,owner_generation(ARC_RPC_OBS_ERROR),0,NULL,0); }
static bool sink(void *opaque,const void *data,size_t bytes)
{
    written+=bytes;
    if(written>193 && sink_control==1)return false;
    if(written>193 && sink_control==2)(void)end_stream();
    if(hash_sink){arc_sha256_update(&stream_hash,data,bytes);return true;}
    if(discard_sink)return true;
    bool ok=fwrite(data,1,bytes,opaque)==bytes;
    /* This optional test fixture invokes the preparation's real consumed
     * callback after P0 was cached and before the fetch checkpoint. It never
     * marks the observer owner directly. Normal loader runs have no probe. */
    const uint8_t *frame=data;
    if(ok && retirement_probe && !retirement_changed && bytes>=53 &&
        (frame[0]==1 || frame[0]==2) && frame[29]==0){
        retirement_changed=retirement_probe(1);
        if(!retirement_changed)return false;
    }
    return ok;
}
static int refusal_probe(const char *path,bool shipping)
{
#ifdef _WIN32
    library=LoadLibraryA(path);
#else
    library=dlopen(path,RTLD_NOW|RTLD_LOCAL);
#endif
    if(!library || !find("retro_get_system_info"))return 2;
    const char *internal[]={"arc_rpc_lockstep_inject","arc_rpc_lockstep_p0","arc_rpc_lockstep_fatal"};
    for(unsigned i=0;i<3;++i)if(find(internal[i]))return 1;
    if(shipping){
        const char *api[]={"arc_rpc_test_lockstep_sink","arc_rpc_test_lockstep_status","arc_rpc_test_lockstep_end"};
        for(unsigned i=0;i<3;++i)if(find(api[i]))return 1;
    }else{
        unsigned (*caps)(void);bool (*attach)(ArcRpcLockstepSink,void *,const ArcRpcLockstepConfig *),(*end)(void);
        const char *(*status)(void);
        LOAD(caps,"arc_rpc_test_capabilities");LOAD(attach,"arc_rpc_test_lockstep_sink");
        LOAD(end,"arc_rpc_test_lockstep_end");LOAD(status,"arc_rpc_test_lockstep_status");
        if(caps()&32 || attach(NULL,NULL,NULL) || end() || strcmp(status(),"lockstep stream unavailable"))return 1;
    }
#ifdef _WIN32
    FreeLibrary(library);
#else
    dlclose(library);
#endif
    return 0;
}
int main(int argc,char **argv)
{
    if(argc==3 && (!strcmp(argv[1],"--refusal") || !strcmp(argv[1],"--shipping")))
        return refusal_probe(argv[2],!strcmp(argv[1],"--shipping"));
    if(argc!=12 && argc!=14 && argc!=16)return 2;
#ifdef _WIN32
    library=LoadLibraryA(argv[1]);
#else
    library=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
#endif
    if(!library)return 2;
    bool (*start)(ArcRpcModel,const uint8_t *,size_t,const uint8_t *,uint64_t,char *,size_t),(*advance)(uint64_t);
    bool (*attach)(ArcRpcLockstepSink,void *,const ArcRpcLockstepConfig *),(*end)(void);
    void (*snapshot)(ArcRpcTestSnapshot *),(*quota)(unsigned);
    const char *(*status)(void);
    const ArcEngineBackendOps *(*engine)(void);
    LOAD(start,"arc_rpc_test_start");LOAD(advance,"arc_rpc_test_advance");LOAD(snapshot,"arc_rpc_test_snapshot");
    LOAD(attach,"arc_rpc_test_lockstep_sink");LOAD(status,"arc_rpc_test_lockstep_status");LOAD(end,"arc_rpc_test_lockstep_end");
    LOAD(quota,"arc_rpc_session_test_quota");LOAD(engine,"arc_rpc_test_engine_api");
    end_stream=end;
    sink_control=!strcmp(argv[11],"fail-sink")?1:!strcmp(argv[11],"reentrant")?2:0;
    uint8_t *rom=malloc(2097152);if(!rom)return 2;
    FILE *file=fopen(argv[2],"rb");if(!file)return 2;
    bool read=fread(rom,1,2097152,file)==2097152 && fgetc(file)==EOF;
    if(fclose(file) || !read)return 2;
    FILE *stream=NULL;
    bool attached=strcmp(argv[4],"off")!=0;
    hash_sink=!strcmp(argv[4],"hash");discard_sink=!strcmp(argv[4],"discard");
    arc_sha256_init(&stream_hash);
    ArcRpcLockstepConfig config={.version=1,.size=sizeof(config),
        .model=(uint32_t)strtoul(argv[3],NULL,10),.seed=1,.detail_last=UINT64_MAX};
    config.flags=(uint32_t)strtoul(argv[6],NULL,10);
    config.inject_kind=(uint32_t)strtoul(argv[7],NULL,10);
    config.inject_k=strtoull(argv[8],NULL,10);
    config.allocation_shift=(uint32_t)strtoul(argv[9],NULL,10);
    if(argc>=14 && strcmp(argv[12],"-")){config.flags|=ARC_RPC_LOCKSTEP_DETAIL;config.detail_first=strtoull(argv[12],NULL,10);config.detail_last=strtoull(argv[13],NULL,10);}
    unsigned ram_words=argc==16?(unsigned)strtoul(argv[14],NULL,10):0;
    unsigned frames=argc==16?(unsigned)strtoul(argv[15],NULL,10):0;
    if(ram_words>4096 || frames>100)return 2;
    ArcSha256 plan_hash;arc_sha256_init(&plan_hash);arc_sha256_update(&plan_hash,rom,2097152);arc_sha256_final(&plan_hash,config.plan);
    if(attached){
        if(!hash_sink && !discard_sink){stream=fopen(argv[4],"wb");if(!stream)return 2;}
        if(!attach(sink,stream,&config))return 2;
    }
    uint8_t seed[256]={0};char error[256]={0};
    bool failed_start=!strcmp(argv[11],"failed-start");
    unsigned fail_stage=!strncmp(argv[11],"alloc:",6)?(unsigned)strtoul(argv[11]+6,NULL,10):0;
    if(fail_stage){void (*fail_alloc)(unsigned);LOAD(fail_alloc,"arc_rpc_test_fail_alloc");fail_alloc(fail_stage);}
    bool ok=start(failed_start?ARC_RPC_MODEL_COUNT:(ArcRpcModel)config.model,rom,2097152,seed,0,error,sizeof(error));
    if(failed_start || (fail_stage && !ok)){
        ArcRpcTestSnapshot failed={0};snapshot(&failed);
        printf("failed=%u allocations=%u live=%u\n",!ok,failed.allocations,failed.live_allocations);
        unsigned result=ok?1:0;
        if(attached){if(!end() || status() || (stream && fclose(stream)))result=1;print_hash();}
        free(rom);return (int)result;
    }
    if(!ok){fprintf(stderr,"start refused: %s\n",error);return 2;}
    if(!strcmp(argv[11],"preparation-retirement")){
        LOAD(retirement_probe,"arc_test_lockstep_retirement");
        if(!retirement_probe(0))return 2;
    }
    ArcRpcLaunchAttempt attempt;ArcRpcAttemptToken token;ArcRpcLaunchController controller;
    bool byte_text=strstr(argv[11],"byte-text")!=NULL;
    if(byte_text){
        void (*override)(unsigned,uint32_t,uint32_t,uint32_t);LOAD(override,"arc_engine_test_observers_override");
        override(255,0,0,UINT32_MAX);
        bool (*bind)(const ArcRpcLaunchBinding *);LOAD(bind,"arc_engine_rpc_launch_bind");
        ArcRpcLaunchPlan plan={.action=ARC_RPC_PLAN_BOOT,.media={1}};
        strcpy(plan.target,"ADFS::0.$.!Boot");
        if(!arc_riscpc_attempt_init(&attempt,1234,1) ||
           !arc_riscpc_attempt_prepare(&attempt,&plan,0,&token) ||
           !arc_riscpc_controller_build(&plan,&token,&controller))return 2;
        strcpy(controller.text,"Run \x80\xff");
        ArcRpcLaunchBinding binding={.attempt=&attempt,.controller=&controller,.revalidate=revalidate};
        if(!bind(&binding) || !engine()->launch_outcome_arm(controller.text))return 2;
    }
    if(!strcmp(argv[11],"reattach")){
        if(!stream || !end() || fclose(stream))return 2;
        stream=fopen(argv[4],"wb");if(!stream || !attach(sink,stream,&config))return 2;
    }
    uint8_t *media_allocation=NULL;
    ArcJfdImage image={0};ArcJfdOverlay overlay={0};ArcJfdSector sector={0};
    bool media_case=!strncmp(argv[11],"raw",3) || !strncmp(argv[11],"jfd",3);
    if(media_case){
        size_t padding=(size_t)config.allocation_shift*64u;
        media_allocation=calloc(655360u+padding,1);if(!media_allocation)return 2;
        uint8_t *data=media_allocation+padding;
        if(strstr(argv[11],"changed"))data[0]=1;
        bool (*mount)(unsigned,const ArcEngineMedia *,char *,size_t);LOAD(mount,"arc_engine_test_floppy_mount");
        ArcEngineMedia media={.kind=ARC_MEDIA_ADL_640K,.geometry=ARC_GEOMETRY_640K_L,.data=data,.size=655360};
        if(!strncmp(argv[11],"jfd",3)){
            sector=(ArcJfdSector){.size=1024,.density=2};image.decoded=data;image.decoded_size=1024;
            if(byte_text){image.title[0]=(char)0x80;image.title[1]=(char)0xff;image.identification_format=(char)0xfe;}
            image.sectors=&sector;image.sector_count=1;image.track_count=1;image.tracks[0].sector_count=1;
            image.overlay=&overlay;image.file_data.reason="authored";image.identification_reason="authored";
            media=(ArcEngineMedia){.kind=ARC_MEDIA_JFD,.jfd=&image};
        }
        if(!mount(0,&media,error,sizeof(error))){fprintf(stderr,"mount: %s\n",error);return 2;}
    }
    if(!strcmp(argv[11],"rate")){
        void (*rate)(unsigned);LOAD(rate,"arc_rpc_session_test_rate");rate(7);
    }
    if(!strcmp(argv[11],"stop"))engine()->stop();
    quota((unsigned)strtoul(argv[10],NULL,10));
    unsigned observer_mode=!strcmp(argv[11],"unmatched")?1:!strcmp(argv[11],"trace")?2:!strcmp(argv[11],"active")?3:0;
    bool consume_swi=!strcmp(argv[11],"consume");
    if(!lockstep_observers(find,observer_mode))return 2;
    if(consume_swi){
        bool (*filter)(ArcRpcObserver,const ArcRpcObserveFilter *);
        LOAD(filter,"arc_rpc_observer_test_filter");LOAD(complete,"arc_rpc_observer_complete");LOAD(owner_generation,"arc_rpc_observer_generation");
        ArcRpcObserveFilter f={.swi=0,.swi_mask=0xffffff,.modes=~0u,.kinds=ARC_RPC_OBS_SWI,.callback=consume};
        if(!filter(ARC_RPC_OBS_ERROR,&f))return 2;
    }
    if(!strncmp(argv[11],"end:",4) || observer_mode || consume_swi || media_case || !strcmp(argv[11],"idle-frames")){
        bool (*endpoint)(uint64_t,uint64_t,bool);LOAD(endpoint,"arc_rpc_test_timing_endpoint");
        uint64_t last=observer_mode || consume_swi || media_case || !strcmp(argv[11],"idle-frames")?17:strtoull(argv[11]+4,NULL,10);
        if(!endpoint(1,last,false))return 2;
    }
    bool advanced=true;
    if(frames){
        ArcInputState input={0};ArcEngineFrame frame={0};
        for(unsigned i=0;i<frames;++i)if(!engine()->run_frame(&input,&frame)){advanced=false;break;}
    }else advanced=advance(strtoull(argv[5],NULL,10));
    if(!strcmp(argv[11],"reset") || !strcmp(argv[11],"reset-run")){
        if(!engine()->reset(error,sizeof(error)))return 2;
        if(!strcmp(argv[11],"reset-run"))advanced=advance(strtoull(argv[5],NULL,10));
    }else if(!strcmp(argv[11],"idle-frames")){
        ArcInputState input={0};ArcEngineFrame frame={0};
        if(!engine()->run_frame(&input,&frame) || !engine()->run_frame(&input,&frame))return 2;
    }
    ArcRpcTestSnapshot s={0};snapshot(&s);
    printf("now=%" PRIu64 " executed=%" PRIu64 " idle=%" PRIu64 " allocations=%u live=%u fault=%u advanced=%u\n",
        s.now_ns,s.executed,s.idle_ns,s.allocations,s.live_allocations,s.faulted,advanced);
    fputs("regs=",stdout);for(unsigned i=0;i<17;++i)printf("%s%08x",i?",":"",s.regs[i]);puts("");
    if(ram_words && !s.faulted){
        bool (*read_ram)(unsigned,uint32_t *);LOAD(read_ram,"arc_rpc_test_read_ram");
        fputs("ram=",stdout);for(unsigned i=0;i<ram_words;++i){uint32_t value;if(!read_ram(262144u+4u*i,&value))return 2;printf("%s%08x",i?",":"",value);}puts("");
    }
    unsigned result=retirement_probe && !retirement_changed?1:0;
    if(attached){
        if(!end() || !end() || status()){fprintf(stderr,"emitter: %s\n",status()?status():"End failure");result=1;}
        if(stream && fclose(stream))result=1;
        print_hash();
    }
    engine()->stop();free(media_allocation);free(rom);
    return (int)result;
}
