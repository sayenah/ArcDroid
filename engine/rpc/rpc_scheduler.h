/* ArcDroid RPCEmu virtual-time scheduler. GPL-2.0-only. */
#ifndef ARC_RPC_SCHEDULER_H
#define ARC_RPC_SCHEDULER_H

#include <stdbool.h>
#include <stdint.h>

/* Coincident pending events are delivered in this order. An event is disarmed
 * before its handler runs; scheduling the same deadline is a new arm. */
typedef enum {
   ARC_RPC_EVENT_TIMER0,
   ARC_RPC_EVENT_TIMER1,
   ARC_RPC_EVENT_KEYBOARD,
   ARC_RPC_EVENT_MOUSE,
   ARC_RPC_EVENT_FDC,
   ARC_RPC_EVENT_IDE,
   ARC_RPC_EVENT_SOUND,
   ARC_RPC_EVENT_VIDEO,
   ARC_RPC_EVENT_INPUT, /* Input tick/typematic after pre-existing tied devices. */
   ARC_RPC_EVENT_COUNT
} ArcRpcEvent;

typedef struct {
   uint64_t now_ns;
   uint64_t deadlines[ARC_RPC_EVENT_COUNT];
   bool armed[ARC_RPC_EVENT_COUNT];
   unsigned rate_mips;
   unsigned cpu_fraction;
} ArcRpcScheduler;

bool arc_rpc_scheduler_init(ArcRpcScheduler *scheduler, unsigned rate_mips);
bool arc_rpc_schedule(ArcRpcScheduler *scheduler, ArcRpcEvent event, uint64_t due_ns);
void arc_rpc_cancel(ArcRpcScheduler *scheduler, ArcRpcEvent event);
uint64_t arc_rpc_next_deadline(const ArcRpcScheduler *scheduler, uint64_t limit_ns);
/* A single bounded delivery: false advances to limit when no event is due.
 * Past limits are rejected without changing time. The engine driver owns the
 * per-entry event budget; this module has no guest-controlled callback loop. */
bool arc_rpc_take_event(ArcRpcScheduler *scheduler, uint64_t limit_ns, ArcRpcEvent *event);
bool arc_rpc_cpu_credit(ArcRpcScheduler *scheduler, uint64_t elapsed_ns,
                        uint64_t *instructions);

#endif
