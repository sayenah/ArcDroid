/* ArcDroid-owned RPCEmu empty drives. GPL-2.0-only. */
#include "rpc_floppy.h"

ArcRpcFloppyResult arc_rpc_floppy_request(const ArcRpcFloppyRequest *request)
{
   (void)request;
   return ARC_RPC_FLOPPY_NO_MEDIA;
}
