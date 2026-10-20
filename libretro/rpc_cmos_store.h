/* RPC-only CMOS persistence. GPL-2.0-only. */
#ifndef ARC_RPC_CMOS_STORE_H
#define ARC_RPC_CMOS_STORE_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "arc_io.h"
#include "arc_lock.h"
#define ARC_RPC_CMOS_PROCEDURE_REVISION 4u
/* Procedure-defined 2000-01-01 UTC; independent of content and plan RTC. */
#define ARC_RPC_CMOS_GENERATION_EPOCH UINT64_C(946684800)
#define ARC_RPC_CMOS_TIMING_REVISION 1u
#define ARC_RPC_CMOS_RECORD_SIZE 512u
#define ARC_RPC_CMOS_CAPTURE_NS UINT64_C(45000000000)
#define ARC_RPC_CMOS_STABILITY_NS UINT64_C(5000000000)
#define ARC_RPC_CMOS_DELETE_NS ARC_RPC_CMOS_CAPTURE_NS
typedef enum { ARC_RPC_LAYOUT_UNKNOWN, ARC_RPC_LAYOUT_RO350, ARC_RPC_LAYOUT_RO360, ARC_RPC_LAYOUT_RO370 } ArcRpcCmosLayout;
typedef struct {
    char content[41], firmware[65];
    unsigned model, layout, procedure, timing;
    bool defaults;
} ArcRpcCmosKey;
typedef struct {
    unsigned rate;
    uint64_t epoch, writes, last_write;
    uint8_t mask[32];
} ArcRpcCmosEvidence;
typedef struct {
    uint64_t operations, bytes, metadata, namespace_operations,
        step_operations, step_bytes, step_metadata, step_namespace,
        max_operations, max_bytes, max_metadata, max_namespace;
} ArcRpcCmosStats;
typedef struct {
    ArcRpcCmosKey key;
    ArcLock *owner; /* Borrowed through close; required even for recovery. */
    char base[1024];
    uint64_t generation;
    uint8_t payload[240];
    bool present, refused, publication_pending;
    ArcRpcCmosEvidence evidence;
    ArcRpcCmosStats stats;
    struct retro_vfs_dir_handle *directory_handle;
    char directory[1024];
    uint8_t candidate[512], inspected[512];
    unsigned phase, slot, attempts;
    size_t path_cursor;
    bool reconciled, exhausted, slot_found[2], destination_found, putting;
    bool marker_checked,marker_present;
} ArcRpcCmosStore;
typedef enum { ARC_RPC_CMOS_UNAVAILABLE = -2, ARC_RPC_CMOS_REFUSED = -1,
    ARC_RPC_CMOS_MISSING, ARC_RPC_CMOS_FOUND, ARC_RPC_CMOS_INSPECTING } ArcRpcCmosResult;
/* Begin does no I/O. Step retries reported inspection errors at most 3 times.
 * Payload is usable only after reconciliation, even if one slot was read. */
ArcRpcCmosResult arc_riscpc_cmos_store_begin(ArcRpcCmosStore *, const char *,
    const ArcRpcCmosKey *, ArcLock *, char *, size_t);
ArcRpcCmosResult arc_riscpc_cmos_store_step(ArcRpcCmosStore *, char *, size_t);
bool arc_riscpc_cmos_store_put_begin(ArcRpcCmosStore *, const uint8_t[240],
    const ArcRpcCmosEvidence *, char *, size_t);
bool arc_riscpc_cmos_store_put_step(ArcRpcCmosStore *, bool *done, char *, size_t);
void arc_riscpc_cmos_store_close(ArcRpcCmosStore *);
bool arc_riscpc_cmos_store_unsaved(ArcRpcCmosStore *);
bool arc_riscpc_cmos_store_mark_unsaved(ArcRpcCmosStore *);
/* A seed is admitted only after verified absence; never overwrites a record. */
bool arc_riscpc_cmos_store_seed(ArcRpcCmosStore *, const uint8_t[240], char *, size_t);
unsigned arc_riscpc_cmos_layout(const char *name);
bool arc_riscpc_cmos_hex(const char *text, size_t length, char *out);
bool arc_riscpc_cmos_checksum(const uint8_t payload[240]);
/* R-Y1: requested preparation sessions only; physical C0/C1 and 3F. */
void arc_riscpc_cmos_session_year(uint8_t payload[240], unsigned year);
/* Generation only: preserve every bit except logical 135 bits 0..2 and checksum. */
bool arc_riscpc_cmos_two_floppies(unsigned layout, uint8_t payload[240]);
void arc_riscpc_cmos_required(unsigned layout, uint8_t mask[32]);
bool arc_riscpc_cmos_oracle(unsigned layout, const uint8_t payload[240], const ArcRpcCmosEvidence *e);
ArcRpcCmosResult arc_riscpc_cmos_store_load(ArcRpcCmosStore *store, const char *save,
    const ArcRpcCmosKey *key, ArcLock *owner, char *error, size_t error_size);
bool arc_riscpc_cmos_store_put(ArcRpcCmosStore *store, const uint8_t payload[240],
    const ArcRpcCmosEvidence *evidence, char *error, size_t error_size);
#endif
