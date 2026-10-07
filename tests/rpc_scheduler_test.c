/* Independently expected scheduler/credit boundaries. GPL-2.0-only. */
#include "rpc_scheduler.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
   ArcRpcScheduler scheduler;
   ArcRpcEvent event;
   assert(arc_rpc_scheduler_init(&scheduler, 35u));
   /* Reverse arm order must still produce enum tie order at time 10. */
   for (unsigned i = ARC_RPC_EVENT_COUNT; i; --i)
      assert(arc_rpc_schedule(&scheduler, (ArcRpcEvent)(i - 1u), 10u));
   assert(arc_rpc_next_deadline(&scheduler, 100u) == 10u);
   assert(!arc_rpc_take_event(&scheduler, 5u, &event) && scheduler.now_ns == 5u);
   /* Rearm with unchanged value before the deadline; cancellation also wins. */
   arc_rpc_cancel(&scheduler, ARC_RPC_EVENT_TIMER0);
   assert(arc_rpc_schedule(&scheduler, ARC_RPC_EVENT_TIMER0, 10u));
   for (unsigned i = 0u; i < ARC_RPC_EVENT_COUNT; ++i)
   {
      assert(arc_rpc_take_event(&scheduler, 10u, &event));
      assert(event == (ArcRpcEvent)i && scheduler.now_ns == 10u);
   }
   /* Re-arm at the exact delivery time is a distinct event. */
   assert(arc_rpc_schedule(&scheduler, ARC_RPC_EVENT_TIMER0, 10u));
   assert(arc_rpc_take_event(&scheduler, 10u, &event) && event == ARC_RPC_EVENT_TIMER0);
   assert(!arc_rpc_take_event(&scheduler, 11u, &event));
   assert(!arc_rpc_schedule(&scheduler, ARC_RPC_EVENT_TIMER0, 10u));
   assert(arc_rpc_schedule(&scheduler, ARC_RPC_EVENT_TIMER0, 12u));
   arc_rpc_cancel(&scheduler, ARC_RPC_EVENT_TIMER0);
   assert(!arc_rpc_take_event(&scheduler, 100u, &event) && scheduler.now_ns == 100u);
   assert(arc_rpc_next_deadline(&scheduler, 1000000000u) == 1000000000u);
   assert(!arc_rpc_take_event(&scheduler, 99u, &event) && scheduler.now_ns == 100u);
   /* No sentinel collision at the full uint64_t boundary. */
   assert(arc_rpc_schedule(&scheduler, ARC_RPC_EVENT_VIDEO, UINT64_MAX));
   assert(arc_rpc_take_event(&scheduler, UINT64_MAX, &event) && event == ARC_RPC_EVENT_VIDEO);

   const unsigned rates[] = {20u, 35u, 100u, 200u};
   for (unsigned r = 0u; r < sizeof(rates) / sizeof(rates[0]); ++r)
   {
      uint64_t whole, split = 0u, count;
      assert(arc_rpc_scheduler_init(&scheduler, rates[r]));
      assert(arc_rpc_cpu_credit(&scheduler, 1000000000u, &whole));
      assert(whole == (uint64_t)rates[r] * 1000000u && scheduler.cpu_fraction == 0u);
      assert(arc_rpc_scheduler_init(&scheduler, rates[r]));
      /* Sum exactly one second across arbitrary event/instruction slices. */
      uint64_t left = 1000000000u;
      while (left)
      {
         uint64_t delta = left < 997u ? left : 997u;
         assert(arc_rpc_cpu_credit(&scheduler, delta, &count));
         split += count;
         left -= delta;
      }
      assert(split == whole && scheduler.cpu_fraction == 0u);
      ArcRpcScheduler before = scheduler;
      assert(!arc_rpc_cpu_credit(&scheduler, UINT64_MAX, &count));
      assert(!memcmp(&before, &scheduler, sizeof(scheduler)));
   }
   assert(!arc_rpc_scheduler_init(&scheduler, 0u));
   assert(!arc_rpc_scheduler_init(&scheduler, 201u));
   assert(!arc_rpc_schedule(&scheduler, (ArcRpcEvent)-1, 0u));
   puts("rpc-scheduler: tie order, same-value rearm, cancellation, idle and exact credit passed");
   return 0;
}
