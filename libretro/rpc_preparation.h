/* Dormant session orchestration. GPL-2.0-only. */
#ifndef ARC_RPC_PREPARATION_H
#define ARC_RPC_PREPARATION_H
#include "arcdroid_engine.h"
#include "rpc_selection.h"
#include "rpc_cmos_store.h"
#include "rpc_boot.h"
#define ARC_RPC_PREP_BUDGET_NS UINT64_C(3333333)
#define ARC_RPC_PREP_NAMESPACE_NS UINT64_C(8333333)
#define ARC_RPC_PREP_UNIT_NS UINT64_C(1000000)
#define ARC_RPC_GENERATION_BUDGET_NS UINT64_C(66666664)
#define ARC_RPC_GENERATION_UNIT_NS ARC_RPC_GENERATION_BUDGET_NS
#define ARC_RPC_GENERATION_HEADROOM_NS UINT64_C(2666664)
#define ARC_RPC_GENERATION_QUANTUM_NS UINT64_C(16666667)
#define ARC_RPC_PREP_STEPS 4096u
#define ARC_RPC_COST_SAMPLES 16u
typedef enum {
    ARC_RPC_PREP_VERIFY, ARC_RPC_PREP_RECOVER, ARC_RPC_PREP_VALIDATE,
    ARC_RPC_PREP_RECORD, ARC_RPC_PREP_DEFAULTS, ARC_RPC_PREP_GENERATE,
    ARC_RPC_PREP_SAVE_DEFAULT, ARC_RPC_PREP_SAVE_RECORD, ARC_RPC_PREP_POPULATE,
    ARC_RPC_PREP_START, ARC_RPC_PREP_RUNNING, ARC_RPC_PREP_FAILED,
    ARC_RPC_PREP_CANCELLED
} ArcRpcPreparationState;
typedef struct {
    uint64_t (*clock)(void *);
    void (*notice)(void *, const char *, bool terminal);
    void (*log)(void *, const char *);
    /* Final media preparation/attachment, no guest execution. Backing stays
     * frontend-owned through engine stop. Optional rate seam stays in client. */
    bool (*attach)(void *, char *, size_t);
    void (*started)(void *, bool factory);
    void *opaque;
} ArcRpcPreparationHost;
typedef struct {
    const uint8_t *firmware; size_t firmware_size;
    const ArcRpcFirmwareFingerprint *fingerprint;
    ArcRpcSelection selection;
    ArcRpcCmosKey key;
    const char *save, *system, *import_path;
    const uint8_t *seed; /* 256 bytes; verified absence only */
    const char *seed_path; /* Low-level alternative; read only after verified absence. */
    const ArcEngineMedia *media, *helper;
    uint64_t epoch;
    bool hard_disc, populate;
} ArcRpcPreparationRequest;
typedef struct ArcRpcPreparation ArcRpcPreparation;
typedef struct {
    uint64_t frames, elapsed_ns, max_frame_ns, reserved_ns, namespace_attempts;
    uint64_t budget_ns, attachment_operations, attachment_bytes, attachment_namespace;
    unsigned steps, generation_quanta, namespace_units, attachment_units;
    ArcRpcPreparationState state;
} ArcRpcPreparationStats;
/* Resolved policy entry requires the qualified pair and matching catalogue.
 * Copies firmware, seed, paths, key, descriptors and callback table. The host
 * opaque context and media backing remain borrowed through free/stop. */
ArcRpcPreparation *arc_riscpc_preparation_begin(const ArcRpcPreparationRequest *,
    const ArcRpcPreparationHost *, char *, size_t);
/* Explicit low-level caller: authored ROMs, arbitrary layout/model, optional
 * IDE/import/seed. Never used by ordinary content admission. */
ArcRpcPreparation *arc_riscpc_preparation_low_level(const ArcRpcPreparationRequest *,
    const ArcRpcPreparationHost *, char *, size_t);
ArcRpcPreparationState arc_riscpc_preparation_frame(ArcRpcPreparation *);
ArcRpcPreparationState arc_riscpc_preparation_state(const ArcRpcPreparation *);
const char *arc_riscpc_preparation_error(const ArcRpcPreparation *);
void arc_riscpc_preparation_stats(const ArcRpcPreparation *, ArcRpcPreparationStats *);
ArcRpcHdStore *arc_riscpc_preparation_disc(ArcRpcPreparation *);
bool arc_riscpc_preparation_reset(ArcRpcPreparation *);
void arc_riscpc_preparation_cancel(ArcRpcPreparation *);
/* Running schedule: exactly one HD tick + eager persistence, no batching clock. */
void arc_riscpc_preparation_maintenance(ArcRpcPreparation *);
bool arc_riscpc_preparation_persist(ArcRpcPreparation *, char *, size_t);
/* Persist latest snapshot on normal unload, then detach/free even on failure. */
bool arc_riscpc_preparation_free(ArcRpcPreparation *, char *, size_t);
bool arc_riscpc_preparation_pacing(ArcRpcPreparation *, unsigned);
uint64_t arc_riscpc_preparation_clock(void *);
#endif
