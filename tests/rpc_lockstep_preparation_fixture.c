/* SPDX-License-Identifier: GPL-2.0-only */
/* Appended to a disposable copy of rpc_preparation.c by the cache test.
 * The same owning TU and real consumed callback maintain the actual lease;
 * no probe symbol or fixture storage enters any repository core target. */
#if !defined(ARCDROID_RPC_LOCKSTEP)
#error "Preparation cache fixture requires the lockstep class"
#endif
#if defined(_WIN32)
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
bool arc_test_lockstep_retirement(unsigned stage)
{
    static ArcRpcPreparation preparation;
    if(stage==0){
        ArcRpcLaunchPlan plan={.action=ARC_RPC_PLAN_BOOT,.media={1}};
        ArcRpcAttemptToken token;
        strcpy(plan.target,"ADFS::0.$.!Boot");
        if(!arc_riscpc_attempt_init(&preparation.attempt,1234,1) ||
            !arc_riscpc_attempt_prepare(&preparation.attempt,&plan,0,&token) ||
            !arc_riscpc_controller_build(&plan,&token,&preparation.controller))return false;
        ArcRpcLaunchBinding binding={.attempt=&preparation.attempt,
            .controller=&preparation.controller,.revalidate=launch_revalidate,
            .opaque=&preparation,.consumed=launch_consumed,.boundary=launch_boundary};
        return !preparation.retirement_pending && arc_engine_rpc_launch_bind(&binding);
    }
    if(stage!=1 || preparation.retirement_pending)return false;
    uint64_t generation=arc_rpc_lockstep_preparation_generation;
    launch_consumed(&preparation);
    return preparation.retirement_pending && arc_rpc_lockstep_preparation_generation==generation+1;
}
