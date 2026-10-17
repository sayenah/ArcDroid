/* Session-owned RPC observation services, D-174/D-175. GPL-2.0-only. */
#ifndef ARC_RPC_OBSERVER_H
#define ARC_RPC_OBSERVER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { ARC_RPC_OBSERVERS=4, ARC_RPC_WATCHES=8, ARC_RPC_WORK_LIMIT=16,
       ARC_RPC_COPY_LIMIT=256, ARC_RPC_TRACE_LIMIT=65536,
       ARC_RPC_WATCH_ATTEMPT_LIMIT=1000000000 };
typedef enum { ARC_RPC_OBS_BOOT, ARC_RPC_OBS_LAUNCH, ARC_RPC_OBS_ERROR,
               ARC_RPC_OBS_DISC } ArcRpcObserver;
typedef enum { ARC_RPC_OBS_SWI=1, ARC_RPC_OBS_CONTINUATION=2,
               ARC_RPC_OBS_MODE=4, ARC_RPC_OBS_GAP=8, ARC_RPC_OBS_WATCH_LOST=16 } ArcRpcObservationKind;
typedef enum { ARC_RPC_BACKING_NONE, ARC_RPC_BACKING_ROM,
               ARC_RPC_BACKING_RAM0, ARC_RPC_BACKING_RAM1,
               ARC_RPC_BACKING_VRAM } ArcRpcBackingKind;
typedef struct { ArcRpcBackingKind kind; uint32_t offset; } ArcRpcBacking;
typedef enum { ARC_RPC_INSPECT_OK, ARC_RPC_INSPECT_REFUSED,
               ARC_RPC_INSPECT_TRUNCATED } ArcRpcInspectStatus;
typedef struct { ArcRpcInspectStatus status; size_t copied; } ArcRpcInspectResult;
typedef struct {
    uint64_t order, instruction, epoch, ns, watch;
    ArcRpcObservationKind kind;
    uint32_t swi, pc, old_mode, mode, nzcv, registers[15];
    uint16_t register_mask;
    uint8_t width; bool privileged, matched;
    ArcRpcBacking backing;
} ArcRpcObservation;
typedef void (*ArcRpcObserveCallback)(void *, const ArcRpcObservation *);
typedef struct {
    uint32_t swi, swi_mask, modes;
    uint16_t registers, results;
    unsigned kinds;
    ArcRpcObserveCallback callback; void *opaque;
    /* Optional bounded, host-owned list; it must outlive the filter. Numbers
     * ignore X, while swi/swi_mask still admit exact decorations. Count zero
     * preserves the ordinary mask-only subscription. */
    const uint32_t *swis; unsigned swi_count;
} ArcRpcObserveFilter;
typedef enum { ARC_RPC_CAP_OBSERVE, ARC_RPC_CAP_COMPLETE,
               ARC_RPC_CAP_REPLACE, ARC_RPC_CAP_OBSERVE_B, ARC_RPC_CAP_REPLACE_B } ArcRpcObserverCapability;
typedef struct {
    unsigned catalogue, cpu, profile;
    ArcRpcObserver observer; ArcRpcObserverCapability capability;
    bool enabled; const char *reason;
} ArcRpcObserverQualification;
ArcRpcObserverQualification arc_rpc_observer_qualification(unsigned catalogue,
    unsigned cpu, ArcRpcObserver, ArcRpcObserverCapability);

bool arc_rpc_observer_filter(ArcRpcObserver, const ArcRpcObserveFilter *);
bool arc_rpc_observer_watch_live(ArcRpcObserver, uint64_t);
/* Renew only a live current-generation watch from its owner's SWI callback.
 * Continuation identity and return state are preserved. */
bool arc_rpc_observer_watch_renew(ArcRpcObserver, uint64_t, unsigned attempts);
/* The interpreter tests this before constructing any observation context. */
extern bool arc_rpc_observer_active, arc_rpc_observer_context;
void arc_rpc_observe_batch_begin(void);
void arc_rpc_observe_advance(unsigned attempts);
void arc_rpc_observe_batch_end(void);
void arc_rpc_observe_unsubscribed_mode(uint32_t old_mode);
bool arc_rpc_observe_swi_at(uint32_t opcode,uint32_t pc,const uint32_t *page);

void arc_rpc_observe_begin(uint32_t pc);
void arc_rpc_observe_fetched(const uint32_t *page);
bool arc_rpc_observe_swi(uint32_t opcode);
void arc_rpc_observe_mode(void);
void arc_rpc_observe_return(uint32_t completed_mode);
ArcRpcBacking arc_rpc_observer_backing(const uint32_t *word);

/* Epoch retirement is called by the session before resource retirement. */
void arc_rpc_observer_retire(bool retain_trace);
uint64_t arc_rpc_observer_epoch(void);
bool arc_rpc_observer_unavailable(ArcRpcObserver,ArcRpcObserverCapability);
uint64_t arc_rpc_observer_generation(ArcRpcObserver);
void arc_rpc_observer_cancel(ArcRpcObserver);
uint64_t arc_rpc_observer_watch(ArcRpcObserver, uint32_t pc, uint32_t mode,
    uint32_t sp, unsigned attempts);
