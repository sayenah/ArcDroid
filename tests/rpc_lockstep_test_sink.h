/* SPDX-License-Identifier: GPL-2.0-only */
/* Optional real-emitter attachment for the unchanged native golden checks. */
#ifndef ARC_RPC_LOCKSTEP_TEST_SINK_H
#define ARC_RPC_LOCKSTEP_TEST_SINK_H
#include "rpc_lockstep.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
static const char *(*rpc_golden_status)(void);
static bool (*rpc_golden_end)(void);
static bool rpc_golden_discard(void *context,const void *data,size_t bytes)
{ (void)context;(void)data;(void)bytes;return true; }
static void rpc_golden_begin(void *(*find)(const char *),unsigned model)
{
    const char *density=getenv("ARCDROID_RPC_LOCKSTEP_TEST_DENSITY");
    if(!density)return;
    bool (*attach)(ArcRpcLockstepSink,void *,const ArcRpcLockstepConfig *);
    void *symbol=find("arc_rpc_test_lockstep_sink");assert(symbol);memcpy(&attach,&symbol,sizeof(attach));
    symbol=find("arc_rpc_test_lockstep_status");assert(symbol);memcpy(&rpc_golden_status,&symbol,sizeof(rpc_golden_status));
    symbol=find("arc_rpc_test_lockstep_end");assert(symbol);memcpy(&rpc_golden_end,&symbol,sizeof(rpc_golden_end));
    ArcRpcLockstepConfig config={.version=1,.size=sizeof(config),.model=model,.detail_last=UINT64_MAX};
    if(!strcmp(density,"baseline"))config.flags=ARC_RPC_LOCKSTEP_BASELINE;
    else if(!strcmp(density,"every-instruction"))config.flags=ARC_RPC_LOCKSTEP_EVERY;
    else {assert(!strcmp(density,"full-audit"));config.flags=ARC_RPC_LOCKSTEP_AUDIT;}
    assert(attach(rpc_golden_discard,NULL,&config));
}
static void rpc_golden_finish(void)
{ if(rpc_golden_end){assert(rpc_golden_end());assert(!rpc_golden_status());} }
#endif
