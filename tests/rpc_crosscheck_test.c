/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "rpc_crosscheck.h"
int main(int argc,char **argv)
{
    arc_rpc_crosscheck_reset();uint64_t a=7,b=7,calls=0,failed=0;
    assert(arc_rpc_crosscheck_field("issue_ticks",&a,&b,sizeof(a)));
    assert(!arc_rpc_crosscheck_status(&calls,&failed) && calls==1 && failed==0);
    if(argc==2 && !strcmp(argv[1],"--mismatch"))b=8;
    (void)arc_rpc_crosscheck_field("issue_ticks",&a,&b,sizeof(a));
    const char *field=arc_rpc_crosscheck_status(&calls,&failed);
    if(field){fprintf(stderr,"RPC cross-check mismatch: %s (%llu/%llu)\n",field,(unsigned long long)failed,(unsigned long long)calls);return 1;}
    arc_rpc_crosscheck_reset();assert(!arc_rpc_crosscheck_status(&calls,&failed) && !calls && !failed);
    return 0;
}
