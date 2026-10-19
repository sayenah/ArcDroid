/* Per-session production frontend preparation host. GPL-2.0-only. */
#ifndef ARC_RPC_PREPARATION_HOST_H
#define ARC_RPC_PREPARATION_HOST_H
#include "rpc_preparation.h"
#include "libretro.h"
#include "arc_session.h"
typedef struct ArcRpcFrontendHost ArcRpcFrontendHost;
typedef struct {
    uint64_t load_entry_ns;
    ArcSessionKind kind;
    ArcRpcPreparationHost module; /* Production callbacks; opaque remains borrowed. */
    bool (*environment)(unsigned,void *);
    void (*log)(void *,enum retro_log_level,const char *);
    bool (*stage)(void *,const char *,const char *,ArcRpcPreparationRequest *,char *,size_t);
} ArcRpcFrontendOps;
#if defined(ARCDROID_TEST_PLANS)
typedef struct {
    void (*started_after)(void *,bool);
    bool (*attach_after)(void *,char *,size_t);
    void (*attached_after)(void *);
    void (*diagnostics_after)(void *);
    void *opaque;
} ArcRpcFrontendExtension;
ArcRpcFrontendHost *arc_riscpc_host_low_level(const ArcRpcPreparationRequest *,const ArcRpcFrontendOps *,
    const ArcRpcFrontendExtension *,char *,size_t);
#endif
ArcRpcFrontendHost *arc_riscpc_host_begin(const ArcRpcPreparationRequest *,const ArcRpcFrontendOps *,char *,size_t);
ArcRpcPreparation *arc_riscpc_host_preparation(const ArcRpcFrontendHost *); /* Borrowed. */
bool arc_riscpc_host_pending(const ArcRpcFrontendHost *);
void arc_riscpc_host_frame_begin(ArcRpcFrontendHost *);
void arc_riscpc_host_frame_end(ArcRpcFrontendHost *);
void arc_riscpc_host_pending_frame(ArcRpcFrontendHost *);
void arc_riscpc_host_maintenance(ArcRpcFrontendHost *);
void arc_riscpc_host_diagnostics(ArcRpcFrontendHost *);
void arc_riscpc_host_load_failure(const ArcRpcFrontendOps *,const char *id,const char *redacted);
bool arc_riscpc_host_reset(ArcRpcFrontendHost *);
void arc_riscpc_host_guest_time(ArcRpcFrontendHost *,uint64_t);
void arc_riscpc_host_first_field(ArcRpcFrontendHost *,unsigned,unsigned,double,bool);
void arc_riscpc_host_av_change(ArcRpcFrontendHost *,bool guest);
void arc_riscpc_host_advisory(ArcRpcFrontendHost *,const char *id,const char *text);
const int16_t *arc_riscpc_host_silence(ArcRpcFrontendHost *,size_t frames);
void arc_riscpc_host_clear(ArcRpcFrontendHost *);
#if defined(ARCDROID_TEST_FAULTS)
bool arc_riscpc_host_terminal(const ArcRpcFrontendHost *);
#endif
bool arc_riscpc_host_free(ArcRpcFrontendHost *,char *,size_t);
#endif
