/* ArcDroid timing-only RiscPC memory system, D-139. GPL-2.0-only. */
#ifndef ARC_RPC_MEM_TIMING_H
#define ARC_RPC_MEM_TIMING_H
#include <stdbool.h>
#include <stdint.h>
#include "rpc_scheduler.h"
#include "rpc_profile.h"
#include "rpc_exercise.h"

/* Public only to the owned interpreter batch shortcut. Exact service can
 * complete locally before a boundary; intervening events return to the driver. */
typedef struct {
    bool enabled, pending, bus_busy, memo_valid, recording;
    bool rom_sequential, after_branch, track_rom;
    bool integer_batch;
    ArcRpcScheduler *clock;
    uint32_t fetch_pc;
    uint32_t opcode;
    uint64_t instructions;
    unsigned memo_shift; uint32_t memo_line;
    uint64_t fetch_hits;
#if defined(ARCDROID_TEST_FAULTS) && !defined(ARCDROID_RPC_MEASUREMENT)
    bool test_stop, test_measure;
#endif
    uint32_t denominator;
    uint64_t issue_ticks, cpu_due_ticks, base_ticks, cpu_due_ns;
    uint64_t issue_floor, due_floor;
    unsigned issue_fraction, due_fraction, base_whole, base_fraction;
} ArcRpcMemFast;
extern ArcRpcMemFast arc_rpc_mem_fast;

typedef struct {
    uint64_t instructions, words, fills, dirty_halves, packets, hits;
    uint64_t completed_ticks, buffer_wait_ticks;
    unsigned buffered_words, buffered_packets;
    uint32_t denominator;
} ArcRpcMemStats;

#if defined(ARCDROID_TEST_FAULTS) || defined(ARCDROID_TEST_API)
typedef struct {
    bool started, done;
    uint64_t start_ticks, end_ticks;
    uint64_t start_fetch_hits, end_fetch_hits;
    ArcRpcMemStats start, end;
} ArcRpcMemInterval;
typedef struct {
    uint64_t instructions, issue_ticks, now_ns, cpu, memory, scheduler;
} ArcRpcTestPrefix;
bool arc_rpc_test_prefix_arm(uint64_t last);
bool arc_rpc_test_prefix_snapshot(ArcRpcTestPrefix *out);
void arc_rpc_mem_measure(uint64_t first, uint64_t last, bool drained);
void arc_rpc_mem_interval(ArcRpcMemInterval *out);
bool arc_rpc_mem_exercise_interval(ArcRpcExercise *out); /* Endpoint windows only. */
bool arc_rpc_mem_endpoint(uint64_t first, uint64_t last, bool drained);
unsigned arc_rpc_mem_endpoint_admit(unsigned count);
void arc_rpc_mem_endpoint_return(void);
typedef struct {
    uint64_t first, last, previous_completion, end_completion;
    bool drained, endpoint, intrusive, stopped, previous_valid, pending_end;
    ArcRpcMemInterval interval;
} ArcRpcMeasureState;
void arc_rpc_mem_measure_state(ArcRpcMeasureState *out);
bool arc_rpc_mem_measure_equal(const ArcRpcMeasureState *a, const ArcRpcMeasureState *b);
#endif

void arc_rpc_mem_bind(ArcRpcScheduler *clock, unsigned model, unsigned rate);
void arc_rpc_mem_begin_slow(void);
void arc_rpc_mem_end_slow(void);
void arc_rpc_mem_batch_flush(void);
void arc_rpc_mem_access(uint32_t va, uint32_t physical, unsigned bytes,
                        bool write, bool fetch, unsigned attributes, bool backed);
void arc_rpc_mem_fetch_slow(uint32_t va, uint32_t physical, unsigned attributes, bool backed);
void arc_rpc_mem_control(uint32_t control);
void arc_rpc_mem_permission_change(void);
void arc_rpc_mem_maintenance(unsigned crn, unsigned opc2, unsigned crm, uint32_t va);
void arc_rpc_mem_clock_switch(bool enable);
void arc_rpc_mem_video_hold(uint64_t ns);
/* Called after tied device delivery. No guest functional effects occur here. */
void arc_rpc_mem_run(uint64_t now_ns);
uint64_t arc_rpc_mem_next(uint64_t limit_ns, bool idle);
bool arc_rpc_mem_ready(bool idle);
bool arc_rpc_mem_batch_service(ArcRpcCpuBatch *batch);
void arc_rpc_mem_idle(bool idle);
void arc_rpc_mem_stats(ArcRpcMemStats *out);

