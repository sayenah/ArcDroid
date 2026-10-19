/* SPDX-License-Identifier: GPL-2.0-only */
/* Executes the emitter's field digest against independent fixture vectors. */
#include "arcdroid_core.h"
#include "rpc_lockstep_generated.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#define ARC_RPC_LS_POLICY_ONLY 1
#include "rpc_lockstep_tables.h"
#include "rpc_lockstep_digest.h"
int main(int argc,char **argv)
{
    if(argc!=2)return 2;
    FILE *file=fopen(argv[1],"rb");if(!file)return 2;
    uint64_t *values=calloc(ARC_RPC_LS_WORDS,sizeof(*values));if(!values)return 2;
    for(unsigned i=0;i<ARC_RPC_LS_WORDS;++i){uint8_t bytes[8];if(fread(bytes,1,8,file)!=8)return 2;
        for(unsigned j=0;j<8;++j)values[i]|=(uint64_t)bytes[j]<<(8*j);}
    if(fgetc(file)!=EOF || fclose(file))return 2;
    for(unsigned f=0;f<ARC_RPC_LS_FIELDS;++f){
        uint64_t a=arc_rpc_lockstep_digest_field(f,values,0,0);
        values[ls_fields[f].offset]^=1;
        uint64_t b=arc_rpc_lockstep_digest_field(f,values,0,0);
        values[ls_fields[f].offset]^=1;
        printf("%u %016" PRIx64 " %016" PRIx64 "\n",f,a,b);
    }
    free(values);return 0;
}
