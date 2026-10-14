/* Non-mutating virtual reads; never execution translation. GPL-2.0-only. */
#include "rpc_observer.h"
bool arc_rpc_inspect_byte(uint32_t va,bool privileged,uint8_t *out)
{
    uint32_t physical;
    return out && arc_rpc_inspect_translate(va,privileged,&physical) && arc_rpc_inspect_physical(physical,out);
}
bool arc_rpc_inspect_word(uint32_t va,bool privileged,uint32_t *out)
{
    if(!out || (va&3) || va>UINT32_MAX-3)return false;
    uint32_t value=0;
    for(unsigned i=0;i<4;++i){uint8_t byte;if(!arc_rpc_inspect_byte(va+i,privileged,&byte))return false;
        value|=(uint32_t)byte<<(i*8);}
    *out=value;return true;
}
ArcRpcInspectResult arc_rpc_inspect_bytes(uint32_t va,bool privileged,void *out,size_t length)
{
    ArcRpcInspectResult r={ARC_RPC_INSPECT_REFUSED,0};
    if(!out || length>ARC_RPC_COPY_LIMIT || (length && length-1>UINT32_MAX-va))return r;
    for(;r.copied<length;++r.copied)if(!arc_rpc_inspect_byte(va+(uint32_t)r.copied,privileged,(uint8_t *)out+r.copied))return r;
    r.status=ARC_RPC_INSPECT_OK;return r;
}
ArcRpcInspectResult arc_rpc_inspect_string(uint32_t va,bool privileged,char *out,size_t capacity)
{
    ArcRpcInspectResult r={ARC_RPC_INSPECT_REFUSED,0};
    if(!out || !capacity || capacity>ARC_RPC_COPY_LIMIT)return r;
    out[0]=0;
    /* Reserve one output byte for a NUL even on refusal/truncation. A NUL
     * or CR in the final allowed guest byte still counts as success. */
    for(size_t i=0;i<capacity;++i){uint8_t byte;
        if(i>UINT32_MAX-va || !arc_rpc_inspect_byte(va+(uint32_t)i,privileged,&byte))return r;
        if(!byte || byte==13){r.status=ARC_RPC_INSPECT_OK;return r;}
        if(i==capacity-1){r.status=ARC_RPC_INSPECT_TRUNCATED;return r;}
        out[r.copied++]=(char)byte;out[r.copied]=0;
    }
    return r;
}
