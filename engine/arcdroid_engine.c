/* ArcDroid session dispatcher. GPL-2.0-only. */
#include "engine_backend.h"
extern const ArcEngineBackendOps arc_rpc_backend;
#include "rpc/rpc_session.h"
#include "rpc/rpc_floppy.h"
#include "rpc/rpc_ide.h"
#include "rpc/rpc_rtc.h"
#include "rpc/rpc_observers.h"
#include "arc_io.h"
#include "rpc_selection.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#if defined(ARCDROID_TEST_FAULTS)
#include "engine_test.h"
static uint64_t arc_test_frame_ns, arc_test_callback_ns;
static bool arc_test_measuring;
static ArcEngineLogCallback arc_test_log;
static void *arc_test_log_opaque;
static void arc_test_log_proxy(void *opaque, const char *message)
{
    (void)opaque;
    uint64_t start = arc_test_clock_ns();
    if (arc_test_log) arc_test_log(arc_test_log_opaque, message);
    if (arc_test_measuring) arc_test_callback_ns += arc_test_clock_ns() - start;
}
RETRO_API uint64_t arc_engine_test_frame_ns(void) { return arc_test_frame_ns; }
void arc_engine_test_frame_begin(void) { arc_test_frame_ns=0; }
void arc_engine_test_frame_add_ns(uint64_t elapsed) { arc_test_frame_ns += elapsed; }
RETRO_API bool arc_engine_test_ide_backend(const ArcIdeBackend *backend)
{ return arc_engine_rpc_ide(backend); }
RETRO_API bool arc_rpc_test_start(ArcRpcModel model, const uint8_t *rom, size_t size,
    const uint8_t seed[256], uint64_t epoch, char *error, size_t error_size);
#endif

/* Shipping launches retain the existing Arculator path. The operations
 * pointer is also valid before construction and after teardown, keeping
 * observer snapshots and failed-start cleanup identical to the baseline. */
static const ArcEngineBackendOps *arc_backend = &arc_arculator_backend;
/* Explicit stop masks queries without rebinding baseline snapshot operations. */
static bool arc_lifecycle;

static ArcEngineMedia rpc_helper;
static char rpc_helper_path[1024];
static uint64_t rpc_identity_epoch;
static ArcIdeBackend rpc_identity_ide;
static char rpc_helper_name[11],rpc_hd_name[11];
static uint8_t rpc_hd_record_name[10];
static bool rpc_helper_known=true,rpc_hd_known=true;
static const uint32_t rpc_map_lba=((2u*(512u*8u-32u)-480u)<<13)/512u;
static void rpc_helper_derive(void);
static void rpc_hd_derive(void);
static void rpc_identity_clear(void)
{
    rpc_helper=(ArcEngineMedia){0};rpc_identity_ide=(ArcIdeBackend){0};
    rpc_identity_epoch=0;rpc_helper_path[0]=rpc_helper_name[0]=rpc_hd_name[0]=0;
    rpc_helper_known=rpc_hd_known=true;
}
bool arc_engine_is_rpc(void) { return arc_lifecycle && arc_backend == &arc_rpc_backend; }

uint64_t arc_engine_session_generation(void)
{ return arc_backend->session_generation ? arc_backend->session_generation() : 0; }
size_t arc_engine_audio_peek(int16_t *output, size_t capacity)
{ return arc_backend->audio_peek ? arc_backend->audio_peek(output, capacity) : 0; }
void arc_engine_audio_commit(size_t offered, size_t accepted)
{ if (arc_backend->audio_commit) arc_backend->audio_commit(offered, accepted); }
void arc_engine_audio_fault_silence(size_t frames)
{ if (arc_backend->audio_fault_silence) arc_backend->audio_fault_silence(frames); }

ArcEngineCapabilities arc_engine_capabilities(void)
{
   return arc_lifecycle ? arc_backend->capabilities() : (ArcEngineCapabilities){0};
}

