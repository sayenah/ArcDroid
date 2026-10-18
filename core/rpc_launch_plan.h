/* Host-only helper plan 2. GPL-2.0-only. */
#ifndef ARC_RPC_LAUNCH_PLAN_H
#define ARC_RPC_LAUNCH_PLAN_H
#include "arcdroid_core.h"
#define ARC_RPC_PLAN_VERSION 2u
#define ARC_RPC_PLAN_TARGET_MAX 64u
#define ARC_RPC_PLAN_MAX_BYTES 128u
#define ARC_RPC_PLAN_COMMAND_MAX 80u
#define ARC_RPC_PLAN_RAMFS 1u
typedef enum { ARC_RPC_PLAN_BOOT = 1, ARC_RPC_PLAN_APPLICATION = 2,
               ARC_RPC_PLAN_PRODUCT = 3 } ArcRpcPlanAction;
typedef struct {
   uint32_t action, flags;
   uint8_t media[32];
   char target[ARC_RPC_PLAN_TARGET_MAX + 1u];
} ArcRpcLaunchPlan;
typedef enum { ARC_RPC_PLAN_OK, ARC_RPC_PLAN_INVALID, ARC_RPC_PLAN_VERSION_UNKNOWN,
   ARC_RPC_PLAN_ACTION_UNKNOWN, ARC_RPC_PLAN_AMBIGUOUS,
   ARC_RPC_PLAN_STALE_PRODUCT, ARC_RPC_PLAN_BUFFER } ArcRpcPlanResult;
/* index -1 selects only an unambiguous application; boot is always explicit. */
ArcRpcPlanResult arc_riscpc_plan_build(const ArcLaunchDiscovery *, int index,
   ArcRpcPlanAction, bool ramfs, const uint8_t media[32], ArcRpcLaunchPlan *);
/* Captured/current identities bracket validated post-install discovery. */
ArcRpcPlanResult arc_riscpc_plan_build_product(const ArcLaunchDiscovery *, int,
   bool, const uint8_t media[32], const uint8_t captured[32],
   const uint8_t current[32], ArcRpcLaunchPlan *);
ArcRpcPlanResult arc_riscpc_plan_encode(const ArcRpcLaunchPlan *, uint8_t *,
                                    size_t, size_t *);
ArcRpcPlanResult arc_riscpc_plan_decode(const uint8_t *, size_t, ArcRpcLaunchPlan *);
ArcRpcPlanResult arc_riscpc_plan_command(const ArcRpcLaunchPlan *, char *, size_t);
bool arc_riscpc_plan_digest(const ArcRpcLaunchPlan *, uint8_t out[32]);
/* Escaped diagnostics only; never a guest command. */
bool arc_riscpc_plan_label(const ArcLaunchCandidate *, char *, size_t);
#endif
