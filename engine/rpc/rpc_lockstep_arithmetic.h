/* SPDX-License-Identifier: GPL-2.0-only */
/* Projection arithmetic: its caller's fail() records transport status only. */
#ifndef ARC_RPC_LOCKSTEP_ARITHMETIC_H
#define ARC_RPC_LOCKSTEP_ARITHMETIC_H
static bool add(uint64_t a,uint64_t b,uint64_t *out)
{
    if(UINT64_MAX-a<b){fail("lockstep projection overflow");return false;}
    *out=a+b;return true;
}
static bool ticks(uint64_t floor,uint64_t fraction,uint64_t d,uint64_t *out)
{
    if(!d || floor>UINT64_MAX/d){fail("lockstep projection overflow");return false;}
    return add(floor*d,fraction,out);
}
#endif