bool arc_engine_start(const uint8_t *firmware, size_t firmware_size,
                      const ArcEngineMedia *media,
                      const ArcEngineMedia *helper,
                      const ArcMachineDescriptor *machine,
                      ArcOsClass os,
                      const ArcIdeBackend *ide_backend,
                      const uint8_t cmos[ARC_CMOS_MACHINE_SIZE],
                      const uint8_t locked_raw_mask[32],
                      ArcEngineCmosIgnoredCallback ignored_callback,
                      void *ignored_opaque,
                      char *error, size_t error_size)
{
   if (arc_backend != &arc_arculator_backend) {
      arc_backend->stop();
      arc_backend = &arc_arculator_backend;
   }
   arc_lifecycle = true;
   return arc_backend->start(firmware, firmware_size, media, helper, machine, os,
      ide_backend, cmos, locked_raw_mask, ignored_callback, ignored_opaque,
      error, error_size);
}

void arc_engine_set_log_callback(ArcEngineLogCallback callback, void *opaque)
{
   arc_arculator_backend.set_log_callback(callback, opaque);
#if defined(ARCDROID_TEST_FAULTS)
   arc_test_log = callback; arc_test_log_opaque = opaque;
   arc_rpc_backend.set_log_callback(callback ? arc_test_log_proxy : NULL, NULL);
#else
   arc_rpc_backend.set_log_callback(callback, opaque);
#endif
}

bool arc_engine_reset(char *error, size_t error_size)
{
   bool ok=arc_backend->reset(error,error_size);
   if(arc_engine_is_rpc()){
      rpc_helper_known=rpc_hd_known=false;
      if(ok){rpc_identity_epoch=arc_rpc_observer_epoch();rpc_helper_derive();rpc_hd_derive();}
   }
   return ok;
}

void arc_engine_stop(void)
{
   arc_backend->stop();rpc_identity_clear();arc_lifecycle=false;
}

bool arc_engine_run_frame(ArcInputState *input, ArcEngineFrame *frame)
{
#if defined(ARCDROID_TEST_FAULTS)
   if (arc_engine_is_rpc()) {
      arc_test_callback_ns = 0; arc_test_measuring = true;
      uint64_t start = arc_test_clock_ns();
      bool result = arc_backend->run_frame(input, frame);
      arc_test_frame_ns = arc_test_clock_ns() - start - arc_test_callback_ns;
      arc_test_measuring = false;
      return result;
   }
#endif
   return arc_backend->run_frame(input, frame);
}

void arc_engine_power_on_arm(ArcInputState *input, bool armed)
{
   arc_backend->power_on_arm(input, armed);
}

bool arc_engine_power_on_take_release(uint64_t *at_us, bool *boot_action)
{
   return arc_backend->power_on_take_release(at_us, boot_action);
}

void arc_engine_boot_watch_arm(void)
{
   arc_backend->boot_watch_arm();
}

void arc_engine_boot_watch_cancel(void)
{
   arc_backend->boot_watch_cancel();
}

ArcBootWatchState arc_engine_boot_watch_state(void)
{
   return arc_backend->boot_watch_state();
}

bool arc_engine_launch_outcome_arm(const char *command)
{
   return arc_backend->launch_outcome_arm(command);
}

void arc_engine_launch_outcome_cancel(const char *reason)
{
   arc_backend->launch_outcome_cancel(reason);
}

void arc_engine_launch_outcome_refused(unsigned drive)
{
   arc_backend->launch_outcome_refused(drive);
}

void arc_engine_launch_outcome_snapshot(ArcLaunchOutcome *outcome)
{
   arc_backend->launch_outcome_snapshot(outcome);
}

void arc_engine_error_snapshot(ArcErrorBox *box)
{
   arc_backend->error_snapshot(box);
}

bool arc_engine_error_request(ArcErrorAnswer answer)
{
   return arc_backend->error_request(answer);
}

void arc_engine_error_discard(void)
{
   arc_backend->error_discard();
}

void arc_engine_disc_configure(bool enabled, const char *inserted_name,
   ArcDiscResolveCallback resolve, ArcDiscObservationCallback observe,
   void *opaque)
{
   arc_backend->disc_configure(enabled, inserted_name, resolve, observe, opaque);
}

void arc_engine_disc_clear(void)
{
   arc_backend->disc_clear();
}

void arc_engine_disc_snapshot(ArcDiscRequest *out)
{
   arc_backend->disc_snapshot(out);
}

void arc_engine_disc_a_notified(void)
{
   arc_backend->disc_a_notified();
}

uint64_t arc_engine_disc_time_us(void)
{
   return arc_backend->disc_time_us();
}

bool arc_engine_disc_controller_idle(void)
{
   return arc_backend->disc_controller_idle();
}