/* Only a current synchronous SWI callback can complete. Mask is R0–R12 only. */
bool arc_rpc_observer_complete(ArcRpcObserver, uint64_t event, uint64_t epoch,
    uint64_t generation, uint16_t mask, const uint32_t registers[13], uint32_t cv);
bool arc_rpc_observer_answer(ArcRpcObserver, uint32_t value);
bool arc_rpc_observer_take_answer(ArcRpcObserver, uint32_t *value);

/* These never call execution translation or device reads. */
bool arc_rpc_inspect_byte(uint32_t va, bool privileged, uint8_t *out);
bool arc_rpc_inspect_word(uint32_t va, bool privileged, uint32_t *out);
ArcRpcInspectResult arc_rpc_inspect_bytes(uint32_t va, bool privileged,
    void *out, size_t length);
ArcRpcInspectResult arc_rpc_inspect_string(uint32_t va, bool privileged,
    char *out, size_t capacity);
bool arc_rpc_inspect_physical(uint32_t address, uint8_t *out);
bool arc_rpc_inspect_translate(uint32_t va, bool privileged, uint32_t *physical);

typedef struct {
    ArcRpcObserver observer; uint64_t generation, epoch, media_generation, helper_generation, ns;
    uint64_t previous_ns; /* Previous source observation, copied by the owner. */
    unsigned kind; uint32_t operation, continuation, sp; uint64_t watch;
    size_t size; uint8_t bytes[ARC_RPC_COPY_LIMIT];
} ArcRpcHostWork;
typedef struct {
    bool (*valid)(void *, const ArcRpcHostWork *);
    bool (*resolve)(void *, const ArcRpcHostWork *);
    void (*mutate)(void *, const ArcRpcHostWork *);
    void *opaque;
} ArcRpcHostWorkHandler;
bool arc_rpc_observer_queue(const ArcRpcHostWork *);
/* Inspect copied work before a possible overflow retires its generation.
 * No host work runs. */
bool arc_rpc_observer_queued(ArcRpcObserver,unsigned);
/* Drop only matching copied payloads, preserving other identities/order. */
void arc_rpc_observer_drop_queued(ArcRpcObserver,unsigned,const void *,size_t);
void arc_rpc_observer_host_handler(ArcRpcObserver, const ArcRpcHostWorkHandler *);
bool arc_rpc_observer_in_host(void);
void arc_rpc_observer_drain(void); /* Frame boundary only, never guest execution. */

#if defined(ARCDROID_TEST_FAULTS) || defined(ARCDROID_TEST_API)
const ArcRpcObserverQualification *arc_rpc_observer_test_qualifications(size_t *count);
#include "libretro.h"
extern uint64_t arc_rpc_observer_integer_attempts;
RETRO_API bool arc_rpc_observer_test_filter(ArcRpcObserver, const ArcRpcObserveFilter *);
typedef struct {
    ArcRpcObservation event;
    ArcRpcInspectStatus inspection;
    bool scalar_valid; uint32_t scalar;
    char path[ARC_RPC_COPY_LIMIT];
    uint64_t lost, lost_end_ns;
} ArcRpcObservationTrace;
typedef struct { unsigned count; uint64_t lost, integer_attempts; bool enabled, failed, incomplete; } ArcRpcTraceStatus;
typedef struct {
    unsigned classes, capacity, swi_count;
    uint32_t swis[64];
} ArcRpcTraceSelection;
RETRO_API bool arc_rpc_observer_trace_select(const ArcRpcTraceSelection *);
typedef bool (*ArcRpcTraceSink)(void *, const ArcRpcObservationTrace *);
RETRO_API void arc_rpc_observer_trace(bool enabled, bool private_paths);
RETRO_API void arc_rpc_observer_trace_status(ArcRpcTraceStatus *);
RETRO_API size_t arc_rpc_observer_trace_read(ArcRpcObservationTrace *, size_t);
RETRO_API void arc_rpc_observer_trace_drain(ArcRpcTraceSink, void *);
/* Byte snapshots are bounded and test-only; no guest memory is included. */
size_t arc_rpc_inspector_snapshot(void *, size_t);
size_t arc_rpc_mem_observation_snapshot(void *, size_t);
RETRO_API void arc_rpc_observer_test_seed_pending(void);
RETRO_API bool arc_rpc_observer_test_clean(void);
RETRO_API uint64_t arc_rpc_observer_test_epoch(void);
uint64_t arc_rpc_observer_test_attempts(void);
ArcRpcObservation arc_rpc_observer_test_origin(void); /* Captured metadata only. */
uint64_t arc_rpc_observer_test_events(void);
uint64_t arc_rpc_observer_test_captures(void);
uint64_t arc_rpc_observer_test_number_checks(void);
#endif
#endif
