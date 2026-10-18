/* Real-process store callers own their sentinel through process exit.
 * Store reopen borrows the same held token. GPL-2.0-only. */
#ifndef ARC_RPC_NATIVE_STORE_OWNER_H
#define ARC_RPC_NATIVE_STORE_OWNER_H
#include "arc_lock.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
static ArcLock rpc_native_owner;
static bool rpc_native_owner_ready;
static void rpc_native_owner_release(void){arc_lock_release(&rpc_native_owner);rpc_native_owner_ready=false;}
static ArcLock *rpc_native_test_owner(const char *save,const char *content)
{
    if(!rpc_native_owner_ready){
        assert(arc_lock_content(&rpc_native_owner,save,content,NULL,NULL)==ARC_LOCK_TRANSIENT);
        ArcLockResult result;
        unsigned steps=0;
        do{result=arc_lock_acquire(&rpc_native_owner);assert(++steps<100);}while(result==ARC_LOCK_TRANSIENT);
        assert(result==ARC_LOCK_HELD);rpc_native_owner_ready=true;atexit(rpc_native_owner_release);
    }
    return &rpc_native_owner;
}
#endif
