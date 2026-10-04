/* ArcDroid typed presentation/controller boundary. GPL-2.0-only.
 * Types mirror the read-only Arculator JFD presentation boundary; the RPCEmu
 * wrappers below have their own concrete signatures (no pointer casts). */
#ifndef PROBE_PRESENTATION_H
#define PROBE_PRESENTATION_H
#include "jfd.h"
#include "rpcemu.h"
typedef struct {
 void (*seek)(int,int);
 void (*readsector)(int,int,int,int,int);
 void (*writesector)(int,int,int,int,int,int,int);
 void (*readaddress)(int,int,int,int);
 void (*format)(int,int,int,int);
 void (*stop)(void);void (*poll)(void);void (*close)(int);
 int high_res_poll,multiwrite;
} PresentDriveOps;
typedef struct {
 void (*data)(uint8_t,void *);void (*spindown)(void *);void (*finishread)(void *);
 void (*notfound)(void *);void (*datacrcerror)(void *);void (*headercrcerror)(void *);
 void (*writeprotect)(void *);int (*getdata)(int,void *);
 void (*sectorid)(uint8_t,uint8_t,uint8_t,uint8_t,uint8_t,uint8_t,void *);
 void (*indexpulse)(void *);void (*lostdata)(void *);
} PresentFdcOps;
extern PresentDriveOps *probe_present_drives[2];
extern PresentFdcOps *probe_present_fdc;
extern void *probe_present_context;
extern int probe_present_writeprot[2];
extern int probe_requested_fm;
unsigned disc_poll_period_us(void);
void jfd_init(void);bool jfd_load(int,const ArcJfdImage *);
void jfd_set_requested_ch(unsigned,unsigned);void jfd_set_requested_size(int);
void jfd_motor_start(void);
void probe_present_latch_id(uint8_t,uint8_t,uint8_t,uint8_t);
#endif
