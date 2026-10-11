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

/* ArcDroid modification (2026-10-09), devices-r2c-ide: single ATA
 * master through ArcIdeBackend, bounded transactional PIO, virtual scheduler,
 * nIEN/status/reset containment; no host files, slave or ATAPI. */
#include "ide.h"
#include "iomd.h"
#include "rpcemu.h"
#include "rpc_ide.h"
#include "rpc_timing.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define ERR 1u
#define DRQ 8u
#define READY 0x50u
#define BUSY 0x80u
#define ABRT 4u
#define IDNF 0x10u
/* One millisecond per PIO readiness/completion, independent of host I/O. */
#define PIO_DELAY_NS UINT64_C(1000000)
static struct {
    ArcIdeBackend backend;
    uint8_t error, features, count, sector, low, high, head, control, command, status;
    uint8_t data[512], transaction[256*512], unsupported[32];
    unsigned position, total, transferred, heads, spt, cylinders;
    uint32_t lba;
    bool active, writing, irq, resetting, refused;
} ide;
static bool master(void) { return ide.backend.read&&ide.backend.sectors&&!(ide.head&0x10); }
static void irq_update(void)
{
    if(master()&&ide.irq&&!(ide.control&2))iomd.irqb.status|=IOMD_IRQB_IDE;
    else iomd.irqb.status&=~IOMD_IRQB_IDE;
    updateirqs();
}
static void irq_set(bool value) { ide.irq=value;irq_update(); }
void arc_rpc_ide_cancel(void)
{ arc_rpc_event_cancel(ARC_RPC_EVENT_IDE);ide.active=ide.writing=ide.resetting=false;ide.position=ide.total=ide.transferred=0;irq_set(false); }
bool arc_rpc_ide_pending(void) { return ide.active||ide.resetting; }
void resetide(void)
{
    ArcIdeBackend b=ide.backend;bool refused=ide.refused;arc_rpc_ide_cancel();memset(&ide,0,sizeof(ide));
    ide.backend=b;ide.refused=refused;ide.count=ide.sector=1;ide.head=0xa0;ide.heads=16;ide.spt=63;ide.cylinders=256;ide.status=READY;
    irq_update();
}
void arc_rpc_ide_set_backend(const ArcIdeBackend *backend)
{ arc_rpc_ide_cancel();memset(&ide,0,sizeof(ide));if(backend)ide.backend=*backend;resetide(); }
static void fail(uint8_t error)
{ arc_rpc_ide_cancel();ide.error=error;ide.status=READY|ERR;irq_set(true); }
static void delay(void) { ide.status=READY|BUSY;arc_rpc_event_after(ARC_RPC_EVENT_IDE,PIO_DELAY_NS); }
static bool address(unsigned count,uint32_t *lba)
{
    if(!master()||!count||count>256)return false;
    uint32_t first;
    if(ide.head&0x40)first=((uint32_t)(ide.head&15)<<24)|((uint32_t)ide.high<<16)|((uint32_t)ide.low<<8)|ide.sector;
    else {unsigned cylinder=ide.low|((unsigned)ide.high<<8),head=ide.head&15;
       if(!ide.sector||ide.sector>ide.spt||head>=ide.heads||cylinder>=ide.cylinders)return false;
       first=(cylinder*ide.heads+head)*ide.spt+ide.sector-1;}
    if(first>=ide.backend.sectors||count>ide.backend.sectors-first)return false;*lba=first;return true;
}
static void advance(void)
{
    uint32_t next=ide.lba+ide.transferred;
    if(ide.head&0x40){ide.sector=(uint8_t)next;ide.low=(uint8_t)(next>>8);ide.high=(uint8_t)(next>>16);ide.head=(uint8_t)((ide.head&0xf0)|(next>>24&15));}
    else{uint32_t cylinder=next/(ide.spt*ide.heads);ide.sector=(uint8_t)(next%ide.spt+1);ide.head=(uint8_t)((ide.head&0xf0)|(next/ide.spt%ide.heads));ide.low=(uint8_t)cylinder;ide.high=(uint8_t)(cylinder>>8);}
    ide.count=(uint8_t)(ide.total-ide.transferred);
}
static void word(unsigned at,uint16_t value) { ide.data[at*2]=(uint8_t)value;ide.data[at*2+1]=(uint8_t)(value>>8); }
static void identify_string(unsigned at,unsigned words,const char *text)
{ for(unsigned i=0;i<words;++i){uint8_t a=*text?(uint8_t)*text++:' ',b=*text?(uint8_t)*text++:' ';word(at+i,(uint16_t)((a<<8)|b));} }
static void identify(void)
{
    memset(ide.data,0,512);word(0,0x40);word(1,256);word(3,16);word(6,63);
    identify_string(10,10,"ARCDROID-RPC-00000001");identify_string(23,4,"1.0");identify_string(27,20,"ArcDroid RiscPC Hard Disc");
    word(49,0x200);word(50,0x4000);word(53,1);word(54,(uint16_t)ide.cylinders);word(55,(uint16_t)ide.heads);word(56,(uint16_t)ide.spt);
    uint32_t current=ide.cylinders*ide.heads*ide.spt;word(57,(uint16_t)current);word(58,(uint16_t)(current>>16));
    word(60,(uint16_t)ide.backend.sectors);word(61,(uint16_t)(ide.backend.sectors>>16));
    ide.position=0;ide.status=READY|DRQ;irq_set(true);
}
void callbackide(void)
{
    if(!ide.backend.read||!ide.backend.sectors||(ide.control&4))return;
    if(ide.resetting){ide.resetting=false;ide.error=1;ide.count=ide.sector=1;ide.low=ide.high=0;ide.status=READY;return;}
    if(!master())return;
    if(ide.active){
        if(ide.writing){
            if(ide.transferred==ide.total){
                /* D-083/D-139 latch: precisely one backend write per complete
                 * command, never one write for each transferred sector. */
                if(ide.refused||!ide.backend.writable||!ide.backend.write||!ide.backend.write(ide.backend.opaque,ide.lba,ide.total,ide.transaction)){
                    ide.refused=true;if(ide.backend.failed)ide.backend.failed(ide.backend.opaque,"write-commit-failed");fail(ABRT);return;}
                ide.active=ide.writing=false;ide.status=READY;
            }else{ide.position=0;ide.status=READY|DRQ;}
        }else{
            if(!ide.backend.read(ide.backend.opaque,ide.lba+ide.transferred,1,ide.data)){fail(IDNF);return;}
            ide.position=0;ide.status=READY|DRQ;
        }
        irq_set(true);return;
    }
    uint8_t command=ide.command;
    if((command&0xf0)==0x10){ide.low=ide.high=0;ide.status=READY;irq_set(true);return;}
    if((command&0xf0)==0x70){ide.status=READY;irq_set(true);return;}
    switch(command){
    case 0x40:case 0x41:
        for(unsigned i=0;i<ide.total;++i)if(!ide.backend.read(ide.backend.opaque,ide.lba+i,1,ide.data)){fail(IDNF);return;}
        ide.transferred=ide.total;advance();ide.status=READY;irq_set(true);return;
    case 0x91:{unsigned heads=(ide.head&15)+1,spt=ide.count;uint32_t cylinders=spt?ide.backend.sectors/(heads*spt):0;
        if(!spt||!cylinders||cylinders>65535){fail(ABRT);return;}ide.heads=heads;ide.spt=spt;ide.cylinders=cylinders;ide.status=READY;irq_set(true);return;}
    case 0xec:identify();return;
    case 0xe5:ide.count=0xff;/* fall through */
    case 0xe0:case 0xe1:case 0xe2:case 0xe3:ide.status=READY;irq_set(true);return;
    default:fail(ABRT);return;
    }
}
static uint8_t read_data(void)
{
    if(!(ide.status&DRQ)||ide.writing||ide.position>=512){fail(ABRT);return 0xff;}
    uint8_t value=ide.data[ide.position++];
    if(ide.position==512){ide.position=0;
        if(ide.active){++ide.transferred;advance();if(ide.transferred<ide.total)delay();else{ide.active=false;ide.status=READY;}}
        else ide.status=READY;
    }return value;
}
static void write_data(uint8_t value)
{
    if(!(ide.status&DRQ)||!ide.active||!ide.writing||ide.position>=512){fail(ABRT);return;}
    ide.transaction[ide.transferred*512+ide.position++]=value;
    if(ide.position==512){ide.position=0;++ide.transferred;advance();delay();}
}
uint16_t readidew(void)
{ if(!master())return 0xffff;uint16_t low=read_data();if(!(ide.status&ERR))low|=(uint16_t)read_data()<<8;else low|=0xff00;return low; }
void writeidew(uint16_t value) { if(!master())return;write_data((uint8_t)value);if(!(ide.status&ERR))write_data((uint8_t)(value>>8)); }
uint8_t readide(uint16_t addr)
{
    if(!master())return addr==0x1f7||addr==0x3f6?0:0xff;
    switch(addr){case 0x1f0:return read_data();case 0x1f1:return ide.error;case 0x1f2:return ide.count;
    case 0x1f3:return ide.sector;case 0x1f4:return ide.low;case 0x1f5:return ide.high;case 0x1f6:return ide.head;
    case 0x1f7:irq_set(false);/* fall through */case 0x3f6:return ide.status;default:return 0xff;}
}
void writeide(uint16_t addr,uint8_t value)
{
    if(!ide.backend.read||!ide.backend.sectors)return;
    if(addr==0x1f6){if((value^ide.head)&0x10)arc_rpc_ide_cancel();ide.head=value;irq_update();return;}
    if(addr==0x3f6){if(value&4){arc_rpc_ide_cancel();ide.status=BUSY;}
        else if(ide.control&4){arc_rpc_ide_cancel();ide.resetting=true;ide.status=BUSY;arc_rpc_event_after(ARC_RPC_EVENT_IDE,500000);}
        ide.control=value;irq_update();return;}
    if(!master()||(ide.control&4))return;
    switch(addr){case 0x1f0:write_data(value);return;case 0x1f1:ide.features=value;return;case 0x1f2:ide.count=value;return;
    case 0x1f3:ide.sector=value;return;case 0x1f4:ide.low=value;return;case 0x1f5:ide.high=value;return;
    case 0x1f7:{arc_rpc_ide_cancel();ide.command=value;ide.error=0;
        unsigned count=ide.count?ide.count:256;uint32_t lba;
        if(value==0x20||value==0x21||value==0x30||value==0x31||value==0x40||value==0x41||(value&0xf0)==0x70){
            if(!address((value&0xf0)==0x70?1:count,&lba)){fail(IDNF);return;}
            ide.lba=lba;ide.total=count;ide.transferred=ide.position=0;
            if(value==0x30||value==0x31){if(ide.refused||!ide.backend.writable){fail(ABRT);return;}
                ide.active=ide.writing=true;ide.status=READY|DRQ;return;}
            if(value==0x20||value==0x21)ide.active=true;delay();return;
        }
        if((value&0xf0)==0x10||value==0x91||value==0xec||(value>=0xe0&&value<=0xe3)||value==0xe5){delay();return;}
        unsigned byte=value>>3,mask=1u<<(value&7);if(!(ide.unsupported[byte]&mask)){ide.unsupported[byte]|=(uint8_t)mask;
            if(ide.backend.unsupported)ide.backend.unsupported(ide.backend.opaque,value);}
        fail(ABRT);return;
    }default:return;}
}