static inline void arc_rpc_mem_instruction_begin(void)
{
    ArcRpcMemFast *f=&arc_rpc_mem_fast;
    if (!f->enabled) return;
    f->recording=true;f->track_rom=f->rom_sequential;
    if (f->track_rom) f->after_branch=false;
    ++f->instructions; /* Fetch never classifies the previous data opcode. */
#if defined(ARCDROID_TEST_FAULTS) && !defined(ARCDROID_RPC_MEASUREMENT)
    if (f->test_measure) arc_rpc_mem_begin_slow();
#endif
}
static inline void arc_rpc_mem_batch_begin(ArcRpcCpuBatch *batch)
{
    ArcRpcMemFast *f = &arc_rpc_mem_fast;
    /* Integral costs retain the carried issue fraction, including a phase
     * left by a preceding bus cycle. The shared clock carries the ceiling;
     * materialise the redundant rational representations at boundaries. */
    f->integer_batch = batch && f->enabled && !f->base_fraction &&
        batch->clock->now_ns == f->issue_floor + (f->issue_fraction != 0) &&
        UINT64_MAX - f->issue_ticks >= 200u * f->base_ticks
#if defined(ARCDROID_TEST_FAULTS) && !defined(ARCDROID_RPC_MEASUREMENT)
        && !f->test_measure
#endif
        ;
}
static inline void arc_rpc_mem_opcode(uint32_t opcode)
{ if (arc_rpc_mem_fast.recording) arc_rpc_mem_fast.opcode=opcode; }
/* Executed branches and exceptions break the next ROM request, including a
 * branch to the next address. Current pending fetch service must finish first. */
static inline void arc_rpc_mem_branch(void)
{
    ArcRpcMemFast *f=&arc_rpc_mem_fast;
    if (f->recording) { if (f->track_rom) f->after_branch=true; }
    else f->rom_sequential=false;
}
static inline void arc_rpc_mem_instruction_end(uint32_t next_pc)
{
    ArcRpcMemFast *f=&arc_rpc_mem_fast;
    if (!f->enabled) return;
    if (f->track_rom) f->after_branch |= next_pc != f->fetch_pc + 4u;
    if (f->pending || UINT64_MAX-f->issue_ticks<f->base_ticks
#if defined(ARCDROID_TEST_FAULTS) && !defined(ARCDROID_RPC_MEASUREMENT)
        || f->test_measure
#endif
    ) { arc_rpc_mem_end_slow(); return; }
    f->recording=false;
    if (f->track_rom && f->after_branch) f->rom_sequential=false;
    if (f->integer_batch) {
        if (!f->base_fraction) {
            f->cpu_due_ns = f->clock->now_ns + f->base_whole;
            return;
        }
        arc_rpc_mem_batch_flush();
    }
    unsigned fraction=f->issue_fraction+f->base_fraction;
    f->due_floor=f->issue_floor+f->base_whole+(fraction>=f->denominator);
    f->due_fraction=fraction>=f->denominator?fraction-f->denominator:fraction;
    f->cpu_due_ns=f->due_floor+(f->due_fraction!=0);
    f->cpu_due_ticks=f->issue_ticks+f->base_ticks;
}

static inline void arc_rpc_mem_fetch(uint32_t va, uint32_t physical,
                                      unsigned attributes, bool backed)
{
    ArcRpcMemFast *f = &arc_rpc_mem_fast;
    if (f->track_rom) f->fetch_pc=va;
    /* A TLB collision/re-walk can change backing or region without clearing
     * this virtual-line memo. Match the slow path's pre-lookup eligibility. */
    uint32_t pa = physical & 0x1fffffffu;
    if (backed && (pa < 0x03000000u || pa >= 0x10000000u) &&
        f->memo_valid && f->memo_line == (va >> f->memo_shift)) { ++f->fetch_hits; ARC_RPC_PROFILE(memo()); return; }
    arc_rpc_mem_fetch_slow(va, physical, attributes, backed);
}

#if defined(ARCDROID_RPC_PROFILE)
#define arc_rpc_mem_batch_next arc_rpc_mem_batch_next_profile_body
#endif
static inline bool arc_rpc_mem_batch_next(ArcRpcCpuBatch *batch, bool idle)
{
    ArcRpcMemFast *m = &arc_rpc_mem_fast;
    if (!m->enabled) return arc_rpc_cpu_batch_next(batch, idle);
    if (idle) return false;
#if defined(ARCDROID_TEST_FAULTS) && !defined(ARCDROID_RPC_MEASUREMENT)
    if (m->test_stop) return false;
#endif
    if (m->pending || m->bus_busy) {
        if (m->integer_batch) arc_rpc_mem_batch_flush();
        return arc_rpc_mem_batch_service(batch);
    }
    uint64_t due = m->cpu_due_ns;
    uint64_t boundary = batch->clock->next_ns < batch->limit_ns ?
        batch->clock->next_ns : batch->limit_ns;
    if (due >= boundary) return false;
    batch->clock->now_ns = due;
    if (m->integer_batch) return true;
    m->issue_ticks = m->cpu_due_ticks;
    m->issue_floor = m->due_floor; m->issue_fraction = m->due_fraction;
    return true;
}
#if defined(ARCDROID_RPC_PROFILE)
#undef arc_rpc_mem_batch_next
static inline bool arc_rpc_mem_batch_next(ArcRpcCpuBatch *batch, bool idle)
{
    int previous=arc_rpc_profile_enter(ARC_RPC_COST_TIMING);
    bool result=arc_rpc_mem_batch_next_profile_body(batch,idle);
    arc_rpc_profile_leave(previous);
    return result;
}
#endif

#endif
