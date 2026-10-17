/* ArcDroid private RPCEmu session/test seam. GPL-2.0-only. */
#ifndef ARC_RPC_SESSION_H
#define ARC_RPC_SESSION_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "rpc_audio.h"
#include "libretro.h"
#include "rpc_lockstep.h"
#if defined(ARCDROID_TEST_FAULTS) || defined(ARCDROID_TEST_API)
RETRO_API bool arc_rpc_test_lockstep_sink(ArcRpcLockstepSink, void *, const ArcRpcLockstepConfig *);
RETRO_API const char *arc_rpc_test_lockstep_status(void);
RETRO_API bool arc_rpc_test_lockstep_end(void);
#endif
#include "arcdroid_core.h"
typedef enum { ARC_RPC_ARM610, ARC_RPC_ARM710, ARC_RPC_SA110, ARC_RPC_ARM7500,
    ARC_RPC_MODEL_COUNT } ArcRpcModel;

typedef enum { ARC_RPC_CAPTURE_NONE, ARC_RPC_CAPTURE_RUNNING, ARC_RPC_CAPTURE_DONE,
    ARC_RPC_CAPTURE_CANCELLED, ARC_RPC_CAPTURE_FAILED } ArcRpcCaptureState;
typedef struct {
    ArcRpcCaptureState state; uint64_t elapsed_ns, writes;
    bool delete_held; uint8_t bytes[256], written[32];
} ArcRpcCapture;
bool arc_rpc_session_start_begin(ArcRpcModel,const uint8_t *,size_t,const uint8_t[256],uint64_t,bool,char *,size_t);
bool arc_rpc_session_start_step(bool *,char *,size_t);
bool arc_rpc_session_factory_start(ArcRpcModel model, const uint8_t *rom, size_t size,
    uint64_t epoch, char *error, size_t error_size);
void arc_rpc_capture_status(ArcRpcCapture *out);
bool arc_rpc_session_capture_schedule(uint64_t deadline_ns, uint64_t delete_ns);
uint64_t arc_rpc_session_generation(void);
unsigned arc_rpc_session_cpu(void);
bool arc_rpc_session_firmware(ArcDigest *);
bool arc_rpc_session_advance(uint64_t elapsed_ns);
/* Resolved construction; ordinary frontend policy remains outside this API. */
bool arc_rpc_session_start(ArcRpcModel model, const uint8_t *rom, size_t size,
    const uint8_t seed[256], uint64_t epoch, char *error, size_t error_size);
#if defined(ARCDROID_TEST_FAULTS) || defined(ARCDROID_TEST_API)
void arc_rpc_session_test_rate(unsigned rate);
enum { ARC_RPC_TEST_TRACE=1u, ARC_RPC_TEST_HOT=2u, ARC_RPC_TEST_RANKING=4u,
    ARC_RPC_TEST_EVENT_DIAGNOSTICS=8u, ARC_RPC_TEST_FRAME_TIMER=16u,
    ARC_RPC_TEST_LOCKSTEP=32u };
RETRO_API unsigned arc_rpc_test_capabilities(void);
RETRO_API const char *arc_rpc_test_refusal(void);
bool arc_rpc_test_refuse(const char *facility);
RETRO_API void arc_rpc_session_test_quota(unsigned);
void arc_rpc_session_test_input_ready(void);
#include "libretro.h"
#include "rpc_mem_timing.h"
RETRO_API void arc_rpc_test_timing_measure(uint64_t first, uint64_t last, bool drained);
RETRO_API bool arc_rpc_test_exercise(ArcRpcExercise *out);
RETRO_API bool arc_rpc_test_ranking(ArcRpcExercise *out, uint64_t *attempts);
RETRO_API bool arc_rpc_test_interval_exercise(ArcRpcExercise *out);
RETRO_API bool arc_rpc_test_timing_endpoint(uint64_t first, uint64_t last, bool drained);
RETRO_API void arc_rpc_test_timing_interval(ArcRpcMemInterval *out);
#include "rpc_video.h"
RETRO_API bool arc_rpc_test_factory_start(ArcRpcModel model, const uint8_t *rom, size_t size,
    uint64_t epoch, char *error, size_t error_size);
RETRO_API void arc_rpc_test_cmos_snapshot(uint8_t *out);
RETRO_API void arc_rpc_test_capture_status(ArcRpcCapture *out);
RETRO_API void arc_rpc_test_audio_counts(ArcRpcAudioCounts *out);
RETRO_API size_t arc_rpc_test_audio_peek(int16_t *out, size_t capacity);
RETRO_API void arc_rpc_test_audio_commit(size_t offered, size_t accepted);
RETRO_API void arc_rpc_test_video_status(ArcRpcVideoStatus *out);
RETRO_API uint64_t arc_rpc_test_video_bus_ns(void);
#include "engine_backend.h"
RETRO_API const ArcEngineBackendOps *arc_rpc_test_engine_api(void);
typedef struct {
    uint64_t now_ns, executed, idle_ns, events[9];
    uint32_t regs[17], irq, fiq;
    unsigned allocations, live_allocations, rate_mips;
    bool faulted, active;
    unsigned trace_count;
    struct { uint64_t ns; unsigned event; } trace[64];
} ArcRpcTestSnapshot;
RETRO_API bool arc_rpc_test_start(ArcRpcModel model, const uint8_t *rom, size_t size,
    const uint8_t seed[256], uint64_t epoch, char *error, size_t error_size);
RETRO_API bool arc_rpc_test_advance(uint64_t elapsed_ns);
RETRO_API uint64_t arc_rpc_test_session_generation(void);
RETRO_API void arc_rpc_test_snapshot(ArcRpcTestSnapshot *out);
RETRO_API bool arc_rpc_test_read_ram(unsigned offset, uint32_t *out);
RETRO_API void arc_rpc_test_fail_alloc(unsigned stage);
RETRO_API void arc_rpc_test_fault_next(void);
#endif
#endif
