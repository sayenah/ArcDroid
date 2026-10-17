/* Runner-only JSON schema 2 writer. GPL-2.0-only. */
#ifndef ARC_RPC_PROFILE_JSON_H
#define ARC_RPC_PROFILE_JSON_H
#include "rpc_profile.h"
#include <stdio.h>
void arc_runner_profile_json(FILE *file, const ArcRpcProfile *p, uint64_t frame,
    uint64_t guest_ns, const char *kind, uint64_t *times, size_t count, const uint64_t *probes);
#endif
