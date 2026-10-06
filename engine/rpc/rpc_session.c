/* ArcDroid contained RPCEmu backend. GPL-2.0-only.
 * Logging callbacks run only after protected guest execution returns. Device/CPU
 * entries have a live non-returning fault boundary. Unsupported entries touch no guest
 * state; cleanup cancels events before releasing owned memory. */
#include "engine_backend.h"
#include "rpc_session.h"
#include "rpc_timing.h"
#include "rpc_memory.h"
#include "rpc_rtc.h"
#include "rpc_video.h"
#include "rpc_audio.h"
#include "rpc_input.h"
#include "rpcemu.h"
#include "arm.h"
#include "cp15.h"
#include "mem.h"
#include "keyboard.h"
#include "cmos.h"
#include "vidc20.h"
#include "fdc.h"
#include "i8042.h"
#include "ide.h"
#include "podules.h"
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>

Config config;
Machine machine;
/* This registry is private. No shipping machine enum or name is extended. */
static const struct {
    CPUModel cpu;
    Model model;
    IOMDType iomd_type;
    unsigned vram_mb, rate;
} arc_rpc_registry[ARC_RPC_MODEL_COUNT] = {
    { CPUModel_ARM610, Model_RPCARM610, IOMDType_IOMD, 2, 20 },
    { CPUModel_ARM710, Model_RPCARM710, IOMDType_IOMD, 2, 35 },
    { CPUModel_SA110, Model_RPCSA110, IOMDType_IOMD, 2, 100 },
    { CPUModel_ARM7500, Model_A7000, IOMDType_ARM7500, 0, 35 }
};
static ArcRpcScheduler arc_rpc_scheduler;
static jmp_buf arc_rpc_jump;
static bool arc_rpc_live, arc_rpc_fault;
static char arc_rpc_fault_text[192];
static uint64_t arc_rpc_epoch, arc_rpc_rtc_offset, arc_rpc_executed, arc_rpc_idle_ns;
static unsigned arc_rpc_allocations, arc_rpc_live_allocations;
static unsigned arc_rpc_rate;
static uint64_t arc_rpc_bus_until, arc_rpc_bus_ns;
static ArcEngineLogCallback arc_rpc_logger;
static void *arc_rpc_logger_opaque;
void arc_rpc_video_bus(unsigned transfers, bool dram)
{
    uint64_t ns = (uint64_t)transfers * (dram ? 350u : 156u);
    uint64_t now = arc_rpc_scheduler.now_ns;
    if (arc_rpc_bus_until < now) arc_rpc_bus_until = now;
    arc_rpc_bus_until += ns; arc_rpc_bus_ns += ns;
}
extern int arc_rpc_arm_exec_count(unsigned count);
extern bool arc_rpc_cpu_idle;
static ArcInputState *arc_rpc_input_service; /* Valid only during one protected entry. */
static bool arc_rpc_input_retire;
static ArcRpcCapture arc_rpc_capture;
static uint64_t arc_rpc_generation;
uint64_t arc_rpc_session_generation(void) { return arc_rpc_generation; }
extern uint64_t arc_rpc_cmos_write_evidence(uint8_t mask[32]);
#if defined(ARCDROID_TEST_FAULTS)
static unsigned arc_rpc_fail_alloc;
static bool arc_rpc_fail_entry;
static ArcRpcTestSnapshot arc_rpc_observation;
#endif

