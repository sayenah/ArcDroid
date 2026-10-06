/* ArcDroid contained RPCEmu backend. GPL-2.0-only.
 * No frontend callback is called by this translation unit. Device/CPU entries
 * have a live non-returning fault boundary. Unsupported entries touch no guest
 * state; cleanup cancels events before releasing owned memory. */
#include "engine_backend.h"
#include "rpc_session.h"
#include "rpc_timing.h"
#include "rpc_memory.h"
#include "rpc_rtc.h"
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
extern int arc_rpc_arm_exec_count(unsigned count);
extern bool arc_rpc_cpu_idle;
uint32_t soundaddr[4];
int soundinited, soundlatch, soundcount;
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
/* Run D replaces these sound-register hooks with timed production/drain. */
void sound_samplefreq_change(int frequency) { soundlatch = frequency; }

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
    arc_rpc_timing_bind(NULL);
    memset(&arc_rpc_scheduler, 0, sizeof(arc_rpc_scheduler));
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
    memset(soundaddr, 0, sizeof(soundaddr));
    soundinited = soundlatch = soundcount = 0;
    arc_rpc_executed = arc_rpc_idle_ns = 0;
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
    arc_rpc_inject_entry();
    mem_init();
    if (!arc_rpc_memory_load_rom(bytes, size)) fatal("ROM admission failed");
    arc_rpc_cmos_seed(seed);
    arc_rpc_reset_devices();
    arc_rpc_live = true;
    if (failure && failure_size) failure[0] = 0;
    return true;
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
    default: break; /* Reserved IDE/sound slots, never armed in C2. */
    }
}
static bool arc_rpc_advance(uint64_t elapsed_ns)
{
    if (!arc_rpc_live || arc_rpc_fault || elapsed_ns > 16666667u ||
        arc_rpc_scheduler.now_ns > UINT64_MAX - elapsed_ns) return false;
    if (setjmp(arc_rpc_jump)) return false;
    arc_rpc_inject_entry();
    uint64_t limit = arc_rpc_scheduler.now_ns + elapsed_ns;
    unsigned event_work = 0, instruction_work = 0;
    while (arc_rpc_scheduler.now_ns < limit) {
        if (arc_rpc_cpu_idle && (arm.event & 3)) arc_rpc_cpu_idle = false;
        bool idle = arc_rpc_cpu_idle;
        uint64_t next = arc_rpc_next_deadline(&arc_rpc_scheduler, limit), credit = 0;
        if (!idle) {
            uint64_t cpu_next = arc_rpc_scheduler.now_ns +
                (1000u - arc_rpc_scheduler.cpu_fraction + arc_rpc_rate - 1u) / arc_rpc_rate;
            if (cpu_next < next) next = cpu_next;
            if (!arc_rpc_cpu_credit(&arc_rpc_scheduler, next - arc_rpc_scheduler.now_ns, &credit))
                fatal("RPC CPU credit overflow");
        } else {
            arc_rpc_idle_ns += next - arc_rpc_scheduler.now_ns;
            arc_rpc_scheduler.cpu_fraction = 0;
        }
        arc_rpc_scheduler.now_ns = next;
        ArcRpcEvent event;
        while (arc_rpc_take_event(&arc_rpc_scheduler, next, &event)) {
            if (++event_work > 131072u) fatal("RPC event budget exceeded");
            arc_rpc_deliver(event);
        }
        if (arc_rpc_cpu_idle && (arm.event & 3)) arc_rpc_cpu_idle = false;
        if (credit) {
            if (++instruction_work > 1666667u) fatal("RPC instruction budget exceeded");
            arc_rpc_arm_exec_count(1);
            ++arc_rpc_executed;
            if (arc_rpc_cpu_idle) arc_rpc_scheduler.cpu_fraction = 0;
        }
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
    (void)input; /* Run D adds the ArcDroid input adapter, gathered before entry. */
    if (frame) memset(frame, 0, sizeof(*frame));
    if (!frame || !arc_rpc_advance(16666667u)) return false;
    frame->elapsed_us = 16667; frame->refresh_hz = 60;
    return true; /* C2 has device state only, no completed rendered field. */
}
static void arc_rpc_log_callback(ArcEngineLogCallback cb, void *p) { (void)cb; (void)p; }
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
static size_t arc_rpc_audio(int16_t *o,size_t n) { if(o) memset(o,0,n*2*sizeof(*o)); return o ? n : 0; }
static void arc_rpc_audio_stats(uint64_t *u,uint64_t *o) { if(u)*u=0;if(o)*o=0; }
static bool arc_rpc_media(const ArcEngineMedia *m,char *e,size_t n)
{ (void)m;if(e&&n)snprintf(e,n,"RPCEmu media unsupported in R1");return false; }
static bool arc_rpc_av(unsigned *w,unsigned *h,double *r)
{ if(w)*w=0;if(h)*h=0;if(r)*r=0;return false; }
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
RETRO_API void arc_rpc_test_fail_alloc(unsigned stage) { arc_rpc_fail_alloc=stage; }
RETRO_API void arc_rpc_test_fault_next(void) { arc_rpc_fail_entry=true; }
RETRO_API bool arc_rpc_test_advance(uint64_t ns) { return arc_rpc_advance(ns); }
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
