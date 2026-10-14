/* Frozen ABI adversarial vectors; GPL-2.0-only. */
#include "rpc_launch_helper.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void put(uint8_t *p,unsigned v) {for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(8*i));}
int main(int argc,char **argv)
{
    uint8_t *disc=malloc(ARC_RPC_HELPER_SIZE);assert(disc);
    assert(!arc_riscpc_helper_build(NULL,ARC_RPC_HELPER_SIZE));
    assert(!arc_riscpc_helper_build(disc,ARC_RPC_HELPER_SIZE-1));
    assert(arc_riscpc_helper_build(disc,ARC_RPC_HELPER_SIZE));
    if(argc==3 && !strcmp(argv[1],"--write")){
        FILE *f=fopen(argv[2],"wb");assert(f);assert(fwrite(disc,1,ARC_RPC_HELPER_SIZE,f)==ARC_RPC_HELPER_SIZE);assert(!fclose(f));free(disc);return 0;
    }
    uint8_t original[144];memcpy(original,disc+1792,144);
    uint32_t entry=0;assert(arc_riscpc_helper_descriptor(original,144,&entry)&&entry==64);
    assert(!arc_riscpc_helper_descriptor(NULL,144,NULL));
    for(unsigned n=0;n<144;++n)assert(!arc_riscpc_helper_descriptor(original,n,NULL));
    assert(!arc_riscpc_helper_descriptor(original,ARC_RPC_HELPER_MAX+4,NULL));
    const unsigned cases[][2]={{8,0},{8,2},{12,60},{12,68},{16,128},{16,136},
        {20,0},{20,2},{24,0},{24,2},{28,0},{28,60},{28,65},{28,144},{28,0xfffffffc},
        {32,0},{32,4},{36,0},{36,2},{40,0},{40,2},{44,0},{44,64},{44,123},
        {44,148},{48,9},{48,0xffffffff},{52,1},{56,1},{60,1}};
    for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);++i){uint8_t bad[144];memcpy(bad,original,144);
        put(bad+cases[i][0],cases[i][1]);entry=0xfeed;
        assert(!arc_riscpc_helper_descriptor(bad,144,&entry));assert(entry==0xfeed);
    }
    for(unsigned i=0;i<8;++i){uint8_t bad[144];memcpy(bad,original,144);bad[i]^=1;
        assert(!arc_riscpc_helper_descriptor(bad,144,NULL));}
    /* Replacement code and its independently versioned plan are opaque to
     * the stable gate. No revision-2 HD changes are required for replacement. */
    uint8_t replacement[144];memcpy(replacement,original,144);
    put(replacement+64,0xe3a00003);put(replacement+68,0xe582000c);put(replacement+72,0xe10f3000);put(replacement+76,0xe3130010);
    put(replacement+80,0x01b0f00e);put(replacement+84,0x11a0f00e);
    assert(arc_riscpc_helper_descriptor(replacement,144,NULL));
    assert(!memcmp(original,replacement,64));
    free(disc);puts("RPC helper: carrier, ABI bounds, reserved fields and replacement pass");return 0;
}
