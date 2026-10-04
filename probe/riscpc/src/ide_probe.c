/*
 * ARCDROID-PROBE: marked ATA-only adaptation of RPCEmu 0.9.5 src/ide.c.
 * RPCEmu - An Acorn system emulator
 * Copyright (C) 2005-2010 Sarah Walker
 * This program is free software; you can redistribute it and/or modify it
 * under the GNU General Public License version 2 or (at your option) later.
 * This program is distributed WITHOUT ANY WARRANTY; without even implied
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU GPL.
 * The upstream full license is retained at ../rpcemu-0.9.5/COPYING.
 *
 * ATA registers, CHS/LBA sequencing and PIO phases follow the upstream
 * controller. CD-ROM and ATAPI are absent. Backing is one exclusively-created
 * session image; no upstream filename, automatic growth, missing-sector zero
 * fill, or unchecked I/O survives. Blank FileCore bytes are authored by
 * ArcDroid's existing formatter, compiled read-only by reference.
 */
#include "probe.h"
#include "ide_probe.h"
#include "ide.h"
#include "iomd.h"
#include "install_disc.h"
#include "arcdroid_core.h"
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#define CREATE(p) _open(p,_O_BINARY|_O_RDWR|_O_CREAT|_O_EXCL,_S_IREAD|_S_IWRITE)
#define FDOPEN _fdopen
#define CLOSE _close
#else
#include <unistd.h>
#define CREATE(p) open(p,O_RDWR|O_CREAT|O_EXCL,0600)
#define FDOPEN fdopen
#define CLOSE close
#endif
#define READY 0x40
#define BUSY 0x80
#define DRQ 0x08
#define ERR 0x01
static struct {
 uint8_t status,error,command,control,head,drive;
 uint16_t cylinder;
 unsigned sector,count,remaining,spt,heads,pos;
 bool lba,reset;
 uint8_t buffer[512];
} ata;
static FILE *image;
static unsigned diagnostic_reads;
int idecallback,probe_ide_enabled;
unsigned long long probe_ide_reads,probe_ide_writes,probe_ide_io_failures;
static void irq(int yes) {
 if(yes&&!(ata.control&2))iomd.irqb.status|=IOMD_IRQB_IDE;else iomd.irqb.status&=~IOMD_IRQB_IDE;
 updateirqs();
}
static void abort_command(unsigned error) {ata.status=READY|ERR;ata.error=error;ata.pos=0;idecallback=0;irq(1);}
static void io_failure(void) {probe_ide_io_failures++;fatal("Scratch IDE short/failed I/O");}
void resetide(void) {
 memset(&ata,0,sizeof(ata));ata.status=READY;ata.sector=ata.count=ata.remaining=1;
 ata.spt=ARC_INSTALL_HD_SPT;ata.heads=ARC_INSTALL_HD_HEADS;idecallback=0;irq(0);
}
bool probe_ide_close(void) {
 idecallback=0;irq(0);if(!image)return true;
 int flush=fflush(image),closed=fclose(image);image=NULL;
 if(flush||closed) {probe_ide_io_failures++;return false;}return true;
}
void probe_ide_open(const char *directory,const char *baseline,const char *expected,FILE *custody) {
 if(image)fatal("Scratch IDE already open");
 if(!probe_ide_enabled)return;
 if(!directory||!directory[0])fatal("Scratch IDE requires a session directory");
 uint8_t *bytes=malloc(ARC_INSTALL_HD_BYTES);if(!bytes)fatal("Scratch IDE allocation");
 char sha[41];
 if(strcmp(baseline,"none")) {
  if(strlen(expected)!=40) {free(bytes);fatal("IDE baseline requires expected SHA-1");}
  FILE *f=fopen(baseline,"rb");if(!f){free(bytes);fatal("IDE baseline open failed");}
  size_t n=fread(bytes,1,ARC_INSTALL_HD_BYTES,f);int tail=fgetc(f),bad=ferror(f),closed=fclose(f);
  if(n!=ARC_INSTALL_HD_BYTES||tail!=EOF||bad||closed) {free(bytes);fatal("IDE baseline size/read failure");}
  arc_sha1_hex(bytes,ARC_INSTALL_HD_BYTES,sha);if(strcmp(sha,expected)) {free(bytes);fatal("IDE baseline fingerprint refused");}
 } else {
  if(!arc_install_hd_format(bytes,ARC_INSTALL_HD_BYTES)) {free(bytes);fatal("Authored IDE format failure");}
  arc_sha1_hex(bytes,ARC_INSTALL_HD_BYTES,sha);
 }
 char name[1200];if(snprintf(name,sizeof(name),"%s/scratch.hdf",directory)>=(int)sizeof(name)) {free(bytes);fatal("IDE session path bound");}
 int fd=CREATE(name);if(fd<0) {free(bytes);fatal("IDE exclusive session creation failed");}
 image=FDOPEN(fd,"w+b");if(!image) {CLOSE(fd);free(bytes);fatal("IDE session stream creation failed");}
 if(fwrite(bytes,1,ARC_INSTALL_HD_BYTES,image)!=ARC_INSTALL_HD_BYTES||fflush(image)) {free(bytes);io_failure();}
 free(bytes);diagnostic_reads=0;probe_ide_reads=probe_ide_writes=probe_ide_io_failures=0;
 if(custody&&fprintf(custody,"ide,0,%s,%u,authored-format-%u\n",sha,ARC_INSTALL_HD_BYTES,ARC_INSTALL_HD_FORMAT_VERSION)<0)fatal("IDE custody write failed");
 resetide();
}
static bool sector_offset(uint64_t *out) {
 uint64_t sector;
 if(ata.lba)sector=((uint64_t)ata.head<<24)|((uint64_t)ata.cylinder<<8)|ata.sector;
 else {
  if(!ata.sector||ata.sector>ata.spt||ata.head>=ata.heads)return false;
  sector=((uint64_t)ata.cylinder*ata.heads+ata.head)*ata.spt+ata.sector-1;
 }
 if(sector>=ARC_INSTALL_HD_SECTORS)return false;*out=sector*512;return true;
}
static void next_sector(void) {
 if(ata.lba) {
  uint32_t lba=((uint32_t)ata.head<<24)|((uint32_t)ata.cylinder<<8)|ata.sector;
  lba++;ata.head=(lba>>24)&15;ata.cylinder=lba>>8;ata.sector=lba&255;
 } else if(++ata.sector>ata.spt) {ata.sector=1;if(++ata.head>=ata.heads){ata.head=0;ata.cylinder++;}}
}
static void identify(void) {
 memset(ata.buffer,0,sizeof(ata.buffer));
 #define WORD(n,v) do {ata.buffer[(n)*2]=(uint8_t)(v);ata.buffer[(n)*2+1]=(uint8_t)((v)>>8);}while(0)
 WORD(1,ARC_INSTALL_HD_CYLINDERS);WORD(3,ARC_INSTALL_HD_HEADS);WORD(6,ARC_INSTALL_HD_SPT);
 WORD(49,0x200);WORD(50,0x4000);WORD(60,ARC_INSTALL_HD_SECTORS);WORD(61,ARC_INSTALL_HD_SECTORS>>16);
 const char model[]="ArcDroid session scratch";
 for(unsigned i=0;i<40;i++)ata.buffer[54+(i^1)]=i<sizeof(model)-1?(uint8_t)model[i]:' ';
 #undef WORD
}
void callbackide(void) {
 if(ata.reset) {resetide();return;}
 if(ata.drive||!image) {abort_command(4);return;}
 uint64_t offset;
 switch(ata.command) {
 case 0x08:ata.error=1;ata.cylinder=0;ata.sector=ata.count=ata.remaining=1;ata.status=READY;irq(1);return;
 case 0x10:case 0x70:case 0xe3:ata.status=READY;irq(1);return;
 case 0x20:
  if(!sector_offset(&offset)){abort_command(0x10);return;}
  if(fseek(image,(long)offset,SEEK_SET)||fread(ata.buffer,1,512,image)!=512)io_failure();
  if(probe_boot_log&&diagnostic_reads++<4096)fprintf(probe_boot_log,"%llu,ide_read,%llu,%u,%u,%u,%u\n",(unsigned long long)probe_now,(unsigned long long)offset,ata.cylinder,ata.head,ata.sector,ata.remaining);
  probe_ide_reads++;ata.pos=0;ata.status=READY|DRQ;irq(1);return;
 case 0x30:
  if(!sector_offset(&offset)){abort_command(0x10);return;}
  if(fseek(image,(long)offset,SEEK_SET)||fwrite(ata.buffer,1,512,image)!=512||fflush(image))io_failure();
  probe_ide_writes++;ata.remaining--;ata.pos=0;
  if(ata.remaining){next_sector();ata.status=READY|DRQ;}else ata.status=READY;
  irq(1);return;
 case 0x40:
  /* VERIFY detects both illegal extent and backing failure, no PIO data. */
  while(ata.remaining) {
   if(!sector_offset(&offset)){abort_command(0x10);return;}
   if(fseek(image,(long)offset,SEEK_SET)||fread(ata.buffer,1,512,image)!=512)io_failure();
   probe_ide_reads++;if(--ata.remaining)next_sector();
  }
  ata.status=READY;irq(1);return;
 case 0x91:
  if(!ata.count){abort_command(4);return;}
  ata.spt=ata.count;ata.heads=ata.head+1;ata.status=READY;irq(1);return;
 case 0xec:identify();ata.pos=0;ata.status=READY|DRQ;irq(1);return;
 default:abort_command(4);return;
 }
}
static void read_finished(void) {
 ata.pos=0;ata.status=READY;
 if(ata.command==0x20&&--ata.remaining) {next_sector();ata.status=BUSY;idecallback=200;}
}
uint16_t readidew(void) {
 if(ata.drive||!image||!(ata.status&DRQ)||ata.pos>510)return 0xffff;
 uint16_t v=ata.buffer[ata.pos]|((uint16_t)ata.buffer[ata.pos+1]<<8);ata.pos+=2;
 if(ata.pos==512)read_finished();return v;
}
void writeidew(uint16_t v) {
 if(ata.drive||!image||!(ata.status&DRQ)||ata.command!=0x30||ata.pos>510) {abort_command(4);return;}
 ata.buffer[ata.pos++]=v;ata.buffer[ata.pos++]=v>>8;
 if(ata.pos==512) {ata.pos=0;ata.status=BUSY;idecallback=1000;}
}
uint8_t readide(uint16_t reg) {
 if(reg==0x1f0) {
  if(ata.drive||!image||!(ata.status&DRQ)||ata.pos>=512)return 0xff;
  uint8_t v=ata.buffer[ata.pos++];if(ata.pos==512)read_finished();return v;
 }
 switch(reg) {
 case 0x1f1:return ata.error;case 0x1f2:return (uint8_t)ata.remaining;
 case 0x1f3:return ata.sector;case 0x1f4:return ata.cylinder;case 0x1f5:return ata.cylinder>>8;
 case 0x1f6:return ata.head|(ata.drive<<4)|(ata.lba?0x40:0);
 case 0x1f7:irq(0);return ata.drive||!image?0:ata.status;
 case 0x3f6:return ata.drive||!image?0:ata.status;default:return 0;
 }
}
void writeide(uint16_t reg,uint8_t v) {
 if(reg==0x1f0) {
  if(ata.drive||!image||!(ata.status&DRQ)||ata.command!=0x30||ata.pos>=512) {abort_command(4);return;}
  ata.buffer[ata.pos++]=v;if(ata.pos==512){ata.pos=0;ata.status=BUSY;idecallback=1000;}return;
 }
 switch(reg) {
 case 0x1f1:return;case 0x1f2:ata.count=ata.remaining=v;return;case 0x1f3:ata.sector=v;return;
 case 0x1f4:ata.cylinder=(ata.cylinder&0xff00)|v;return;
 case 0x1f5:ata.cylinder=(ata.cylinder&255)|((uint16_t)v<<8);return;
 case 0x1f6:ata.drive=(v>>4)&1;ata.head=v&15;ata.lba=(v&0x40)!=0;ata.pos=0;irq(0);return;
 case 0x3f6:
  if(v&4) {idecallback=0;ata.status=BUSY;ata.reset=true;irq(0);}
  else if(ata.control&4) {ata.reset=true;idecallback=500;ata.status=BUSY;}
  ata.control=v;return;
 case 0x1f7:
  if(ata.status&BUSY) {abort_command(4);return;}
  ata.command=v;ata.error=0;ata.pos=0;ata.remaining=ata.count?ata.count:256;irq(0);
  if(ata.drive||!image) {abort_command(4);return;}
  if(v==0x30) {uint64_t offset;if(!sector_offset(&offset)){abort_command(0x10);return;}ata.status=READY|DRQ;}
  else {ata.status=BUSY;idecallback=200;}
  return;
 default:return;
 }
}
/* ARCDROID-PROBE END */
