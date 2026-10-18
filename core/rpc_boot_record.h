/* RPC-only established observations, never Arculator ARCBOOT1. GPL-2.0-only. */
#ifndef ARC_RPC_BOOT_RECORD_H
#define ARC_RPC_BOOT_RECORD_H
#include "rpc_launch_attempt.h"
#define ARC_RPC_BOOT_RECORD_VERSION 1u
#define ARC_RPC_BOOT_RECORD_REVISION 1u
#define ARC_RPC_BOOT_RECORD_BYTES 288u
typedef struct {
   uint32_t cpu, plan_revision, profile_revision, action;
   uint8_t firmware[32], configuration[32], content[32], media[32], store[32],
           plan[32], helper[32];
} ArcRpcBootKey;
typedef struct { ArcRpcBootKey key; ArcRpcAttemptOutcome observation; } ArcRpcBootRecord;
/* Only established observations; revalidate all bindings before publication.
 * No filesystem writes or capability qualification happen in this module. */
bool arc_riscpc_boot_record_capture(const ArcRpcLaunchAttempt *, const ArcRpcBootKey *captured,
                                 const ArcRpcBootKey *current, ArcRpcBootRecord *);
bool arc_riscpc_boot_record_encode(const ArcRpcBootRecord *, uint8_t *, size_t);
bool arc_riscpc_boot_record_decode(const uint8_t *, size_t, ArcRpcBootRecord *);
bool arc_riscpc_boot_record_matches(const ArcRpcBootRecord *, const ArcRpcBootKey *);
#endif
