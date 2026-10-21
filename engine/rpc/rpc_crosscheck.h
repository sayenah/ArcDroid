/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef ARC_RPC_CROSSCHECK_H
#define ARC_RPC_CROSSCHECK_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#if defined(ARCDROID_RPC_CROSSCHECK)
#if !defined(ARCDROID_TEST_FAULTS)
#error RPC cross-check requires hot-path test hooks
#endif
void arc_rpc_crosscheck_reset(void);
void arc_rpc_crosscheck_binding(void);
const char *arc_rpc_crosscheck_status(uint64_t *calls,uint64_t *failed);
/* Compare a named normalised canonical field. Callers own isolated execution,
 * pointer identities and live-field selection under invariants section 2.2. */
bool arc_rpc_crosscheck_field(const char *name,const void *reference,const void *candidate,size_t size);
#endif
#endif