void arc_engine_adf_refusal_callback_set(
   void (*callback)(unsigned, unsigned, unsigned, unsigned))
{
   arc_arculator_backend.adf_refusal_callback_set(callback);
   arc_rpc_backend.adf_refusal_callback_set(callback);
}

size_t arc_engine_audio(int16_t *output, size_t capacity_frames)
{
   return arc_backend->audio(output, capacity_frames);
}

void arc_engine_audio_stats(uint64_t *underrun_frames, uint64_t *overrun_frames)
{
   arc_backend->audio_stats(underrun_frames, overrun_frames);
}

bool arc_engine_media_eject(void)
{
   return arc_backend->media_eject();
}

bool arc_engine_media_insert(const ArcEngineMedia *media,
                             char *error, size_t error_size)
{
   return arc_backend->media_insert(media, error, error_size);
}

bool arc_engine_get_av_info(unsigned *width, unsigned *height, double *refresh_hz)
{
   return arc_backend->get_av_info(width, height, refresh_hz);
}

void arc_engine_cmos(uint8_t output[ARC_CMOS_PAYLOAD_SIZE])
{
   arc_backend->cmos(output);
}

bool arc_engine_faulted(const char **message)
{
   if (!arc_lifecycle) { if (message) *message = NULL; return false; }
   return arc_backend->faulted(message);
}

bool arc_engine_active(void)
{
   return arc_backend->active();
}

