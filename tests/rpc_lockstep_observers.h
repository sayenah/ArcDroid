/* Test-only admissions for the three existing loop copies. GPL-2.0-only. */
#ifndef RPC_LOCKSTEP_OBSERVERS_H
#define RPC_LOCKSTEP_OBSERVERS_H
#include "rpc_observer.h"
#include <string.h>
static bool lockstep_observers(void *(*get)(const char *),unsigned mode)
{
    if(!mode)return true;
    if(mode==2){
        void (*trace)(bool,bool);void *p=get("arc_rpc_observer_trace");
        if(!p)return false;memcpy(&trace,&p,sizeof(trace));trace(true,false);return true;
    }
    if(mode!=1 && mode!=3)return false;
    bool (*subscribe)(ArcRpcObserver,const ArcRpcObserveFilter *);
    void *p=get("arc_rpc_observer_test_filter");if(!p)return false;memcpy(&subscribe,&p,sizeof(subscribe));
    ArcRpcObserveFilter filter={.swi=0xffffff,.swi_mask=0xffffff,.modes=~0u,
        .kinds=ARC_RPC_OBS_SWI|(mode==3?ARC_RPC_OBS_MODE:0)};
    return subscribe(ARC_RPC_OBS_ERROR,&filter);
}
#endif
