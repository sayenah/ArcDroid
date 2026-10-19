/* Ordinary-load policy helpers. GPL-2.0-only. */
#ifndef ARC_RPC_ROUTE_H
#define ARC_RPC_ROUTE_H
#include "rpc_selection.h"
#include "arc_lock.h"
#include <time.h>
#ifndef ARC_RPC_ORDINARY_LOAD
#define ARC_RPC_ORDINARY_LOAD 0
#endif
#if ARC_RPC_ORDINARY_LOAD != 0 && ARC_RPC_ORDINARY_LOAD != 1
#error Invalid ordinary RiscPC gate
#endif
#ifndef ARC_RPC_CATALOGUE_AUTHORED
#define ARC_RPC_CATALOGUE_AUTHORED 0
#endif
typedef enum { ARC_RPC_STORE_UNKNOWN, ARC_RPC_STORE_ABSENT, ARC_RPC_STORE_EXISTS } ArcRpcStorePresence;
/* Read-only, no package access, <=64 ancestors/4096 entries/16384 operations. */
ArcRpcStorePresence arc_riscpc_store_presence(const char *,const char *);
/* Injectable host clock; production converts its local civil components once. */
bool arc_riscpc_route_epoch_with_clock(time_t (*)(time_t *),uint64_t *out);
bool arc_riscpc_route_epoch(uint64_t *out);
const char *arc_riscpc_route_notice(const ArcRpcSelection *,const ArcRpcFirmwareInventory *,char *,size_t);
/* Non-authoritative advisory. Called only after successful admission under owner. */
bool arc_riscpc_pair_admitted(const char *,const char *,unsigned,unsigned,ArcLock *);
#endif
