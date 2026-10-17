/* Authored-ROM audio/input/factory checks through the real test core. GPL-2.0-only. */
#include "rpc_session.h"
#include "rpc_d2_rom.h"
#include "rpc_d2_expected.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
static HMODULE library;
static void *symbol(const char *s) { return (void *)GetProcAddress(library,s); }
#else
#include <dlfcn.h>
static void *library;
static void *symbol(const char *s) { return dlsym(library,s); }
#endif
#define LOAD(var,name) do { void *p=symbol(name);assert(p);memcpy(&var,&p,sizeof(var)); } while(0)
static const char *(*reference_status)(uint64_t *,uint64_t *);
static void reference_check(void)
{
    if(reference_status){
        uint64_t calls,failed;const char *field=reference_status(&calls,&failed);
        if(field || failed){fprintf(stderr,"RPC cross-check mismatch: %s (%llu/%llu)\n",
            field?field:"unnamed",(unsigned long long)failed,(unsigned long long)calls);abort();}
    }
}
static bool (*admit_frontend)(void);
static bool (*start_rpc)(ArcRpcModel,const uint8_t *,size_t,const uint8_t *,uint64_t,char *,size_t);
static bool (*factory_rpc)(ArcRpcModel,const uint8_t *,size_t,uint64_t,char *,size_t);
static bool (*advance_raw)(uint64_t);
static bool advance_rpc(uint64_t ns)
{ bool ok=advance_raw(ns);reference_check();return ok; }
static void (*snapshot_rpc)(ArcRpcTestSnapshot *);
static bool (*read_rpc)(unsigned,uint32_t *);
static void (*counts_rpc)(ArcRpcAudioCounts *);
static size_t (*peek_rpc)(int16_t *,size_t);
static void (*commit_rpc)(size_t,size_t);
static void (*capture_rpc)(ArcRpcCapture *);
static void (*cmos_rpc)(uint8_t *);
static void (*fault_next)(void);
static const ArcEngineBackendOps *(*engine_api)(void);
static const ArcEngineBackendOps *api;
static void (*run_retro_raw)(void),(*init_retro)(void),(*deinit_retro)(void),(*reset_retro)(void);
static void run_retro(void) { run_retro_raw();reference_check(); }
static void (*set_environment)(retro_environment_t),(*set_audio)(retro_audio_sample_batch_t);
static void (*set_poll)(retro_input_poll_t),(*set_state)(retro_input_state_t);
static void (*set_port)(unsigned,unsigned);
static uint8_t rom[2097152],seed[256];
static char error[256];
static ArcInputState input;
static unsigned logs,accept_mode,audio_calls,polled,press_pad;
static uint64_t accepted_pcm_hash,poll_ns;
static bool map_a,options_update,keyboard_down;
static int mouse_dx,mouse_dy;
static unsigned mouse_buttons;
static ArcRpcTestSnapshot snapshot(void) { ArcRpcTestSnapshot s;snapshot_rpc(&s);return s; }
static uint32_t ram(unsigned offset) { uint32_t value;assert(read_rpc(0x20000+offset,&value));return value; }
static ArcRpcAudioCounts counts(void)
{
    ArcRpcAudioCounts c;counts_rpc(&c);
    assert(c.generated==c.accepted+c.discarded+c.queued&&c.queued<=ARC_RPC_AUDIO_QUEUE);return c;
}
static uint64_t hash(const int16_t *pcm,size_t n)
{
    uint64_t h=UINT64_C(14695981039346656037);
    for(size_t i=0;i<n*2;++i)for(unsigned j=0;j<2;++j) {
        h^=((uint16_t)pcm[i]>>(8*j))&255;h*=UINT64_C(1099511628211);
    }
    return h;
}
static void log_message(void *p,const char *s)
{ (void)p;assert(strstr(s,"8-bit compatibility"));++logs; }
static void load_words(const uint32_t *words,size_t count)
{
    assert(count<=sizeof(rom)/4);
    for(size_t offset=0;offset<sizeof(rom);offset+=4) {
        size_t i=offset/4;
        uint32_t w=i<count?words[i]:0xeafffffe;
        rom[offset]=(uint8_t)w;rom[offset+1]=(uint8_t)(w>>8);
        rom[offset+2]=(uint8_t)(w>>16);rom[offset+3]=(uint8_t)(w>>24);
    }
}
#define GUEST(kind,m) (const uint32_t *[]){rpc_d2_##kind##_0,rpc_d2_##kind##_1,rpc_d2_##kind##_2,rpc_d2_##kind##_3}[m]
#define SIZE(kind,m) (size_t[]){sizeof(rpc_d2_##kind##_0),sizeof(rpc_d2_##kind##_1),sizeof(rpc_d2_##kind##_2),sizeof(rpc_d2_##kind##_3)}[m]/4
static void start(unsigned model,const uint32_t *words,size_t count)
{
    load_words(words,count);assert(start_rpc((ArcRpcModel)model,rom,sizeof(rom),seed,0,error,sizeof(error)));
    assert(admit_frontend());
    arc_input_reset(&input);logs=0;api->set_log_callback(log_message,NULL);
    uint8_t actual[256];cmos_rpc(actual);assert(!memcmp(actual,seed,256));
}
static void frame(void) { ArcEngineFrame f;assert(api->run_frame(&input,&f));reference_check();assert(!api->faulted(NULL)); }
static void frames(unsigned n) { for(unsigned i=0;i<n;++i)frame(); }
static void until(uint64_t ns)
{
    while(snapshot().now_ns<ns) { uint64_t delta=ns-snapshot().now_ns;if(delta>16666667)delta=16666667;assert(advance_rpc(delta)); }
}
static void check_pcm(unsigned model,bool compat)
{
    start(model,compat?GUEST(compat,model):GUEST(audio,model),compat?SIZE(compat,model):SIZE(audio,model));
    assert(advance_rpc(10000000));
    const uint32_t *expected_audio[]={rpc_expected_audio_0,rpc_expected_audio_1,rpc_expected_audio_2,rpc_expected_audio_3};
    const uint32_t *expected_compat[]={rpc_expected_compat_0,rpc_expected_compat_1,rpc_expected_compat_2,rpc_expected_compat_3};
    const unsigned *expected_dma[]={rpc_expected_audio_0_dma,rpc_expected_audio_1_dma,rpc_expected_audio_2_dma,rpc_expected_audio_3_dma};
    const unsigned *compat_dma[]={rpc_expected_compat_0_dma,rpc_expected_compat_1_dma,rpc_expected_compat_2_dma,rpc_expected_compat_3_dma};
    int16_t pcm[960];size_t n=peek_rpc(pcm,480);assert(n==480);
    const uint32_t *expected=compat?expected_compat[model]:expected_audio[model];
    for(size_t i=0;i<n;++i) {
        uint32_t actual=(uint16_t)pcm[2*i]|((uint32_t)(uint16_t)pcm[2*i+1]<<16);
        if(actual!=expected[i])fprintf(stderr,"PCM model=%u compat=%u at=%zu actual=%08x expected=%08x\n",model,compat,i,actual,expected[i]);
        assert(actual==expected[i]);
    }
    commit_rpc(n,n);ArcRpcAudioCounts c=counts();const unsigned *dma=compat?compat_dma[model]:expected_dma[model];
    if(c.dma_frames!=dma[0]||c.dma_blocks!=dma[1])fprintf(stderr,"DMA actual=%llu/%llu expected=%u/%u\n",(unsigned long long)c.dma_frames,(unsigned long long)c.dma_blocks,dma[0],dma[1]);
    assert(c.generated==480&&c.accepted==480&&!c.discarded&&!c.queued&&c.dma_frames==dma[0]&&c.dma_blocks==dma[1]);
    printf("%s model=%u generated=480 accepted=480 discarded=0 queued=0 dma=%llu/%llu hash=%016llx\n",compat?"compat":"pcm",model,(unsigned long long)c.dma_frames,(unsigned long long)c.dma_blocks,(unsigned long long)hash(pcm,n));
    frame();frame();assert(logs==1); /* once, including later re-enable */
    /* The unchanged delay loops include uncached ROM service now. Both initial
     * banks have completed before the first status read (§8 contract delta). */
    assert(ram(16)==0xa5 && ram(24)==6 && ram(28)==6 && ram(32)==16);
    api->stop();assert(!counts().queued);
}
static void dma_state(unsigned model)
{
    const uint64_t end[]={rpc_expected_dma_end_0,rpc_expected_dma_end_1,rpc_expected_dma_end_2,rpc_expected_dma_end_3};
    start(model,GUEST(dma,model),SIZE(dma,model));until(end[model]);
    const uint8_t *states[]={rpc_expected_dma_states_0,rpc_expected_dma_states_1,rpc_expected_dma_states_2,rpc_expected_dma_states_3};
    const unsigned *expected_counts[]={rpc_expected_dma_counts_0,rpc_expected_dma_counts_1,rpc_expected_dma_counts_2,rpc_expected_dma_counts_3};
    assert(ram(100)==0xa5);
    for(unsigned i=0;i<16;++i) {
        if(ram(i*4)!=states[model][i])fprintf(stderr,"dma-state model=%u index=%u actual=%u expected=%u\n",model,i,ram(i*4),states[model][i]);
        assert(ram(i*4)==states[model][i]);
    }
    ArcRpcAudioCounts c=counts();assert(c.dma_blocks==expected_counts[model][0]&&c.dma_frames==expected_counts[model][1]);
    fputs("dma-state=",stdout);for(unsigned i=0;i<16;++i)printf("%s%02x",i?",":"",states[model][i]);
    printf(" quads=%llu pairs=%llu wrap=4k\n",(unsigned long long)c.dma_blocks,(unsigned long long)c.dma_frames);
    api->stop();
}
static bool environment(unsigned cmd,void *data)
{
    if(cmd==RETRO_ENVIRONMENT_GET_INPUT_DEVICE_CAPABILITIES) {
        *(uint64_t *)data=UINT64_C(1)<<RETRO_DEVICE_KEYBOARD;return true;
    }
    if(cmd==RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE) { *(bool *)data=options_update;options_update=false;return true; }
    if(cmd==RETRO_ENVIRONMENT_GET_VARIABLE) {
        struct retro_variable *v=data;
        if(map_a&&!strcmp(v->key,"arcdroid_pad1_a")) { v->value="a";return true; }
        return false;
    }
    if(cmd==RETRO_ENVIRONMENT_GET_CAN_DUPE) { *(bool *)data=true;return true; }
    return false;
}
static void poll(void) { ++polled;poll_ns=snapshot().now_ns; }
static int16_t input_state(unsigned port,unsigned device,unsigned index,unsigned id)
{
    (void)index;assert(snapshot().now_ns==poll_ns);
    if(port==0&&device==RETRO_DEVICE_KEYBOARD&&id==RETROK_a)return keyboard_down;
    if(port==1&&device==RETRO_DEVICE_MOUSE) {
        if(id==RETRO_DEVICE_ID_MOUSE_X) { int value=mouse_dx;mouse_dx=0;return (int16_t)value; }
        if(id==RETRO_DEVICE_ID_MOUSE_Y) { int value=mouse_dy;mouse_dy=0;return (int16_t)value; }
        if(id==RETRO_DEVICE_ID_MOUSE_LEFT)return (mouse_buttons&1)!=0;
    }
    return port==0&&device==RETRO_DEVICE_JOYPAD&&id==RETRO_DEVICE_ID_JOYPAD_A&&press_pad;
}
static size_t audio_callback(const int16_t *pcm,size_t n)
{
    ++audio_calls;
    size_t result=accept_mode==1?0:accept_mode==2?n/3:accept_mode==3?n+17:n;
    size_t accepted=result<n?result:n;
    for(size_t i=0;i<accepted*2;++i)for(unsigned b=0;b<2;++b) {
        accepted_pcm_hash^=((uint16_t)pcm[i]>>(8*b))&255;
        accepted_pcm_hash*=UINT64_C(1099511628211);
    }
    if(accept_mode==4)for(size_t i=0;i<n*2;++i)assert(!pcm[i]);
    return result;
}
static void frontend_setup(void)
{ set_environment(environment);init_retro();set_poll(poll);set_state(input_state);set_audio(audio_callback);accept_mode=0;press_pad=0;audio_calls=polled=0;map_a=options_update=keyboard_down=false;mouse_dx=mouse_dy=0;mouse_buttons=0; }
static void pressure(unsigned model)
{
    frontend_setup();start(model,GUEST(audio,model),SIZE(audio,model));
    for(unsigned mode=1;mode<=3;++mode) { accept_mode=mode==3?0:mode;run_retro(); }
    ArcRpcAudioCounts c=counts();assert(c.generated==2400&&c.accepted==1066&&c.discarded==1334&&!c.queued);
    uint64_t executed=snapshot().executed,now=snapshot().now_ns;
    /* Same guest chronology with full acceptance has identical DMA and CPU. */
    uint64_t dma=c.dma_frames,blocks=c.dma_blocks;
    start(model,GUEST(audio,model),SIZE(audio,model));accept_mode=0;accepted_pcm_hash=UINT64_C(14695981039346656037);
    for(unsigned i=0;i<3;++i)run_retro();
    c=counts();assert(c.generated==2400&&c.accepted==2400&&!c.discarded&&c.dma_frames==dma&&c.dma_blocks==blocks);
    assert(snapshot().executed==executed&&snapshot().now_ns==now);
    printf("pressure generated=2400 accepted=1066 discarded=1334 queued=0 recovery=800\n");
    const uint64_t expected_hash[]={rpc_expected_frontend_0,rpc_expected_frontend_1,rpc_expected_frontend_2,rpc_expected_frontend_3};
    assert(accepted_pcm_hash==expected_hash[model]);
    printf("frontend generated=2400 accepted=2400 hash=%016llx\n",(unsigned long long)accepted_pcm_hash);
    /* Out-of-contract overaccept is clamped; absent callback discards offer. */
    accept_mode=3;run_retro();c=counts();assert(c.accepted==3200&&!c.discarded);
    set_audio(NULL);run_retro();c=counts();assert(c.generated==4000&&c.discarded==800&&!c.queued);set_audio(audio_callback);
    /* Independently bounded queue overflow, repeated peek, and recovery. */
    start(model,GUEST(audio,model),SIZE(audio,model));until(200000000);
    c=counts();assert(c.generated==9600&&c.discarded==1408&&c.queued==8192);
    int16_t queue[ARC_RPC_AUDIO_QUEUE*2];assert(peek_rpc(queue,ARC_RPC_AUDIO_QUEUE)==8192);
    assert(peek_rpc(queue,ARC_RPC_AUDIO_QUEUE)==8192&&counts().queued==8192);
    commit_rpc(8192,8192);assert(!counts().queued&&counts().accepted==8192);
    printf("overflow generated=9600 discarded=1408 queued=8192 recovered=8192\n");
    start(model,GUEST(audio,model),SIZE(audio,model));assert(advance_rpc(1000000));
    commit_rpc(48,48); /* retain the earlier missing-callback accounting below */
    set_audio(NULL);run_retro();set_audio(audio_callback);
    /* Unread generated PCM purged before post-fault silence, no guest resumes. */
    assert(advance_rpc(1000000));assert(counts().queued==48);fault_next();accept_mode=4;run_retro();
    c=counts();assert(api->faulted(NULL)&&!c.queued&&c.discarded==848);
    uint64_t frozen=snapshot().executed;uint64_t before=c.accepted;run_retro();
    assert(snapshot().executed==frozen&&counts().accepted==before+800);
    assert(!api->reset(error,sizeof(error)));api->stop();api->stop();assert(!counts().queued);
    /* Reset purges without erasing accounting, and can recover a healthy session. */
    start(model,GUEST(audio,model),SIZE(audio,model));assert(advance_rpc(1000000));
    assert(counts().queued==48);assert(api->reset(error,sizeof(error)));assert(!counts().queued&&counts().discarded==48);
    assert(advance_rpc(1000000));api->stop();assert(!counts().queued&&counts().discarded==96);
    printf("fault silence=800 frozen=1 purge=1 reset=1 stop=1\n");
    deinit_retro();
}
static void bytes_at(unsigned offset,const uint8_t *expected,size_t n)
{
    for(size_t i=0;i<n;++i) {
        uint32_t value=ram(offset+(unsigned)i*4);
        if(value!=expected[i])fprintf(stderr,"guest byte offset=%u i=%zu actual=%02x expected=%02x\n",offset,i,value,expected[i]);
        assert(value==expected[i]);
    }
}
static void keyboard(unsigned model,bool param)
{
    start(model,param?GUEST(typematic,model):GUEST(input,model),param?SIZE(typematic,model):SIZE(input,model));
    arc_input_source_key(&input,ARC_INPUT_SOURCE_KEYBOARD,RETROK_a,true,true);
    arc_input_source_key(&input,ARC_INPUT_SOURCE_PAD0,RETROK_a,true,true);
    unsigned hold=param?30:38;frames(hold);
    arc_input_clear_source(&input,ARC_INPUT_SOURCE_KEYBOARD);frames(1);
    unsigned before=ram(0);arc_input_clear_source(&input,ARC_INPUT_SOURCE_PAD0);frames(3);
    const uint8_t standard[]={0x1c,0x1c,0x1c,0xf0,0x1c};
    const uint8_t fast[]={0xfa,0xfa,0x1c,0x1c,0x1c,0x1c,0x1c,0x1c,0x1c,0x1c,0x1c,0xf0,0x1c};
    const uint8_t *expected=param?fast:standard;size_t n=param?sizeof(fast):sizeof(standard);
    if(ram(0)!=n)fprintf(stderr,"keyboard model=%u fast=%u actual=%u expected=%zu before=%u\n",model,param,ram(0),n,before);
    assert(ram(0)==n&&before==n-2);bytes_at(256,expected,n);
    /* Held levels and repeat state retire on engine reset, without seed changes. */
    arc_input_source_key(&input,ARC_INPUT_SOURCE_KEYBOARD,RETROK_a,true,true);frames(2);
    assert(api->reset(error,sizeof(error)));frame();assert(!input.fifo_count&&!input.effective_keys[RETROK_a]);
    frames(model>=2?36:2);unsigned base=param?2:0;assert(ram(0)==base);uint8_t cmos[256];cmos_rpc(cmos);assert(!memcmp(cmos,seed,256));
    printf(param?"typematic param=00 make=1c repeat=1c,1c,1c,1c,1c,1c,1c,1c break=f0,1c\n":"keyboard make=1c repeat=1c,1c break=f0,1c shared=held reset=released\n");
    api->stop();
}
static void pad_mouse(unsigned model)
{
    frontend_setup();start(model,GUEST(input,model),SIZE(input,model));
    run_retro();press_pad=1;for(unsigned i=0;i<2;++i)run_retro();
    assert(ram(0)==1&&ram(256)==0x29&&polled==3);
    set_port(0,RETRO_DEVICE_NONE);press_pad=0;for(unsigned i=0;i<3;++i)run_retro();
    const uint8_t pad[]={0x29,0xf0,0x29};assert(ram(0)==3);bytes_at(256,pad,3);
    set_port(0,RETRO_DEVICE_JOYPAD);press_pad=0;run_retro();press_pad=1;for(unsigned i=0;i<2;++i)run_retro();
    map_a=options_update=true;for(unsigned i=0;i<3;++i)run_retro();
    press_pad=0;run_retro();press_pad=1;for(unsigned i=0;i<2;++i)run_retro();
    keyboard_down=true;run_retro();set_port(0,RETRO_DEVICE_NONE);press_pad=0;
    for(unsigned i=0;i<2;++i)run_retro();
    if(ram(0)!=7) { fprintf(stderr,"remap bytes=%u: ",ram(0));for(unsigned i=0;i<ram(0);++i)fprintf(stderr,"%02x,",ram(256+4*i));fprintf(stderr,"\n"); }
    assert(ram(0)==7);
    keyboard_down=false;for(unsigned i=0;i<3;++i)run_retro();
    const uint8_t remap[]={0x29,0xf0,0x29,0x29,0xf0,0x29,0x1c,0xf0,0x1c};assert(ram(0)==9);bytes_at(256,remap,9);
    deinit_retro();start(model,GUEST(input,model),SIZE(input,model));
    arc_input_source_buttons(&input,ARC_INPUT_SOURCE_MOUSE0,1);arc_input_motion(&input,12,-7,false);frames(4);
    if(model!=3)assert(ram(8)==12&&ram(12)==7&&ram(20)==0x30);
    arc_input_clear_source(&input,ARC_INPUT_SOURCE_MOUSE0);frames(3);
    if(model==3) {
        const uint8_t mouse[]={0xfa,0x09,0,0,0x09,12,7,0x08,0,0};assert(ram(4)==sizeof(mouse));bytes_at(512,mouse,sizeof(mouse));
    }else assert(ram(20)==0x70);
    printf("pad space=29,f0,29 mouse=%s\n",model==3?"ps2=fa,09,00,00,09,0c,07,08,00,00":"quadrature=12,7 buttons=30,70");
    arc_input_source_buttons(&input,ARC_INPUT_SOURCE_MOUSE0,1);
    arc_input_source_buttons(&input,ARC_INPUT_SOURCE_MOUSE1,6);frames(4);
    if(model!=3)assert(ram(20)==0);
    arc_input_clear_source(&input,ARC_INPUT_SOURCE_MOUSE0);frames(2);
    if(model!=3)assert(ram(20)==0x40);
    arc_input_clear_source(&input,ARC_INPUT_SOURCE_MOUSE1);frames(3);
    arc_input_motion(&input,-130,130,false);frames(4);
    if(model==3) {
        const uint8_t more[]={0x09,0,0,0x0b,0,0,0x0f,0,0,0x0e,0,0,0x0c,0,0,0x08,0,0,
            0x38,0xc1,0xc1,0x38,0xc1,0xc1,0x38,0xfc,0xfc};
        assert(ram(4)==37);bytes_at(512+40,more,sizeof(more));
    }else assert(ram(8)==65418&&ram(12)==65413&&ram(20)==0x70);
    printf("buttons all=09,0b,0f retire=0e,0c,08 motion=-130,130 split=63,63,4\n");
    api->stop();
    frontend_setup();start(model,GUEST(input,model),SIZE(input,model));
    mouse_dx=12;mouse_dy=-7;mouse_buttons=1;for(unsigned i=0;i<4;++i)run_retro();
    if(model!=3)assert(ram(8)==12&&ram(12)==7&&ram(20)==0x30);
    mouse_buttons=0;for(unsigned i=0;i<3;++i)run_retro();
    if(model==3) {
        const uint8_t mouse[]={0xfa,0x08,12,7,0x09,0,0,0x08,0,0};assert(ram(4)==10);bytes_at(512,mouse,10);
    }else assert(ram(20)==0x70);
    printf("frontend-input remap=29,f0,29,1c shared=held keyboard=f0,1c mouse=%s\n",
        model==3?"fa,08,0c,07,09,00,00,08,00,00":"12,7,30,70");
    keyboard_down=true;for(unsigned i=0;i<2;++i)run_retro();
    assert(ram(0)==1&&ram(256)==0x1c);
    reset_retro();keyboard_down=false;for(unsigned i=0;i<3;++i)run_retro();
    assert(!api->faulted(NULL)&&ram(0)==0);
    deinit_retro();
}
static void factory(unsigned model)
{
    const uint32_t *words=model==2?rpc_d2_factory_2:rpc_d2_factory_3;
    size_t n=model==2?sizeof(rpc_d2_factory_2)/4:sizeof(rpc_d2_factory_3)/4;
    load_words(words,n);assert(factory_rpc((ArcRpcModel)model,rom,sizeof(rom),0,error,sizeof(error)));
    ArcRpcCapture c;capture_rpc(&c);assert(c.state==ARC_RPC_CAPTURE_RUNNING&&c.delete_held&&!c.elapsed_ns&&!c.writes);
    until(UINT64_C(11999999999));capture_rpc(&c);assert(c.delete_held&&c.writes==256);
    assert(advance_rpc(1));capture_rpc(&c);assert(!c.delete_held&&c.elapsed_ns==UINT64_C(12000000000));
    until(UINT64_C(30000000000));capture_rpc(&c);assert(c.state==ARC_RPC_CAPTURE_DONE&&c.writes==256&&!c.delete_held);
    for(unsigned i=0;i<256;++i)assert(c.bytes[i]==(uint8_t)(i^0x5a));
    for(unsigned i=0;i<32;++i)assert(c.written[i]==255);
    unsigned received=ram(0);assert(received>3&&ram(256+(received-2)*4)==0xf0&&ram(256+(received-1)*4)==0x71);
    uint64_t frozen=snapshot().executed;assert(advance_rpc(1000000));assert(snapshot().executed==frozen&&snapshot().now_ns==UINT64_C(30000000000));
    printf("factory model=%u time=30000000000 delete=0 writes=256 mask=",model);for(unsigned i=0;i<32;++i)printf("%02x",c.written[i]);
    printf("\nbytes=");for(unsigned i=0;i<256;++i)printf("%02x",c.bytes[i]);puts("");
    assert(api->reset(error,sizeof(error)));uint8_t retained[256];cmos_rpc(retained);assert(!memcmp(retained,c.bytes,256));assert(advance_rpc(1000000));assert(snapshot().executed>0);
    capture_rpc(&c);assert(c.state==ARC_RPC_CAPTURE_CANCELLED&&!c.delete_held);api->stop();
    words=model==2?rpc_d2_empty_2:rpc_d2_empty_3;n=model==2?sizeof(rpc_d2_empty_2)/4:sizeof(rpc_d2_empty_3)/4;
    load_words(words,n);assert(factory_rpc((ArcRpcModel)model,rom,sizeof(rom),0,error,sizeof(error)));
    until(UINT64_C(29999999999));assert(!advance_rpc(1));capture_rpc(&c);
    assert(c.state==ARC_RPC_CAPTURE_FAILED&&!c.writes&&!counts().queued&&api->faulted(NULL));
    assert(strstr(error,"no guest writes")==NULL);const char *fault;assert(api->faulted(&fault)&&strstr(fault,"no guest writes"));
    printf("empty time=30000000000 fault=1 writes=0\n");api->stop();
}
int main(int argc,char **argv)
{
    assert(argc==4);unsigned model=(unsigned)strtoul(argv[3],NULL,10);assert(model<4);
#ifdef _WIN32
    library=LoadLibraryA(argv[1]);
#else
    library=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
#endif
    assert(library);
    void *reference=symbol("arc_rpc_crosscheck_status");
    if(reference)memcpy(&reference_status,&reference,sizeof(reference_status));

    LOAD(admit_frontend,"arc_libretro_test_rpc_admit");LOAD(start_rpc,"arc_rpc_test_start");LOAD(factory_rpc,"arc_rpc_test_factory_start");LOAD(advance_raw,"arc_rpc_test_advance");
    LOAD(snapshot_rpc,"arc_rpc_test_snapshot");LOAD(read_rpc,"arc_rpc_test_read_ram");LOAD(counts_rpc,"arc_rpc_test_audio_counts");
    LOAD(peek_rpc,"arc_rpc_test_audio_peek");LOAD(commit_rpc,"arc_rpc_test_audio_commit");LOAD(capture_rpc,"arc_rpc_test_capture_status");
    LOAD(cmos_rpc,"arc_rpc_test_cmos_snapshot");LOAD(fault_next,"arc_rpc_test_fault_next");LOAD(engine_api,"arc_rpc_test_engine_api");api=engine_api();
    LOAD(set_environment,"retro_set_environment");LOAD(set_audio,"retro_set_audio_sample_batch");LOAD(set_poll,"retro_set_input_poll");LOAD(set_state,"retro_set_input_state");
    LOAD(set_port,"retro_set_controller_port_device");LOAD(run_retro_raw,"retro_run");LOAD(init_retro,"retro_init");LOAD(deinit_retro,"retro_deinit");LOAD(reset_retro,"retro_reset");
    for(unsigned i=0;i<256;++i)seed[i]=(uint8_t)(i^0xa5);
    if(!strcmp(argv[2],"--factory")) { assert(model>=2);factory(model); }
    else { assert(!strcmp(argv[2],"--guest"));check_pcm(model,false);check_pcm(model,true);dma_state(model);pressure(model);keyboard(model,false);keyboard(model,true);pad_mouse(model); }
    api->stop();
#ifdef _WIN32
    FreeLibrary(library);
#else
    dlclose(library);
#endif
    return 0;
}
