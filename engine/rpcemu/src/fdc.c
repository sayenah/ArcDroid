/* ArcDroid modification (2026-10-08): replace the incomplete empty-drive
 * controller with the D-139 FDC37C665GT command/FIFO/virtual-time model over
 * an instance-owned, generation-checked ArcDroid drive service.
 * GPL-2.0-only additions; RPCEmu notice preserved below. */
/*
  RPCEmu - An Acorn system emulator

  Copyright (C) 2005-2010 Sarah Walker

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "rpcemu.h"
#include "fdc.h"
#include "rpc_timing.h"
#include "rpc_floppy.h"
#include "rpc_observer.h"
#include <string.h>
#include <limits.h>

/* Internal equal-deadline order: reset, seek unit 0..3, index drive 0..1,
 * head unload drive 0..1, transfer. The one scheduler FDC slot is disarmed
 * before entry. A callback validates session/mount/command generations before
 * touching a sector. Seeks depend on the session, not the mounted medium;
 * index/unload events also validate their drive's mount generation. */
enum { RESET, SEEK0, SEEK1, SEEK2, SEEK3, INDEX0, INDEX1, UNLOAD0, UNLOAD1, TRANSFER, EVENTS };
enum { IDLE, PARAMETERS, EXECUTE, RESULT };
enum { SEARCH, BYTE, CRC, GAP, FORMAT_IDS, FORMAT_CRC, IMPLIED };
static ArcRpcFloppy media;
static bool media_initialized;
static uint64_t media_session_generation; /* Init/reset epoch; close advances media's epoch. */
static struct {
    uint8_t dor,rate,specify[2],config,pretrk,perp,lock,pcn[4],eot;
    uint8_t command,base,p[8],param_count,param_at,phase;
    uint8_t result[10],result_count,result_at;
    uint8_t pending,pending_st0[4],pending_pcn[4];
    uint8_t fifo[16],fifo_tc[16],fifo_at,fifo_count,stage;
    uint8_t c,h,r,n,unit,head,st1,st2;
    unsigned physical_drive,position,host_bytes,track_count,verify_count;
    uint8_t write_buffer[1024],format_ids[256*4];
    unsigned format_at;
    bool tc,tc_queued,write,verify,read_track,cm,held,resetting,service,sector_done,interrupt,fifo_irq;
    uint64_t command_generation,deadline[EVENTS];
    bool armed[EVENTS];
    ArcRpcFloppyGeneration token[EVENTS];
    int seek_delta[4];
    uint8_t seek_target[4];
    unsigned seek_drive[4],seek_at[4],seek_total[4];
    ArcRpcFloppyResult selected_result;
    ArcRpcFloppySector sector;
} fdc;
int fdccallback,motoron;
static void mux(void)
{
    uint64_t next=UINT64_MAX;bool found=false;
    for(unsigned i=0;i<EVENTS;++i)if(fdc.armed[i]&&(!found||fdc.deadline[i]<next)){next=fdc.deadline[i];found=true;}
    arc_rpc_event_cancel(ARC_RPC_EVENT_FDC);
    fdccallback=0;
    if(found){uint64_t now=arc_rpc_time_ns(),delay=next>now?next-now:0;
        arc_rpc_event_after(ARC_RPC_EVENT_FDC,delay);
        fdccallback=(int)((delay+999)/1000);if(!fdccallback)fdccallback=1;}
}
static void arm_event(unsigned event,uint64_t delay)
{
    fdc.armed[event]=true;fdc.deadline[event]=arc_rpc_time_ns()+delay;
    unsigned d=(event>=SEEK0&&event<=SEEK3)?fdc.seek_drive[event-SEEK0]:(event==INDEX0||event==UNLOAD0)?0:(event==INDEX1||event==UNLOAD1)?1:fdc.physical_drive;
    fdc.token[event]=(ArcRpcFloppyGeneration){media.session_generation,
        d<2?media.drives[d].mount_generation:0,media.command_generation};
    mux();
}
static void irq_publish(void)
{
    bool value=(fdc.dor&8) && (fdc.interrupt||fdc.fifo_irq);
    if(value)iomd.irqb.status|=IOMD_IRQB_FLOPPY;else iomd.irqb.status&=~IOMD_IRQB_FLOPPY;
    updateirqs();
}
static void irq(bool value) { fdc.interrupt=value;irq_publish(); }
static void dma(bool value)
{ if(value)iomd.fiq.status|=IOMD_FIQ_FLOPPY_DMA_REQUEST;else iomd.fiq.status&=~IOMD_FIQ_FLOPPY_DMA_REQUEST;updateirqs(); }
static unsigned byte_ns(void)
{
    static const unsigned rates[]={500,300,250,1000};
    /* 8 data bits MFM; FM occupies twice the channel time. 300 kbit/s
     * uses a carried sector position, not per-byte rounding. */
    return (8000000u*(fdc.command&0x40?1u:2u)+rates[fdc.rate]-1)/rates[fdc.rate];
}
static uint64_t byte_deadline(unsigned position)
{
    static const unsigned rates[]={500,300,250,1000};
    return ((uint64_t)position*8000000u*(fdc.command&0x40?1u:2u)+rates[fdc.rate]-1)/rates[fdc.rate];
}
static uint64_t step_time(unsigned steps)
{
    static const unsigned rates[]={500,300,250,1000};
    return ((uint64_t)steps*(16-(fdc.specify[0]>>4))*UINT64_C(500000000)+rates[fdc.rate]-1)/rates[fdc.rate];
}
static uint64_t head_load(void)
{
    static const unsigned rates[]={500,300,250,1000};
    unsigned value=fdc.specify[1]>>1;if(!value)value=128;
    return ((uint64_t)value*UINT64_C(1000000000)+rates[fdc.rate]-1)/rates[fdc.rate];
}
static void empty_command(void)
{ fdc.phase=IDLE;fdc.param_count=fdc.param_at=0;fdc.service=false;fdc.fifo_irq=false;irq_publish();dma(false); }
static void results(const uint8_t *bytes,unsigned count,bool interrupt)
{
    memcpy(fdc.result,bytes,count);fdc.result_at=0;fdc.result_count=(uint8_t)count;
    fdc.phase=RESULT;fdc.param_count=fdc.param_at=0;fdc.service=false;fdc.fifo_irq=false;irq_publish();dma(false);
    if(interrupt)irq(true);
}
static void cancel_transfer(void)
{
    arc_rpc_floppy_cancel(&media);fdc.armed[TRANSFER]=false;
    /* Mechanics outlive a data command. Retag their command epoch without
     * moving deadlines; mount/session retirement still cancels them. */
    for(unsigned i=0;i<TRANSFER;++i)if(fdc.armed[i])fdc.token[i].command=media.command_generation;
    fdc.fifo_count=fdc.fifo_at=0;fdc.position=fdc.format_at=0;fdc.tc=fdc.tc_queued=false;
    fdc.service=false;fdc.fifo_irq=false;irq_publish();dma(false);mux();
}
static void finish(uint8_t st1,uint8_t st2,bool abnormal)
{
    uint8_t result[]={ (uint8_t)((abnormal?0x40:0)|(fdc.head?4:0)|fdc.unit),st1,st2,fdc.c,fdc.h,fdc.r,fdc.n };
    cancel_transfer();results(result,7,true);
    if(fdc.physical_drive<2){
        static const unsigned rates[]={500,300,250,1000};
        unsigned hut=fdc.specify[0]&15;if(!hut)hut=16;
        arm_event(UNLOAD0+fdc.physical_drive,((uint64_t)hut*UINT64_C(8000000000)+rates[fdc.rate]-1)/rates[fdc.rate]);
    }
}
static void terminal(ArcRpcFloppyResult result)
{
    switch(result){
    case ARC_RPC_FLOPPY_OK:finish(fdc.st1,fdc.st2,fdc.st1!=0);break;
    case ARC_RPC_FLOPPY_NO_MEDIA:fdc.c=fdc.h=fdc.r=fdc.n=0;finish(5,0,true);break;
    case ARC_RPC_FLOPPY_NOT_FOUND:finish((fdc.base==10||fdc.read_track)?1:4,0,true);break;
    case ARC_RPC_FLOPPY_ID_CRC:finish(0x20,0,true);break;
    case ARC_RPC_FLOPPY_DATA_CRC:finish(0x20,0x20,true);break;
    case ARC_RPC_FLOPPY_WRITE_PROTECT:finish(2,0,true);break;
    case ARC_RPC_FLOPPY_WRONG_CYLINDER:finish(4,0x10|(fdc.sector.c==0xff?2:0),true);break;
    case ARC_RPC_FLOPPY_CANCELLED:finish(0x10,0,true);break;
    }
}
static unsigned fifo_capacity(void) { return fdc.config&0x20?1:16; }
static void service(void)
{
    bool desired=false;
    if(fdc.phase==EXECUTE && !fdc.verify){
        unsigned threshold=(fdc.config&15)+1,capacity=fifo_capacity();
        if(fdc.write || fdc.base==13){
            if(!fdc.tc_queued && !fdc.tc && fdc.fifo_count<capacity && (fdc.service||fdc.fifo_count<=threshold))desired=true;
        }else{
            unsigned trigger=capacity==1?1:(16-threshold?16-threshold:1);
            desired=fdc.fifo_count && (fdc.service || fdc.fifo_count>=trigger || fdc.sector_done);
        }
    }
    fdc.service=desired;
    fdc.fifo_irq=(fdc.specify[1]&1) && desired;irq_publish();
    if(fdc.specify[1]&1)dma(false);
    else dma(desired && (fdc.dor&8));
}
static bool push(uint8_t byte)
{
    if(fdc.fifo_count==fifo_capacity())return false;
    unsigned at=(fdc.fifo_at+fdc.fifo_count)%16;fdc.fifo[at]=byte;fdc.fifo_tc[at]=0;++fdc.fifo_count;return true;
}
static uint8_t pop(void)
{ uint8_t value=fdc.fifo[fdc.fifo_at];if(fdc.fifo_tc[fdc.fifo_at])fdc.tc=true;fdc.fifo_at=(fdc.fifo_at+1)%16;--fdc.fifo_count;return value; }
static ArcRpcFloppyRequest request(ArcRpcFloppyOperation operation)
{
    static const unsigned rates[]={500,300,250,1000};
    ArcRpcFloppyRequest r={0};r.operation=operation;r.drive=fdc.physical_drive;
    r.cylinder=r.drive<2?media.drives[r.drive].cylinder:0;r.head=fdc.head;
    r.c=fdc.c;r.h=fdc.h;r.r=fdc.r;r.n=fdc.n;r.eot=fdc.p[5];r.gpl=fdc.p[6];r.dtl=fdc.p[7];
    r.encoding=fdc.command&0x40?ARC_RPC_FLOPPY_MFM:ARC_RPC_FLOPPY_FM;r.rate_kbps=rates[fdc.rate];
    if(operation==ARC_RPC_FLOPPY_FORMAT){r.sc=fdc.p[2];r.gpl=fdc.p[3];r.fill=fdc.p[4];}
    r.mt=(fdc.command&0x80)!=0;r.sk=(fdc.command&0x20)!=0;r.deleted=fdc.base==9||fdc.base==12;
    return r;
}
static void select_sector(void)
{
    ArcRpcFloppyOperation op=fdc.base==10?ARC_RPC_FLOPPY_READ_ID:fdc.base==13?ARC_RPC_FLOPPY_FORMAT:
        fdc.verify?ARC_RPC_FLOPPY_VERIFY:fdc.read_track?ARC_RPC_FLOPPY_READ_TRACK:
        fdc.write?ARC_RPC_FLOPPY_WRITE:ARC_RPC_FLOPPY_READ;
    ArcRpcFloppyRequest r=request(op);
    uint64_t load=0;
    if(fdc.physical_drive<2 && !media.drives[fdc.physical_drive].loaded)load=head_load();
    fdc.selected_result=arc_rpc_floppy_select(&media,&r,arc_rpc_time_ns()+load,&fdc.sector);
    if(fdc.selected_result==ARC_RPC_FLOPPY_NO_MEDIA){
        fdc.stage=SEARCH;arm_event(TRANSFER,UINT64_C(4000000));return;
    }
    if(fdc.selected_result==ARC_RPC_FLOPPY_WRITE_PROTECT){terminal(fdc.selected_result);return;}
    if(fdc.physical_drive<2)fdc.armed[UNLOAD0+fdc.physical_drive]=false;
    fdc.stage=SEARCH;
    uint64_t delay=load+fdc.sector.delay_ns;
    if(fdc.selected_result==ARC_RPC_FLOPPY_NOT_FOUND){
        uint64_t phase=arc_rpc_floppy_phase(&media,fdc.physical_drive,arc_rpc_time_ns()+load);
        delay=load+2*ARC_RPC_FLOPPY_REV_NS-phase;
    }
    if(fdc.physical_drive<2 && !media.drives[fdc.physical_drive].loaded){
        media.drives[fdc.physical_drive].loaded=true;
    }
    if(fdc.selected_result==ARC_RPC_FLOPPY_OK && (fdc.write || fdc.base==13))service();
    arm_event(TRANSFER,delay);
}
static void begin_media_operation(void)
{
    if(fdc.read_track && fdc.physical_drive<2 && media.drives[fdc.physical_drive].present){
        /* READ TRACK always starts at index, including after an implied seek.
         * Load first so head settling does not skip the first rotational ID. */
        uint64_t load=media.drives[fdc.physical_drive].loaded?0:head_load();
        media.drives[fdc.physical_drive].loaded=true;fdc.armed[UNLOAD0+fdc.physical_drive]=false;
        fdc.stage=GAP;
        arm_event(TRANSFER,load+ARC_RPC_FLOPPY_REV_NS-arc_rpc_floppy_phase(&media,fdc.physical_drive,arc_rpc_time_ns()+load));
    }else select_sector();
}
/* Table 27 (p.65): advance from the sector actually transferred, never rebuild
 * CHRN from the original parameters. TC does not suppress this result update. */
