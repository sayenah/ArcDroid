/* ArcDroid integer RPCEmu scheduler. GPL-2.0-only. */
#include "rpc_scheduler.h"

#include <string.h>

static void arc_rpc_minimum(ArcRpcScheduler *s)
{
   s->next_event = ARC_RPC_EVENT_COUNT;
   s->next_ns = UINT64_MAX;
   for (unsigned i = 0; i < ARC_RPC_EVENT_COUNT; ++i)
      if (s->armed[i] && (s->next_event == ARC_RPC_EVENT_COUNT || s->deadlines[i] < s->next_ns)) {
         s->next_event = i;
         s->next_ns = s->deadlines[i];
      }
}

void arc_rpc_scheduler_rate(ArcRpcScheduler *s, unsigned rate)
{
   s->rate_mips = rate;
   for (unsigned i = 0; i < 1000u; ++i)
      s->cpu_delta[i] = (uint16_t)((1000u - i + rate - 1u) / rate);
}

bool arc_rpc_scheduler_init(ArcRpcScheduler *scheduler, unsigned rate_mips)
{
   if (!scheduler || !rate_mips || rate_mips > 200u) return false;
   memset(scheduler, 0, sizeof(*scheduler));
   arc_rpc_scheduler_rate(scheduler, rate_mips);
   arc_rpc_minimum(scheduler);
   return true;
}

bool arc_rpc_schedule(ArcRpcScheduler *scheduler, ArcRpcEvent event, uint64_t due_ns)
{
   if (!scheduler || event >= ARC_RPC_EVENT_COUNT || (int)event < 0 ||
       due_ns < scheduler->now_ns) return false;
   scheduler->deadlines[event] = due_ns;
   scheduler->armed[event] = true;
   arc_rpc_minimum(scheduler);
   return true;
}

void arc_rpc_cancel(ArcRpcScheduler *scheduler, ArcRpcEvent event)
{
   if (scheduler && event < ARC_RPC_EVENT_COUNT && (int)event >= 0)
   {
      scheduler->armed[event] = false;
      if (scheduler->next_event == (unsigned)event) arc_rpc_minimum(scheduler);
   }
}

uint64_t arc_rpc_next_deadline(const ArcRpcScheduler *scheduler, uint64_t limit_ns)
{
   if (!scheduler || limit_ns < scheduler->now_ns) return limit_ns;
   if (scheduler->next_ns < limit_ns) limit_ns = scheduler->next_ns;
   return limit_ns;
}

bool arc_rpc_take_event(ArcRpcScheduler *scheduler, uint64_t limit_ns, ArcRpcEvent *event)
{
   if (!scheduler || !event || limit_ns < scheduler->now_ns) return false;
   unsigned selected = scheduler->next_event;
   if (selected == ARC_RPC_EVENT_COUNT || scheduler->next_ns > limit_ns) {
      scheduler->now_ns = limit_ns;
      return false;
   }
   scheduler->now_ns = scheduler->next_ns;
   scheduler->armed[selected] = false;
   arc_rpc_minimum(scheduler);
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
