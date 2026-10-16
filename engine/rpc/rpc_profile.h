/* Test-only interpreter profiling, 6b-P0i. GPL-2.0-only. */
#ifndef ARC_RPC_PROFILE_H
#define ARC_RPC_PROFILE_H
#include <stdbool.h>
#include <stdint.h>
#if defined(ARCDROID_RPC_PROFILE)
#if !defined(ARCDROID_TEST_FAULTS)
#error RPC profiling requires a test build
#endif
#include "rpc_scheduler.h"
#define ARC_RPC_PROFILE_EVENTS 9u
_Static_assert(ARC_RPC_PROFILE_EVENTS == ARC_RPC_EVENT_COUNT, "profile schema 1 event count");
#define ARC_RPC_PROFILE_CLASSES 13u
#define ARC_RPC_PROFILE_EXITS 12u
#define ARC_RPC_PROFILE_BUCKETS 65u
#define ARC_RPC_PROFILE_RECENT 1000000u
/* Order is part of JSON schema 1. */
enum { ARC_RPC_COST_FETCH, ARC_RPC_COST_DISPATCH, ARC_RPC_COST_TIMING,
       ARC_RPC_COST_BUS, ARC_RPC_COST_MMIO, ARC_RPC_COST_DEVICES, ARC_RPC_COST_PROFILE, ARC_RPC_COST_COUNT };
enum { ARC_RPC_EXIT_E1, ARC_RPC_EXIT_E2, ARC_RPC_EXIT_E3, ARC_RPC_EXIT_E4,
       ARC_RPC_EXIT_E5, ARC_RPC_EXIT_E6, ARC_RPC_EXIT_E7A, ARC_RPC_EXIT_E7B,
       ARC_RPC_EXIT_E8, ARC_RPC_EXIT_E9, ARC_RPC_EXIT_E10, ARC_RPC_EXIT_E11 };
typedef struct {
    uint64_t attempts, fetched, condition_passed, candidates;
    uint64_t classes[ARC_RPC_PROFILE_CLASSES], exits[ARC_RPC_PROFILE_EXITS];
    uint64_t blocks[ARC_RPC_PROFILE_BUCKETS], block_instructions, block_max, open_block, censored_blocks, censored_instructions;
    uint64_t fetch_memo_hits, cache_fills, bus_calls;
    uint64_t branches, pc_writes, reads, writes, mmio_reads, mmio_writes;
    uint64_t recent_writes, recent_pages, events[ARC_RPC_PROFILE_EVENTS], resets, faults, epoch;
    uint64_t batch_quota, batch_boundary, batch_idle, batch_test_stop;
    uint64_t sampled_ns[ARC_RPC_COST_COUNT], samples[ARC_RPC_COST_COUNT], clock_reads, abandoned_samples;
} ArcRpcProfile;
void arc_rpc_profile_configure(unsigned stride, uint64_t (*clock_ns)(void));
void arc_rpc_profile_snapshot(ArcRpcProfile *out);
/* Seven charged probe counts, then calibration elapsed ns and iterations. */
void arc_rpc_profile_probe_snapshot(uint64_t out[ARC_RPC_COST_COUNT+2]);
void arc_rpc_profile_reset(void);
void arc_rpc_profile_fault(void);
void arc_rpc_profile_begin(uint32_t pc, uint32_t mode, uint32_t masks);
void arc_rpc_profile_fetched(const uint32_t *page, uint32_t opcode, bool passed);
void arc_rpc_profile_end(uint32_t pc, uint32_t mode, uint32_t masks, bool idle);
void arc_rpc_profile_exception(uint32_t vector);
void arc_rpc_profile_maintenance(void);
void arc_rpc_profile_side_exit(void);
void arc_rpc_profile_access(uint32_t physical, bool write);
void arc_rpc_profile_memo(void);
void arc_rpc_profile_fill(void);
void arc_rpc_profile_mmio(bool write);
void arc_rpc_profile_store(const void *base, uint32_t offset);
void arc_rpc_profile_batch(bool quota, bool idle, bool stopped);
void arc_rpc_profile_event(unsigned event);
int arc_rpc_profile_enter(unsigned cost);
void arc_rpc_profile_leave(int previous);
void arc_rpc_profile_stage(unsigned cost);
#define ARC_RPC_PROFILE(call) arc_rpc_profile_##call
#else
#define ARC_RPC_PROFILE(call) ((void)0)
#endif
#endif