static bool advance_chrn(void)
{
    if(fdc.r!=fdc.p[5]){++fdc.r;return false;}
    fdc.r=1;
    if(fdc.command&0x80){
        fdc.h^=1;fdc.head^=1;
        if(fdc.physical_drive<2)media.drives[fdc.physical_drive].head=fdc.head;
        if(fdc.head==1)return false;
    }
    ++fdc.c;return true;
}
static void sector_complete(void)
{
    if(fdc.write){
        unsigned consumed=fdc.position;
        ArcRpcFloppyResult result=arc_rpc_floppy_commit(&media,&fdc.sector,fdc.write_buffer,consumed,fdc.tc);
        ArcRpcFloppyEvent complete=arc_rpc_floppy_event(&media,&fdc.sector,
            ARC_RPC_FLOPPY_EVENT_TERMINAL,fdc.position,result);
        if(complete.result!=ARC_RPC_FLOPPY_OK){terminal(complete.result);return;}
    }
    if(fdc.sector.jfd && (fdc.sector.view.sector.flags&ARC_JFD_DATA_CRC_BAD)){
        if(fdc.read_track){fdc.st1|=0x20;fdc.st2|=0x20;}
        else {terminal(ARC_RPC_FLOPPY_DATA_CRC);return;}}
    if(fdc.cm && !(fdc.command&0x20)){finish(fdc.st1,fdc.st2|0x40,true);return;}
    if(fdc.read_track){
        ++fdc.track_count;
        if(fdc.tc || fdc.track_count>=fdc.p[5]){finish(fdc.st1,fdc.st2,fdc.st1!=0);return;}
    }else{
        bool eoc=advance_chrn();
        bool count_end=fdc.verify && (fdc.p[0]&0x80) && !--fdc.verify_count;
        if(fdc.tc||count_end || eoc){
            bool en=eoc && !fdc.tc && (!fdc.verify || ((fdc.p[0]&0x80) && !count_end));
            finish(fdc.st1|(en?0x80:0),fdc.st2,en||fdc.st1!=0);return;
        }
    }
    fdc.stage=GAP;arm_event(TRANSFER,byte_deadline(fdc.p[6]+2));
}
static void transfer_event(void)
{
    if(fdc.phase!=EXECUTE || fdc.token[TRANSFER].session!=media.session_generation ||
       fdc.token[TRANSFER].command!=media.command_generation ||
       (fdc.physical_drive<2 && fdc.token[TRANSFER].mount!=media.drives[fdc.physical_drive].mount_generation))return;
    switch(fdc.stage){
    case IMPLIED:{
        unsigned unit=fdc.unit;
        if(fdc.seek_at[unit]<fdc.seek_total[unit]){
            int direction=fdc.seek_delta[unit]<0?-1:1;
            arc_rpc_floppy_step(&media,fdc.physical_drive,direction);fdc.pcn[unit]=(uint8_t)(fdc.pcn[unit]+direction);
            ++fdc.seek_at[unit];
            if(fdc.seek_at[unit]<fdc.seek_total[unit]){
                arm_event(TRANSFER,step_time(fdc.seek_at[unit]+1)-step_time(fdc.seek_at[unit]));return;}
        }
        fdc.pcn[unit]=fdc.c;begin_media_operation();return;}
    case GAP:select_sector();return;
    case SEARCH:{
        ArcRpcFloppyEvent id=arc_rpc_floppy_event(&media,&fdc.sector,
            ARC_RPC_FLOPPY_EVENT_ID,0,fdc.selected_result);
        if(fdc.base==10){fdc.c=fdc.sector.c;fdc.h=fdc.sector.h;fdc.r=fdc.sector.r;fdc.n=fdc.sector.n;}
        if(fdc.read_track && id.result==ARC_RPC_FLOPPY_ID_CRC){fdc.st1|=0x20;id.result=ARC_RPC_FLOPPY_OK;}
        if(fdc.selected_result==ARC_RPC_FLOPPY_NO_MEDIA)id.result=fdc.selected_result;
        if(id.result!=ARC_RPC_FLOPPY_OK){
            if(fdc.selected_result==ARC_RPC_FLOPPY_NOT_FOUND)arc_rpc_floppy_missing(&media,&fdc.sector.request);
            terminal(id.result);return;}
        if(fdc.base==10){finish(0,0,false);return;}
        if(fdc.base==13){fdc.stage=FORMAT_IDS;fdc.position=0;fdc.format_at=0;service();arm_event(TRANSFER,byte_deadline(162));return;}
        if(fdc.read_track){
            if(fdc.sector.c!=fdc.p[1] || fdc.sector.h!=fdc.p[2] || fdc.sector.r!=fdc.p[3] || fdc.sector.n!=fdc.p[4])fdc.st1|=4;
            fdc.c=fdc.sector.c;fdc.h=fdc.sector.h;fdc.r=fdc.sector.r;fdc.n=fdc.sector.n;}
        fdc.cm=fdc.sector.deleted!=(fdc.base==12);
        if(fdc.cm && (fdc.command&0x20) && !fdc.write && !fdc.read_track){fdc.st2|=0x40;sector_complete();return;}
        fdc.position=0;fdc.sector_done=false;memset(fdc.write_buffer,0,sizeof(fdc.write_buffer));
        fdc.host_bytes=fdc.n?fdc.sector.bytes:(fdc.p[7]&&fdc.p[7]<128?fdc.p[7]:128);
        fdc.stage=BYTE;service();arm_event(TRANSFER,byte_ns());return;}
    case FORMAT_IDS:
        if(fdc.fifo_count){fdc.format_ids[fdc.format_at++]=pop();service();}
        else if(fdc.tc){finish(0x10,0,true);return;}
        else {finish(0x10,0,true);return;}
        if(fdc.format_at==fdc.p[2]*4u){
            fdc.stage=FORMAT_CRC;
            arm_event(TRANSFER,byte_deadline((128u<<fdc.n)+fdc.p[3]+42));return;}
        arm_event(TRANSFER,byte_deadline((fdc.format_at%4)?1:(128u<<fdc.n)+fdc.p[3]+58));return;
    case FORMAT_CRC:{
        ArcRpcFloppyResult result=arc_rpc_floppy_format(&media,&fdc.sector,fdc.format_ids,fdc.p[2],fdc.p[4]);
        if(result==ARC_RPC_FLOPPY_OK){unsigned at=fdc.format_at-4;
            fdc.c=fdc.format_ids[at];fdc.h=fdc.format_ids[at+1];fdc.r=fdc.format_ids[at+2];fdc.n=fdc.format_ids[at+3];}
        terminal(result);return;}
    case BYTE:
        if(fdc.write){
            ArcRpcFloppyEvent available=arc_rpc_floppy_event(&media,&fdc.sector,
                ARC_RPC_FLOPPY_EVENT_WRITE_BYTE_AVAILABLE,fdc.position,ARC_RPC_FLOPPY_OK);
            if(available.result!=ARC_RPC_FLOPPY_OK){terminal(available.result);return;}
            if(fdc.fifo_count && fdc.position<fdc.host_bytes)fdc.write_buffer[fdc.position++]=pop();
            else if(fdc.tc || fdc.position>=fdc.host_bytes){
                /* Drain all queued FIFO bytes before deciding completeness.
                 * Raw short transfer is padded by commit; JFD is discarded. */
                if(fdc.tc && !fdc.fifo_count && fdc.position<fdc.sector.bytes){fdc.stage=CRC;arm_event(TRANSFER,byte_deadline(fdc.sector.bytes-fdc.position+2));service();return;}
                ++fdc.position;
            }else { /* Raw under-run has hardware zero-fill, JFD never commits. */
                if(!fdc.sector.jfd){
                    ArcRpcFloppyResult result=arc_rpc_floppy_commit(&media,&fdc.sector,fdc.write_buffer,fdc.position,true);
                    if(result!=ARC_RPC_FLOPPY_OK){terminal(result);return;}
                }
                finish(0x10,0,true);return;
            }
        }else{
            ArcRpcFloppyEvent byte=arc_rpc_floppy_event(&media,&fdc.sector,
                ARC_RPC_FLOPPY_EVENT_READ_BYTE,fdc.position,ARC_RPC_FLOPPY_OK);
            if(byte.result!=ARC_RPC_FLOPPY_OK){terminal(byte.result);return;}
            if(!fdc.verify && !fdc.tc && fdc.position<fdc.host_bytes && !push(byte.byte)){finish(0x10,0,true);return;}
            ++fdc.position;
        }
        fdc.sector_done=fdc.position==fdc.sector.bytes;service();
        if(fdc.sector_done){fdc.stage=CRC;arm_event(TRANSFER,byte_deadline(2));}
        else arm_event(TRANSFER,byte_deadline(fdc.position+1)-byte_deadline(fdc.position));
        return;
    case CRC:
        if(!fdc.write && !fdc.verify && fdc.fifo_count && !fdc.tc){
            /* Sector CRC is complete, but result/advance waits for FIFO drain. */
            fdc.sector_done=true;service();return;}
        sector_complete();return;
    }
}
static bool valid_opcode(uint8_t op,uint8_t *base,unsigned *parameters)
{
    unsigned b=op&31;*base=(uint8_t)b;
    switch(b){
    case 2:if(op&0xa0)return false;*parameters=8;return true;
    case 5:case 9:if(op&0x20)return false;*parameters=8;return true;
    case 6:case 12:case 22:*parameters=8;return true;
    case 10:if(op&0xa0)return false;*parameters=1;return true;
    case 13:if(op&0xa0)return false;*parameters=5;return true;
    case 15:if(op!=15&&op!=0x8f&&op!=0xcf)return false;*parameters=2;return true;
    case 20:if(op!=20&&op!=0x94)return false;*parameters=0;return true;
    case 3:case 4:case 7:case 8:case 14:case 16:case 18:case 19:
        if(op!=b)return false;
        *parameters=b==3?2:b==19?3:(b==4||b==7||b==18)?1:0;return true;
    default:return false; /* SCAN/reserved remain invalid. */
    }
}
static void execute(void)
{
    uint8_t result[10];
    switch(fdc.base){
    case 3:fdc.specify[0]=fdc.p[0];fdc.specify[1]=fdc.p[1];empty_command();return;
    case 4:{
        unsigned drive=fdc.dor&3;
        ArcRpcFloppySignals signals=arc_rpc_floppy_signals(&media,drive,arc_rpc_time_ns());
        result[0]=(fdc.p[0]&7)|0x28|(signals.track_zero?0x10:0)|(signals.write_protected?0x40:0);
        results(result,1,false);return;}
    case 8:
        irq(false);
        if(!fdc.pending){result[0]=0x80;results(result,1,false);return;}
        for(unsigned i=0;i<4;++i)if(fdc.pending&(1u<<i)){
            result[0]=fdc.pending_st0[i];result[1]=fdc.pending_pcn[i];fdc.pending&=~(1u<<i);results(result,2,false);return;}
        return;
    case 14:
        memcpy(result,fdc.pcn,4);result[4]=fdc.specify[0];result[5]=fdc.specify[1];result[6]=fdc.eot;
        result[7]=(fdc.lock?0x80:0)|fdc.perp;result[8]=fdc.config;result[9]=fdc.pretrk;results(result,10,false);return;
    case 16:result[0]=0x90;results(result,1,false);return;
    case 18:fdc.perp=(fdc.perp&0x3c)|(fdc.p[0]&3);if(fdc.p[0]&0x80)fdc.perp=(fdc.perp&3)|(fdc.p[0]&0x3c);empty_command();return;
    case 19:fdc.config=fdc.p[1]&0x7f;fdc.pretrk=fdc.p[2];empty_command();return;
    case 20:fdc.lock=(fdc.command>>7)&1;result[0]=fdc.lock<<4;results(result,1,false);return;
    case 7:case 15:{
        unsigned unit=fdc.p[0]&3,drive=fdc.dor&3;int delta;
        /* D-144: the command definition governs the 79-pulse limit. */
        if(fdc.base==7)delta=drive<2?-(int)(media.drives[drive].cylinder>79?79:media.drives[drive].cylinder):-79;
        else if(fdc.command&0x80)delta=(fdc.command&0x40)?fdc.p[1]:-(int)fdc.p[1];
        else delta=(int)fdc.p[1]-fdc.pcn[unit];
        fdc.seek_delta[unit]=delta;fdc.seek_drive[unit]=drive;
        fdc.seek_target[unit]=fdc.base==7?0:(fdc.command&0x80)?(uint8_t)(fdc.pcn[unit]+delta):fdc.p[1];
        fdc.pending_st0[unit]=0x20|unit;
        if((fdc.command&0x80)&&drive<2&&delta<0&&(unsigned)-delta>media.drives[drive].cylinder)fdc.pending_st0[unit]|=0x10;
        if(fdc.base==7&&(drive>=2||media.drives[drive].cylinder>79))fdc.pending_st0[unit]|=0x50;
        fdc.physical_drive=drive;fdc.seek_at[unit]=0;fdc.seek_total[unit]=(unsigned)(delta<0?-delta:delta);
        arm_event(SEEK0+unit,step_time(fdc.seek_total[unit]?1:0));
        empty_command();return;}
    default:break;
    }
    cancel_transfer();fdc.phase=EXECUTE;fdc.unit=fdc.p[0]&3;fdc.physical_drive=fdc.dor&3;
    fdc.head=(fdc.p[0]>>2)&1; /* HDS is physical; requested H is an ID compare. */
    fdc.c=fdc.p[1];fdc.h=fdc.p[2];fdc.r=fdc.p[3];fdc.n=fdc.p[4];fdc.st1=fdc.st2=0;
    fdc.tc=false;fdc.cm=false;fdc.write=fdc.base==5||fdc.base==9;fdc.verify=fdc.base==22;fdc.read_track=fdc.base==2;
    fdc.track_count=0;fdc.verify_count=fdc.p[7]?fdc.p[7]:256;fdc.eot=fdc.base==13?fdc.p[2]:fdc.p[5];
    if(fdc.base==10){fdc.c=fdc.h=fdc.r=fdc.n=0;}
    if(fdc.base==13){fdc.n=fdc.p[1];fdc.c=fdc.physical_drive<2?(uint8_t)media.drives[fdc.physical_drive].cylinder:0;fdc.h=fdc.head;fdc.r=0;}
    if(fdc.n>7 || (fdc.base==13&&!fdc.p[2])){terminal(ARC_RPC_FLOPPY_NOT_FOUND);return;}
    if((fdc.config&0x40) && fdc.base!=10 && fdc.base!=13 && fdc.c!=fdc.pcn[fdc.unit]){
        fdc.seek_delta[fdc.unit]=(int)fdc.c-fdc.pcn[fdc.unit];fdc.stage=IMPLIED;fdc.seek_at[fdc.unit]=0;
        fdc.seek_total[fdc.unit]=(unsigned)(fdc.seek_delta[fdc.unit]<0?-fdc.seek_delta[fdc.unit]:fdc.seek_delta[fdc.unit]);
        arm_event(TRANSFER,step_time(1));return;
    }
    begin_media_operation();
}
void fdc_reset(void)
{
    if(!media_initialized){arc_rpc_floppy_init(&media,++media_session_generation);media_initialized=true;}
    else media.session_generation=++media_session_generation;
    cancel_transfer();memset(&fdc,0,sizeof(fdc));fdc.config=0x20;fdc.rate=2;
    /* Hardware reset moves no carriage; a fresh session initializes mechanics. */
    for(unsigned i=0;i<2;++i){arc_rpc_floppy_motor(&media,i,false,arc_rpc_time_ns());media.drives[i].loaded=false;}
    motoron=0;fdccallback=0;arc_rpc_event_cancel(ARC_RPC_EVENT_FDC);irq(false);dma(false);
}
void fdc_init(void) { fdc_reset(); }
static void software_reset(bool held)
{
    uint8_t dor=fdc.dor,rate=fdc.rate,lock=fdc.lock,config=fdc.config,pretrk=fdc.pretrk,perp=fdc.perp;
    cancel_transfer();memset(fdc.armed,0,sizeof(fdc.armed));fdc_reset();fdc.dor=dor;fdc.rate=rate;fdc.lock=lock;
    fdc.perp=perp&0x3c;fdc.config=lock?(config&0x2f):0x20;fdc.pretrk=lock?pretrk:0;
    motoron=dor&0xf0;for(unsigned i=0;i<2;++i)arc_rpc_floppy_motor(&media,i,(dor&(0x10u<<i))!=0,arc_rpc_time_ns());
    fdc.held=held;fdc.resetting=!held;if(!held)arm_event(RESET,UINT64_C(500000));
}
static void motor_events(void)
{
    for(unsigned i=0;i<2;++i){
        bool on=(fdc.dor&(0x10u<<i))!=0;arc_rpc_floppy_motor(&media,i,on,arc_rpc_time_ns());
        fdc.armed[INDEX0+i]=false;
        if(on&&media.drives[i].present)arm_event(INDEX0+i,ARC_RPC_FLOPPY_REV_NS-arc_rpc_floppy_phase(&media,i,arc_rpc_time_ns()));
    }mux();
}
void fdc_write(uint32_t addr,uint32_t value)
{
    uint8_t val=(uint8_t)value;
    switch(addr){
    case 0x3f2:{uint8_t old=fdc.dor;fdc.dor=val;
        if(!(val&4))software_reset(true);
        else if(!(old&4))software_reset(false);
        motoron=val&0xf0;motor_events();service();return;}
    case 0x3f4:fdc.rate=val&3;if(val&0x80)software_reset(!(fdc.dor&4));motor_events();return;
    case 0x3f7:fdc.rate=val&3;return;
    case 0x3f5:
        if(fdc.held||fdc.resetting)return;
        if(fdc.phase==EXECUTE){
            if((fdc.write||fdc.base==13) && fdc.service && !fdc.tc){if(!push(val)){finish(0x10,0,true);return;}service();}return;}
        if(fdc.phase==RESULT)return;
        if(fdc.phase==PARAMETERS){fdc.p[fdc.param_at++]=val;if(fdc.param_at==fdc.param_count)execute();return;}
        fdc.command=val;unsigned count;
        if(!valid_opcode(val,&fdc.base,&count)){uint8_t invalid=0x80;results(&invalid,1,false);return;}
        memset(fdc.p,0,sizeof(fdc.p));fdc.param_at=0;fdc.param_count=(uint8_t)count;
        if(count)fdc.phase=PARAMETERS;else execute();return;
    default:return;
    }
}
uint8_t fdc_read(uint32_t addr)
{
    switch(addr){
    case 0x3f2:return fdc.dor;
    case 0x3f4:{
        if(fdc.held||fdc.resetting)return 0;
        uint8_t busy=0;for(unsigned i=0;i<4;++i)if(fdc.armed[SEEK0+i])busy|=1u<<i;
        if(fdc.phase==EXECUTE && fdc.stage==IMPLIED)busy|=1u<<fdc.unit;
        if(fdc.phase==IDLE)return 0x80|busy;
        if(fdc.phase==PARAMETERS)return 0x90|busy;
        if(fdc.phase==RESULT)return 0xd0|busy;
        return 0x10|busy|((fdc.specify[1]&1)?0x20:0)|
            ((!fdc.write&&fdc.base!=13&&!fdc.verify)?0x40:0)|((fdc.service&&(fdc.specify[1]&1))?0x80:0);}
    case 0x3f5:
        if(fdc.phase==RESULT){irq(false);uint8_t val=fdc.result[fdc.result_at++];
            if(fdc.result_at==fdc.result_count)empty_command();
            return val;}
        if(fdc.phase==EXECUTE&&!fdc.write&&!fdc.verify&&fdc.fifo_count){
            uint8_t val=pop();service();if(fdc.stage==CRC&&!fdc.fifo_count&&!fdc.armed[TRANSFER])arm_event(TRANSFER,0);return val;}
        return 0;
    case 0x3f7:return arc_rpc_floppy_signals(&media,fdc.dor&3,arc_rpc_time_ns()).disc_changed?0x80:0;
    default:return 0;
    }
}
void fdc_callback(void)
{
    uint64_t now=arc_rpc_time_ns();
    for(unsigned i=0;i<EVENTS;++i){
        if(!fdc.armed[i]||fdc.deadline[i]>now)continue;
        fdc.armed[i]=false;
        if(fdc.token[i].session!=media.session_generation || fdc.token[i].command!=media.command_generation)continue;
        if(i==RESET){fdc.resetting=false;fdc.pending=(fdc.config&0x10)?0:15;
            for(unsigned d=0;d<4;++d){fdc.pending_st0[d]=0xc0|d;fdc.pending_pcn[d]=fdc.pcn[d]=0;}
            if(fdc.pending)irq(true);}
        else if(i>=SEEK0&&i<=SEEK3){unsigned unit=i-SEEK0,drive=fdc.seek_drive[unit];
            if(fdc.seek_at[unit]<fdc.seek_total[unit]){
                int direction=fdc.seek_delta[unit]<0?-1:1;
                arc_rpc_floppy_step(&media,drive,direction);fdc.pcn[unit]=(uint8_t)(fdc.pcn[unit]+direction);
                ++fdc.seek_at[unit];
                if(fdc.seek_at[unit]<fdc.seek_total[unit]){
                    arm_event(i,step_time(fdc.seek_at[unit]+1)-step_time(fdc.seek_at[unit]));continue;}
            }
            fdc.pcn[unit]=fdc.seek_target[unit];fdc.pending_pcn[unit]=fdc.pcn[unit];fdc.pending|=1u<<unit;irq(true);}
        else if(i==INDEX0||i==INDEX1){unsigned d=i-INDEX0;
            ArcRpcFloppyEvent index={fdc.token[i],ARC_RPC_FLOPPY_EVENT_INDEX,ARC_RPC_FLOPPY_OK,d,0,0,0,0,0};
            if(!arc_rpc_floppy_valid(&media,index.drive,index.generation))continue;
            if(media.drives[d].motor&&media.drives[d].present){if((fdc.dor&3)==d){iomd.irqa.status|=IOMD_IRQA_FLOPPY_INDEX;updateirqs();}
                arm_event(i,ARC_RPC_FLOPPY_REV_NS);}}
        else if(i==UNLOAD0||i==UNLOAD1){unsigned d=i-UNLOAD0;
            if(fdc.token[i].session==media.session_generation && fdc.token[i].mount==media.drives[d].mount_generation && fdc.token[i].command==media.command_generation)media.drives[d].loaded=false;}
        else transfer_event();
    }mux();
}
uint8_t fdc_dma_read(uint32_t addr)
{
    if(fdc.phase!=EXECUTE || !(fdc.dor&8) || fdc.write || fdc.verify || (fdc.specify[1]&1) || !fdc.fifo_count)return 0;
    uint8_t val=pop();if(addr==0x302a000){fdc.tc=true;fdc.fifo_count=0;}
    service();if(fdc.stage==CRC&&!fdc.fifo_count&&!fdc.armed[TRANSFER])arm_event(TRANSFER,0);return val;
}
void fdc_dma_write(uint32_t addr,uint8_t val)
{
    if(fdc.phase!=EXECUTE || !(fdc.dor&8) || (!fdc.write&&fdc.base!=13) || (fdc.specify[1]&1) || fdc.tc || !fdc.service)return;
    if(!push(val)){finish(0x10,0,true);return;}
    if(addr==0x302a000){fdc.tc_queued=true;fdc.fifo_tc[(fdc.fifo_at+fdc.fifo_count-1)%16]=1;}service();
}
static void cancel_drive(unsigned drive)
{
    if(fdc.phase==EXECUTE && fdc.physical_drive==drive){cancel_transfer();empty_command();irq(fdc.pending!=0);}
    if(drive<2){fdc.armed[INDEX0+drive]=false;fdc.armed[UNLOAD0+drive]=false;}
    mux();
}
bool arc_rpc_fdc_mount(unsigned drive,const ArcEngineMedia *m,char *error,size_t size)
{
    if(!media_initialized){arc_rpc_floppy_init(&media,++media_session_generation);media_initialized=true;}
    cancel_drive(drive);
    bool ok=arc_rpc_floppy_mount(&media,drive,m,error,size);motor_events();return ok;
}
void arc_rpc_fdc_eject(unsigned drive)
{
    cancel_drive(drive);
    arc_rpc_floppy_eject(&media,drive);motor_events();
}
void arc_rpc_fdc_close(void)
{
    if(!media_initialized)return;
    cancel_transfer();arc_rpc_floppy_close(&media);memset(&fdc,0,sizeof(fdc));
    motoron=fdccallback=0;arc_rpc_event_cancel(ARC_RPC_EVENT_FDC);dma(false);irq(false);
    media_initialized=false;
}
void arc_rpc_fdc_rebase(void)
{
    if(!media_initialized)return;
    /* fdc_reset freezes rotation using the old virtual time. Retain physical
     * head position and mounts, then invalidate callbacks from that session. */
    fdc_reset();
}
/* Legacy RPCEmu entry points remain private; no untyped media callback uses
 * them. All production drive traffic goes through the typed sector/event API. */
