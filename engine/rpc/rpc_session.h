/* ArcDroid private RPCEmu session/test seam. GPL-2.0-only. */
#ifndef ARC_RPC_SESSION_H
#define ARC_RPC_SESSION_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "rpc_audio.h"
typedef enum { ARC_RPC_ARM610, ARC_RPC_ARM710, ARC_RPC_SA110, ARC_RPC_ARM7500,
    ARC_RPC_MODEL_COUNT } ArcRpcModel;

typedef enum { ARC_RPC_CAPTURE_NONE, ARC_RPC_CAPTURE_RUNNING, ARC_RPC_CAPTURE_DONE,
    ARC_RPC_CAPTURE_CANCELLED, ARC_RPC_CAPTURE_FAILED } ArcRpcCaptureState;
typedef struct {
    ArcRpcCaptureState state; uint64_t elapsed_ns, writes;
    bool delete_held; uint8_t bytes[256], written[32];
} ArcRpcCapture;
bool arc_rpc_session_factory_start(ArcRpcModel model, const uint8_t *rom, size_t size,
    uint64_t epoch, char *error, size_t error_size);
void arc_rpc_capture_status(ArcRpcCapture *out);
uint64_t arc_rpc_session_generation(void);
/* Never included by shipping frontend launch code. */
bool arc_rpc_session_start(ArcRpcModel model, const uint8_t *rom, size_t size,
    const uint8_t seed[256], uint64_t epoch, char *error, size_t error_size);
#if defined(ARCDROID_TEST_FAULTS)
void arc_rpc_session_test_rate(unsigned rate);
void arc_rpc_session_test_input_ready(void);
#include "libretro.h"
#include "rpc_mem_timing.h"
RETRO_API void arc_rpc_test_timing_measure(uint64_t first, uint64_t last, bool drained);
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
RETRO_API void arc_rpc_test_snapshot(ArcRpcTestSnapshot *out);
RETRO_API bool arc_rpc_test_read_ram(unsigned offset, uint32_t *out);
RETRO_API void arc_rpc_test_fail_alloc(unsigned stage);
RETRO_API void arc_rpc_test_fault_next(void);
#endif
#endif
