/* Dormant session orchestration. GPL-2.0-only. */
#ifndef ARC_RPC_PREPARATION_H
#define ARC_RPC_PREPARATION_H
#include "arcdroid_engine.h"
#include "rpc_selection.h"
#include "rpc_cmos_store.h"
#include "rpc_boot.h"
#include "rpc_boot_record.h"
#define ARC_RPC_PREP_OFFLINE_BUDGET_NS UINT64_C(13333333)
#define ARC_RPC_PREP_HEADROOM_NS UINT64_C(833333)
#define ARC_RPC_PREP_NAMESPACE_UNIT_NS UINT64_C(8333333)
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
#define ARC_RPC_PREP_PRODUCT_KIND (ARC_RPC_PREP_CANCELLED+(ARC_RPC_BOOT_CANCELLED+1)*ARC_RPC_BOOT_WORK_COUNT)
#define ARC_RPC_PREP_LOCK_KIND (ARC_RPC_PREP_PRODUCT_KIND+1)
#define ARC_RPC_PREP_COST_KINDS (ARC_RPC_PREP_LOCK_KIND+1)
typedef enum {
    ARC_RPC_NOTICE_PROGRESS, ARC_RPC_NOTICE_TERMINAL,
    ARC_RPC_NOTICE_ADVISORY, ARC_RPC_NOTICE_CLEAR
} ArcRpcNoticeSeverity;
typedef struct {
    ArcRpcNoticeSeverity severity;
    const char *id; /* Fixed catalogue ID/family, selected at the producer. */
    bool stopping, drain_outcome;
} ArcRpcNotice;
typedef struct {
    uint64_t (*clock)(void *);
    void (*notice)(void *, const char *, ArcRpcNotice);
    void (*log)(void *, const char *);
    /* Final media preparation/attachment, no guest execution. Backing stays
     * frontend-owned through engine stop. Optional rate seam stays in client. */
    bool (*attach)(void *, char *, size_t);
    void (*started)(void *, bool factory);
    void (*diagnostics)(void *);
    void (*attached)(void *);
    /* Frozen A-3 steps 3 and 5. Neither executes guest instructions. The
     * first re-stages drive 0 only; the second resets/rebinds observers/input. */
    bool (*restage_drive0)(void *,char *,size_t);
    bool (*reset_rebind)(void *,char *,size_t);
    bool (*restage_stopped)(void *,char *,size_t);
    /* Exact ordered media/policy lease: no I/O/allocation; <=32768 metadata bytes.
     * False refuses observation publication, never command delivery. */
    bool (*media_identity)(void *,uint8_t[32]);
    const ArcLockBackend *lock_backend; /* NULL: native sentinel backend. */
    void *lock_opaque;
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
    bool ordinary; /* M1 admission: advisory-only pair record, never a launch plan. */
    bool helper_override; /* Explicit drive1, including empty, suppresses generation. */
    const ArcRpcLaunchPlan *launch; /* NULL: delivery unavailable/no plan. Copied. */
    uint64_t session_id; /* Injected; unique per library load, kept across Restart; not RTC epoch. */
} ArcRpcPreparationRequest;
typedef struct ArcRpcPreparation ArcRpcPreparation;
typedef struct {
    uint64_t frames, elapsed_ns, max_frame_ns, reserved_ns, namespace_attempts;
    uint64_t lock_attempts,lock_wait_frames,identity_checks,identity_max_ns,lock_max_ns;
    uint64_t namespace_exceptions,checkpoint_exceptions,rollback_exceptions,rollback_max_ns;
    uint64_t restart_record_exceptions,restart_record_max_ns,restart_defaults_exceptions,restart_defaults_max_ns;
    uint64_t budget_ns, attachment_operations, attachment_bytes, attachment_namespace;
    /* Last estimate and kind: state, or CANCELLED + boot phase * WORK_COUNT
     * + work. Boot DONE groups low-level blank/import units without a boot
     * context. RUNNING denotes a virtual advance. */
    uint64_t estimated_ns;
    uint64_t helper_bytes; /* In-memory construction bytes this frame, no VFS I/O. */
    unsigned unit_kind;
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
void arc_riscpc_preparation_attempt(const ArcRpcPreparation *,ArcRpcLaunchAttempt *);
bool arc_riscpc_preparation_execution_allowed(const ArcRpcPreparation *);
bool arc_riscpc_preparation_observation(ArcRpcPreparation *,ArcRpcBootRecord *);
void arc_riscpc_preparation_interrupt(ArcRpcPreparation *,ArcRpcAttemptInterrupt);
/* Explicit low-level drive-1 lease, captured after frontend attachment. */
bool arc_riscpc_preparation_helper_override(ArcRpcPreparation *,const ArcEngineMedia *);
/* Step-3 callback may update permission, never backing identity/content. */
bool arc_riscpc_preparation_helper_write_policy(ArcRpcPreparation *,bool);
bool arc_riscpc_preparation_reset(ArcRpcPreparation *);
void arc_riscpc_preparation_cancel(ArcRpcPreparation *);
/* Borrowed ownership diagnostics; no caller may release these tokens. */
void arc_riscpc_preparation_pair(const ArcRpcPreparation *,unsigned *model,unsigned *os);
bool arc_riscpc_preparation_owned(const ArcRpcPreparation *);
unsigned arc_riscpc_preparation_catalogue(const ArcRpcPreparation *);
bool arc_riscpc_preparation_replace_media(ArcRpcPreparation *,const ArcEngineMedia *);
bool arc_riscpc_preparation_complete_publication(ArcRpcPreparation *,char *,size_t);
bool arc_riscpc_preparation_draining(const ArcRpcPreparation *);
void arc_riscpc_preparation_failed_frame(ArcRpcPreparation *);
bool arc_riscpc_preparation_hd_close_ok(const ArcRpcPreparation *);
bool arc_riscpc_preparation_take_hd_close(ArcRpcPreparation *); /* Consume this lifecycle event once. */
/* Running schedule: exactly one HD tick + eager persistence, no batching clock. */
void arc_riscpc_preparation_maintenance(ArcRpcPreparation *);
bool arc_riscpc_preparation_persist(ArcRpcPreparation *, char *, size_t);
/* Persist latest snapshot on normal unload, then detach/free even on failure. */
bool arc_riscpc_preparation_free_stats(ArcRpcPreparation *,char *,size_t,ArcRpcPreparationStats *);
bool arc_riscpc_preparation_free(ArcRpcPreparation *, char *, size_t);
bool arc_riscpc_preparation_pacing(ArcRpcPreparation *, unsigned);
uint64_t arc_riscpc_preparation_clock(void *);
#if defined(ARCDROID_TEST_FAULTS)
RETRO_API void arc_riscpc_test_fail_helper(void);
#endif
#endif
