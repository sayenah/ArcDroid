/* Independent exact rational instruction clock and delivery boundaries.
 * GPL-2.0-only. */
#include "rpc_scheduler.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    for (unsigned rate = 1; rate <= 200; ++rate) {
        ArcRpcScheduler s;
        assert(arc_rpc_scheduler_init(&s, rate));
        uint64_t bus = 0;
        ArcRpcCpuBatch batch = { &s, &bus, UINT64_MAX };
        for (unsigned i = 1; i <= 2000; ++i) {
            assert(arc_rpc_cpu_batch_next(&batch, false));
            uint64_t expected = ((uint64_t)i * 1000 + rate - 1) / rate;
            assert(s.now_ns == expected);
            assert(s.cpu_fraction == expected * rate - (uint64_t)i * 1000);
        }
    }
    ArcRpcScheduler s;
    assert(arc_rpc_scheduler_init(&s, 100));
    uint64_t bus = 0;
    ArcRpcCpuBatch batch = { &s, &bus, 100 };
    assert(arc_rpc_schedule(&s, ARC_RPC_EVENT_VIDEO, 80));
    assert(arc_rpc_cpu_batch_next(&batch, false) && s.now_ns == 10);
    /* A guest MMIO write replaces an existing deadline, including later. */
    assert(arc_rpc_schedule(&s, ARC_RPC_EVENT_VIDEO, 20));
    assert(!arc_rpc_cpu_batch_next(&batch, false) && s.now_ns == 10);
    assert(arc_rpc_schedule(&s, ARC_RPC_EVENT_VIDEO, 90));
    assert(arc_rpc_cpu_batch_next(&batch, false) && s.now_ns == 20);
    assert(arc_rpc_schedule(&s, ARC_RPC_EVENT_TIMER0, 25));
    assert(!arc_rpc_cpu_batch_next(&batch, false) && s.now_ns == 20);
    arc_rpc_cancel(&s, ARC_RPC_EVENT_TIMER0);
    assert(arc_rpc_cpu_batch_next(&batch, false) && s.now_ns == 30);
    bus = 31;
    assert(!arc_rpc_cpu_batch_next(&batch, false) && s.now_ns == 30);
    bus = 0;
    assert(!arc_rpc_cpu_batch_next(&batch, true) && s.now_ns == 30);
    arc_rpc_cancel(&s, ARC_RPC_EVENT_VIDEO);
    batch.limit_ns = 40;
    assert(!arc_rpc_cpu_batch_next(&batch, false) && s.now_ns == 30);
    batch.limit_ns = UINT64_MAX;
    s.now_ns = UINT64_MAX - 9;
    assert(!arc_rpc_cpu_batch_next(&batch, false));
    puts("rpc-scheduler-batch: all 200 rates, exact fractions, MMIO rearm/cancel, ties, idle, bus and limit passed");
    return 0;
}
