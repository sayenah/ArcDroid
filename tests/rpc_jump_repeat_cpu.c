/* D-155: repeated actual component fault paths. GPL-2.0-only. */
#define main component_main
#include "rpc_cpu_test.c"
#undef main
static unsigned iteration;
int main(void)
{
    arm_init(); protected=1; machine.cpu_model=CPUModel_SA110;
    machine.model=Model_RPCSA110; config.mem_size=64; config.vram_size=2;
    for(iteration=0;iteration<1000;++iteration) {
        for(fail_at=1;fail_at<=4;++fail_at) {
            allocations=0;
            if(!setjmp(boundary)) { mem_init(); assert(0); }
            arc_rpc_memory_close();
            assert(!rom && !romb && !ram00 && !ram01 && !ram1 && !vram);
        }
        if(!setjmp(boundary)) { arc_rpc_arm_exec_count(201); assert(0); }
        if(!setjmp(boundary)) { updatemode(4); assert(0); }
    }
    puts("jump repeat: CPU allocation stages 1-4/entry/mode faults 1000 each"); return 0;
}