bool arc_engine_rpc_start_begin(unsigned model,const uint8_t *rom,size_t size,
    const uint8_t seed[256],uint64_t epoch,bool factory,char *e,size_t n)
{
    rpc_identity_clear();arc_backend->stop();arc_backend=&arc_rpc_backend;arc_lifecycle=true;
    return arc_rpc_session_start_begin((ArcRpcModel)model,rom,size,seed,epoch,factory,e,n);
}
bool arc_engine_rpc_start_step(bool *done,char *e,size_t n)
{ return arc_engine_is_rpc() && arc_rpc_session_start_step(done,e,n); }
bool arc_engine_rpc_start(unsigned model, const uint8_t *rom, size_t size,
    const uint8_t seed[256], uint64_t epoch, bool factory, char *error, size_t n)
{
    rpc_identity_clear();arc_backend->stop(); arc_backend = &arc_rpc_backend;arc_lifecycle=true;
    bool ok = factory ? arc_rpc_session_factory_start((ArcRpcModel)model,rom,size,epoch,error,n)
                      : arc_rpc_session_start((ArcRpcModel)model,rom,size,seed,epoch,error,n);
    if (!ok) arc_backend->stop();
    return ok;
}
void arc_engine_rpc_observers_bind(unsigned catalogue)
{
    if(!arc_engine_is_rpc())return;
    ArcDigest digest;size_t count=0;unsigned actual=0;
    const ArcRpcFirmwareFingerprint *table=arc_riscpc_firmware_catalogue(&count);
    if(arc_rpc_session_firmware(&digest)){
        const ArcRpcFirmwareFingerprint *fingerprint=arc_riscpc_firmware_recognize(&digest,table,count);
        if(fingerprint)actual=(unsigned)fingerprint->os;
    }
    arc_rpc_observers_bind(catalogue==actual?actual:0,arc_rpc_session_cpu());
}
void arc_engine_rpc_disc_status(ArcRpcDiscStatus *out)
{ if(out){memset(out,0,sizeof(*out));if(arc_engine_is_rpc())arc_rpc_observers_disc_status(out);} }
bool arc_engine_rpc_observers_replace(ArcDiscSignal signal)
{ return arc_engine_is_rpc() && arc_rpc_observers_replace(signal); }
bool arc_engine_rpc_observers_capability(unsigned bit)
{ return arc_engine_is_rpc() && arc_rpc_observers_capability(bit); }
void arc_engine_rpc_diagnostics(void)
{ if(arc_engine_is_rpc())arc_rpc_observer_drain(); }
static bool rpc_identity_name_valid(const char *s)
{
    for(unsigned i=0;s[i];++i)
        if((unsigned char)s[i]<0x21 || (unsigned char)s[i]>0x7e || strchr(".:*/\\",s[i]))return false;
    return true;
}
static void rpc_helper_derive(void)
{
    rpc_helper_name[0]=0;rpc_helper_known=!rpc_helper.kind;
    if(!rpc_helper.kind || rpc_helper.writable)return;
    void *allocated=NULL;const uint8_t *data=rpc_helper.data;size_t size=rpc_helper.size;
    if(!data && !arc_io_read_all(rpc_helper.path,&allocated,&size))return;
    if(!data)data=allocated;
    ArcMediaInfo info={0};ArcGuestName name={0};
    bool valid=arc_media_inspect(rpc_helper.path?rpc_helper.path:"helper.adf",data,size,&info,NULL,0) &&
        arc_disc_read_name(data,size,&info,NULL,&name) && name.length>0 && name.length<=10;
    if(valid){memcpy(rpc_helper_name,name.bytes,name.length);rpc_helper_name[name.length]=0;}
    free(allocated);rpc_helper_known=valid && rpc_identity_name_valid(rpc_helper_name);
}
static void rpc_hd_derive(void)
{
    rpc_hd_name[0]=0;rpc_hd_known=!rpc_identity_ide.read;
    if(!rpc_identity_ide.read)return;
    uint8_t boot[512],map[512],backup[512];
    /* Authored 126 MiB format: all three records must agree. */
    if(!rpc_identity_ide.read(rpc_identity_ide.opaque,6,1,boot) ||
       !rpc_identity_ide.read(rpc_identity_ide.opaque,rpc_map_lba,1,map) ||
       !rpc_identity_ide.read(rpc_identity_ide.opaque,rpc_map_lba+5,1,backup))return;
    unsigned sum=0;for(unsigned i=0;i<511;++i)sum+=boot[i];
    while(sum>255)sum=(sum&255)+(sum>>8);
    if(sum!=boot[511] || boot[0x1c0]!=9 || boot[0x1c1]!=63 || boot[0x1c2]!=16 ||
       memcmp(boot+0x1c0+22,map+26,10) || memcmp(map+26,backup+26,10))return;
    memcpy(rpc_hd_name,boot+0x1c0+22,10);rpc_hd_name[10]=0;
    memcpy(rpc_hd_record_name,boot+0x1c0+22,10);
    size_t end=10;while(end && (rpc_hd_name[end-1]==' ' || !rpc_hd_name[end-1]))rpc_hd_name[--end]=0;
    rpc_hd_known=end && rpc_identity_name_valid(rpc_hd_name);
}
bool arc_rpc_observers_identities(char helper[11],char hd[11])
{
    helper[0]=hd[0]=0;
    if(rpc_identity_epoch!=arc_rpc_observer_epoch() || !rpc_helper_known || !rpc_hd_known)return false;
    memcpy(helper,rpc_helper_name,11);memcpy(hd,rpc_hd_name,11);
    if(helper[0] && hd[0]){
        unsigned i=0;bool same=true;
        while(helper[i] || hd[i]){unsigned a=(unsigned char)helper[i],b=(unsigned char)hd[i];
            if(a>='A' && a<='Z')a+='a'-'A';if(b>='A' && b<='Z')b+='a'-'A';
            if(a!=b){same=false;break;}++i;}
        if(same)return false;
    }
    return true;
}
static bool rpc_ide_read(void *opaque,uint32_t lba,uint32_t count,uint8_t *out)
{ (void)opaque;return rpc_identity_ide.read(rpc_identity_ide.opaque,lba,count,out); }
static bool rpc_ide_write(void *opaque,uint32_t lba,uint32_t count,const uint8_t *bytes)
{
    (void)opaque;
    uint64_t end=(uint64_t)lba+count;
    bool known=rpc_hd_known,unchanged=true;
    const uint32_t records[]={6,rpc_map_lba,rpc_map_lba+5};
    for(unsigned r=0;r<3;++r)if(lba<=records[r] && end>records[r]){
        const uint8_t *record=bytes+(size_t)(records[r]-lba)*512;
        unchanged=unchanged && !memcmp(record+(r?26:0x1c0+22),rpc_hd_record_name,10);
        if(!r){
            unsigned sum=0;for(unsigned i=0;i<511;++i)sum+=record[i];
            while(sum>255)sum=(sum&255)+(sum>>8);
            unchanged=unchanged && sum==record[511] && record[0x1c0]==9 &&
                record[0x1c1]==63 && record[0x1c2]==16;
        }
    }
    if(count && ((lba<=6 && end>6) || (lba<=rpc_map_lba && end>rpc_map_lba) ||
        (lba<=rpc_map_lba+5 && end>rpc_map_lba+5)))rpc_hd_known=false;
    bool ok=rpc_identity_ide.write(rpc_identity_ide.opaque,lba,count,bytes);
    /* A successful ordinary map update proves the cached identity unchanged.
     * Changed names or failed writes remain unknown until lifecycle refresh. */
    rpc_hd_known=ok && known && unchanged;
    return ok;
}
static void rpc_ide_failed(void *opaque,const char *message)
{
    (void)opaque;rpc_hd_known=false;
    rpc_identity_ide.failed(rpc_identity_ide.opaque,message);
}
static void rpc_ide_unsupported(void *opaque,uint8_t command)
{ (void)opaque;rpc_identity_ide.unsupported(rpc_identity_ide.opaque,command); }
bool arc_engine_rpc_ide(const ArcIdeBackend *ide)
{
    if (!arc_engine_is_rpc() || !arc_engine_active()) return false;
    rpc_identity_epoch=arc_rpc_observer_epoch();rpc_identity_ide=ide?*ide:(ArcIdeBackend){0};
    rpc_hd_derive();
    ArcIdeBackend bridge=rpc_identity_ide;
    bridge.opaque=NULL;
    bridge.read=bridge.read?rpc_ide_read:NULL;bridge.write=bridge.write?rpc_ide_write:NULL;
    bridge.failed=bridge.failed?rpc_ide_failed:NULL;
    bridge.unsupported=bridge.unsupported?rpc_ide_unsupported:NULL;
    arc_rpc_ide_set_backend(ide?&bridge:NULL); return true;
}
bool arc_engine_rpc_mount(unsigned drive,const ArcEngineMedia *media,char *e,size_t n)
{
    if(drive==1 && media && media->path && strlen(media->path)>=sizeof(rpc_helper_path)){
        if(e && n)snprintf(e,n,"RPC helper path is too long");return false;
    }
    bool ok = arc_engine_is_rpc() && arc_engine_active() && arc_rpc_fdc_mount(drive,media,e,n);
    if(drive==1){rpc_identity_epoch=arc_rpc_observer_epoch();rpc_helper=(ArcEngineMedia){0};
        if(ok && media){rpc_helper=*media;
            if(media->path){size_t length=strlen(media->path);
                memcpy(rpc_helper_path,media->path,length+1);rpc_helper.path=rpc_helper_path;}}
        rpc_helper_derive();
    }
    return ok;
}
void arc_engine_rpc_eject(unsigned drive)
{ if (arc_engine_is_rpc() && arc_engine_active()){arc_rpc_fdc_eject(drive);if(drive==1){rpc_helper=(ArcEngineMedia){0};rpc_helper_derive();}} }
bool arc_engine_rpc_attach(const ArcEngineMedia *media,const ArcEngineMedia *helper,
    const ArcIdeBackend *ide,char *e,size_t n)
{
    bool ok = (!media || arc_engine_rpc_mount(0,media,e,n)) &&
              (!helper || arc_engine_rpc_mount(1,helper,e,n)) && arc_engine_rpc_ide(ide);
    if (!ok) arc_engine_stop();
    return ok;
}
bool arc_engine_rpc_capture_schedule(uint64_t deadline,uint64_t release)
{ return arc_engine_is_rpc() && arc_rpc_session_capture_schedule(deadline,release); }
bool arc_engine_rpc_advance(uint64_t ns)
{
    if (!arc_engine_is_rpc()) return false;
    while (ns) {
        uint64_t step = ns < 16666667u ? ns : 16666667u;
        if (!arc_rpc_session_advance(step)) return false;
        ns -= step;
        int16_t audio[2048]; size_t n;
        while ((n=arc_engine_audio_peek(audio,1024))!=0) arc_engine_audio_commit(n,0);
        ArcRpcCapture c; arc_rpc_capture_status(&c);
        if (c.state != ARC_RPC_CAPTURE_RUNNING) break;
    }
    return true;
}
void arc_engine_rpc_evidence(uint8_t bytes[256],uint8_t mask[32],uint64_t *writes,
    uint64_t *last,uint64_t *now,unsigned *state)
{
    ArcRpcCapture c; arc_rpc_capture_status(&c); arc_rpc_cmos_snapshot(bytes);
    *writes=arc_rpc_cmos_payload_evidence(mask,last); *now=c.elapsed_ns; *state=(unsigned)c.state;
}

