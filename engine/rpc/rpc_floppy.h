/* ArcDroid-owned instance-based floppy presentation. GPL-2.0-only. */
#ifndef ARC_RPC_FLOPPY_H
#define ARC_RPC_FLOPPY_H
#include "arcdroid_engine.h"
#include "arc_io.h"
#define ARC_RPC_FLOPPY_REV_NS UINT64_C(200000000)
#define ARC_RPC_FLOPPY_MAX_SECTOR 1024u

typedef struct {
    uint64_t session, mount, command;
} ArcRpcFloppyGeneration;
typedef struct {
    bool track_zero, disc_changed, index, write_protected;
} ArcRpcFloppySignals;
typedef enum { ARC_RPC_FLOPPY_FM, ARC_RPC_FLOPPY_MFM } ArcRpcFloppyEncoding;
typedef enum { ARC_RPC_FLOPPY_READ, ARC_RPC_FLOPPY_WRITE,
    ARC_RPC_FLOPPY_READ_ID, ARC_RPC_FLOPPY_READ_TRACK,
    ARC_RPC_FLOPPY_VERIFY, ARC_RPC_FLOPPY_FORMAT } ArcRpcFloppyOperation;
typedef struct {
    ArcRpcFloppyOperation operation;
    unsigned drive, cylinder, head;
    uint8_t c,h,r,n,eot,gpl,dtl,sc,fill;
    ArcRpcFloppyEncoding encoding;
    unsigned rate_kbps;
    bool mt,sk,deleted;
} ArcRpcFloppyRequest;
typedef enum {
    ARC_RPC_FLOPPY_OK, ARC_RPC_FLOPPY_NO_MEDIA, ARC_RPC_FLOPPY_NOT_FOUND,
    ARC_RPC_FLOPPY_ID_CRC, ARC_RPC_FLOPPY_DATA_CRC,
    ARC_RPC_FLOPPY_WRITE_PROTECT, ARC_RPC_FLOPPY_WRONG_CYLINDER,
    ARC_RPC_FLOPPY_CANCELLED
} ArcRpcFloppyResult;
typedef struct {
    bool present, writable, motor, changed, loaded;
    unsigned cylinder, head, sectors, bytes, sides, density;
    uint64_t mount_generation, motor_epoch, stopped_phase, unload_ns;
    /* The prepared JFD belongs to the frontend. The mount pins that immutable
     * preparation until generation invalidation; raw file handles are owned. */
    const ArcJfdImage *jfd;
    ArcIoFile *file;
    uint8_t *raw;
    size_t extent;
} ArcRpcFloppyDrive;
typedef struct {
    ArcRpcFloppyDrive drives[2];
    uint64_t session_generation, command_generation;
} ArcRpcFloppy;
typedef struct {
    ArcRpcFloppyGeneration generation;
    ArcRpcFloppyRequest request;
    ArcJfdView view;
    uint8_t c,h,r,n;
    unsigned bytes;
    size_t raw_offset;
    uint64_t delay_ns;
    bool jfd, deleted, committed;
    uint8_t data[ARC_RPC_FLOPPY_MAX_SECTOR];
} ArcRpcFloppySector;
/* Typed controller/drive events carry all three lifetime tokens. */
typedef enum { ARC_RPC_FLOPPY_EVENT_ID, ARC_RPC_FLOPPY_EVENT_READ_BYTE,
    ARC_RPC_FLOPPY_EVENT_WRITE_BYTE_AVAILABLE, ARC_RPC_FLOPPY_EVENT_INDEX,
    ARC_RPC_FLOPPY_EVENT_TERMINAL } ArcRpcFloppyEventKind;
typedef struct {
    ArcRpcFloppyGeneration generation;
    ArcRpcFloppyEventKind kind;
    ArcRpcFloppyResult result;
    unsigned drive;
    uint8_t c,h,r,n,byte;
} ArcRpcFloppyEvent;
ArcRpcFloppyEvent arc_rpc_floppy_event(const ArcRpcFloppy *f,
    const ArcRpcFloppySector *sector,ArcRpcFloppyEventKind kind,
    unsigned position,ArcRpcFloppyResult result);
void arc_rpc_floppy_init(ArcRpcFloppy *f,uint64_t session);
void arc_rpc_floppy_close(ArcRpcFloppy *f);
bool arc_rpc_floppy_mount(ArcRpcFloppy *f,unsigned drive,const ArcEngineMedia *media,
    char *error,size_t size);
void arc_rpc_floppy_eject(ArcRpcFloppy *f,unsigned drive);
void arc_rpc_floppy_cancel(ArcRpcFloppy *f);
bool arc_rpc_floppy_valid(const ArcRpcFloppy *f,unsigned drive,ArcRpcFloppyGeneration g);
void arc_rpc_floppy_motor(ArcRpcFloppy *f,unsigned drive,bool on,uint64_t now);
void arc_rpc_floppy_step(ArcRpcFloppy *f,unsigned drive,int delta);
uint64_t arc_rpc_floppy_phase(const ArcRpcFloppy *f,unsigned drive,uint64_t now);
ArcRpcFloppySignals arc_rpc_floppy_signals(const ArcRpcFloppy *f,unsigned drive,uint64_t now);
ArcRpcFloppyResult arc_rpc_floppy_select(ArcRpcFloppy *f,const ArcRpcFloppyRequest *r,
    uint64_t now,ArcRpcFloppySector *out);
ArcRpcFloppyResult arc_rpc_floppy_commit(ArcRpcFloppy *f,ArcRpcFloppySector *s,
    const uint8_t *bytes,unsigned consumed,bool tc);
ArcRpcFloppyResult arc_rpc_floppy_format(ArcRpcFloppy *f,ArcRpcFloppySector *s,
    const uint8_t *ids,unsigned count,uint8_t fill);
void arc_rpc_floppy_missing(ArcRpcFloppy *f,const ArcRpcFloppyRequest *r);
/* Session integration: all media mutations cancel the controller first. */
bool arc_rpc_fdc_mount(unsigned drive,const ArcEngineMedia *m,char *error,size_t size);
void arc_rpc_fdc_eject(unsigned drive);
void arc_rpc_fdc_close(void);
/* Quiesce mechanics in the old clock domain before a session clock reset. */
void arc_rpc_fdc_rebase(void);
bool arc_rpc_fdc_idle(unsigned drive);
uint64_t arc_rpc_fdc_media_generation(unsigned drive);
#endif
