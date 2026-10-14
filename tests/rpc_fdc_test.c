/* Independent FDC37C665GT expectations: tables 17–22,27,28,31/32.
 * Controller component boundary; guest/MMIO coverage is rpc-floppy-guest.
 * Fixtures, bytes and VFS are authored here. GPL-2.0-only. */
#include "rpc_namespace.h"
#include "rpcemu.h"
#include "fdc.h"
#include "rpc_floppy.h"
#include "rpc_timing.h"
#include <stdlib.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
struct iomd iomd;
static ArcRpcScheduler clock_state;
static uint8_t raw[2][819200];
static bool fail_store;
static unsigned commits,raw_writes;
struct retro_vfs_file_handle { unsigned drive;size_t at;bool writable,readable; };
static struct retro_vfs_file_handle handles[2];
static struct retro_vfs_file_handle *open_file(const char *path,unsigned mode,unsigned hints)
{
    (void)hints;unsigned d=!strcmp(path,"raw0")?0:!strcmp(path,"raw1")?1:2;
    if(d==2)return NULL;
    handles[d]=(struct retro_vfs_file_handle){d,0,(mode&RETRO_VFS_FILE_ACCESS_WRITE)!=0,(mode&(RETRO_VFS_FILE_ACCESS_READ|RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING))!=0};return &handles[d];
}
static int close_file(struct retro_vfs_file_handle *f) { (void)f;return 0; }
static int64_t file_size(struct retro_vfs_file_handle *f) { return f->drive?655360:819200; }
static int64_t seek_file(struct retro_vfs_file_handle *f,int64_t at,int whence)
{ int64_t start=whence==RETRO_VFS_SEEK_POSITION_CURRENT?(int64_t)f->at:whence==RETRO_VFS_SEEK_POSITION_END?file_size(f):0;
  if(start+at<0||start+at>file_size(f))return -1;
  f->at=(size_t)(start+at);return 0; }
static int64_t read_file(struct retro_vfs_file_handle *f,void *out,uint64_t n)
{ if(!f->readable||n>(uint64_t)file_size(f)-f->at)return -1;memcpy(out,raw[f->drive]+f->at,(size_t)n);f->at+=(size_t)n;return (int64_t)n; }
static int64_t write_file(struct retro_vfs_file_handle *f,const void *in,uint64_t n)
{ if(fail_store||!f->writable||n>(uint64_t)file_size(f)-f->at)return -1;memcpy(raw[f->drive]+f->at,in,(size_t)n);f->at+=(size_t)n;++raw_writes;return (int64_t)n; }
static int flush_file(struct retro_vfs_file_handle *f) { (void)f;return fail_store?-1:0; }
void updateirqs(void) { }
static void tick(void)
{
    ArcRpcEvent event;
    assert(clock_state.next_event!=ARC_RPC_EVENT_COUNT);
    assert(arc_rpc_take_event(&clock_state,clock_state.next_ns,&event));
    assert(event==ARC_RPC_EVENT_FDC);fdc_callback();
}
static void command(uint8_t op,const uint8_t *p,unsigned n)
{ assert((fdc_read(0x3f4)&0xc0)==0x80);fdc_write(0x3f5,op);
  if(n)assert(!arc_rpc_fdc_idle(0)&&!arc_rpc_fdc_idle(1));
  for(unsigned i=0;i<n;++i)fdc_write(0x3f5,p[i]); }
