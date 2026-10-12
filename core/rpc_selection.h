/* Diagnostic-only RiscPC catalogue and selection. GPL-2.0-only. */
#ifndef ARC_RPC_SELECTION_H
#define ARC_RPC_SELECTION_H
#include "arcdroid_core.h"

/* Deliberately separate from the serialized legacy machine/CPU/OS enums. */
typedef enum { ARC_RPC_SELECT_AUTO = 0, ARC_RPC_SELECT_ARM610 = 1,
   ARC_RPC_SELECT_ARM710 = 2, ARC_RPC_SELECT_SA110 = 3,
   ARC_RPC_SELECT_ARM7500 = 4 } ArcRpcSelectionModel;
typedef enum { ARC_RPC_OS_AUTO = 0, ARC_RPC_OS_350 = 1,
   ARC_RPC_OS_360 = 2, ARC_RPC_OS_370 = 3, ARC_RPC_OS_371 = 4 } ArcRpcSelectionOs;
#define ARC_RPC_OS_BIT(os) (UINT32_C(1) << (unsigned)(os))

typedef struct {
   ArcRpcSelectionOs os;
   size_t size;
   uint32_t crc32;
   const char *sha1;
} ArcRpcFirmwareFingerprint;
typedef struct {
   uint32_t installed; /* Only recognised, whole-image fingerprints. */
   bool vfs_failure;
   bool bounded; /* Entry/depth/path/read budget exhausted, not a VFS error. */
   bool resource_failure;
} ArcRpcFirmwareInventory;
/* Unshifted JFD categories: CPU bits 0 ARM3, 1 ARM250, 2 ARM610/710,
 * 3 ARM7500, 4 StrongARM, 5 ARMv5+; OS bits 0 RISC OS 2, 1 3.1,
 * 2 3.5, 3 3.7, 4 3.8/4.x, 5 5.x, 6 6.x. Zero is unknown. */
typedef struct { uint8_t cpu, os; } ArcRpcCompatibility;
typedef enum { ARC_RPC_SELECTION_LEGACY = 0, ARC_RPC_SELECTION_SELECTED,
   ARC_RPC_SELECTION_CONFLICTING_DISCS, ARC_RPC_SELECTION_UNSUPPORTED,
   ARC_RPC_SELECTION_UNQUALIFIED, ARC_RPC_SELECTION_MISSING_FIRMWARE,
   ARC_RPC_SELECTION_UNREADABLE_INVENTORY } ArcRpcSelectionReason;
typedef struct {
   ArcRpcSelectionReason reason;
   ArcRpcSelectionModel model;
   ArcRpcSelectionOs os;
   uint32_t needed; /* Qualified, feasible OS families, including on failure. */
   bool incomplete;
} ArcRpcSelection;

const ArcRpcFirmwareFingerprint *arc_riscpc_firmware_catalogue(size_t *count);
const ArcRpcFirmwareFingerprint *arc_riscpc_firmware_recognize(const ArcDigest *digest,
   const ArcRpcFirmwareFingerprint *catalogue, size_t count);
ArcRpcCompatibility arc_riscpc_compatibility(bool extended, uint32_t flags);
/* Existing per-disc RiscPC notice predicate; also gates diagnostic scanning. */
bool arc_riscpc_notice_required(ArcRpcCompatibility disc);
bool arc_riscpc_selection_qualified(ArcRpcSelectionModel model, ArcRpcSelectionOs os);
/* Zero masks are independently unknown. Nonzero masks are never widened.
 * A manual model overrides CPU hints only; a manual OS overrides OS hints only.
 * Neither can bypass qualification/recognition (D-166(2)). */
ArcRpcSelection arc_riscpc_select(const ArcRpcCompatibility *discs, size_t count,
   const ArcRpcFirmwareInventory *inventory, ArcRpcSelectionModel manual_model,
   ArcRpcSelectionOs manual_os);
const char *arc_riscpc_selection_reason_name(ArcRpcSelectionReason reason);
const char *arc_riscpc_selection_model_name(ArcRpcSelectionModel model);
const char *arc_riscpc_selection_os_name(ArcRpcSelectionOs os);
void arc_riscpc_selection_diagnostic(const ArcRpcSelection *selection,
   char *text, size_t capacity);
#endif