#if defined(ARCDROID_TEST_FAULTS)
bool arc_engine_test_cmos_slice(uint64_t ns)
{
    arc_test_callback_ns=0;arc_test_measuring=true;uint64_t start=arc_test_clock_ns();
    bool ok=arc_engine_rpc_advance(ns);
    arc_test_frame_ns=arc_test_clock_ns()-start-arc_test_callback_ns;arc_test_measuring=false;return ok;
}
bool arc_engine_test_cmos_schedule(uint64_t deadline_ns,uint64_t delete_ns)
{ return arc_engine_rpc_capture_schedule(deadline_ns,delete_ns); }
void arc_engine_test_cmos_evidence(uint8_t bytes[256],uint8_t mask[32],uint64_t *writes,uint64_t *last,uint64_t *now,unsigned *state)
{ arc_engine_rpc_evidence(bytes,mask,writes,last,now,state); }
RETRO_API bool arc_engine_test_floppy_mount(unsigned drive,const ArcEngineMedia *media,char *error,size_t size)
{ return arc_engine_rpc_mount(drive,media,error,size); }
RETRO_API void arc_engine_test_floppy_eject(unsigned drive)
{ arc_engine_rpc_eject(drive); }
void arc_engine_test_plan_input_ready(void)
{ if (arc_engine_is_rpc()) arc_rpc_session_test_input_ready(); }
void arc_engine_test_plan_rate(unsigned rate)
{ if(arc_engine_is_rpc() && rate)arc_rpc_session_test_rate(rate); }
bool arc_engine_test_plan_start(unsigned model, unsigned rate, bool factory,
    const uint8_t *rom, size_t size, const uint8_t seed[256], uint64_t epoch,
    char *error, size_t error_size)
{
    if (rate > 100) {
       if (error && error_size) snprintf(error, error_size, "Rate must be 1..100 MIPS");
       return false;
    }
    bool started = arc_engine_rpc_start(model,rom,size,seed,epoch,factory,error,error_size);
    if (started && rate) arc_rpc_session_test_rate(rate);
    arc_test_frame_ns = 0;
    return started;
}
RETRO_API bool arc_rpc_test_start(ArcRpcModel model, const uint8_t *rom, size_t size,
    const uint8_t seed[256], uint64_t epoch, char *error, size_t error_size)
{
    return arc_engine_rpc_start(model,rom,size,seed,epoch,false,error,error_size);
}
#endif

