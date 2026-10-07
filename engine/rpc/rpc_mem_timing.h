/* ArcDroid timing-only RiscPC memory system, D-139. GPL-2.0-only. */
#ifndef ARC_RPC_MEM_TIMING_H
#define ARC_RPC_MEM_TIMING_H
#include <stdbool.h>
#include <stdint.h>
#include "rpc_scheduler.h"

/* Public only to the owned interpreter batch shortcut. A hot batch advances
 * exact rational time; any queue, grant or hold returns to the event driver. */
typedef struct {
    bool enabled, pending, bus_busy, memo_valid, recording;
    bool rom_sequential, after_branch, track_rom;
    uint32_t fetch_pc;
    uint32_t opcode;
    uint64_t instructions;
    unsigned memo_shift; uint32_t memo_line;
    uint64_t fetch_hits;
#if defined(ARCDROID_TEST_FAULTS)
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

#if defined(ARCDROID_TEST_FAULTS)
typedef struct {
    bool started, done;
    uint64_t start_ticks, end_ticks;
    ArcRpcMemStats start, end;
} ArcRpcMemInterval;
void arc_rpc_mem_measure(uint64_t first, uint64_t last, bool drained);
void arc_rpc_mem_interval(ArcRpcMemInterval *out);
#endif

void arc_rpc_mem_bind(ArcRpcScheduler *clock, unsigned model, unsigned rate);
void arc_rpc_mem_begin_slow(void);
void arc_rpc_mem_end_slow(void);
void arc_rpc_mem_access(uint32_t va, uint32_t physical, unsigned bytes,
                        bool write, bool fetch, unsigned attributes, bool backed);
void arc_rpc_mem_control(uint32_t control);
void arc_rpc_mem_permission_change(void);
void arc_rpc_mem_maintenance(unsigned crn, unsigned opc2, unsigned crm, uint32_t va);
void arc_rpc_mem_clock_switch(bool enable);
void arc_rpc_mem_video_hold(uint64_t ns);
/* Called after tied device delivery. No guest functional effects occur here. */
void arc_rpc_mem_run(uint64_t now_ns);
uint64_t arc_rpc_mem_next(uint64_t limit_ns, bool idle);
bool arc_rpc_mem_ready(bool idle);
void arc_rpc_mem_idle(bool idle);
void arc_rpc_mem_stats(ArcRpcMemStats *out);

static inline void arc_rpc_mem_instruction_begin(void)
{
    ArcRpcMemFast *f=&arc_rpc_mem_fast;
    if (!f->enabled) return;
    f->recording=true;f->track_rom=f->rom_sequential;
    if (f->track_rom) f->after_branch=false;
    f->opcode=0;++f->instructions;
#if defined(ARCDROID_TEST_FAULTS)
    if (f->test_measure) arc_rpc_mem_begin_slow();
#endif
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
#if defined(ARCDROID_TEST_FAULTS)
        || f->test_measure
#endif
    ) { arc_rpc_mem_end_slow(); return; }
    f->recording=false;
    if (f->track_rom && f->after_branch) f->rom_sequential=false;
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
    if (f->memo_valid && f->memo_line == (va >> f->memo_shift)) { ++f->fetch_hits; return; }
    arc_rpc_mem_access(va, physical, 4, false, true, attributes, backed);
}

static inline bool arc_rpc_mem_batch_next(ArcRpcCpuBatch *batch, bool idle)
{
    ArcRpcMemFast *m = &arc_rpc_mem_fast;
    if (!m->enabled) return arc_rpc_cpu_batch_next(batch, idle);
    if (idle || m->pending || m->bus_busy) return false;
#if defined(ARCDROID_TEST_FAULTS)
    if (m->test_stop) return false;
#endif
    uint64_t due = m->cpu_due_ns;
    uint64_t boundary = batch->clock->next_ns < batch->limit_ns ?
        batch->clock->next_ns : batch->limit_ns;
    if (due >= boundary) return false;
    batch->clock->now_ns = due;
    m->issue_ticks = m->cpu_due_ticks;
    m->issue_floor = m->due_floor; m->issue_fraction = m->due_fraction;
    return true;
}
#endif
