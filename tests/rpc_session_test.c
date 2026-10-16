/* Integrated RPC session and same-library Arculator switching. GPL-2.0-only. */
#include "rpc_session.h"
#include "rpc_session_rom.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
static HMODULE library;
static void *symbol(const char *name) { return (void *)GetProcAddress(library,name); }
#else
#include <dlfcn.h>
static void *library;
static void *symbol(const char *name) { return dlsym(library,name); }
#endif
static bool (*start_rpc)(ArcRpcModel,const uint8_t *,size_t,const uint8_t *,uint64_t,char *,size_t);
static bool (*advance_rpc)(uint64_t);
static void (*snapshot_rpc)(ArcRpcTestSnapshot *);
static bool (*read_ram)(unsigned,uint32_t *);
static void (*fail_alloc)(unsigned);
static void (*fault_next)(void);
static const ArcEngineBackendOps *(*engine_api)(void);
static const ArcEngineBackendOps *api;
static uint8_t *rom;
static uint8_t seed[256];
static char error[256];
static unsigned rate[] = {20,35,100,35};
#define LOAD(var,name) do { void *p=symbol(name);assert(p);memcpy(&var,&p,sizeof(var)); } while(0)
static void open_library(const char *path)
{
#ifdef _WIN32
    library=LoadLibraryA(path);
#else
    library=dlopen(path,RTLD_NOW|RTLD_LOCAL);
#endif
    assert(library);
}
static void load_api(void)
{
    LOAD(start_rpc,"arc_rpc_test_start"); LOAD(advance_rpc,"arc_rpc_test_advance");
    LOAD(snapshot_rpc,"arc_rpc_test_snapshot"); LOAD(read_ram,"arc_rpc_test_read_ram");
    LOAD(fail_alloc,"arc_rpc_test_fail_alloc"); LOAD(fault_next,"arc_rpc_test_fault_next");
    LOAD(engine_api,"arc_rpc_test_engine_api");api=engine_api();
}
static void rom_words(const uint32_t *words,size_t count)
{
    for(size_t i=0;i<2u*1024u*1024u;i+=4) {
        uint32_t w=i/4<count?words[i/4]:0xeafffffe;
        rom[i]=(uint8_t)w;rom[i+1]=(uint8_t)(w>>8);rom[i+2]=(uint8_t)(w>>16);rom[i+3]=(uint8_t)(w>>24);
    }
}
static void guest_rom(unsigned model)
{
    const uint32_t *words[]={rpc_session_rom_0,rpc_session_rom_1,rpc_session_rom_2,rpc_session_rom_3};
    size_t count[]={sizeof(rpc_session_rom_0)/4,sizeof(rpc_session_rom_1)/4,
        sizeof(rpc_session_rom_2)/4,sizeof(rpc_session_rom_3)/4};
    rom_words(words[model],count[model]);
}
static ArcRpcTestSnapshot snapshot(void) { ArcRpcTestSnapshot s;snapshot_rpc(&s);return s; }
static uint32_t word(unsigned off) { uint32_t v;assert(read_ram(off,&v));return v; }
static void cleared_capabilities(void)
{
    ArcEngineCapabilities c=api->capabilities();
    assert(c.av&&c.keyboard&&c.mouse&&c.reset);
    assert(!c.boot_watch&&!c.launch_outcome&&!c.error_box&&!c.disc_request&&c.floppy&&c.helper);
    assert(c.ide&&!c.native_joystick&&!c.power_on_input&&!c.cmos_persistence&&!c.serialisation);
    ArcLaunchOutcome l,zero_l={0};ArcErrorBox b,zero_b={.modal_unknown=true};ArcDiscRequest d,zero_d={0};
    memset(&l,0xa5,sizeof(l));memset(&b,0xa5,sizeof(b));memset(&d,0xa5,sizeof(d));
    api->launch_outcome_snapshot(&l);api->error_snapshot(&b);api->disc_snapshot(&d);
    assert(!memcmp(&l,&zero_l,sizeof(l))&&!memcmp(&b,&zero_b,sizeof(b))&&!memcmp(&d,&zero_d,sizeof(d)));
    assert(!api->launch_outcome_arm("test")&&!api->error_request(ARC_ERROR_OK));
    assert(api->media_eject()&&!api->media_insert(NULL,error,sizeof(error)));
    uint64_t t=123;bool boot=true;assert(!api->power_on_take_release(&t,&boot)&&!t&&!boot);
    uint8_t cmos[ARC_CMOS_PAYLOAD_SIZE];memset(cmos,0xa5,sizeof(cmos));api->cmos(cmos);
    for(unsigned i=0;i<sizeof(cmos);++i)assert(!cmos[i]);
    assert(api->boot_watch_state()==ARC_BOOT_WATCH_DISABLED&&!api->disc_controller_idle());
}
static void stopped_queries(void)
{
    ArcEngineCapabilities c=api->capabilities(),zero={0};
    assert(!memcmp(&c,&zero,sizeof(c)));
    const char *message="stale";assert(!api->faulted(&message)&&message==NULL);
}
static void guest_test(unsigned model)
{
    guest_rom(model);assert(start_rpc((ArcRpcModel)model,rom,2u*1024u*1024u,seed,946684800,error,sizeof(error)));
    assert(!error[0]);cleared_capabilities();
    assert(advance_rpc(8000000));
    ArcRpcTestSnapshot s=snapshot();
    assert(s.active&&!s.faulted&&s.now_ns==8000000&&s.rate_mips==rate[model]);
    assert(word(0)>0&&word(4)==0x60); /* Guest IRQ saw both tied timer requests. */
    assert(word(8)==0x40&&word(12)==5&&word(16)==0&&word(20)==0); /* No-media result. */
    assert(!(s.fiq&1)&&word(36)==0&&word(40)==0xffff&&word(44)==0xffff&&word(48)==0xffffffff);
    assert(word(52)==0xa5&&s.events[4]==1&&s.events[7]>0&&(s.irq&8));
    assert(word(56)==0xfa&&s.events[2]==1);
    assert(word(60)==(model==3?0xfeu:UINT32_MAX)&&s.events[3]==(model==3?1u:0u));
    bool tie=false;
    for(unsigned i=1;i<s.trace_count;++i)
        if(s.trace[i-1].event==0&&s.trace[i].event==1&&s.trace[i-1].ns==s.trace[i].ns)tie=true;
    assert(tie);
    assert(model<2?s.idle_ns==0:s.idle_ns>100000);
    assert(s.executed<=8000u*rate[model]);
    if(model<2)assert(s.executed<8000u*rate[model]); /* Uncached fetch/data service; hot ceiling calibrated separately. */
    printf("rpc-session model=%u rate=%u guest-irq=%u fdc=40,05 tie=timer0,timer1 idle=%s\n",
        model,s.rate_mips,word(0),model<2?"running":"hardware");
    assert(api->reset(error,sizeof(error)));assert(word(0)==0);
    assert(advance_rpc(8000000));assert(word(52)==0xa5);
    api->stop();api->stop();stopped_queries();assert(!snapshot().live_allocations&&!api->active());
}
static void lifecycle(void)
{
    for(unsigned model=0;model<4;++model) {
        guest_rom(model);
        unsigned stages=model==3?3:4;
        for(unsigned stage=1;stage<=stages;++stage) {
            fail_alloc(stage);
            assert(!start_rpc((ArcRpcModel)model,rom,2097152,seed,946684800,error,sizeof(error)));
            ArcRpcTestSnapshot s=snapshot();assert(!s.active&&!s.live_allocations&&s.allocations==stage&&error[0]);
            api->stop();api->stop();
        }
        fail_alloc(0);
        assert(!start_rpc((ArcRpcModel)model,rom,6u*1024u*1024u,seed,0,error,sizeof(error)));
        assert(!start_rpc((ArcRpcModel)model,rom,2097152,NULL,0,error,sizeof(error)));
        assert(!start_rpc((ArcRpcModel)model,rom,2097152,seed,UINT64_MAX,error,sizeof(error)));
        assert(!start_rpc(ARC_RPC_MODEL_COUNT,rom,2097152,seed,0,error,sizeof(error)));
        fault_next();assert(!start_rpc((ArcRpcModel)model,rom,2097152,seed,0,error,sizeof(error)));
        assert(!snapshot().active&&!snapshot().live_allocations&&api->faulted(NULL));api->stop();stopped_queries();
        assert(start_rpc((ArcRpcModel)model,rom,2097152,seed,0,error,sizeof(error)));
        assert(!advance_rpc(16666668)&&snapshot().now_ns==0&&!api->faulted(NULL));
        fault_next();assert(!advance_rpc(10000));
        const char *message=NULL;assert(api->faulted(&message)&&message&&message[0]);
        uint64_t executed=snapshot().executed;assert(!advance_rpc(10000)&&snapshot().executed==executed);
        ArcInputState input;arc_input_reset(&input);ArcEngineFrame frame,zero_frame={0};
        memset(&frame,0xa5,sizeof(frame));assert(!api->run_frame(&input,&frame));
        assert(!memcmp(&frame,&zero_frame,sizeof(frame)));
        int16_t audio[32];memset(audio,0xa5,sizeof(audio));assert(api->audio(audio,16)==16);
        for(unsigned i=0;i<32;++i)assert(audio[i]==0);
        assert(!api->reset(error,sizeof(error)));api->stop();
        assert(start_rpc((ArcRpcModel)model,rom,2097152,seed,0,error,sizeof(error)));
        assert(advance_rpc(10000)&&!api->faulted(NULL));
        assert(api->run_frame(&input,&frame)&&!frame.pixels&&!frame.field_completed);
        fault_next();assert(!api->reset(error,sizeof(error)));assert(api->faulted(NULL));api->stop();stopped_queries();
    }
    /* A guest invalid mode exercises an actual interpreter failure class. */
    const uint32_t bad[]={0xe3a00070,0xee010f10,0xe3a00014,0xe121f000,0xeafffffe};
    rom_words(bad,sizeof(bad)/4);
    assert(start_rpc(ARC_RPC_SA110,rom,2097152,seed,0,error,sizeof(error)));
    assert(!advance_rpc(10000)&&api->faulted(NULL));api->stop();stopped_queries();
    for(unsigned i=0;i<50;++i) {
        unsigned model=i%4;guest_rom(model);
        assert(start_rpc((ArcRpcModel)model,rom,2097152,seed,0,error,sizeof(error)));
        assert(advance_rpc(10000));api->stop();api->stop();
        assert(!snapshot().live_allocations);
    }
    puts("rpc-session lifecycle: allocation stages, fault recovery, reset fault, guest fault and 50 cycles passed");
}
static uint64_t hash(uint64_t h,const void *data,size_t size)
{ const uint8_t *p=data;for(size_t i=0;i<size;++i){h^=p[i];h*=UINT64_C(1099511628211);}return h; }
static uint64_t arculator_trace(const uint8_t *bytes,size_t size)
{
    uint8_t cmos[ARC_CMOS_MACHINE_SIZE]={0},mask[32]={0};
    const ArcMachineDescriptor *model=arc_machine_descriptor(ARC_MACHINE_A3000);
    assert(api->start(bytes,size,NULL,NULL,model,ARC_OS_TEST,NULL,cmos,mask,NULL,NULL,error,sizeof(error)));
    assert(api->capabilities().floppy);
    ArcInputState input;arc_input_reset(&input);uint64_t h=UINT64_C(14695981039346656037);
    for(unsigned i=0;i<8;++i) {
        ArcEngineFrame frame;assert(api->run_frame(&input,&frame));
        h=hash(h,&frame.width,sizeof(frame.width));h=hash(h,&frame.height,sizeof(frame.height));
        h=hash(h,&frame.elapsed_us,sizeof(frame.elapsed_us));
        if(frame.pixels)h=hash(h,frame.pixels,frame.height*frame.pitch);
        int16_t pcm[1600];size_t n=api->audio(pcm,800);h=hash(h,pcm,n*4);
    }
    api->stop();return h;
}
int main(int argc,char **argv)
{
    assert(argc>=3);open_library(argv[1]);
    if(!strcmp(argv[2],"--shipping")) {
        assert(!symbol("arc_rpc_test_start")&&!symbol("arc_rpc_test_engine_api"));
        const char *names[]={"RiscPC ARM610","RiscPC ARM710","RiscPC SA-110","A7000","rpcarm610","rpcsa110"};
        assert(ARC_MACHINE_COUNT==4);
        for(unsigned i=0;i<sizeof(names)/sizeof(*names);++i)assert(!arc_machine_from_name(names[i]));
        puts("rpc-shipping-route: test entry absent; private models rejected by shipping registry");return 0;
    }
    load_api();
    { ArcEngineCapabilities c=api->capabilities(),zero={0};assert(!memcmp(&c,&zero,sizeof(c))); }
    rom=malloc(2097152);assert(rom);
    for(unsigned i=0;i<256;++i)seed[i]=(uint8_t)(i^0x5a);
    if(!strcmp(argv[2],"--guest")) { assert(argc==4);guest_test((unsigned)atoi(argv[3])); }
    else if(!strcmp(argv[2],"--lifecycle"))lifecycle();
    else {
        assert(argc==4);FILE *f=fopen(argv[3],"rb");assert(f);
        assert(!fseek(f,0,SEEK_END));long size=ftell(f);assert(size>0);rewind(f);
        uint8_t *bytes=malloc((size_t)size);assert(bytes&&fread(bytes,1,(size_t)size,f)==(size_t)size);fclose(f);
        uint64_t fresh=arculator_trace(bytes,(size_t)size);
        if(!strcmp(argv[2],"--switch")) {
            for(unsigned model=0;model<4;++model) {
                guest_test(model);
                uint64_t after=arculator_trace(bytes,(size_t)size);assert(after==fresh);
            }
        }
        printf("arculator-trace=%016llx\n",(unsigned long long)fresh);free(bytes);
    }
    free(rom);
#ifdef _WIN32
    FreeLibrary(library);
#else
    assert(!dlclose(library));
#endif
    return 0;
}
