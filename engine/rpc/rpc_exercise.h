/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef ARC_RPC_EXERCISE_H
#define ARC_RPC_EXERCISE_H
#include <stdint.h>
#include <stdbool.h>
/* Stable schema-2 scalar order, followed by two 64-way hit histograms. */
#define ARC_RPC_COUNTER_NAMES(X) \
 X(lookup_access_empty) X(lookup_access_miss) X(lookup_access_hit) \
 X(lookup_access_probes) X(lookup_access_hit_probes) X(lookup_access_beyond_zero) \
 X(lookup_maintenance_empty) X(lookup_maintenance_miss) X(lookup_maintenance_hit) \
 X(lookup_maintenance_probes) X(lookup_maintenance_hit_probes) X(lookup_maintenance_beyond_zero) \
 X(service_pending) X(service_video) X(service_due_finish) X(service_s1) X(service_busy) X(service_other) \
 X(service_finish_valid) X(service_finish_invalid) \
 X(fill_fetch_memory) X(fill_fetch_rom) X(fill_data_memory) X(fill_data_rom) \
 X(shortcut_attempts) X(shortcut_commits) X(shortcut_rollbacks) X(shortcut_memory_fills) \
 X(tlb_data_walk) X(tlb_data_hit) X(tlb_data_eligible) \
 X(tlb_fetch_walk) X(tlb_fetch_hit) X(tlb_fetch_eligible) \
 X(integer_unobserved) X(integer_observed) X(integer_cheap) \
 X(rational_unobserved) X(rational_observed) X(rational_cheap)
#define ARC_RPC_COUNTER_ENUM(name) ARC_RPC_COUNT_##name,
enum { ARC_RPC_COUNTER_NAMES(ARC_RPC_COUNTER_ENUM) ARC_RPC_COUNT_SCALARS,
    ARC_RPC_COUNT_ACCESS_WAYS=ARC_RPC_COUNT_SCALARS,
    ARC_RPC_COUNT_MAINTENANCE_WAYS=ARC_RPC_COUNT_SCALARS+64,
    ARC_RPC_COUNT_TOTAL=ARC_RPC_COUNT_SCALARS+128 };
#undef ARC_RPC_COUNTER_ENUM
typedef struct { uint64_t values[ARC_RPC_COUNT_TOTAL]; } ArcRpcExercise;
#if defined(ARCDROID_TEST_FAULTS) && !defined(ARCDROID_RPC_MEASUREMENT)
extern ArcRpcExercise arc_rpc_exercise;
extern unsigned arc_rpc_exercise_path;
#if defined(ARCDROID_RPC_PROFILE)
void arc_rpc_profile_rank(unsigned index, uint64_t amount);
#endif
static inline void arc_rpc_exercise_add(unsigned index,uint64_t amount)
{
    arc_rpc_exercise.values[index] += amount;
#if defined(ARCDROID_RPC_PROFILE)
    arc_rpc_profile_rank(index,amount);
#endif
}
#define ARC_RPC_COUNT(name,amount) arc_rpc_exercise_add(ARC_RPC_COUNT_##name,(amount))
void arc_rpc_mem_exercise_begin(bool batched);
void arc_rpc_mem_exercise_end(void);
#else
#define ARC_RPC_COUNT(name,amount) ((void)0)
#endif
#endif
