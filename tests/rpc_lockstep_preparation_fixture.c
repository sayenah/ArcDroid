/* SPDX-License-Identifier: GPL-2.0-only */
/* Appended to a disposable copy of rpc_preparation.c by the cache test.
 * The same owning TU and real consumed callback maintain the actual lease;
 * no probe symbol or fixture storage enters any repository core target. */
#if !defined(ARCDROID_RPC_LOCKSTEP)
#error "Preparation cache fixture requires the lockstep class"
#endif
extern ArcRpcPreparationHost arc_test_lockstep_host_context(ArcRpcPreparation *,bool);
#if defined(_WIN32)
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
bool arc_test_lockstep_retirement(unsigned stage)
{
    static ArcRpcPreparation preparation;
    if(stage==0){
        preparation.host=arc_test_lockstep_host_context(&preparation,false);
#if defined(ARC_LOCKSTEP_HOST_ONLY_PROBE)
        preparation.draining=true;
#endif
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
#if defined(ARC_LOCKSTEP_HOST_ONLY_PROBE)
    /* Only the actual host reset entry invalidates the cached owner here. */
    preparation.host=arc_test_lockstep_host_context(&preparation,true);
    return preparation.host.clock!=NULL;
#endif
    uint64_t generation=arc_rpc_lockstep_preparation_generation;
    launch_consumed(&preparation);
    if(!preparation.retirement_pending || arc_rpc_lockstep_preparation_generation!=generation+1)return false;
    preparation.content_owner.valid=preparation.content_owner.opened=true;
    preparation.content_owner.held=preparation.content_owner.parents_done=true;
    preparation.defaults_owner.lost=true;
    preparation.record.owner=&preparation.content_owner;
    preparation.defaults.owner=&preparation.defaults_owner;
    preparation.observations.owner=&preparation.content_owner;
    preparation.content_waiting=preparation.defaults_waiting=true;
    preparation.observations_begun=preparation.hd_close_ok=true;
    preparation.stopped_window=preparation.marker_checked=preparation.released=true;
    preparation.ownership_lost=preparation.draining=preparation.drain_started=true;
    preparation.drain_ok=true;preparation.drain_frames=7;
    preparation.record.marker_checked=preparation.record.marker_present=true;
    preparation.defaults.marker_checked=preparation.defaults.marker_present=true;
    memset(preparation.drain_payload,0x5a,sizeof(preparation.drain_payload));
    preparation.host=arc_test_lockstep_host_context(&preparation,true);
    return preparation.host.clock!=NULL;
}
