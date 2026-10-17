/* Authored ARM610-compatible helper generator. GPL-2.0-only. */
#ifndef ARC_RPC_LAUNCH_CONTROLLER_H
#define ARC_RPC_LAUNCH_CONTROLLER_H
#include "rpc_launch_attempt.h"
#include "rpc_launch_helper.h"
#define ARC_RPC_LAUNCH_PROFILE_REVISION 2u
enum { ARC_RPC_STATUS_CONSUME=1, ARC_RPC_STATUS_REGISTERED,
    ARC_RPC_STATUS_DISPATCH, ARC_RPC_STATUS_START_RETURNED,
    ARC_RPC_STATUS_RETURNED, ARC_RPC_STATUS_DESKTOP,
    ARC_RPC_STATUS_REJECTED, ARC_RPC_STATUS_CLOSED };
typedef struct {
    uint8_t image[ARC_RPC_HELPER_MAX];
    size_t size;
    uint32_t query, status, command, plan, latch, start;
    char text[ARC_RPC_PLAN_COMMAND_MAX];
} ArcRpcLaunchController;
bool arc_riscpc_controller_build(const ArcRpcLaunchPlan *,const ArcRpcAttemptToken *,ArcRpcLaunchController *);
#endif
