/* ArcDroid-owned RPCEmu floppy boundary. GPL-2.0-only. */
#ifndef ARC_RPC_FLOPPY_H
#define ARC_RPC_FLOPPY_H

#include <stdint.h>

typedef enum {
   ARC_RPC_FLOPPY_READ,
   ARC_RPC_FLOPPY_WRITE,
   ARC_RPC_FLOPPY_READ_ID,
   ARC_RPC_FLOPPY_FORMAT
} ArcRpcFloppyOperation;

typedef struct {
   ArcRpcFloppyOperation operation;
   unsigned drive;
   unsigned cylinder;
   unsigned head;
   uint8_t c, h, r, n;
   unsigned density;
} ArcRpcFloppyRequest;

typedef enum {
   ARC_RPC_FLOPPY_NO_MEDIA
} ArcRpcFloppyResult;

/* R1 has empty drives. Refusal is synchronous: the controller must finish
 * before raising DMA. This interface performs no host I/O or callbacks. */
ArcRpcFloppyResult arc_rpc_floppy_request(const ArcRpcFloppyRequest *request);

#endif
