/* RPC-only CMOS persistence. GPL-2.0-only. */
#ifndef ARC_RPC_CMOS_STORE_H
#define ARC_RPC_CMOS_STORE_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define ARC_RPC_CMOS_PROCEDURE_REVISION 2u
#define ARC_RPC_CMOS_TIMING_REVISION 1u
#define ARC_RPC_CMOS_RECORD_SIZE 512u
#define ARC_RPC_CMOS_CAPTURE_NS UINT64_C(45000000000)
#define ARC_RPC_CMOS_STABILITY_NS UINT64_C(5000000000)
#define ARC_RPC_CMOS_DELETE_NS ARC_RPC_CMOS_CAPTURE_NS
typedef enum { ARC_RPC_LAYOUT_UNKNOWN, ARC_RPC_LAYOUT_RO350, ARC_RPC_LAYOUT_RO360, ARC_RPC_LAYOUT_RO370 } ArcRpcCmosLayout;
typedef struct {
    char content[41], firmware[65];
    unsigned model, layout, procedure, timing;
    bool defaults;
} ArcRpcCmosKey;
typedef struct {
    unsigned rate;
    uint64_t epoch, writes, last_write;
    uint8_t mask[32];
} ArcRpcCmosEvidence;
typedef struct {
    ArcRpcCmosKey key;
    char base[1024];
    uint64_t generation;
    uint8_t payload[240];
    bool present, refused;
    ArcRpcCmosEvidence evidence;
} ArcRpcCmosStore;
typedef enum { ARC_RPC_CMOS_REFUSED = -1, ARC_RPC_CMOS_MISSING, ARC_RPC_CMOS_FOUND } ArcRpcCmosResult;
unsigned arc_riscpc_cmos_layout(const char *name);
bool arc_riscpc_cmos_hex(const char *text, size_t length, char *out);
bool arc_riscpc_cmos_checksum(const uint8_t payload[240]);
void arc_riscpc_cmos_required(unsigned layout, uint8_t mask[32]);
bool arc_riscpc_cmos_oracle(unsigned layout, const uint8_t payload[240], const ArcRpcCmosEvidence *e);
ArcRpcCmosResult arc_riscpc_cmos_store_load(ArcRpcCmosStore *store, const char *save,
    const ArcRpcCmosKey *key, char *error, size_t error_size);
bool arc_riscpc_cmos_store_put(ArcRpcCmosStore *store, const uint8_t payload[240],
    const ArcRpcCmosEvidence *evidence, char *error, size_t error_size);
#endif
