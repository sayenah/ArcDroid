/* SPDX-License-Identifier: GPL-2.0-only */
/* The emitter and its generated field-sensitivity test share this actual
 * digest implementation. The Python comparator implements it independently. */
#ifndef ARC_RPC_LOCKSTEP_DIGEST_H
#define ARC_RPC_LOCKSTEP_DIGEST_H
static uint64_t arc_rpc_lockstep_digest_field(unsigned f,const uint64_t *values,unsigned phase,unsigned mode)
{
    const LsField *field=ls_fields+f;ArcSha256 hash;arc_sha256_init(&hash);
    if(!(field->modes&(1u<<mode)) && field->simple)
        for(unsigned i=0;i<field->count;++i)if(field->simple==1 || ls_included(f,i,phase,mode,values)){
            uint64_t words[3]={i,values[field->offset+i*field->width],0};
            if(field->width==2)words[2]=values[field->offset+i*field->width+1];
            uint8_t bytes[24];
            for(unsigned w=0;w<=field->width;++w)for(unsigned b=0;b<8;++b)bytes[w*8+b]=(uint8_t)(words[w]>>(b*8));
            arc_sha256_update(&hash,bytes,8u*(field->width+1u));
        }
    uint8_t digest[32];arc_sha256_final(&hash,digest);uint64_t result=0;
    for(unsigned b=0;b<8;++b)result|=(uint64_t)digest[b]<<(b*8);
    return result;
}
#endif
