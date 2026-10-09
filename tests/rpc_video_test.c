/* ArcDroid ROM-driven video/frontend qualification. GPL-2.0-only. */
#include "rpc_session.h"
#include "rpc_video_rom.h"
#include "rpc_video_paths.h"
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
    ArcEngineFrame f;
    /* Uncached A7000 setup may span several ordinary frontend frames. The
     * oracle qualifies complete fields; no raster is omitted after startup. */
    unsigned attempts=0;
    do { assert(api->run_frame(&input,&f)); assert(++attempts<=16); } while(!f.field_completed);
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
            unsigned split=model==3?5:4;
            expect_pixel(g,&f,2047,0,0xff000000);expect_pixel(g,&f,2047,split,0xff555555);
            assert(s.pixels==split*32u+(8u-split)*2064u);
        }
        if(!strcmp(g->name,"vstart"))expect_pixel(g,&f,0,4,model==3?0xff555555:0xff00ff00);
    }
    if(!strcmp(g->name,"memory")) { expect_pixel(g,&f,0,3,0xff555555);expect_pixel(g,&f,0,7,model==3?0xff555555:0xff000000); }
    if(!strcmp(g->name,"hclip")) {
        assert(s.pixels==2064u*8u&&s.wraps==4&&ram(0)==(model==3?rpc_a7000_hclip_readback[0]:0x02000000u));
        expect_pixel(g,&f,2047,7,0xff555555);
    }
    if(!strcmp(g->name,"vclip")) {
        assert(s.pixels==32u*2056u&&s.wraps==16&&ram(0)==(model==3?rpc_a7000_vclip_readback[0]:0x02000000u));
        expect_pixel(g,&f,0,2047,0xff555555);
    }
    if(!strcmp(g->name,"ring")) {
        expect_pixel(g,&f,0,4,0xff555555);expect_pixel(g,&f,0,5,model==3?0xff555555:0xffaaaaaa);assert(s.wraps==(model==3?0u:1u));
    }
    if(!strcmp(g->name,"unaligned")) { expect_pixel(g,&f,0,3,model==3?0xff000000:0xffc80c50);if(model==3)expect_pixel(g,&f,0,4,0xff324354);assert(s.wraps==1); }
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
static void scroll(unsigned model)
{
    const struct RpcVideoGuest *g=&rpc_scroll_guests[model];
    const unsigned length=16384,base=model==3?0x10000000u:0x02000000u;
    start(model,g);uint64_t wraps=0;
    for(unsigned step=0;step<8;++step) {
        unsigned init=(length-2672+step*640)%length;
        ArcEngineFrame f=frame();ArcRpcVideoStatus v=status();
        assert(f.width==640&&f.height==32);
        /* Independent circular byte stream, with no half-SAM implementation
         * or captured raster involved. Check every displayed pixel. */
        for(unsigned y=0;y<32;++y)for(unsigned x=0;x<640;++x) {
            unsigned address=(init+y*640+x)%length;
            unsigned index=((address/4)^(address/256))&255;
            expect_pixel(g,&f,x,y,0xff000000u|index*0x010101u);
        }
        assert(ram(step*16)==(model==3?0x70u:0x28u));
        assert(ram(step*16+8)==((base+init) | (model!=3&&init>=length-2048?0x40000000u:0)));
        /* The guest samples VIDCUR after five rows. DRAM has consumed 3200
         * bytes; VRAM requests at physical half-SAM boundaries and retains
         * INIT's low address bits until reloading START. */
        unsigned consumed=(model==3?6:5)*640,cur,last=0;
        if(model==3)cur=(init+consumed)%length;
        else {
            if(init+consumed>=length)cur=((init+consumed-length)/2048)*2048;
            else cur=init+((init%2048+consumed)/2048)*2048;
            if(cur/2048==(length-2048)/2048)last=0x40000000u;
        }
        assert(ram(step*16+4)==(model==3?rpc_a7000_scroll_readback[step]:((base+cur)|last)));
        wraps+=(init+640*32)/length;assert(v.wraps==wraps);
        printf("scroll model=%u step=%u init=%u pixels=20480 PASS\n",model,step,init);
    }
    api->stop();
}
static void pending_row(const ArcEngineFrame *f)
{
    /* Between rows, the following row must retain the preceding field's
     * pixels. All reads stay inside the allocation. This catches cropped
     * quad writes even when later rows/heap padding hide the overrun. */
    bool (*advance)(uint64_t); LOAD(advance,"arc_rpc_test_advance");
    ArcRpcVideoStatus s=status();assert(s.raster==0&&s.fields==1);
    for(unsigned x=0;x<16;++x)assert(pixel(f,x,1)==rpc_path_row1_prefix[x]);
    unsigned steps=0;
    do { assert(advance(100));s=status();assert(++steps<=4000); } while(s.raster<3);
    /* VDSR=2: raster 3 draws row zero; raster 4 has not drawn row one.
     * HCR+8=2400 at 24 MHz gives one line every 100,000 ns. */
    assert(s.raster==3&&s.fields==1);
    for(unsigned x=0;x<16;++x)assert(pixel(f,x,1)==rpc_path_row1_prefix[x]);
}
static void paths(unsigned model)
{
    for(unsigned i=0;i<4;++i){
        const struct RpcVideoGuest *g=&rpc_path_guests[model][i];start(model,g);
        ArcEngineFrame f=frame();ArcRpcVideoStatus s=status();
        assert(f.width==(g->width>2048?2048:g->width)&&f.height==g->height);
        printf("%s %ux%u rate=%.6f hash=%016llx pixels=%llu wraps=%llu transfers=%llu hold=%llu\n",
            g->name,f.width,f.height,f.refresh_hz,(unsigned long long)hash(&f),
            (unsigned long long)s.pixels,(unsigned long long)s.wraps,
            (unsigned long long)s.transfers,(unsigned long long)bus_ns());
        assert(s.fields==1 && logs==(g->width>2048));
        if(!strcmp(g->name,"right-edge"))pending_row(&f);
        api->stop();
    }
}

