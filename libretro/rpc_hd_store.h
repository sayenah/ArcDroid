/* ArcDroid RiscPC owned hard-disc store. GPL-2.0-only. */
#ifndef ARC_RPC_HD_STORE_H
#define ARC_RPC_HD_STORE_H
#include "arc_storage.h"
#include <stddef.h>
#include <stdint.h>
#define ARC_RPC_HD_CHUNK 262144u
#define ARC_RPC_HD_CHUNKS 504u
#define ARC_RPC_HD_JOURNAL_CAP 8388608u
#define ARC_RPC_HD_TRIGGER 1048576u
typedef struct ArcRpcHdStore ArcRpcHdStore;
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
    bool create_blank, char *error, size_t error_size);
void arc_riscpc_hd_close(ArcRpcHdStore *store);
void arc_riscpc_hd_backend(ArcRpcHdStore *store, ArcIdeBackend *out);
bool arc_riscpc_hd_read(ArcRpcHdStore *, uint32_t lba, uint32_t count, uint8_t *out);
bool arc_riscpc_hd_write(ArcRpcHdStore *, uint32_t lba, uint32_t count, const uint8_t *in);
void arc_riscpc_hd_tick(ArcRpcHdStore *store);
void arc_riscpc_hd_stats(const ArcRpcHdStore *, ArcRpcHdStats *out);
const char *arc_riscpc_hd_notice(ArcRpcHdStore *store);
const char *arc_riscpc_hd_diagnostic(ArcRpcHdStore *store);
/* Offline ownership: caller quiesces/detaches IDE before begin. R2 creates and
 * imports before attaching the backend; R3 must preserve that precondition. */
bool arc_riscpc_hd_population_begin(ArcRpcHdStore *store);
bool arc_riscpc_hd_population_read(ArcRpcHdStore *, uint32_t lba, uint32_t count, uint8_t *out);
bool arc_riscpc_hd_population_write(ArcRpcHdStore *, uint32_t lba, uint32_t count, const uint8_t *in);
bool arc_riscpc_hd_population_seal(ArcRpcHdStore *store);
bool arc_riscpc_hd_population_publish(ArcRpcHdStore *store);
void arc_riscpc_hd_population_abort(ArcRpcHdStore *store);
bool arc_riscpc_hd_import(ArcRpcHdStore *store, const char *source);
#endif
