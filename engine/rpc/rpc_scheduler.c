/* ArcDroid integer RPCEmu scheduler. GPL-2.0-only. */
#include "rpc_scheduler.h"

#include <string.h>

bool arc_rpc_scheduler_init(ArcRpcScheduler *scheduler, unsigned rate_mips)
{
   if (!scheduler || !rate_mips || rate_mips > 200u) return false;
   memset(scheduler, 0, sizeof(*scheduler));
   scheduler->rate_mips = rate_mips;
   return true;
}

bool arc_rpc_schedule(ArcRpcScheduler *scheduler, ArcRpcEvent event, uint64_t due_ns)
{
   if (!scheduler || event >= ARC_RPC_EVENT_COUNT || (int)event < 0 ||
       due_ns < scheduler->now_ns) return false;
   scheduler->deadlines[event] = due_ns;
   scheduler->armed[event] = true;
   return true;
}

void arc_rpc_cancel(ArcRpcScheduler *scheduler, ArcRpcEvent event)
{
   if (scheduler && event < ARC_RPC_EVENT_COUNT && (int)event >= 0)
      scheduler->armed[event] = false;
}

uint64_t arc_rpc_next_deadline(const ArcRpcScheduler *scheduler, uint64_t limit_ns)
{
   if (!scheduler || limit_ns < scheduler->now_ns) return limit_ns;
   for (unsigned i = 0u; i < ARC_RPC_EVENT_COUNT; ++i)
      if (scheduler->armed[i] && scheduler->deadlines[i] < limit_ns)
         limit_ns = scheduler->deadlines[i];
   return limit_ns;
}

bool arc_rpc_take_event(ArcRpcScheduler *scheduler, uint64_t limit_ns, ArcRpcEvent *event)
{
   if (!scheduler || !event || limit_ns < scheduler->now_ns) return false;
   unsigned selected = ARC_RPC_EVENT_COUNT;
   uint64_t due = limit_ns;
   for (unsigned i = 0u; i < ARC_RPC_EVENT_COUNT; ++i)
      if (scheduler->armed[i] && scheduler->deadlines[i] <= due &&
          (selected == ARC_RPC_EVENT_COUNT || scheduler->deadlines[i] < due))
      {
         selected = i;
         due = scheduler->deadlines[i];
      }
   scheduler->now_ns = due;
   if (selected == ARC_RPC_EVENT_COUNT) return false;
   scheduler->armed[selected] = false;
   *event = (ArcRpcEvent)selected;
   return true;
}

bool arc_rpc_cpu_credit(ArcRpcScheduler *scheduler, uint64_t elapsed_ns,
                        uint64_t *instructions)
{
   if (!scheduler || !instructions || !scheduler->rate_mips ||
       elapsed_ns > (UINT64_MAX - scheduler->cpu_fraction) / scheduler->rate_mips)
      return false;
   uint64_t credit = elapsed_ns * scheduler->rate_mips + scheduler->cpu_fraction;
   *instructions = credit / 1000u;
   scheduler->cpu_fraction = (unsigned)(credit % 1000u);
   return true;
}
