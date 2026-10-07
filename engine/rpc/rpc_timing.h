/* ArcDroid explicit RPC device scheduling edge. GPL-2.0-only. */
#ifndef ARC_RPC_TIMING_H
#define ARC_RPC_TIMING_H
#include "rpc_scheduler.h"
void arc_rpc_timing_bind(ArcRpcScheduler *scheduler);
uint64_t arc_rpc_time_ns(void);
void arc_rpc_event_after(ArcRpcEvent event, uint64_t delay_ns);
void arc_rpc_event_cancel(ArcRpcEvent event);
/* Retain component-visible counters, but every write explicitly arms/cancels.
 * Keyboard/mouse legacy units are 250 us; FDC legacy units are 1 us. */
void arc_rpc_response(ArcRpcEvent event, int *counter, unsigned units);
void arc_rpc_iomd_timer_event(unsigned timer);
void arc_rpc_video_event(void);
#endif