static void result(uint8_t *out,unsigned count)
{
    assert((fdc_read(0x3f4)&0xf0)==0xd0);
    for(unsigned i=0;i<count;++i){assert(!arc_rpc_fdc_idle(0)&&!arc_rpc_fdc_idle(1));out[i]=fdc_read(0x3f5);}
    assert((fdc_read(0x3f4)&0xf0)==0x80);
    assert(!(iomd.fiq.status&IOMD_FIQ_FLOPPY_DMA_REQUEST));
}
static void wait_result(void)
{ unsigned bound=0;while((fdc_read(0x3f4)&0xf0)!=0xd0){assert(++bound<100000);tick();} }
static void sis(uint8_t st0,uint8_t pcn)
{ uint8_t bytes[2];command(8,NULL,0);result(bytes,st0==0x80?1:2);assert(bytes[0]==st0);if(st0!=0x80)assert(bytes[1]==pcn); }
static void start(void)
{
    arc_rpc_fdc_close();memset(&iomd,0,sizeof(iomd));assert(arc_rpc_scheduler_init(&clock_state,100));arc_rpc_timing_bind(&clock_state);
    fdc_reset();assert(!arc_rpc_fdc_idle(0)&&!arc_rpc_fdc_idle(1));
    fdc_write(0x3f2,0x1c);assert(!arc_rpc_fdc_idle(0)&&!arc_rpc_fdc_idle(1));
    tick();assert(!arc_rpc_fdc_idle(0)&&!arc_rpc_fdc_idle(1));
    for(unsigned d=0;d<4;++d)sis(0xc0|d,0);
    assert(arc_rpc_fdc_idle(0)&&arc_rpc_fdc_idle(1));
    uint8_t specify[]={0xf1,2};command(3,specify,2);fdc_write(0x3f7,2);
}
static void mount_raw(unsigned drive,bool writable)
{
    ArcEngineMedia m={0};m.path=drive?"raw1":"raw0";m.kind=drive?ARC_MEDIA_ADL_640K:ARC_MEDIA_ADF_800K;
    m.geometry=drive?ARC_GEOMETRY_640K_L:ARC_GEOMETRY_800K_D;m.writable=writable;
    char error[128];assert(arc_rpc_fdc_mount(drive,&m,error,sizeof(error)));
}
static void seek(unsigned unit,unsigned drive,uint8_t cylinder)
{
    uint8_t p[]={unit,cylinder};fdc_write(0x3f2,0x3c|drive);command(15,p,2);
    assert(fdc_read(0x3f4)&(1u<<unit));
    while(!(iomd.irqb.status&IOMD_IRQB_FLOPPY))tick();
    sis(0x20|unit,cylinder);
}
static void data_command(uint8_t opcode,unsigned unit,unsigned head,unsigned c,unsigned h,unsigned r,unsigned n,unsigned eot,unsigned dtl)
{ uint8_t p[]={unit|(head<<2),c,h,r,n,eot,0x35,dtl};command(opcode,p,8); }
static unsigned read_transfer(uint8_t *bytes,unsigned tc_at,bool nd)
{
    unsigned at=0,bound=0;
    while((fdc_read(0x3f4)&0xf0)!=0xd0){
        assert(++bound<100000);
        bool service=nd?(fdc_read(0x3f4)&0xc0)==0xc0:(iomd.fiq.status&IOMD_FIQ_FLOPPY_DMA_REQUEST)!=0;
        if(service){assert(at<32768);bytes[at]=nd?fdc_read(0x3f5):fdc_dma_read(at+1==tc_at?0x302a000:0x3012000);++at;}
        else tick();
    }return at;
}
static void write_transfer(unsigned count,bool tc,uint8_t value,bool nd)
{
    unsigned at=0,bound=0;
    while((fdc_read(0x3f4)&0xf0)!=0xd0){
        assert(++bound<100000);
        bool service=nd?(fdc_read(0x3f4)&0xc0)==0x80:(iomd.fiq.status&IOMD_FIQ_FLOPPY_DMA_REQUEST)!=0;
        if(service&&at<count){if(nd)fdc_write(0x3f5,value);else fdc_dma_write(tc&&at+1==count?0x302a000:0x3012000,value);++at;}
        else tick();
    }assert(at==count);
}
static void format_track(unsigned drive,unsigned head,unsigned n,const uint8_t *ids,unsigned count,uint8_t fill,uint8_t *res)
{
    fdc_write(0x3f2,0x3c|drive);
    command(0x4d,(const uint8_t[]){drive|(head<<2),n,count,0x32,fill},5);
    unsigned at=0,bound=0;
    while((fdc_read(0x3f4)&0xf0)!=0xd0){
        assert(++bound<100000);
        if(iomd.fiq.status&1){assert(at<count*4);fdc_dma_write(at+1==count*4?0x302a000:0x3012000,ids[at]);++at;}
        else tick();
    }
    result(res,7);
    assert(at==(count==(drive?16u:5u)?count*4:0));
}
static void format_id_sets(void)
{
    /* D-143: validate all tuples before any write, including a bad last ID.
     * E/D raw has five IDs; the ADL helper has sixteen, on either head. */
    static uint8_t snapshot[sizeof(raw[0])];
    uint8_t ids[17*4],bad[sizeof(ids)],res[7],bytes[32768];
    for(unsigned drive=0;drive<2;++drive){
        unsigned count=drive?16:5,n=drive?1:3,size=drive?256:1024;
        for(unsigned track=0;track<3;++track){
            unsigned c=track/2,h=track%2;seek(drive,drive,c);
            for(unsigned i=0;i<count;++i){
                ids[4*i]=c;ids[4*i+1]=h;ids[4*i+2]=track==2?(i+count-1)%count:i;ids[4*i+3]=n;
            }
            uint8_t fill=0xa0+drive*8+track;
            memcpy(snapshot,raw[drive],sizeof(snapshot));
            format_track(drive,h,n,ids,count,fill,res);
            assert(res[0]==(drive|(h<<2))&&res[1]==0&&res[2]==0);
            assert(!memcmp(res+3,ids+(count-1)*4,4));
            memset(snapshot+(size_t)track*count*size,fill,count*size);
            assert(!memcmp(raw[drive],snapshot,sizeof(snapshot)));
            for(unsigned r=0;r<count;++r){
                data_command(0x46,drive,h,c,h,r,n,r,255);
                assert(read_transfer(bytes,size,false)==size);result(res,7);
                assert(res[1]==0&&res[2]==0);
                for(unsigned b=0;b<size;++b)assert(bytes[b]==fill);
            }
        }
        /* Duplicate (also omits an ID), missing tuple, out-of-range R,
         * wrong C/H/N on the last tuple, and an excess count. */
        for(unsigned defect=0;defect<7;++defect){
            memcpy(bad,ids,count*4);unsigned supplied=count;
            if(defect==0)bad[(count-1)*4+2]=bad[2];
            else if(defect==1)--supplied;
            else if(defect==2)bad[(count-1)*4+2]=count;
            else if(defect==3)bad[(count-1)*4]=0;
            else if(defect==4)bad[(count-1)*4+1]=1;
            else if(defect==5)bad[(count-1)*4+3]=n+1;
            else ++supplied;
            unsigned before=raw_writes;
            format_track(drive,0,n,bad,supplied,0xf2,res);
            assert(res[0]==(0x40|drive)&&res[1]==2&&res[2]==0);
            assert(raw_writes==before&&!memcmp(raw[drive],snapshot,sizeof(snapshot)));
        }
    }
    seek(0,0,0);
}
static ArcJfdImage jfd;
static ArcJfdOverlay overlay;
static uint8_t decoded[5*1024],eligible[5],*replacements[5];
static ArcJfdSector sectors[5];
static bool commit_sector(ArcJfdImage *im,size_t key,const uint8_t *data)
{
    assert(im==&jfd&&key<5);++commits;if(fail_store)return false;
    memcpy(decoded+key*1024,data,1024);return true;
}
static void mount_jfd(void)
{
    memset(&jfd,0,sizeof(jfd));memset(&overlay,0,sizeof(overlay));
    jfd.decoded=decoded;jfd.decoded_size=sizeof(decoded);jfd.sectors=sectors;jfd.sector_count=5;jfd.track_count=2;
    jfd.tracks[0]=(ArcJfdTrack){0,5,true,NULL};overlay.writable=true;overlay.visible=true;
    overlay.eligible=eligible;overlay.data=replacements;overlay.commit=commit_sector;jfd.overlay=&overlay;
    for(unsigned i=0;i<5;++i){sectors[i]=(ArcJfdSector){i*36352,i*1024,1024,i,2,0};eligible[i]=1;}
    ArcEngineMedia m={0};m.jfd=&jfd;m.kind=ARC_MEDIA_JFD;m.writable=true;
    char error[128];assert(arc_rpc_fdc_mount(0,&m,error,sizeof(error)));
}
static void advance_to(uint64_t until)
{
    ArcRpcEvent event;unsigned bound=0;
    while(arc_rpc_take_event(&clock_state,until,&event)){
        assert(++bound<100000&&event==ARC_RPC_EVENT_FDC);fdc_callback();
    }
}
static void wait_seek_end(void)
{
    unsigned bound=0;
    while(!(iomd.irqb.status&IOMD_IRQB_FLOPPY)){assert(++bound<100000);tick();}
}
static void media_change_seek(void)
{
    /* Table 17 / D-144: a medium does not gate STEP or seek completion.
     * P1's SEEK to 40 must return 20 28 for every media retirement. */
    for(unsigned drive=0;drive<2;++drive)for(unsigned op=0;op<3;++op)for(unsigned action=0;action<3;++action){
        start();mount_raw(drive,true);fdc_write(0x3f2,0x3c|drive);
        if(op==2)seek(drive,drive,40);
        if(op==2)command(7,(const uint8_t[]){drive},1);
        else command(op==0?15:0xcf,(const uint8_t[]){drive,40},2);
        for(unsigned i=0;i<5;++i)tick();
        assert(fdc_read(0x3f4)&(1u<<drive));
        if(action==0)arc_rpc_fdc_eject(drive);
        else if(action==1)mount_raw(drive,true);
        else {ArcEngineMedia bad={0};assert(!arc_rpc_fdc_mount(drive,&bad,NULL,0));}
        assert(fdc_read(0x3f4)&(1u<<drive));wait_seek_end();sis(0x20|drive,op==2?0:40);
        uint8_t res[10];command(14,NULL,0);result(res,10);assert(res[drive]==(op==2?0:40));
        command(4,(const uint8_t[]){drive},1);result(res,1);
        assert((res[0]&0x10)==(op==2?0x10:0));
    }
    /* Reset and teardown retire the mechanical session, including stale callbacks. */
    for(unsigned action=0;action<3;++action){
        start();mount_raw(0,true);command(15,(const uint8_t[]){0,40},2);
        for(unsigned i=0;i<5;++i)tick();
        if(action==0)fdc_reset();else if(action==1)arc_rpc_fdc_rebase();else {arc_rpc_fdc_close();start();}
        fdc_callback();advance_to(UINT64_C(100000000));assert(!(iomd.irqb.status&IOMD_IRQB_FLOPPY));sis(0x80,0);
    }
    arc_rpc_fdc_close();
}
static void pending_irq_cancel(void)
{
    /* N1: cancelling a read with no pending interrupt must not raise IRQB.
     * Cover both drives and eject, replacement, and failed insertion. */
    for(unsigned drive=0;drive<2;++drive)for(unsigned action=0;action<3;++action){
        start();mount_raw(drive,true);fdc_write(0x3f2,0x3c|drive);
        data_command(0x46,drive,0,0,0,0,drive?1:3,4,255);
        assert((fdc_read(0x3f4)&0xf0)==0x50);
        assert(!(iomd.irqb.status&IOMD_IRQB_FLOPPY));
        if(action==0)arc_rpc_fdc_eject(drive);
        else if(action==1)mount_raw(drive,true);
        else {ArcEngineMedia bad={0};assert(!arc_rpc_fdc_mount(drive,&bad,NULL,0));}
        assert(!(iomd.irqb.status&IOMD_IRQB_FLOPPY));
        assert(!iomd.fiq.status);
    }
    /* P2: unit 1 completes SEEK during a drive-0 read. Ejecting drive 0
     * cancels DMA, but cannot acknowledge unit 1's pending 21 03 status.
     * Repeat with the drives reversed and with all three retirement paths. */
    for(unsigned drive=0;drive<2;++drive)for(unsigned action=0;action<3;++action){
        unsigned other=drive^1;start();mount_raw(0,true);mount_raw(1,true);
        fdc_write(0x3f2,0x3c|other);command(15,(const uint8_t[]){other,3},2);
        fdc_write(0x3f2,0x3c|drive);data_command(0x46,drive,0,0,0,0,drive?1:3,4,255);
        wait_seek_end();assert((fdc_read(0x3f4)&0xf0)==0x50);
        if(action==0)arc_rpc_fdc_eject(drive);
        else if(action==1)mount_raw(drive,true);
        else {ArcEngineMedia bad={0};assert(!arc_rpc_fdc_mount(drive,&bad,NULL,0));}
        assert(iomd.irqb.status&IOMD_IRQB_FLOPPY);assert(!iomd.fiq.status);
        sis(0x20|other,3);assert(!(iomd.irqb.status&IOMD_IRQB_FLOPPY));
    }
    arc_rpc_fdc_close();
}
static void qa_drive_isolation(void)
{
    /* Only the DOR-selected drive's index reaches IOMD (Q27). */
    start();mount_raw(0,true);mount_raw(1,true);fdc_write(0x3f2,0x2c);
    advance_to(UINT64_C(200500000));assert(!(iomd.irqa.status&IOMD_IRQA_FLOPPY_INDEX));
    fdc_write(0x3f2,0x2d);advance_to(UINT64_C(400500000));assert(iomd.irqa.status&IOMD_IRQA_FLOPPY_INDEX);
    iomd.irqa.status&=~IOMD_IRQA_FLOPPY_INDEX;arc_rpc_fdc_eject(1);
    advance_to(UINT64_C(600500000));assert(!(iomd.irqa.status&IOMD_IRQA_FLOPPY_INDEX));
    mount_raw(1,true);advance_to(UINT64_C(800500000));assert(iomd.irqa.status&IOMD_IRQA_FLOPPY_INDEX);
    /* Independent change latches; DOR selects DIR even when PCN differs (Q31). */
    seek(0,0,1);assert(fdc_read(0x3f7)==0);fdc_write(0x3f2,0x3d);assert(fdc_read(0x3f7)==0x80);
    seek(1,1,1);assert(fdc_read(0x3f7)==0);arc_rpc_fdc_eject(0);assert(fdc_read(0x3f7)==0);
    fdc_write(0x3f2,0x3c);assert(fdc_read(0x3f7)==0x80);
    /* A stopped spindle at index position has no index signal (Q13). */
    ArcRpcFloppy model;arc_rpc_floppy_init(&model,1);
    ArcEngineMedia m={0};m.data=raw[0];m.size=sizeof(raw[0]);m.kind=ARC_MEDIA_ADF_800K;m.geometry=ARC_GEOMETRY_800K_D;
    assert(arc_rpc_floppy_mount(&model,0,&m,NULL,0));
    assert(!arc_rpc_floppy_signals(&model,0,0).index);arc_rpc_floppy_motor(&model,0,true,0);
    assert(arc_rpc_floppy_signals(&model,0,0).index);arc_rpc_floppy_motor(&model,0,false,0);
    assert(!arc_rpc_floppy_signals(&model,0,0).index);arc_rpc_floppy_close(&model);
}
static void qa_fifo(void)
{
    /* p.44: reads assert at 16-threshold and stay active until empty;
     * writes resume at exactly threshold bytes left, including equality. */
    uint8_t res[7],bytes[32768];memset(raw[0],0x57,1024);
    for(unsigned threshold=2;threshold<=14;threshold+=6){
        start();mount_raw(0,true);command(19,(const uint8_t[]){0,threshold-1,0},3);
        data_command(0x46,0,0,0,0,0,3,0,255);unsigned trigger=16-threshold;
        advance_to(UINT64_C(200000000)+(trigger-1)*UINT64_C(32000));assert(!iomd.fiq.status);
        advance_to(UINT64_C(200000000)+trigger*UINT64_C(32000));assert(iomd.fiq.status&1);
        for(unsigned i=0;i<trigger;++i){assert(iomd.fiq.status&1);assert(fdc_dma_read(0x3012000)==0x57);}
        assert(!iomd.fiq.status);assert(read_transfer(bytes,1024-trigger,false)==1024-trigger);result(res,7);assert(!res[1]);
        start();mount_raw(0,true);command(19,(const uint8_t[]){0,threshold-1,0},3);
        data_command(0x45,0,0,0,0,0,3,0,255);
        for(unsigned i=0;i<16;++i){assert(iomd.fiq.status&1);fdc_dma_write(0x3012000,0x57);}
        assert(!iomd.fiq.status);
        advance_to(UINT64_C(200000000)+(16-threshold-1)*UINT64_C(32000));assert(!iomd.fiq.status);
        advance_to(UINT64_C(200000000)+(16-threshold)*UINT64_C(32000));assert(iomd.fiq.status&1);
        write_transfer(1008,true,0x57,false);result(res,7);assert(!res[1]);
    }
    /* TC consumes its byte and discards already queued read bytes (Q18). */
    start();mount_raw(0,true);command(19,(const uint8_t[]){0,7,0},3);
    data_command(0x46,0,0,0,0,0,3,0,255);
    while(!(iomd.fiq.status&1))tick();
    assert(fdc_dma_read(0x302a000)==0x57);
    assert(!iomd.fiq.status&&fdc_dma_read(0x3012000)==0);wait_result();result(res,7);assert(!res[1]);
}
static void qa_controls(void)
{
    uint8_t res[10];
    start();mount_raw(0,true);
    /* EC=0 ignores SC; EC=1 counts sectors, without host DMA (Q20, p.66). */
    command(0x56,(const uint8_t[]){0,0,0,0,3,4,0x35,1},8);
    while((fdc_read(0x3f4)&0xf0)!=0xd0){assert(!iomd.fiq.status);tick();}
    result(res,7);assert(res[0]==0&&res[1]==0&&res[3]==1&&res[5]==1);
    command(0x56,(const uint8_t[]){0x80,0,0,0,3,4,0x35,2},8);
    while((fdc_read(0x3f4)&0xf0)!=0xd0){assert(!iomd.fiq.status);tick();}
    result(res,7);assert(res[0]==0&&res[1]==0&&res[3]==0&&res[5]==2);
    /* LOCK retains EFIFO/threshold, while EIS/POLL reset (Q24/Q33). */
    for(unsigned config=0x1d;config<=0x3d;config+=0x20){
        command(19,(const uint8_t[]){0,config,11},3);command(0x94,NULL,0);result(res,1);
        fdc_write(0x3f4,0x82);tick();for(unsigned d=0;d<4;++d)sis(0xc0|d,0);
        command(14,NULL,0);result(res,10);assert(res[8]==(config&0x2f)&&res[9]==11);
        assert(!(iomd.irqb.status&IOMD_IRQB_FLOPPY));sis(0x80,0);
    }
    /* Hardware DSR is 02H; last DSR/CCR write and software-reset retention
     * are observed through one STEP's independent SRT duration (p.33). */
    for(unsigned variant=0;variant<4;++variant){
        start();mount_raw(0,true);
        if(variant==0){fdc_reset();fdc_write(0x3f2,0x1c);}
        else if(variant==1){fdc_write(0x3f7,0);fdc_write(0x3f2,8);fdc_write(0x3f2,0x1c);}
        else if(variant==2)fdc_write(0x3f4,0x81);
        else {fdc_write(0x3f4,2);fdc_write(0x3f7,3);}
        if(variant<3){tick();for(unsigned d=0;d<4;++d)sis(0xc0|d,0);}
        command(3,(const uint8_t[]){0xf1,2},2);uint64_t begin=clock_state.now_ns;
        command(15,(const uint8_t[]){0,1},2);wait_seek_end();
        const uint64_t step[]={2000000,1000000,1666667,500000};assert(clock_state.now_ns-begin==step[variant]);sis(0x20,1);
    }
}
static void qa_mechanics(void)
{
    uint8_t res[10];
    /* D-144: 79 pulses suffice from 79; from 80 they leave carriage at 1
     * and raise SE|EC/abnormal. PCN resets to zero in both cases (Q22). */
    for(unsigned cylinder=79;cylinder<=80;++cylinder){
        start();mount_raw(0,true);seek(0,0,cylinder);uint64_t begin=clock_state.now_ns;
        command(7,(const uint8_t[]){0},1);wait_seek_end();assert(clock_state.now_ns-begin==UINT64_C(158000000));
        sis(cylinder==79?0x20:0x70,0);command(14,NULL,0);result(res,10);assert(res[0]==0);
        command(4,(const uint8_t[]){0},1);result(res,1);assert(res[0]==(cylinder==79?0x38:0x28));
        /* PCN is zero; a +1 Relative Seek distinguishes carriage 0/1 by
         * then stepping out twice: EC occurs only from carriage 1 (Q23). */
        command(0xcf,(const uint8_t[]){0,1},2);wait_seek_end();sis(0x20,1);
        command(0x8f,(const uint8_t[]){0,2},2);wait_seek_end();sis(cylinder==79?0x30:0x20,255);
        command(4,(const uint8_t[]){0},1);result(res,1);assert(res[0]==0x38);
    }
    start();fdc_write(0x3f2,0x1e);uint64_t begin=clock_state.now_ns;
    command(7,(const uint8_t[]){2},1);wait_seek_end();assert(clock_state.now_ns-begin==UINT64_C(158000000));sis(0x72,0);
    /* Physical WP is reported by SENSE DRIVE STATUS (Q26). */
    start();mount_raw(0,false);command(4,(const uint8_t[]){0},1);result(res,1);assert(res[0]==0x78);
}
static void qa_refusals_waits(void)
{
    uint8_t res[7];unsigned before;
    start();mount_raw(0,true);before=raw_writes;
    data_command(0x49,0,0,0,0,0,3,0,255);result(res,7);assert(res[1]==2&&raw_writes==before);
    data_command(0x46,0,0,0,0,0,2,0,255);wait_result();result(res,7);assert(res[1]==4);
    /* Failed JFD/raw stores latch WP even after the host store recovers. */
    for(unsigned kind=0;kind<2;++kind){
        start();if(kind==0)mount_jfd();else mount_raw(0,true);
        fail_store=true;data_command(0x45,0,0,0,0,0,3,0,255);write_transfer(1024,true,0x39,false);result(res,7);assert(res[1]==2);
        fail_store=false;before=kind==0?commits:raw_writes;
        data_command(0x45,0,0,0,0,0,3,0,255);result(res,7);assert(res[1]==2);
        assert(before==(kind==0?commits:raw_writes));
    }
    /* Q10 removes FORMAT's distinct raw-store failure latch. */
    start();mount_raw(0,true);uint8_t ids[20];
    for(unsigned r=0;r<5;++r){ids[4*r]=ids[4*r+1]=0;ids[4*r+2]=r;ids[4*r+3]=3;}
    fail_store=true;format_track(0,0,3,ids,5,0x48,res);assert(res[1]==2);fail_store=false;before=raw_writes;
    command(0x4d,(const uint8_t[]){0,3,5,0x32,0x48},5);result(res,7);assert(res[1]==2&&raw_writes==before);
    /* Missing ID waits through two index occurrences (Q29, pp.40/69). */
    start();mount_raw(0,true);advance_to(UINT64_C(55000000));data_command(0x46,0,0,0,0,99,3,99,255);
    while((fdc_read(0x3f4)&0xf0)!=0xd0){assert(!iomd.fiq.status);tick();}
    assert(clock_state.now_ns==UINT64_C(400000000));result(res,7);assert(res[1]==4);
    /* Head load at 198 ms crosses index; R0 must wait to 400 ms (Q40). */
    start();mount_raw(0,true);advance_to(UINT64_C(198000000));data_command(0x46,0,0,0,0,0,3,0,255);
    unsigned bound=0;while(!(iomd.fiq.status&1)){assert(++bound<100000);tick();}
    assert(clock_state.now_ns==UINT64_C(400032000));arc_rpc_fdc_eject(0);
}
static void idle_services(void)
{
    uint8_t bytes[7];
    for(unsigned drive=0;drive<2;++drive){
        start();mount_raw(0,true);mount_raw(1,true);fdc_write(0x3f2,0x3c|drive);
        uint64_t generation=arc_rpc_fdc_media_generation(drive);
        assert(arc_rpc_fdc_idle(0)&&arc_rpc_fdc_idle(1));
        /* Motor and recurring index IRQs do not block admission. */
        tick();assert(arc_rpc_fdc_idle(0)&&arc_rpc_fdc_idle(1));
        command(15,(const uint8_t[]){drive,1},2);
        assert(!arc_rpc_fdc_idle(drive)&&arc_rpc_fdc_idle(drive^1));
        while(!(iomd.irqb.status&IOMD_IRQB_FLOPPY))tick();
        assert(!arc_rpc_fdc_idle(drive)&&arc_rpc_fdc_idle(drive^1));
        /* Reading an unrelated result acknowledges IRQ, preserving seek cause. */
        command(16,NULL,0);result(bytes,1);assert(!(iomd.irqb.status&IOMD_IRQB_FLOPPY));
        assert(!arc_rpc_fdc_idle(drive));sis(0x20|drive,1);assert(arc_rpc_fdc_idle(drive));
        command(7,(const uint8_t[]){drive},1);assert(!arc_rpc_fdc_idle(drive));
        while(!(iomd.irqb.status&IOMD_IRQB_FLOPPY))tick();sis(0x20|drive,0);assert(arc_rpc_fdc_idle(drive));
        data_command(0x46,drive,0,0,0,0,drive?1:3,0,255);
        assert(!arc_rpc_fdc_idle(drive)&&!arc_rpc_fdc_idle(drive^1));
        while(!(iomd.fiq.status&1))tick();assert(!arc_rpc_fdc_idle(drive));
        /* IRQ/FIQ masks never decide controller idle. */
        iomd.fiq.mask=0;iomd.irqb.mask=0;assert(!arc_rpc_fdc_idle(drive));
        (void)fdc_dma_read(0x302a000);wait_result();assert(!arc_rpc_fdc_idle(drive));
        (void)fdc_read(0x3f5);assert(!arc_rpc_fdc_idle(drive));
        for(unsigned i=1;i<7;++i)(void)fdc_read(0x3f5);
        assert(arc_rpc_fdc_idle(drive)&&arc_rpc_fdc_idle(drive^1));
        arc_rpc_fdc_eject(drive);assert(arc_rpc_fdc_media_generation(drive)!=generation);
        fdc_write(0x3f2,0x38|drive);assert(!arc_rpc_fdc_idle(0)&&!arc_rpc_fdc_idle(1));
    }
    arc_rpc_fdc_close();assert(!arc_rpc_fdc_idle(0)&&!arc_rpc_fdc_idle(2));
}
int main(int argc,char **argv)
{
    struct retro_vfs_interface vfs={0};vfs.open=open_file;vfs.close=close_file;vfs.size=file_size;vfs.seek=seek_file;
    vfs.read=read_file;vfs.write=write_file;vfs.flush=flush_file;arc_io_set_vfs(&vfs);
    if(argc==2){
        if(!strcmp(argv[1],"--media-change"))media_change_seek();
        else {assert(!strcmp(argv[1],"--pending-irq"));pending_irq_cancel();}
        puts("rpc-fdc: media-change mechanics / pending IRQ passed");return 0;
    }
    uint8_t res[10],bytes[32768];
    start();command(16,NULL,0);result(res,1);assert(res[0]==0x90);
    for(unsigned op=0;op<256;++op)if((op&31)==17||(op&31)==25||(op&31)==29){command(op,NULL,0);result(res,1);assert(res[0]==0x80);}
    command(0xff,NULL,0);result(res,1);assert(res[0]==0x80);
    const uint8_t conf[]={0,0x4d,17},spec[]={0xa7,5},perp[]={0x9f};
    command(19,conf,3);command(3,spec,2);command(18,perp,1);command(0x94,NULL,0);result(res,1);assert(res[0]==0x10);
    command(14,NULL,0);result(res,10);assert(res[4]==0xa7&&res[5]==5&&res[7]==0x9f&&res[8]==0x4d&&res[9]==17);
    fdc_write(0x3f4,0x82);tick();for(unsigned d=0;d<4;++d)sis(0xc0|d,0);
    command(14,NULL,0);result(res,10);assert(res[7]==0x9c&&res[8]==0x0d&&res[9]==17);
    command(20,NULL,0);result(res,1);assert(res[0]==0);
    /* All empty-drive refusals precede DMA, including VALID FM commands. */
    start();const unsigned opcodes[]={5,6,9,12,22,0x45,0x46,0x49,0x4c,0x56};
    for(unsigned i=0;i<sizeof(opcodes)/sizeof(opcodes[0]);++i){data_command(opcodes[i],0,0,0,0,0,3,4,255);wait_result();result(res,7);assert(res[0]==0x40&&res[1]==5);}
    mount_raw(0,true);mount_raw(1,true);
    fdc_write(0x3f2,0x14);command(0x4a,(const uint8_t[]){0},1);wait_result();
    assert(!(iomd.irqb.status&0x10));fdc_write(0x3f2,0x1c);assert(iomd.irqb.status&0x10);result(res,7);
    command(0x0a,(const uint8_t[]){0},1);wait_result();result(res,7);assert(res[1]==1);
    for(unsigned d=0;d<2;++d)for(unsigned c=0;c<2;++c)for(unsigned h=0;h<2;++h)for(unsigned r=0;r<(d?16:5);++r)
        memset(raw[d]+(((size_t)c*2+h)*(d?16:5)+r)*(d?256:1024),0x10+d*0x40+c*0x20+h*8+r,d?256:1024);
    fdc_write(0x3f2,0x14);data_command(0x46,0,0,0,0,0,3,0,255);tick();tick();
    assert(!(iomd.fiq.status&1)&&fdc_dma_read(0x302a000)==0);
    fdc_write(0x3f2,0x1c);assert(iomd.fiq.status&1);
    assert(read_transfer(bytes,1024,false)==1024&&bytes[0]==0x10);result(res,7);
    fdc_write(0x3f2,0x2c);data_command(0x46,0,0,0,0,0,3,0,255);
    while((fdc_read(0x3f4)&0xf0)!=0xd0){assert(!(iomd.fiq.status&1));tick();}
    result(res,7);assert(res[1]==4);fdc_write(0x3f2,0x3c);
    command(15,(const uint8_t[]){0,2},2);tick();assert(!(iomd.irqb.status&0x10));
    command(4,(const uint8_t[]){0},1);result(res,1);assert(res[0]==0x28); /* first STEP already moved */
    tick();sis(0x20,2);seek(0,0,0);
    /* DOR chooses the physical drive while unit chooses the controller PCN. */
    seek(1,0,1);command(14,NULL,0);result(res,10);assert(res[0]==0&&res[1]==1);
    uint8_t ds[]={1};command(4,ds,1);result(res,1);assert(res[0]==0x29); /* DOR0 moved */
    fdc_write(0x3f2,0x3d);command(4,ds,1);result(res,1);assert(res[0]==0x39); /* DOR1 remains TRK0 */
    seek(0,0,0); /* PCN0 was zero: no step, physical head stays at 1. */
    data_command(0x46,0,0,0,0,0,3,0,255);wait_result();result(res,7);assert(res[2]&0x10);
    seek(1,0,0);
    /* Requested H=1 does not select physical HDS=0. */
    data_command(0x46,0,0,0,1,0,3,0,255);wait_result();result(res,7);assert(res[1]==4&&res[2]==0);
    data_command(0x46,0,1,0,1,0,3,0,255);assert(read_transfer(bytes,1024,false)==1024);result(res,7);
    assert(bytes[0]==0x18&&res[0]==4&&res[3]==1&&res[4]==1&&res[5]==1);
    /* CHRN advances: two logical sectors, exactly 2048 host bytes. */
    data_command(0x46,0,0,0,0,0,3,1,255);assert(read_transfer(bytes,2048,false)==2048);result(res,7);
    assert(bytes[0]==0x10&&bytes[1024]==0x11&&res[3]==1&&res[5]==1);
    data_command(0x46,1,0,0,0,0,3,3,255);assert(read_transfer(bytes,3072,false)==3072);result(res,7);
    assert(bytes[0]==0x10&&bytes[1024]==0x11&&bytes[2048]==0x12&&res[5]==3);
    /* MT crosses HDS0 EOT to sector 1 on HDS1; final CHRN complements H. */
    data_command(0xc6,0,0,0,0,0,3,1,255);assert(read_transfer(bytes,3072,false)==3072);result(res,7);
    assert(bytes[2048]==0x19&&res[3]==1&&res[4]==0&&res[5]==1);
    /* VERIFY uses no DRQ and count includes every selected sector. */
    uint8_t verify[]={0x80,0,0,0,3,4,0x35,2};command(0x56,verify,8);
    while((fdc_read(0x3f4)&0xf0)!=0xd0){assert(!iomd.fiq.status);tick();}result(res,7);assert(res[3]==0&&res[5]==2);
    /* A normal raw mark: READ DELETED transfers with CM, SK skips it. */
    data_command(0x4c,0,0,0,0,0,3,1,255);assert(read_transfer(bytes,0,false)==1024);result(res,7);assert(res[2]==0x40&&res[5]==0);
    data_command(0x6c,0,0,0,0,0,3,1,255);wait_result();result(res,7);assert(res[2]&0x40);
    /* FIFO TC is attached to the queued byte, then drained before commit. */
    const uint8_t fifo[]={0,0x0d,0};command(19,fifo,3);
    unsigned before=raw_writes;data_command(0x45,0,0,0,0,0,3,1,255);write_transfer(2048,true,0x73,false);result(res,7);
    assert(raw_writes==before+2&&raw[0][0]==0x73&&raw[0][1024]==0x73);
    data_command(0x45,0,0,0,0,2,3,4,255);write_transfer(1023,true,0x62,false);result(res,7);
    assert(raw[0][2*1024+1022]==0x62&&raw[0][2*1024+1023]==0);
    /* Raw underrun zero-fill still maps a failed flush to write protect. */
    command(19,(const uint8_t[]){0,0x20,0},3);fail_store=true;
    data_command(0x45,0,0,0,0,3,3,3,255);fdc_dma_write(0x3012000,0x35);
    wait_result();result(res,7);assert(res[1]==2);fail_store=false;mount_raw(0,true);command(19,fifo,3);
    for(unsigned threshold=0;threshold<16;++threshold){
        command(19,(const uint8_t[]){0,threshold,0},3);
        data_command(0x46,0,0,0,0,0,3,0,255);assert(read_transfer(bytes,1024,false)==1024);result(res,7);assert(bytes[0]==0x73);
    }
    /* Non-DMA FIFO output uses RQM/FINT, never FIQ DMA. */
    const uint8_t nd[]={0xf1,3};command(3,nd,2);data_command(0x46,0,0,0,0,0,3,0,255);
    assert(read_transfer(bytes,0,true)==1024);result(res,7);assert(res[1]==0x80&&bytes[0]==0x73);
    data_command(0x45,0,0,0,0,4,3,4,255);write_transfer(1024,false,0x88,true);result(res,7);assert(res[1]==0x80&&raw[0][4*1024]==0x88);
    command(3,(const uint8_t[]){0xf1,2},2);
    /* READ TRACK starts at index and returns rotational order through EOT. */
    data_command(0x42,0,0,0,0,0,3,3,255);assert(read_transfer(bytes,3072,false)==3072);result(res,7);
    assert(bytes[0]==0x73&&bytes[1024]==0x73&&bytes[2048]==0x62);
    command(19,(const uint8_t[]){0,0x4d,0},3);
    data_command(0x42,0,0,1,0,0,3,2,255);assert(read_transfer(bytes,0,false)==2048);result(res,7);
    assert(bytes[0]==raw[0][10*1024]&&bytes[1024]==raw[0][11*1024]&&res[3]==1&&res[5]==1);
    seek(0,0,0);command(19,fifo,3);
    /* FORMAT consumes supplied tuples, keeps supplied fill, rejects mismatch. */
    uint8_t format[]={0,3,5,0x74,0xa6};command(0x4d,format,5);unsigned at=0;
    while((fdc_read(0x3f4)&0xf0)!=0xd0){if(iomd.fiq.status&1){unsigned lane=at%4;
        uint8_t value=lane==0?0:lane==1?0:lane==2?at/4:3;fdc_dma_write(at==19?0x302a000:0x3012000,value);++at;}else tick();}
    result(res,7);assert(at==20&&res[1]==0&&raw[0][0]==0xa6&&raw[0][5119]==0xa6);
    command(0x4d,(const uint8_t[]){0,3,5,0xff,0x33},5);result(res,7);assert(res[1]==2);
    before=raw_writes;command(0x4d,format,5);at=0;
    while((fdc_read(0x3f4)&0xf0)!=0xd0){if(iomd.fiq.status&1){unsigned lane=at%4;
        uint8_t value=lane==0?1:lane==1?0:lane==2?at/4:3;fdc_dma_write(at==19?0x302a000:0x3012000,value);++at;}else tick();}
    result(res,7);assert(res[1]==2&&raw_writes==before&&raw[0][0]==0xa6);
    format_id_sets();
    /* Relative PCN wraps, while physical cylinder remains independent. */
    command(0xcf,(const uint8_t[]){0,255},2);while(!(iomd.irqb.status&0x10))tick();sis(0x20,255);
    command(0xcf,(const uint8_t[]){0,1},2);while(!(iomd.irqb.status&0x10))tick();sis(0x20,0);
    command(0x8f,(const uint8_t[]){0,255},2);while(!(iomd.irqb.status&0x10))tick();sis(0x20,1);
    seek(0,0,0);assert(fdc_read(0x3f7)==0);
    arc_rpc_fdc_eject(0);assert(fdc_read(0x3f7)==0x80);mount_jfd();
    commits=0;data_command(0x45,0,0,0,0,0,3,0,255);write_transfer(1024,true,0x95,false);result(res,7);assert(commits==1&&decoded[0]==0x95);
    data_command(0x45,0,0,0,0,0,3,0,255);write_transfer(1023,true,0x85,false);result(res,7);assert(commits==1&&decoded[0]==0x95);
    data_command(0x45,0,0,0,0,0,3,0,255);while(!(iomd.fiq.status&1))tick();
        fdc_dma_write(0x3012000,0x33);
    arc_rpc_fdc_eject(0);assert(!iomd.fiq.status);assert(commits==1);mount_jfd();
    for(unsigned i=0;i<5;++i)sectors[i].flags=ARC_JFD_ID_CRC_BAD;
    command(0x4a,(const uint8_t[]){0},1);wait_result();result(res,7);assert(res[1]==0x20);
    for(unsigned i=0;i<5;++i)sectors[i].flags=0;
    sectors[0].density=1;
    data_command(6,0,0,0,0,0,3,0,255);assert(read_transfer(bytes,1024,false)==1024);result(res,7);assert(res[1]==0);
    sectors[0].density=2;sectors[0].flags=ARC_JFD_DATA_CRC_BAD;
    data_command(0x46,0,0,0,0,0,3,0,255);assert(read_transfer(bytes,0,false)==1024);result(res,7);assert(res[1]==0x20&&res[2]==0x20);
    sectors[0].flags=ARC_JFD_ID_CRC_BAD;sectors[1].flags=ARC_JFD_DATA_CRC_BAD;
    data_command(0x42,0,0,0,0,0,3,2,255);assert(read_transfer(bytes,0,false)==2048);result(res,7);assert(res[1]==0x24&&res[2]==0x20);
    sectors[0].flags=sectors[1].flags=0;
    fail_store=true;data_command(0x45,0,0,0,0,0,3,0,255);write_transfer(1024,true,0x11,false);result(res,7);assert(res[1]==2);fail_store=false;
    mount_jfd();jfd.tracks[0].sector_count=1;sectors[0].id=1;sectors[0].flags=ARC_JFD_ID_CRC_BAD;
    command(0x4a,(const uint8_t[]){0},1);wait_result();result(res,7);assert(res[1]==0x20&&res[5]==1&&res[6]==3);
    mount_jfd();
    /* Generation invalidation is checked before dereferencing borrowed bytes. */
    ArcRpcFloppy model;arc_rpc_floppy_init(&model,10);ArcEngineMedia m={0};m.jfd=&jfd;m.kind=ARC_MEDIA_JFD;m.writable=true;
    assert(arc_rpc_floppy_mount(&model,0,&m,NULL,0));arc_rpc_floppy_motor(&model,0,true,0);ArcRpcFloppyRequest request={0};request.operation=ARC_RPC_FLOPPY_WRITE;request.n=3;request.rate_kbps=250;request.encoding=ARC_RPC_FLOPPY_MFM;
    ArcRpcFloppySector selected;assert(arc_rpc_floppy_select(&model,&request,0,&selected)==ARC_RPC_FLOPPY_OK);
    arc_rpc_floppy_eject(&model,0);assert(arc_rpc_floppy_commit(&model,&selected,decoded,1024,true)==ARC_RPC_FLOPPY_CANCELLED);
    assert(arc_rpc_floppy_mount(&model,0,&m,NULL,0));
    assert(arc_rpc_floppy_select(&model,&request,0,&selected)==ARC_RPC_FLOPPY_OK);
    arc_rpc_floppy_cancel(&model);assert(arc_rpc_floppy_commit(&model,&selected,decoded,1024,true)==ARC_RPC_FLOPPY_CANCELLED);
    for(unsigned kind=0;kind<3;++kind)for(unsigned action=0;action<6;++action){
        unsigned d=kind==1?1:0,commits_before=commits;
        start();mount_raw(0,true);mount_raw(1,true);if(kind==2)mount_jfd();fdc_write(0x3f2,0x3c|d);
        before=raw_writes;data_command(0x45,d,0,0,0,0,d?1:3,0,255);
        while(!(iomd.fiq.status&1))tick();
        fdc_dma_write(0x3012000,0x21);tick();
        if(action==0)fdc_reset();
        else if(action==1)arc_rpc_fdc_eject(d);
        else if(action==2)mount_raw(d,true);
        else if(action==3){ArcEngineMedia invalid={0};assert(!arc_rpc_fdc_mount(d,&invalid,NULL,0));}
        else if(action==4)arc_rpc_fdc_close();
        else arc_rpc_fdc_rebase();
        assert(!(iomd.fiq.status&1));assert(raw_writes==before);
        fdc_callback();assert(raw_writes==before&&commits==commits_before);
    }
    for(unsigned d=0;d<2;++d){
        ArcEngineMedia prefix={0};prefix.kind=d?ARC_MEDIA_ADL_640K:ARC_MEDIA_ADF_800K;
        prefix.path=d?"raw1":"raw0";prefix.geometry=d?ARC_GEOMETRY_640K_L:ARC_GEOMETRY_800K_D;
        prefix.effective_extent=d?256:1024;
        assert(arc_rpc_floppy_mount(&model,d,&prefix,NULL,0));arc_rpc_floppy_motor(&model,d,true,0);
        request.operation=ARC_RPC_FLOPPY_READ;request.drive=d;request.n=d?1:3;request.r=0;
        assert(arc_rpc_floppy_select(&model,&request,0,&selected)==ARC_RPC_FLOPPY_OK);
        request.c=1;assert(arc_rpc_floppy_select(&model,&request,UINT64_C(50000000),&selected)==ARC_RPC_FLOPPY_WRONG_CYLINDER);
        assert(selected.delay_ns==UINT64_C(150000000));request.c=0;
        request.r=1;assert(arc_rpc_floppy_select(&model,&request,0,&selected)==ARC_RPC_FLOPPY_NOT_FOUND);
    }
    arc_rpc_floppy_close(&model);assert(!arc_rpc_floppy_valid(&model,1,selected.generation));arc_rpc_fdc_close();
    idle_services();qa_drive_isolation();qa_fifo();qa_controls();qa_mechanics();qa_refusals_waits();media_change_seek();pending_irq_cancel();arc_rpc_fdc_close();
    puts("rpc-fdc: command fields, drive isolation, FIFO/ND, TC, media-change mechanics, pending IRQ and generations passed");return 0;
}