void fdc_data(uint8_t dat) { if(fdc.phase==EXECUTE&&!fdc.write){if(!push(dat))finish(0x10,0,true);else service();} }
void fdc_finishread(void) { if(fdc.phase==EXECUTE){fdc.stage=CRC;arm_event(TRANSFER,byte_deadline(2));} }
void fdc_notfound(void) { terminal(ARC_RPC_FLOPPY_NOT_FOUND); }
void fdc_datacrcerror(void) { terminal(ARC_RPC_FLOPPY_DATA_CRC); }
void fdc_headercrcerror(void) { terminal(ARC_RPC_FLOPPY_ID_CRC); }
void fdc_writeprotect(void) { terminal(ARC_RPC_FLOPPY_WRITE_PROTECT); }
int fdc_getdata(int last) { (void)last;return fdc.fifo_count?pop():-1; }
void fdc_sectorid(uint8_t c,uint8_t h,uint8_t r,uint8_t n) { fdc.c=c;fdc.h=h;fdc.r=r;fdc.n=n;finish(0,0,false); }
void fdc_indexpulse(void) { if(arc_rpc_floppy_signals(&media,fdc.dor&3,arc_rpc_time_ns()).index){iomd.irqa.status|=IOMD_IRQA_FLOPPY_INDEX;updateirqs();} }

/* ArcDroid modification (2026-10-13, observation-r3c1): controller state
 * admission service, independent of IRQ masking/acknowledgement. */
bool arc_rpc_fdc_idle(unsigned drive)
{
    if(drive>=2 || !media_initialized || !(fdc.dor&4) || fdc.held || fdc.resetting)return false;
    /* The shared command/FIFO/result engine is busy for both drives. A
     * background seek is drive-specific only after that engine returns IDLE. */
    if(fdc.phase!=IDLE || fdc.fifo_count || fdc.service || fdc.fifo_irq ||
        (iomd.fiq.status&IOMD_FIQ_FLOPPY_DMA_REQUEST))return false;
    for(unsigned unit=0;unit<4;++unit){
        if(fdc.armed[SEEK0+unit] && fdc.seek_drive[unit]==drive)return false;
        if((fdc.pending&(1u<<unit)) &&
            ((fdc.pending_st0[unit]&0xc0)==0xc0 || fdc.seek_drive[unit]==drive))return false;
    }
    return true;
}
uint64_t arc_rpc_fdc_media_generation(unsigned drive)
{ return media_initialized && drive<2?media.drives[drive].mount_generation:0; }