#if defined(ARCDROID_TEST_FAULTS)
RETRO_API bool arc_rpc_test_factory_start(ArcRpcModel model, const uint8_t *rom, size_t size,
    uint64_t epoch, char *error, size_t error_size)
{
    return arc_engine_rpc_start(model,rom,size,NULL,epoch,true,error,error_size);
}
RETRO_API const ArcEngineBackendOps *arc_rpc_test_engine_api(void)
{
   static const ArcEngineBackendOps api = {
   .capabilities = arc_engine_capabilities,
   .start = arc_engine_start,
   .set_log_callback = arc_engine_set_log_callback,
   .reset = arc_engine_reset,
   .stop = arc_engine_stop,
   .run_frame = arc_engine_run_frame,
   .power_on_arm = arc_engine_power_on_arm,
   .power_on_take_release = arc_engine_power_on_take_release,
   .boot_watch_arm = arc_engine_boot_watch_arm,
   .boot_watch_cancel = arc_engine_boot_watch_cancel,
   .boot_watch_state = arc_engine_boot_watch_state,
   .launch_outcome_arm = arc_engine_launch_outcome_arm,
   .launch_outcome_cancel = arc_engine_launch_outcome_cancel,
   .launch_outcome_refused = arc_engine_launch_outcome_refused,
   .launch_outcome_snapshot = arc_engine_launch_outcome_snapshot,
   .error_snapshot = arc_engine_error_snapshot,
   .error_request = arc_engine_error_request,
   .error_discard = arc_engine_error_discard,
   .disc_configure = arc_engine_disc_configure,
   .disc_clear = arc_engine_disc_clear,
   .disc_snapshot = arc_engine_disc_snapshot,
   .disc_a_notified = arc_engine_disc_a_notified,
   .disc_time_us = arc_engine_disc_time_us,
   .disc_controller_idle = arc_engine_disc_controller_idle,
   .adf_refusal_callback_set = arc_engine_adf_refusal_callback_set,
   .audio = arc_engine_audio,
   .audio_stats = arc_engine_audio_stats,
   .media_eject = arc_engine_media_eject,
   .media_insert = arc_engine_media_insert,
   .get_av_info = arc_engine_get_av_info,
   .cmos = arc_engine_cmos,
   .faulted = arc_engine_faulted,
   .active = arc_engine_active,
};
   return &api;
}
#endif