void *arc_rpc_alloc(size_t bytes)
{
    ++arc_rpc_allocations;
#if defined(ARCDROID_TEST_FAULTS)
    if (arc_rpc_allocations == arc_rpc_fail_alloc) return NULL;
#endif
    void *allocation = malloc(bytes);
    if (allocation) ++arc_rpc_live_allocations;
    return allocation;
}
void fatal(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(arc_rpc_fault_text, sizeof(arc_rpc_fault_text), format, args);
    va_end(args);
    arc_rpc_fault = true;
    arc_rpc_sound_purge();
    arc_rpc_input_service = NULL;
    if(arc_rpc_capture.state==ARC_RPC_CAPTURE_RUNNING)arc_rpc_capture.state=ARC_RPC_CAPTURE_FAILED;
    /* All callers are CPU/device code under start/reset/advance. There is no
     * log-and-continue or process termination fallback. */
    memset(arc_rpc_scheduler.armed, 0, sizeof(arc_rpc_scheduler.armed));
    longjmp(arc_rpc_jump, 1);
}
void rpclog(const char *format, ...) { (void)format; }
void resetcodeblocks(void) { pccache = UINT32_MAX; }
void cacheclearpage(uint32_t address) { (void)address; }
bool arc_rpc_rtc_read(ArcRpcCalendar *out)
{
    return arc_rpc_calendar(arc_rpc_epoch, arc_rpc_rtc_offset + arc_rpc_scheduler.now_ns, out);
}

/* Absent IDE: floating data bus, zero status (no BSY/DRQ/IRQ), writes ignored.
 * Absent podule/EASI bus: all bits high; writes have no side effects. */
