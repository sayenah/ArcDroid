/* ArcDroid explicit RPC device scheduling edge. GPL-2.0-only. */
#include "rpc_timing.h"
static ArcRpcScheduler *arc_rpc_clock;
void arc_rpc_timing_bind(ArcRpcScheduler *scheduler) { arc_rpc_clock = scheduler; }
uint64_t arc_rpc_time_ns(void) { return arc_rpc_clock ? arc_rpc_clock->now_ns : 0; }
void arc_rpc_event_after(ArcRpcEvent event, uint64_t delay_ns)
{
    if (arc_rpc_clock) arc_rpc_schedule(arc_rpc_clock, event, arc_rpc_clock->now_ns + delay_ns);
}
void arc_rpc_event_cancel(ArcRpcEvent event)
{
    if (arc_rpc_clock) arc_rpc_cancel(arc_rpc_clock, event);
}
void arc_rpc_response(ArcRpcEvent event, int *counter, unsigned units)
{
    *counter = (int)units;
    if (units) arc_rpc_event_after(event, (uint64_t)units *
        (event == ARC_RPC_EVENT_FDC ? 1000u : 250000u));
    else arc_rpc_event_cancel(event);
}

#if defined(ARCDROID_RPC_LOCKSTEP)
ARC_RPC_LS_SERIALIZE_rpc_timing
#endif
