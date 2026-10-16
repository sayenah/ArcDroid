/* SPDX-License-Identifier: GPL-2.0-only */
#include "rpc_crosscheck.h"
#include "libretro.h"
#include <string.h>
static uint64_t comparisons,mismatches;
static char first_field[96];
void arc_rpc_crosscheck_reset(void)
{ comparisons=mismatches=0;first_field[0]=0; }
bool arc_rpc_crosscheck_field(const char *name,const void *reference,const void *candidate,size_t size)
{
    ++comparisons;
    if(reference && candidate && !memcmp(reference,candidate,size))return true;
    ++mismatches;
    if(!first_field[0]){
        size_t n=name?strlen(name):0;if(n>=sizeof(first_field))n=sizeof(first_field)-1;
        if(n)memcpy(first_field,name,n);first_field[n]=0;
        if(!n)memcpy(first_field,"unnamed",8);
    }
    return false;
}
/* The runner reports outside protected execution. No engine host I/O is added. */
RETRO_API const char *arc_rpc_crosscheck_status(uint64_t *calls,uint64_t *failed)
{ if(calls)*calls=comparisons;if(failed)*failed=mismatches;return mismatches?first_field:NULL; }