#if defined(ARCDROID_TEST_FAULTS)
RETRO_API bool arc_engine_test_observers_disc_devices(unsigned forms)
{ return arc_engine_is_rpc() && arc_rpc_observers_test_disc_devices(forms); }
RETRO_API bool arc_engine_test_observers_error_numbers(ArcErrorNumberClass numbers)
{ return arc_engine_is_rpc() && arc_rpc_observers_test_error_numbers(numbers); }
RETRO_API bool arc_engine_test_observers_firmware(ArcDigest *out)
{ return arc_engine_is_rpc() && arc_rpc_session_firmware(out); }
RETRO_API void arc_engine_test_observers_buttons(uint32_t ok,uint32_t cancel,uint32_t defaults)
{ if(arc_engine_is_rpc())arc_rpc_observers_test_buttons(ok,cancel,defaults); }
RETRO_API void arc_engine_test_observers_override(unsigned mask,uint32_t r1,uint32_t r2,uint32_t forms)
{ if(arc_engine_is_rpc())arc_rpc_observers_override(mask,r1,r2,forms); }
RETRO_API unsigned arc_engine_test_observers_cells(void)
{ unsigned cells=0;for(unsigned bit=1;bit<=128;bit<<=1)if(arc_engine_rpc_observers_capability(bit))cells|=bit;return cells; }
RETRO_API void arc_engine_test_observers_production(unsigned catalogue,unsigned cells)
{ if(arc_engine_is_rpc()){arc_rpc_observers_bind(catalogue,arc_rpc_session_cpu());arc_rpc_observers_test_cells(cells);} }
RETRO_API void arc_engine_test_observers_horizon(unsigned us)
{ if(arc_engine_is_rpc())arc_rpc_observers_test_horizon(us); }
RETRO_API void arc_engine_test_observers_binding(unsigned requested,unsigned *catalogue,unsigned *cpu)
{ if(requested!=UINT32_MAX)arc_engine_rpc_observers_bind(requested);arc_rpc_observers_test_binding(catalogue,cpu); }
RETRO_API bool arc_engine_test_observers_identities(char helper[11],char hd[11])
{ return arc_rpc_observers_identities(helper,hd); }
bool arc_engine_test_refuse(const char *facility) { return arc_rpc_test_refuse(facility); }
void arc_engine_test_observation_profile(bool unmatched,unsigned quota)
{
    if(!arc_engine_is_rpc())return;
#if !defined(ARCDROID_RPC_MEASUREMENT)
    arc_rpc_session_test_quota(quota);
#else
    if(quota!=200)arc_rpc_session_test_quota(quota);
#endif
    ArcRpcObserveFilter f={.modes=~0u,.swi=0xffffff,.swi_mask=0xffffff,.kinds=ARC_RPC_OBS_SWI};
    arc_rpc_observer_test_filter(ARC_RPC_OBS_BOOT,unmatched?&f:NULL);
}
void arc_engine_test_observation_trace(bool enabled,bool paths)
{ arc_rpc_observer_trace(enabled && arc_engine_is_rpc(),paths); }
bool arc_engine_test_observation_select(const ArcRpcTraceSelection *s)
{ return arc_rpc_observer_trace_select(s); }
void arc_engine_test_observation_drain(ArcRpcTraceSink sink,void *opaque)
{ arc_rpc_observer_trace_drain(sink,opaque); }
RETRO_API void arc_engine_test_observation_status(ArcRpcTraceStatus *status)
{ arc_rpc_observer_trace_status(status); }
#endif

unsigned arc_engine_rpc_storage_failure_take(void)
{ return arc_rpc_floppy_storage_failure_take(); }
