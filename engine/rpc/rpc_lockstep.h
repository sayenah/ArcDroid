/* SPDX-License-Identifier: GPL-2.0-only */
/* Private lockstep wire API. Only the lockstep core owns an emitter. */
#ifndef ARC_RPC_LOCKSTEP_H
#define ARC_RPC_LOCKSTEP_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef bool (*ArcRpcLockstepSink)(void *, const void *, size_t);
typedef struct {
    uint32_t version, size, model, flags;
    uint64_t seed, detail_first, detail_last, stop_after, inject_k;
    uint64_t detail_generation, detail_epoch;
    uint32_t inject_kind, allocation_shift;
    uint8_t plan[32], schedule[32], configuration[32];
} ArcRpcLockstepConfig;
enum {
    ARC_RPC_LOCKSTEP_BASELINE=1u, ARC_RPC_LOCKSTEP_EVERY=2u,
    ARC_RPC_LOCKSTEP_AUDIT=4u, ARC_RPC_LOCKSTEP_P1_OFF=8u,
    ARC_RPC_LOCKSTEP_DETAIL=16u
};
#if defined(ARCDROID_RPC_LOCKSTEP)
void arc_rpc_lockstep_p0(unsigned);
void arc_rpc_lockstep_p1(unsigned, unsigned);
void arc_rpc_lockstep_p2(void);
void arc_rpc_lockstep_p3(unsigned);
void arc_rpc_lockstep_batch(uint64_t, unsigned, bool);
void arc_rpc_lockstep_driver(uint64_t, unsigned, uint64_t, uint64_t, bool, bool);
void arc_rpc_lockstep_epoch_begin(void);
void arc_rpc_lockstep_epoch_end(uint64_t, bool);
void arc_rpc_lockstep_epoch_abort(uint64_t);
void arc_rpc_lockstep_fatal(const char *, const char *);
void arc_rpc_lockstep_fatal_guard(void);
void arc_rpc_lockstep_frame(void);
void arc_rpc_lockstep_ram_write(uint32_t, unsigned, uint64_t);
void arc_rpc_lockstep_mmio(uint32_t, unsigned, bool, uint64_t);
void arc_rpc_lockstep_agent(unsigned);
bool arc_rpc_lockstep_inject(unsigned);
void arc_rpc_lockstep_prepared(bool);
void arc_rpc_lockstep_register(unsigned, const void *, size_t);
void arc_rpc_lockstep_pointer(uint64_t *, const void *, unsigned, unsigned);
uint64_t arc_rpc_lockstep_register_index(const uint32_t *);
uint64_t arc_rpc_lockstep_double(double);
uint64_t arc_rpc_lockstep_identity(unsigned, const void *, size_t);
uint64_t arc_rpc_lockstep_string_index(const char *, const char *const *, unsigned);
void *arc_rpc_lockstep_alloc(size_t);
void arc_rpc_lockstep_free(void *);
void arc_rpc_lockstep_dirty(unsigned);
bool arc_rpc_lockstep_collect_due(unsigned, bool);
void arc_rpc_lockstep_continuation(bool);
void arc_rpc_lockstep_output_word(uint64_t);
void arc_rpc_lockstep_content_begin(void);
void arc_rpc_lockstep_content_bytes(const void *,size_t);
uint64_t arc_rpc_lockstep_content_end(void);
void arc_rpc_lockstep_video_output(void);
void arc_rpc_lockstep_audio_output(void);
#define ARC_RPC_LOCKSTEP(call) arc_rpc_lockstep_##call
#else
#define ARC_RPC_LOCKSTEP(call) ((void)0)
#endif
#endif
