/* ArcDroid RiscPC owned hard-disc store. GPL-2.0-only. */
#ifndef ARC_RPC_HD_STORE_H
#define ARC_RPC_HD_STORE_H
#include "arc_storage.h"
#include "arc_lock.h"
#include <stddef.h>
#include <stdint.h>
#define ARC_RPC_HD_CHUNK 262144u
#define ARC_RPC_HD_CHUNKS 504u
#define ARC_RPC_HD_JOURNAL_CAP 8388608u
#define ARC_RPC_HD_TRIGGER 1048576u
#define ARC_RPC_HD_STEP_BYTES 788612u
#define ARC_RPC_HD_STEP_OPERATIONS 1600u
#define ARC_RPC_HD_STEP_HASH_BYTES 1572864u
#define ARC_RPC_HD_STEP_COPY_BYTES 2097152u
typedef struct ArcRpcHdStore ArcRpcHdStore;
#define ARC_RPC_HD_IDENTITY_REVISION 1u
#define ARC_RPC_HD_IDENTITY_HASH_BYTES 32768u
#define ARC_RPC_HD_IDENTITY_WORKSPACE 1024u
typedef struct { uint8_t bytes[32]; } ArcRpcHdIdentity;
/* Exclusively owned, published writable IDLE view only. Zero I/O/allocation;
 * hashes resident root + namespace + maintained journal digest. Recovery
 * suppresses capture until a clean reopen. docs/riscpc-launch-plan.md. */
bool arc_riscpc_hd_identity(ArcRpcHdStore *, ArcRpcHdIdentity *);
bool arc_riscpc_hd_identity_matches(ArcRpcHdStore *, const ArcRpcHdIdentity *);
typedef enum { ARC_RPC_HD_OPEN_PENDING, ARC_RPC_HD_UNPUBLISHED,
    ARC_RPC_HD_UNSUPPORTED, ARC_RPC_HD_UNAVAILABLE, ARC_RPC_HD_CORRUPT,
    ARC_RPC_HD_LEGACY, ARC_RPC_HD_RECEIPTED } ArcRpcHdOutcome;
/* No I/O at begin; one deterministic preparation step per call. */
ArcRpcHdStore *arc_riscpc_hd_open_begin(const char *, const char *, ArcLock *, char *, size_t);
ArcRpcHdOutcome arc_riscpc_hd_open_step(ArcRpcHdStore *);
ArcRpcHdOutcome arc_riscpc_hd_outcome(const ArcRpcHdStore *);
const char *arc_riscpc_hd_outcome_notice(ArcRpcHdOutcome);
/* 0 recovery, 1 population I/O, 2 seal, 3 publish, 4 abort. Worst-case admission. */
bool arc_riscpc_hd_next_namespace(const ArcRpcHdStore *, unsigned operation);
/* Host-owned creation provenance; never represented by guest sectors. */
typedef struct {
    uint8_t packages[2][32];
    uint32_t receipt_revision, populator_revision, authored_revision;
    uint64_t files, bytes;
} ArcRpcHdReceipt;
bool arc_riscpc_hd_receipt(const ArcRpcHdStore *, ArcRpcHdReceipt *);
bool arc_riscpc_hd_population_receipt(ArcRpcHdStore *, const ArcRpcHdReceipt *);
/* One chunk verification or one control operation; done only after sealing. */
bool arc_riscpc_hd_population_seal_step(ArcRpcHdStore *, bool *done);
/* Start captures input; each step performs one chunk/control substep. */
bool arc_riscpc_hd_population_io_begin(ArcRpcHdStore *, uint32_t, uint32_t,
                                      uint8_t *, bool write);
bool arc_riscpc_hd_population_io_step(ArcRpcHdStore *, bool *done);
bool arc_riscpc_hd_population_publish_step(ArcRpcHdStore *, bool *done);
typedef enum { ARC_RPC_HD_IDLE, ARC_RPC_HD_ROTATE, ARC_RPC_HD_MATERIALIZE,
    ARC_RPC_HD_MANIFEST, ARC_RPC_HD_PUBLISH, ARC_RPC_HD_RETIRE, ARC_RPC_HD_GC,
    ARC_RPC_HD_READ_ONLY, ARC_RPC_HD_DAMAGED } ArcRpcHdState;
typedef struct {
    uint64_t bytes_read, bytes_written, operations, tick_bytes, tick_operations,
        max_tick_bytes, max_tick_operations, journal_high, allocation_high,
        commands, checkpoints, sequence, generation, requested_bytes, hash_bytes,
        copied_bytes, open_bytes, open_operations, advisory_operations,
        tick_hash_bytes, tick_copied_bytes, max_tick_hash_bytes, max_tick_copied_bytes, metadata_operations,
        tick_metadata_operations, max_tick_metadata_operations, namespace_operations,
        tick_namespace_operations, max_tick_namespace_operations;
    unsigned chunk_files, max_chunk_files, materialized, state, phase_step;
    bool published, population, read_only;
} ArcRpcHdStats;
/* No creation on damage; create_blank only admits an empty/unpublished store. */
ArcRpcHdStore *arc_riscpc_hd_open(const char *save, const char *content,
    bool create_blank, ArcLock *owner, char *error, size_t error_size);
/* Discard closes clean handles without namespace changes. */
bool arc_riscpc_hd_close(ArcRpcHdStore *store, bool discard);
void arc_riscpc_hd_backend(ArcRpcHdStore *store, ArcIdeBackend *out);
bool arc_riscpc_hd_read(ArcRpcHdStore *, uint32_t lba, uint32_t count, uint8_t *out);
bool arc_riscpc_hd_write(ArcRpcHdStore *, uint32_t lba, uint32_t count, const uint8_t *in);
void arc_riscpc_hd_tick(ArcRpcHdStore *store);
void arc_riscpc_hd_stats(const ArcRpcHdStore *, ArcRpcHdStats *out);
const char *arc_riscpc_hd_notice(ArcRpcHdStore *store);
/* Severity of the pending notice; query before consuming it. Recovery,
 * including a recovered read-only view, is an advisory. */
bool arc_riscpc_hd_notice_is_terminal(const ArcRpcHdStore *store);
const char *arc_riscpc_hd_diagnostic(ArcRpcHdStore *store);
/* Offline ownership: caller quiesces/detaches IDE before begin. R2 creates and
 * imports before attaching the backend; R3 must preserve that precondition. */
bool arc_riscpc_hd_population_begin(ArcRpcHdStore *store);
bool arc_riscpc_hd_population_blank_begin(ArcRpcHdStore *store);
bool arc_riscpc_hd_population_read(ArcRpcHdStore *, uint32_t lba, uint32_t count, uint8_t *out);
bool arc_riscpc_hd_population_write(ArcRpcHdStore *, uint32_t lba, uint32_t count, const uint8_t *in);
bool arc_riscpc_hd_population_seal(ArcRpcHdStore *store);
bool arc_riscpc_hd_population_publish(ArcRpcHdStore *store);
void arc_riscpc_hd_population_abort(ArcRpcHdStore *store);
bool arc_riscpc_hd_import(ArcRpcHdStore *store, const char *source);
#endif
