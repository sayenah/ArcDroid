/* ArcDroid-owned RPCEmu empty drives. GPL-2.0-only. */
#include "rpc_floppy.h"

ArcRpcFloppyResult arc_rpc_floppy_request(const ArcRpcFloppyRequest *request)
{
   (void)request;
   return ARC_RPC_FLOPPY_NO_MEDIA;
}

ArcRpcFloppySignals arc_rpc_floppy_signals(unsigned drive, unsigned cylinder)
{
   ArcRpcFloppySignals signals = {drive < 2u && cylinder == 0u, true, false, false};
   return signals;
}