static void contention(unsigned model)
{
    assert(model==2||model==3);
    for(unsigned disabled=0;disabled<2;++disabled) {
        const struct RpcVideoGuest *g=&rpc_video_guests[model][disabled?23:3];
        start(model,g);(void)frame();
        ArcRpcTestSnapshot before,cpu;snapshot_rpc(&before);
        uint64_t start_bus=bus_ns();ArcRpcVideoStatus old=status();(void)frame();
        snapshot_rpc(&cpu);ArcRpcVideoStatus v=status();
        uint64_t occupancy=bus_ns()-start_bus,elapsed=cpu.now_ns-before.now_ns;
        uint64_t denominator=28u*cpu.rate_mips;
        /* Steady code is an uncached ROM branch. SA still uses reset MCLK;
         * A7000 reset ROM costs 1750 ns. Hold plus CPU service must conserve
         * exact time. At either endpoint one instruction may be incomplete. */
        uint64_t cost=model==2?7875u*cpu.rate_mips:28000u+1750u*denominator;
        uint64_t expected=(elapsed-occupancy)*denominator/cost;
        uint64_t actual=cpu.executed-before.executed;
        if(actual+2<expected||actual>expected+2)fprintf(stderr,"contention model=%u disabled=%u elapsed=%llu hold=%llu actual=%llu expected=%llu cost=%llu/%llu\n",model,disabled,(unsigned long long)elapsed,(unsigned long long)occupancy,(unsigned long long)actual,(unsigned long long)expected,(unsigned long long)cost,(unsigned long long)denominator);
        assert(actual+2>=expected&&actual<=expected+2);
        if(disabled)assert(!occupancy);
        else {
            assert(occupancy==(v.transfers-old.transfers)*(model==3?350u:156u));
            assert(occupancy>0&&expected<elapsed*denominator/cost);
        }
        api->stop();
    }
    puts("contention: DMA hold and uncached CPU service conserve time; disabled display holds none");
}
static void extreme(unsigned model)
{
    bool (*advance)(uint64_t); LOAD(advance,"arc_rpc_test_advance");
    for(unsigned fast=0;fast<2;++fast) {
        start(model,&rpc_extreme_guests[model][fast]);
        assert(advance(1000000)); /* includes programming plus many shortest lines */
        for(unsigned i=0;i<2;++i) assert(advance(16666667));
        ArcRpcVideoStatus v=status();
        assert(!api->faulted(NULL) && v.fields>0 && v.lines>0);
        assert(v.pixels<=v.lines*8u && v.bytes==v.pixels*4u);
        assert(v.refresh_hz<=120000000.0/(8.0*8193.0));
        api->stop();
    }
    puts("extreme HDER/HCR and VCO: bounded DMA/raster without session fault");
}
static void pixel_clock(unsigned model)
{
    bool (*advance)(uint64_t); LOAD(advance,"arc_rpc_test_advance");
    for(unsigned clock=0;clock<3;++clock) {
        const uint64_t hz=clock==0?108000000u:120000000u;
        const uint64_t line_numerator=UINT64_C(1688)*1000000000u;
        start(model,&rpc_clock_guests[model][clock]);
        ArcEngineFrame f=frame(); ArcRpcVideoStatus v=status();
        assert(v.fields==1 && v.raster==0);
        assert(f.refresh_hz==(double)hz/(1688.0*1066.0));
        ArcRpcTestSnapshot before; snapshot_rpc(&before);
        /* Each edge is checked against the independent rational period,
         * including its carried nanosecond remainder across the field. */
        for(unsigned line=1;line<=32;++line) {
            uint64_t previous=(1066u+line-1u)*line_numerator/hz;
            uint64_t next=(1066u+line)*line_numerator/hz;
            assert(advance(next-previous-1u));
            assert(status().raster==line-1u && status().lines==v.lines+line-1u);
            assert(advance(1));
            assert(status().raster==line && status().lines==v.lines+line);
        }
        (void)frame();
        ArcRpcTestSnapshot after; snapshot_rpc(&after);
        assert(after.now_ns-before.now_ns==2132u*line_numerator/hz-1066u*line_numerator/hz);
        assert(!api->faulted(NULL));
        api->stop();
    }
    puts("pixel-clock: exact 108/120 MHz line and field timing; excessive PLL bounded");
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
static uint64_t silence_pairs;
static size_t silence(const int16_t *pcm,size_t count)
{
    for(size_t i=0;i<count*2;++i)assert(pcm[i]==0);
    silence_pairs+=count;return count;
}
static void fault_silence(unsigned model)
{
    void (*run)(void),(*fault)(void),(*set_audio)(retro_audio_sample_batch_t);
    void (*set_environment)(retro_environment_t);
    LOAD(run,"retro_run");LOAD(fault,"arc_rpc_test_fault_next");
    LOAD(set_audio,"retro_set_audio_sample_batch");LOAD(set_environment,"retro_set_environment");
    set_audio(silence);set_environment(environment);
    /* 50 Hz, 60 Hz and a fractional rate all exercise post-fault output.
     * Repeat each fresh load to prove the carry is session-owned. */
    for(unsigned pass=0;pass<2;++pass)for(unsigned scenario=0;scenario<4;++scenario) {
        unsigned index=scenario==0?0:scenario==1?1:24;
        start(model,scenario==3?&rpc_slow_guests[model]:&rpc_video_guests[model][index]);
        for(unsigned i=0;!status().fields && i<200;++i)run();
        ArcRpcVideoStatus v=status();assert(v.fields && v.refresh_hz>0);
        if(scenario==3)assert(48000.0/v.refresh_hz>8192);
        fault();silence_pairs=0;
        for(unsigned i=0;i<17;++i) {
            run();assert(api->faulted(NULL));
            double expected=(i+1)*48000.0/v.refresh_hz;
            assert(fabs((double)silence_pairs-expected)<1.000001);
        }
        api->stop();
    }
    set_audio(NULL);set_environment(NULL);
    puts("post-fault silence: last announced rate, fractional carry and fresh-load reset passed");
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
    if(!strcmp(argv[2],"--paths"))paths(model);
    else if(!strcmp(argv[2],"--extreme"))extreme(model);
    else if(!strcmp(argv[2],"--pixel-clock"))pixel_clock(model);
    else if(!strcmp(argv[2],"--fault-silence"))fault_silence(model);
    else if(!strcmp(argv[2],"--scroll"))scroll(model);
    else if(!strcmp(argv[2],"--frontend"))frontend(model);
    else if(!strcmp(argv[2],"--contention"))contention(model);
    else {
        for(unsigned i=0;i<32;++i)if(i!=27&&i!=28)check(model,&rpc_video_guests[model][i]);
        /* Presentation is allocated lazily inside the protected boundary. */
        fail_alloc(model==3?4:5);start(model,&rpc_video_guests[model][0]);
        ArcEngineFrame f;unsigned attempts=0;
        while(api->run_frame(&input,&f))assert(++attempts<=16);
        assert(api->faulted(NULL));api->stop();fail_alloc(0);
    }
#ifdef _WIN32
    FreeLibrary(library);
#else
    assert(!dlclose(library));
#endif
    return 0;
}
