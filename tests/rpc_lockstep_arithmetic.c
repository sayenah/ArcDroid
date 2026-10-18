/* SPDX-License-Identifier: GPL-2.0-only */
/* Literal boundary vectors exercise the emitter's actual arithmetic. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static unsigned errors,guest_faults;
static void fail(const char *message)
{ if(!strcmp(message,"lockstep projection overflow"))++errors; }
/* The sensitivity build substitutes fatal for fail; no guest mutator is linked. */
static void fatal(const char *message) { (void)message;++guest_faults; }
#include "rpc_lockstep_arithmetic.h"
int main(void)
{
    uint64_t out=77;
#define CHECK(e) do {if(!(e)){fprintf(stderr,"projection vector line %u\n",(unsigned)__LINE__);return 1;}}while(0)
    CHECK(add(UINT64_MAX-3,3,&out) && out==UINT64_MAX);
    out=77;CHECK(!add(UINT64_MAX-3,4,&out) && out==77);
    CHECK(ticks(UINT64_C(6588122883467697),0,2800,&out) && out==UINT64_C(18446744073709551600));
    out=77;CHECK(!ticks(UINT64_C(6588122883467698),0,2800,&out) && out==77);
    CHECK(!ticks(1,0,0,&out) && out==77);
    CHECK(!ticks(UINT64_MAX,1,1,&out) && out==77);
    CHECK(errors==4 && guest_faults==0);
    puts("projection: last valid instruction, add/tick overflow and zero denominator; status only, zero guest faults");
    return 0;
}
