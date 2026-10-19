/* Preparation-owned observation cache; never execution authority. GPL-2.0-only. */
#ifndef ARC_RPC_LAUNCH_RECORD_STORE_H
#define ARC_RPC_LAUNCH_RECORD_STORE_H
#include "arc_io.h"
#include "arc_lock.h"
#include "rpc_boot_record.h"
typedef struct {
    ArcLock *owner;
    char path[1200],temporary[1208],directory[1180];
    ArcRpcBootRecord old;
    struct retro_vfs_dir_handle *scan;
    ArcIoFile *file;
    uint8_t bytes[ARC_RPC_BOOT_RECORD_BYTES];
    unsigned phase,entries;
    uint64_t namespace_operations;
    bool found,found_temporary,ready,have_old,published,failed;
} ArcRpcLaunchRecordStore;
bool arc_riscpc_launch_record_begin(ArcRpcLaunchRecordStore *,const char *save,const char *content,ArcLock *owner);
/* One bounded unit per call. Must finish before HD open/recovery. */
bool arc_riscpc_launch_record_recover(ArcRpcLaunchRecordStore *,bool *done);
bool arc_riscpc_launch_record_namespace(const ArcRpcLaunchRecordStore *);
void arc_riscpc_launch_record_discard(ArcRpcLaunchRecordStore *);
bool arc_riscpc_launch_record_publish_begin(ArcRpcLaunchRecordStore *,const ArcRpcBootRecord *);
/* Revalidate at every unit, including immediately before rename. */
bool arc_riscpc_launch_record_publish_step(ArcRpcLaunchRecordStore *,bool valid,bool *done);
bool arc_riscpc_launch_record_invalidate(ArcRpcLaunchRecordStore *);
void arc_riscpc_launch_record_close(ArcRpcLaunchRecordStore *);
#endif