uint8_t readide(uint16_t address)
{
    return (address == 0x1f7 || address == 0x3f6) ? 0 : 0xff;
}
uint16_t readidew(void) { return 0xffff; }
void writeide(uint16_t address, uint8_t value) { (void)address; (void)value; }
void writeidew(uint16_t value) { (void)value; }
#define ARC_RPC_ABSENT_READ(bits) \
uint##bits##_t podules_read##bits(int n, PoduleIoType t, uint32_t a) \
{ (void)n; (void)t; (void)a; return (uint##bits##_t)~0u; }
#define ARC_RPC_ABSENT_WRITE(bits) \
void podules_write##bits(int n, PoduleIoType t, uint32_t a, uint##bits##_t v) \
{ (void)n; (void)t; (void)a; (void)v; }
ARC_RPC_ABSENT_READ(8) ARC_RPC_ABSENT_READ(16) ARC_RPC_ABSENT_READ(32)
ARC_RPC_ABSENT_WRITE(8) ARC_RPC_ABSENT_WRITE(16) ARC_RPC_ABSENT_WRITE(32)

static void arc_rpc_stop(void)
{
    arc_rpc_sound_purge();
    arc_rpc_input_service = NULL;
    arc_rpc_input_factory(false);
    if(arc_rpc_capture.state==ARC_RPC_CAPTURE_RUNNING || arc_rpc_capture.state==ARC_RPC_CAPTURE_DONE)
        arc_rpc_capture.state=ARC_RPC_CAPTURE_CANCELLED;
    arc_rpc_timing_bind(NULL);
    memset(&arc_rpc_scheduler, 0, sizeof(arc_rpc_scheduler));
    closevideo();
    arc_rpc_memory_close();
    arc_rpc_live_allocations = 0;
    arc_rpc_live = false;
    arc_rpc_cpu_idle = false;
}
static void arc_rpc_reset_devices(void)
{
    arc_rpc_scheduler_init(&arc_rpc_scheduler, arc_rpc_rate);
    arc_rpc_timing_bind(&arc_rpc_scheduler);
    arm_init();
    cp15_reset(machine.cpu_model);
    arm_reset(machine.cpu_model);
    iomd_reset(machine.iomd_type);
    keyboard_reset();
    i8042_reset();
    superio_reset(machine.super_type);
    reseti2c(machine.i2c_devices);
    cmos_reset();
    initvideo();
    arc_rpc_sound_reset(false);
    arc_rpc_input_reset();
    arc_rpc_input_service = NULL;
    arc_rpc_executed = arc_rpc_idle_ns = 0;
    arc_rpc_bus_until = arc_rpc_bus_ns = 0;
#if defined(ARCDROID_TEST_FAULTS)
    memset(&arc_rpc_observation, 0, sizeof(arc_rpc_observation));
#endif
}
static void arc_rpc_inject_entry(void)
{
#if defined(ARCDROID_TEST_FAULTS)
    if (arc_rpc_fail_entry) {
        arc_rpc_fail_entry = false;
        fatal("Injected RPC session failure");
    }
#endif
}
bool arc_rpc_session_start(ArcRpcModel model, const uint8_t *bytes, size_t size,
    const uint8_t seed[256], uint64_t epoch, char *failure, size_t failure_size)
{
    ArcRpcCalendar calendar;
    arc_rpc_stop();
    arc_rpc_fault = false;
    arc_rpc_fault_text[0] = 0;
    arc_rpc_allocations = 0;
    if (model >= ARC_RPC_MODEL_COUNT || (int)model < 0 || !bytes || !seed ||
        (size != 2u*1024u*1024u && size != 4u*1024u*1024u && size != 8u*1024u*1024u) ||
        !arc_rpc_calendar(epoch, 0, &calendar)) {
        if (failure && failure_size) snprintf(failure, failure_size, "Invalid RPC model, ROM, CMOS seed or RTC epoch");
        return false;
    }
    if (setjmp(arc_rpc_jump)) {
        arc_rpc_stop();
        if (failure && failure_size) snprintf(failure, failure_size, "%s", arc_rpc_fault_text);
        return false;
    }
    memset(&config, 0, sizeof(config));
    memset(&machine, 0, sizeof(machine));
    config.mem_size = 64;
    config.vram_size = arc_rpc_registry[model].vram_mb;
    machine.model = arc_rpc_registry[model].model;
    machine.cpu_model = arc_rpc_registry[model].cpu;
    machine.iomd_type = arc_rpc_registry[model].iomd_type;
    machine.super_type = SuperIOType_FDC37C665GT;
    machine.i2c_devices = I2C_PCF8583;
    arc_rpc_rate = arc_rpc_registry[model].rate;
    arc_rpc_epoch = epoch;
    arc_rpc_rtc_offset = 0;
    memset(&arc_rpc_capture,0,sizeof(arc_rpc_capture));
    arc_rpc_input_retire = false;
    arc_rpc_inject_entry();
    mem_init();
    if (!arc_rpc_memory_load_rom(bytes, size)) fatal("ROM admission failed");
    arc_rpc_cmos_seed(seed);
    arc_rpc_reset_devices();
    arc_rpc_sound_reset(true);
    arc_rpc_live = true;
    ++arc_rpc_generation;
    if (failure && failure_size) failure[0] = 0;
    return true;
}
bool arc_rpc_session_factory_start(ArcRpcModel model,const uint8_t *bytes,size_t size,
    uint64_t epoch,char *failure,size_t failure_size)
{
    uint8_t empty[256]={0};
    if(!arc_rpc_session_start(model,bytes,size,empty,epoch,failure,failure_size))return false;
    if(setjmp(arc_rpc_jump))return false;
    arc_rpc_capture.state=ARC_RPC_CAPTURE_RUNNING;
    arc_rpc_input_factory(true);
    return true;
}
void arc_rpc_capture_status(ArcRpcCapture *out)
{
    if(!out)return;
    *out=arc_rpc_capture;
    out->elapsed_ns=arc_rpc_scheduler.now_ns;
    out->delete_held=arc_rpc_input_delete_held();
    if(out->state==ARC_RPC_CAPTURE_RUNNING) {
        out->writes=arc_rpc_cmos_write_evidence(out->written);arc_rpc_cmos_snapshot(out->bytes);
    }
}
static bool arc_rpc_reset(char *failure, size_t failure_size)
{
    if (!arc_rpc_live || arc_rpc_fault) {
        if (failure && failure_size) snprintf(failure, failure_size, "%s",
            arc_rpc_fault ? arc_rpc_fault_text : "RPC session inactive");
        return false;
    }
    if (setjmp(arc_rpc_jump)) {
        if (failure && failure_size) snprintf(failure, failure_size, "%s", arc_rpc_fault_text);
        return false;
    }
    arc_rpc_inject_entry();
    arc_rpc_rtc_offset += arc_rpc_scheduler.now_ns;
    mem_reset(64, config.vram_size);
    arc_rpc_reset_devices(); /* Volatile CMOS is retained. */
    arc_rpc_input_retire=true;
    if(arc_rpc_capture.state==ARC_RPC_CAPTURE_RUNNING || arc_rpc_capture.state==ARC_RPC_CAPTURE_DONE)
        arc_rpc_capture.state=ARC_RPC_CAPTURE_CANCELLED;
    if (failure && failure_size) failure[0] = 0;
    return true;
}

/* Events due at a boundary precede its instruction. MMIO applies immediately
 * at that instruction's timestamp. Idle skips directly to the next event,
 * discards CPU credit, and wakes when either interrupt line is asserted. */
static void arc_rpc_deliver(ArcRpcEvent event)
{
#if defined(ARCDROID_TEST_FAULTS)
    ++arc_rpc_observation.events[event];
    unsigned t = arc_rpc_observation.trace_count;
    if (t < 64) {
        arc_rpc_observation.trace[t].ns = arc_rpc_scheduler.now_ns;
        arc_rpc_observation.trace[t].event = event;
        ++arc_rpc_observation.trace_count;
    }
#endif
    switch (event) {
    case ARC_RPC_EVENT_TIMER0: arc_rpc_iomd_timer_event(0); break;
    case ARC_RPC_EVENT_TIMER1: arc_rpc_iomd_timer_event(1); break;
    case ARC_RPC_EVENT_KEYBOARD: kcallback = 0; keyboard_callback_rpcemu(); break;
    case ARC_RPC_EVENT_MOUSE: mcallback = 0; mouse_ps2_callback(); break;
    case ARC_RPC_EVENT_FDC: fdccallback = 0; fdc_callback(); break;
    case ARC_RPC_EVENT_VIDEO: arc_rpc_video_event(); break;
    case ARC_RPC_EVENT_SOUND: arc_rpc_sound_event(); break;
    case ARC_RPC_EVENT_INPUT: {
        ArcInputEvent input;
        /* One arbitrated event/tick, with complete-packet backpressure. No
         * frontend callback or retained frontend pointer crosses the boundary. */
        if(arc_rpc_input_service && arc_rpc_keyboard_room()>=8 && arc_rpc_mouse_room()>=4 &&
           arc_input_consume(arc_rpc_input_service,true,&input)) {
            if(input.type==ARC_INPUT_EVENT_KEY)arc_rpc_input_key(input.code,input.down);
            else if(input.type==ARC_INPUT_EVENT_BUTTON) {
                if(input.down)mouse_mouse_press((int)input.code);else mouse_mouse_release((int)input.code);
            } else mouse_mouse_move_relative((int)input.dx,(int)input.dy);
        }
        arc_rpc_input_event(); break;
    }
    default: break; /* Reserved absent IDE slot. */
    }
}
static bool arc_rpc_advance_internal(uint64_t elapsed_ns, bool stop_at_field)
{
    if (!arc_rpc_live || arc_rpc_fault || elapsed_ns > 16666667u ||
        arc_rpc_scheduler.now_ns > UINT64_MAX - elapsed_ns) return false;
    if (setjmp(arc_rpc_jump)) return false;
    arc_rpc_inject_entry();
    uint64_t limit = arc_rpc_scheduler.now_ns + elapsed_ns;
    if(arc_rpc_capture.state==ARC_RPC_CAPTURE_DONE)return true;
    if(arc_rpc_capture.state==ARC_RPC_CAPTURE_RUNNING && limit>UINT64_C(30000000000))
        limit=UINT64_C(30000000000);
    ArcRpcVideoStatus video_start; arc_rpc_video_status(&video_start);
    unsigned event_work = 0, instruction_work = 0;
    while (arc_rpc_scheduler.now_ns < limit) {
        if (arc_rpc_cpu_idle && (arm.event & 3)) arc_rpc_cpu_idle = false;
        bool idle = arc_rpc_cpu_idle;
        bool blocked = arc_rpc_scheduler.now_ns < arc_rpc_bus_until;
        uint64_t next = arc_rpc_next_deadline(&arc_rpc_scheduler, limit), credit = 0;
        if (blocked && arc_rpc_bus_until < next) next = arc_rpc_bus_until;
        if (!idle && !blocked) {
            uint64_t cpu_next = arc_rpc_scheduler.now_ns +
                (1000u - arc_rpc_scheduler.cpu_fraction + arc_rpc_rate - 1u) / arc_rpc_rate;
            if (cpu_next < next) next = cpu_next;
            if (!arc_rpc_cpu_credit(&arc_rpc_scheduler, next - arc_rpc_scheduler.now_ns, &credit))
                fatal("RPC CPU credit overflow");
        } else {
            if (idle) arc_rpc_idle_ns += next - arc_rpc_scheduler.now_ns;
            arc_rpc_scheduler.cpu_fraction = 0;
        }
        arc_rpc_scheduler.now_ns = next;
        ArcRpcEvent event;
        bool completed = false;
        while (arc_rpc_take_event(&arc_rpc_scheduler, next, &event)) {
            if (++event_work > 131072u) fatal("RPC event budget exceeded");
            arc_rpc_deliver(event);
            if (event == ARC_RPC_EVENT_VIDEO) {
                ArcRpcVideoStatus current; arc_rpc_video_status(&current);
                if (current.pixels - video_start.pixels > 33554432u) fatal("RPC scanout work budget exceeded");
                completed = stop_at_field && current.fields != video_start.fields;
            }
        }
        if (arc_rpc_cpu_idle && (arm.event & 3)) arc_rpc_cpu_idle = false;
        if (credit && arc_rpc_scheduler.now_ns >= arc_rpc_bus_until) {
            if (++instruction_work > 1666667u) fatal("RPC instruction budget exceeded");
            arc_rpc_arm_exec_count(1);
            ++arc_rpc_executed;
            if (arc_rpc_cpu_idle) arc_rpc_scheduler.cpu_fraction = 0;
        }
        if (completed) break;
    }
    if(arc_rpc_capture.state==ARC_RPC_CAPTURE_RUNNING && arc_rpc_scheduler.now_ns==UINT64_C(30000000000)) {
        arc_rpc_capture.writes=arc_rpc_cmos_write_evidence(arc_rpc_capture.written);
        arc_rpc_cmos_snapshot(arc_rpc_capture.bytes);
        if(!arc_rpc_capture.writes)fatal("Factory CMOS capture: no guest writes in 30 emulated seconds");
        arc_rpc_capture.state=ARC_RPC_CAPTURE_DONE;
    }
    return true;
}



static ArcEngineCapabilities arc_rpc_capabilities(void)
{
    return (ArcEngineCapabilities){ .av = true, .keyboard = true, .mouse = true, .reset = true };
}
static bool arc_rpc_shipping_start(const uint8_t *r, size_t s, const ArcEngineMedia *m,
    const ArcEngineMedia *h, const ArcMachineDescriptor *d, ArcOsClass o,
    const ArcIdeBackend *i, const uint8_t c[ARC_CMOS_MACHINE_SIZE], const uint8_t l[32],
    ArcEngineCmosIgnoredCallback cb, void *p, char *e, size_t n)
{
    (void)r; (void)s; (void)m; (void)h; (void)d; (void)o; (void)i;
    (void)c; (void)l; (void)cb; (void)p;
    if (e && n) snprintf(e, n, "RPCEmu requires the test entry");
    return false;
}
static bool arc_rpc_frame(ArcInputState *input, ArcEngineFrame *frame)
{
    if (frame) memset(frame, 0, sizeof(*frame));
    if (!frame) return false;
    if(arc_rpc_input_retire && input) { arc_input_reset(input);arc_rpc_input_retire=false; }
    arc_rpc_input_service=input;
    uint64_t start = arc_rpc_scheduler.now_ns;
    ArcRpcVideoStatus before, after; arc_rpc_video_status(&before);
    /* At most six budgeted chunks (100 ms): even a stopped/disabled display
     * cannot trap the frontend in an unbounded wait for flyback. */
    for (unsigned chunk = 0; chunk < 6u; ++chunk) {
        if (!arc_rpc_advance_internal(16666667u, true)) { arc_rpc_input_service=NULL;return false; }
        arc_rpc_video_status(&after);
        if (after.fields != before.fields || !vidc_get_xsize() || !vidc_get_ysize()) break;
    }
    arc_rpc_input_service=NULL;
    frame->elapsed_us = (arc_rpc_scheduler.now_ns - start) / 1000u;
    if (after.fields != before.fields && after.width && after.height) {
        frame->pixels = arc_rpc_video_pixels(); frame->width = after.width;
        frame->height = after.height; frame->pitch = ARC_RPC_VIDEO_LIMIT * sizeof(uint32_t);
        frame->refresh_hz = after.refresh_hz; frame->field_completed = true;
    }
    /* Logs/callbacks occur only after leaving the protected guest stack. */
    if (arc_rpc_logger && arc_rpc_video_take_clip())
        arc_rpc_logger(arc_rpc_logger_opaque, "RPC video clipped to 2048x2048; guest scanout unchanged.\n");
    if(arc_rpc_logger && arc_rpc_sound_take_unsupported())
        arc_rpc_logger(arc_rpc_logger_opaque,"RPC 8-bit compatibility sound unsupported; output is silent.\n");
    return true;
}
static void arc_rpc_log_callback(ArcEngineLogCallback cb, void *p)
{ arc_rpc_logger = cb; arc_rpc_logger_opaque = p; }
static void arc_rpc_nop(void) { }
static void arc_rpc_power_on(ArcInputState *i, bool a) { (void)i; (void)a; }
static bool arc_rpc_release(uint64_t *t, bool *a) { if (t) *t=0; if (a) *a=false; return false; }
static ArcBootWatchState arc_rpc_boot(void) { return ARC_BOOT_WATCH_DISABLED; }
static bool arc_rpc_command(const char *c) { (void)c; return false; }
static void arc_rpc_reason(const char *c) { (void)c; }
static void arc_rpc_drive(unsigned d) { (void)d; }
static void arc_rpc_outcome(ArcLaunchOutcome *o) { if (o) memset(o,0,sizeof(*o)); }
static void arc_rpc_error_box(ArcErrorBox *o) { if (o) memset(o,0,sizeof(*o)); }
static bool arc_rpc_answer(ArcErrorAnswer a) { (void)a; return false; }
static void arc_rpc_disc(bool e,const char *n,ArcDiscResolveCallback r,ArcDiscObservationCallback o,void *p)
{ (void)e;(void)n;(void)r;(void)o;(void)p; }
static void arc_rpc_disc_snapshot(ArcDiscRequest *o) { if(o) memset(o,0,sizeof(*o)); }
static uint64_t arc_rpc_disc_time(void) { return 0; }
static bool arc_rpc_false(void) { return false; }
static void arc_rpc_refusal(void (*c)(unsigned,unsigned,unsigned,unsigned)) { (void)c; }
static size_t arc_rpc_audio(int16_t *o,size_t n)
{
    if(arc_rpc_live && arc_rpc_fault && o)arc_rpc_sound_fault_silence(n);
    size_t count=arc_rpc_audio_peek(o,n);arc_rpc_audio_commit(count,count);return count;
}
static void arc_rpc_audio_stats(uint64_t *u,uint64_t *o) { if(u)*u=0;if(o)*o=0; }
static bool arc_rpc_media(const ArcEngineMedia *m,char *e,size_t n)
{ (void)m;if(e&&n)snprintf(e,n,"RPCEmu media unsupported in R1");return false; }
static bool arc_rpc_av(unsigned *w,unsigned *h,double *r)
{
    ArcRpcVideoStatus v; arc_rpc_video_status(&v);
    if(w)*w=v.width;
    if(h)*h=v.height;
    if(r)*r=v.refresh_hz;
    return arc_rpc_live && !arc_rpc_fault && v.width && v.height;
}
static void arc_rpc_cmos_unavailable(uint8_t o[ARC_CMOS_PAYLOAD_SIZE]) { if(o)memset(o,0,ARC_CMOS_PAYLOAD_SIZE); }
static bool arc_rpc_faulted(const char **m) { if(m)*m=arc_rpc_fault_text;return arc_rpc_fault; }
static bool arc_rpc_active(void) { return arc_rpc_live; }
const ArcEngineBackendOps arc_rpc_backend = {
    .capabilities=arc_rpc_capabilities,.start=arc_rpc_shipping_start,.set_log_callback=arc_rpc_log_callback,
    .reset=arc_rpc_reset,.stop=arc_rpc_stop,.run_frame=arc_rpc_frame,.power_on_arm=arc_rpc_power_on,
    .power_on_take_release=arc_rpc_release,.boot_watch_arm=arc_rpc_nop,.boot_watch_cancel=arc_rpc_nop,
    .boot_watch_state=arc_rpc_boot,.launch_outcome_arm=arc_rpc_command,.launch_outcome_cancel=arc_rpc_reason,
    .launch_outcome_refused=arc_rpc_drive,.launch_outcome_snapshot=arc_rpc_outcome,
    .error_snapshot=arc_rpc_error_box,.error_request=arc_rpc_answer,.error_discard=arc_rpc_nop,
    .disc_configure=arc_rpc_disc,.disc_clear=arc_rpc_nop,.disc_snapshot=arc_rpc_disc_snapshot,
    .disc_a_notified=arc_rpc_nop,.disc_time_us=arc_rpc_disc_time,.disc_controller_idle=arc_rpc_false,
    .adf_refusal_callback_set=arc_rpc_refusal,.audio=arc_rpc_audio,.audio_stats=arc_rpc_audio_stats,
    .media_eject=arc_rpc_false,.media_insert=arc_rpc_media,.get_av_info=arc_rpc_av,
    .cmos=arc_rpc_cmos_unavailable,.faulted=arc_rpc_faulted,.active=arc_rpc_active
};
#if defined(ARCDROID_TEST_FAULTS)
RETRO_API void arc_rpc_test_cmos_snapshot(uint8_t *out) { if(out)arc_rpc_cmos_snapshot(out); }
RETRO_API void arc_rpc_test_capture_status(ArcRpcCapture *out) { arc_rpc_capture_status(out); }
RETRO_API void arc_rpc_test_audio_counts(ArcRpcAudioCounts *out) { arc_rpc_audio_counts(out); }
RETRO_API size_t arc_rpc_test_audio_peek(int16_t *out,size_t n) { return arc_rpc_audio_peek(out,n); }
RETRO_API void arc_rpc_test_audio_commit(size_t n,size_t a) { arc_rpc_audio_commit(n,a); }
RETRO_API void arc_rpc_test_video_status(ArcRpcVideoStatus *out) { if(out)arc_rpc_video_status(out); }
RETRO_API uint64_t arc_rpc_test_video_bus_ns(void) { return arc_rpc_bus_ns; }
RETRO_API void arc_rpc_test_fail_alloc(unsigned stage) { arc_rpc_fail_alloc=stage; }
RETRO_API void arc_rpc_test_fault_next(void) { arc_rpc_fail_entry=true; }
RETRO_API bool arc_rpc_test_advance(uint64_t ns) { return arc_rpc_advance_internal(ns,false); }
RETRO_API void arc_rpc_test_snapshot(ArcRpcTestSnapshot *out)
{
    if(!out)return;
    *out=arc_rpc_observation;
    out->now_ns=arc_rpc_scheduler.now_ns;out->executed=arc_rpc_executed;out->idle_ns=arc_rpc_idle_ns;
    memcpy(out->regs,arm.reg,sizeof(out->regs));out->irq=iomd.irqa.status;out->fiq=iomd.fiq.status;
    out->allocations=arc_rpc_allocations;out->live_allocations=arc_rpc_live_allocations;
    out->rate_mips=arc_rpc_rate;out->faulted=arc_rpc_fault;out->active=arc_rpc_live;
}
RETRO_API bool arc_rpc_test_read_ram(unsigned offset,uint32_t *out)
{
    if(!arc_rpc_live||arc_rpc_fault||!out||offset>=32u*1024u*1024u||(offset&3))return false;
    *out=ram00[offset/4];return true;
}
#endif
