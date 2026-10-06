/* ArcDroid ROM-driven video/frontend qualification. GPL-2.0-only. */
#include "rpc_session.h"
#include "rpc_video_rom.h"
#include <assert.h>
#include <math.h>
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
#define LOAD(var,name) do { void *p=symbol(name);assert(p);memcpy(&var,&p,sizeof(var)); } while(0)
static bool (*start_rpc)(ArcRpcModel,const uint8_t *,size_t,const uint8_t *,uint64_t,char *,size_t);
static void (*status_rpc)(ArcRpcVideoStatus *);
static void (*snapshot_rpc)(ArcRpcTestSnapshot *);
static uint64_t (*bus_ns)(void);
static bool (*read_ram)(unsigned,uint32_t *);
static void (*fail_alloc)(unsigned);
static const ArcEngineBackendOps *(*engine_api)(void);
static const ArcEngineBackendOps *api;
static uint8_t rom[2097152],seed[256];
static ArcInputState input;
static char error[256];
static unsigned logs;
static void log_message(void *p,const char *s) { (void)p;assert(strstr(s,"clipped"));++logs; }
static ArcRpcVideoStatus status(void) { ArcRpcVideoStatus s;status_rpc(&s);return s; }
static uint32_t ram(unsigned offset) { uint32_t w;assert(read_ram(0x20000+offset,&w));return w; }
static void start(unsigned model,const struct RpcVideoGuest *guest)
{
    for(unsigned i=0;i<sizeof(rom)/4;++i) {
        uint32_t w=i<guest->count?guest->words[i]:0xeafffffe;
        for(unsigned b=0;b<4;++b)rom[i*4+b]=(uint8_t)(w>>(b*8));
    }
    assert(start_rpc((ArcRpcModel)model,rom,sizeof(rom),seed,0,error,sizeof(error)));
    arc_input_reset(&input);logs=0;api->set_log_callback(log_message,NULL);
}
static ArcEngineFrame frame(void)
{
    ArcEngineFrame f;assert(api->run_frame(&input,&f));
    assert(f.field_completed&&f.pixels&&f.width&&f.height&&f.pitch==8192);
    assert(!api->faulted(NULL));return f;
}
static uint32_t pixel(const ArcEngineFrame *f,unsigned x,unsigned y)
{ assert(x<f->width&&y<f->height);return f->pixels[(size_t)y*f->pitch/4+x]; }
static uint64_t hash(const ArcEngineFrame *f)
{
    uint64_t h=UINT64_C(14695981039346656037);
    /* Canonical little-endian pixels; ignore unused pitch/cropped storage. */
    for(unsigned y=0;y<f->height;++y)for(unsigned x=0;x<f->width;++x) {
        uint32_t v=pixel(f,x,y);for(unsigned b=0;b<4;++b){h^=(v>>(8*b))&255;h*=UINT64_C(1099511628211);}
    }
    return h;
}
static void expect_pixel(const struct RpcVideoGuest *g,const ArcEngineFrame *f,unsigned x,unsigned y,uint32_t expected)
{
    uint32_t actual=pixel(f,x,y);
    if(actual!=expected)fprintf(stderr,"%s (%u,%u) actual=%08x expected=%08x\n",g->name,x,y,actual,expected);
    assert(actual==expected);
}
static void check(unsigned model,const struct RpcVideoGuest *g)
{
    start(model,g);ArcEngineFrame f=frame();ArcRpcVideoStatus s=status();
    unsigned w=g->width>2048?2048:g->width,h=g->height>2048?2048:g->height;
    if(!strcmp(g->name,"grow"))w=2048;
    assert(f.width==w&&f.height==h&&s.fields==1);
    if(g->hz)assert(fabs(f.refresh_hz-g->hz)<1e-8);
    uint32_t grey=0xff555555;
    if(!strcmp(g->name,"last"))grey=0xffaaaaaa;
    if(g->depth==0||g->depth==1)grey=0xff010101;
    else if(g->depth==2)grey=0xff050505;
    if(!strcmp(g->name,"border")||!strcmp(g->name,"disabled"))grey=0xffff0000;
    if(!strcmp(g->name,"cursor"))grey=0xffff0000;
    if(!strcmp(g->name,"unaligned"))grey=0xff000000;
    expect_pixel(g,&f,0,0,grey);
    if(!strcmp(g->name,"palette")||!strcmp(g->name,"border")||!strcmp(g->name,"cursor")||
       !strcmp(g->name,"current")||!strcmp(g->name,"depth")||!strcmp(g->name,"grow")||!strcmp(g->name,"vstart")) {
        uint32_t top=!strcmp(g->name,"border")||!strcmp(g->name,"cursor")?0xffff0000:0xff555555;
        expect_pixel(g,&f,0,3,top);
        uint32_t bottom=!strcmp(g->name,"palette")?0xff0000ff:
            !strcmp(g->name,"border")?0xff00ff00:
            !strcmp(g->name,"current")?0xffaaaaaa:
            !strcmp(g->name,"depth")?0xff050505:0xff555555;
        expect_pixel(g,&f,0,7,bottom);
        if(!strcmp(g->name,"cursor"))expect_pixel(g,&f,4,7,0xff00ff00);
        if(!strcmp(g->name,"grow")) {
            expect_pixel(g,&f,2047,0,0xff000000);expect_pixel(g,&f,2047,4,0xff555555);
            assert(s.pixels==4u*32u+4u*2064u);
        }
        if(!strcmp(g->name,"vstart"))expect_pixel(g,&f,0,4,0xff00ff00);
    }
    if(!strcmp(g->name,"memory")) { expect_pixel(g,&f,0,3,0xff555555);expect_pixel(g,&f,0,7,0xff000000); }
    if(!strcmp(g->name,"hclip")) {
        assert(s.pixels==2064u*8u&&s.wraps==4&&ram(0)==(model==3?0x10000040u:0x02000000u));
        expect_pixel(g,&f,2047,7,0xff555555);
    }
    if(!strcmp(g->name,"vclip")) {
        assert(s.pixels==32u*2056u&&s.wraps==16&&ram(0)==(model==3?0x10000100u:0x02000000u));
        expect_pixel(g,&f,0,2047,0xff555555);
    }
    if(!strcmp(g->name,"ring")) {
        expect_pixel(g,&f,0,4,0xff555555);expect_pixel(g,&f,0,5,0xffaaaaaa);assert(s.wraps==1);
    }
    if(!strcmp(g->name,"unaligned")) { expect_pixel(g,&f,0,3,0xffc80c50);assert(s.wraps==1); }
    if(!strcmp(g->name,"packed"))assert(ram(0)==0x10000010u&&s.transfers==2);
    if(!strcmp(g->name,"last")) {
        expect_pixel(g,&f,2047,0,0xffaaaaaa);expect_pixel(g,&f,0,1,0xff555555);
        assert(s.wraps==4);
    }
    if(!strcmp(g->name,"disabled"))assert(s.pixels==0&&s.bytes==0&&bus_ns()==0);
    else if(strcmp(g->name,"border")&&strcmp(g->name,"packed"))assert(s.pixels&&s.bytes&&(s.transfers==bus_ns()/((model==3||!strcmp(g->name,"ring")||!strcmp(g->name,"packed")||!strcmp(g->name,"unaligned"))?350u:156u) || !strcmp(g->name,"cursor")));
    assert(s.flyback&&s.flyback_rises&&s.flyback_falls);
    assert((s.vidcur&0x1fffffffu)==(model==3?0x10000000u:0x02000000u) || !strcmp(g->name,"init") || !strcmp(g->name,"ring") || !strcmp(g->name,"last") || !strcmp(g->name,"packed") || !strcmp(g->name,"unaligned"));
    assert(logs==(g->width>2048||g->height>2048||!strcmp(g->name,"grow")));
    printf("%s %ux%u rate=%.6f hash=%016llx pixels=%llu wraps=%llu\n",g->name,f.width,f.height,f.refresh_hz,
        (unsigned long long)hash(&f),(unsigned long long)s.pixels,(unsigned long long)s.wraps);
    ArcEngineFrame second=frame();
    if(!strcmp(g->name,"init"))expect_pixel(g,&second,0,0,0xffaaaaaa);
    if(!strcmp(g->name,"hclip")||!strcmp(g->name,"vclip")) {
        expect_pixel(g,&second,0,0,0xff555555);assert(logs==1);
    }
    api->stop();api->stop();
}
static void contention(unsigned model)
{
    assert(model==2||model==3);
    for(unsigned disabled=0;disabled<2;++disabled) {
        const struct RpcVideoGuest *g=&rpc_video_guests[model][disabled?23:3];
        start(model,g);(void)frame();
        ArcRpcTestSnapshot cpu;snapshot_rpc(&cpu);ArcRpcVideoStatus v=status();
        uint64_t baseline=cpu.now_ns*cpu.rate_mips/1000u;
        uint64_t occupancy=bus_ns(),expected=occupancy*cpu.rate_mips/1000u;
        assert(cpu.executed<=baseline);
        uint64_t lost=baseline-cpu.executed;
        if(disabled)assert(!occupancy&&!lost);
        else {
            assert(occupancy==v.transfers*(model==3?350u:156u));
            assert(expected>0&&lost>0);
            /* Each line may discard less than one instruction's fraction.
             * No CPU credit may be recovered after a bus transfer finishes. */
            assert(lost+v.lines+1>=expected&&lost<=expected+v.lines+1);
        }
        api->stop();
    }
    puts("contention: transferred bytes debit CPU credit; disabled display debits none");
}
static unsigned av_count,geometry_count,video_count,rejected;
static bool reject;
static bool environment(unsigned command,void *data)
{
    if(command==RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO||command==RETRO_ENVIRONMENT_SET_GEOMETRY) {
        if(command==RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO) {
            ++av_count;const struct retro_system_av_info *av=data;
            assert(av->geometry.max_width==2048&&av->timing.sample_rate==48000);
        } else ++geometry_count;
        if(reject)++rejected;
        return !reject;
    }
    return false;
}
static void video_callback(const void *pixels,unsigned w,unsigned h,size_t pitch)
{ assert(pixels&&w&&h&&w<=2048&&h<=2048&&pitch>=w*4);++video_count; }
static void frontend(unsigned model)
{
    void (*set_environment)(retro_environment_t),(*set_video)(retro_video_refresh_t),(*run)(void);
    void (*av_info)(struct retro_system_av_info *);
    LOAD(set_environment,"retro_set_environment");LOAD(set_video,"retro_set_video_refresh");
    LOAD(run,"retro_run");LOAD(av_info,"retro_get_system_av_info");
    const struct RpcVideoGuest *guests=rpc_video_guests[model];
    for(unsigned pass=0;pass<3;++pass) {
        reject=pass==1;av_count=geometry_count=video_count=rejected=0;
        set_environment(pass==2?NULL:environment);set_video(video_callback);start(model,&guests[27]);
        for(unsigned n=0;n<18;++n)run();
        struct retro_system_av_info av;av_info(&av);
        assert(av.geometry.base_width==800&&av.geometry.base_height==256&&fabs(av.timing.fps-50)<1e-8);
        assert(video_count==18);
        assert(av_count==(pass==2?0u:2u)&&geometry_count==(pass==2?0u:1u));
        assert(rejected==(pass==1?3u:0u));api->stop();
    }
    av_count=geometry_count=video_count=0;reject=false;set_environment(environment);set_video(video_callback);
    start(model,&guests[28]);for(unsigned n=0;n<12;++n)run();
    assert(av_count==1&&geometry_count==0&&video_count==12);api->stop();
    /* Missing presentation/audio callbacks, replacement environment, and reset. */
    start(model,&guests[27]);set_video(NULL);set_environment(NULL);for(unsigned n=0;n<3;++n)run();
    av_count=geometry_count=0;set_environment(environment);run();assert(av_count==1);api->stop();
    puts("frontend accepted/rejected/missing callbacks, geometry-only, threshold: PASS");
}
int main(int argc,char **argv)
{
    assert(argc==4);unsigned model=(unsigned)atoi(argv[3]);assert(model<4);
#ifdef _WIN32
    library=LoadLibraryA(argv[1]);
#else
    library=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
#endif
    assert(library);LOAD(start_rpc,"arc_rpc_test_start");LOAD(status_rpc,"arc_rpc_test_video_status");
    LOAD(bus_ns,"arc_rpc_test_video_bus_ns");LOAD(snapshot_rpc,"arc_rpc_test_snapshot");LOAD(read_ram,"arc_rpc_test_read_ram");
    LOAD(fail_alloc,"arc_rpc_test_fail_alloc");LOAD(engine_api,"arc_rpc_test_engine_api");api=engine_api();
    if(!strcmp(argv[2],"--frontend"))frontend(model);
    else if(!strcmp(argv[2],"--contention"))contention(model);
    else {
        for(unsigned i=0;i<32;++i)if(i!=27&&i!=28)check(model,&rpc_video_guests[model][i]);
        /* Presentation is allocated lazily inside the protected boundary. */
        fail_alloc(model==3?4:5);start(model,&rpc_video_guests[model][0]);
        ArcEngineFrame f;assert(!api->run_frame(&input,&f)&&api->faulted(NULL));api->stop();fail_alloc(0);
    }
#ifdef _WIN32
    FreeLibrary(library);
#else
    assert(!dlclose(library));
#endif
    return 0;
}
