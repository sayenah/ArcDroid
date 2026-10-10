/* Independent ATA register, transactional latch and virtual IRQ assertions.
 * Authored GPL-2.0-only. */
#include "rpc_namespace.h"
#include "rpcemu.h"
#include "ide.h"
#include "rpc_ide.h"
#include "rpc_timing.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct iomd iomd;
static ArcRpcScheduler scheduler;
static unsigned writes,reads,last_count;static uint32_t last_lba;
static bool fail_read,fail_write;static uint8_t committed[256*512];
void updateirqs(void) { }
static bool read_backend(void *p,uint32_t lba,uint32_t count,uint8_t *out)
{ (void)p;++reads;if(fail_read)return false;for(unsigned i=0;i<count*512;++i)out[i]=(uint8_t)(lba+i);return true; }
static bool write_backend(void *p,uint32_t lba,uint32_t count,const uint8_t *in)
{ (void)p;++writes;last_count=count;last_lba=lba;memcpy(committed,in,count*512);return !fail_write; }
static void start(bool attached)
{
    arc_rpc_ide_set_backend(NULL);memset(&iomd,0,sizeof(iomd));assert(arc_rpc_scheduler_init(&scheduler,100));arc_rpc_timing_bind(&scheduler);
    ArcIdeBackend b={.read=read_backend,.write=write_backend,.sectors=258048,.writable=true};arc_rpc_ide_set_backend(attached?&b:NULL);
    writes=reads=last_count=0;fail_read=fail_write=false;
}
static void tick(void)
{ ArcRpcEvent e;assert(scheduler.next_event==ARC_RPC_EVENT_IDE);scheduler.now_ns=scheduler.next_ns;
  assert(arc_rpc_take_event(&scheduler,scheduler.now_ns,&e)&&e==ARC_RPC_EVENT_IDE);callbackide(); }
static void lba(uint32_t at,unsigned count)
{ writeide(0x1f6,(uint8_t)(0xe0|(at>>24&15)));writeide(0x1f5,(uint8_t)(at>>16));writeide(0x1f4,(uint8_t)(at>>8));writeide(0x1f3,(uint8_t)at);writeide(0x1f2,(uint8_t)count); }
static void write_command(unsigned count,bool words)
{
    lba(511,count);writeide(0x1f7,0x30);assert(readide(0x3f6)&8);
    for(unsigned sector=0;sector<count;++sector){for(unsigned i=0;i<512;i+=words?2:1){
        if(words)writeidew((uint16_t)((sector+i)&255)|((uint16_t)((sector+i+1)&255)<<8));else writeide(0x1f0,(uint8_t)(sector+i));}
        assert(writes==0);assert(readide(0x3f6)&0x80);tick();
        if(sector+1<count)assert(readide(0x3f6)&8);
    }
    assert(writes==1&&last_count==count&&last_lba==511&&!(readide(0x3f6)&9));
    for(unsigned sector=0;sector<count;++sector)for(unsigned i=0;i<512;++i)assert(committed[sector*512+i]==(uint8_t)(sector+i));
}
int main(void)
{
    start(false);for(unsigned i=0;i<256;++i){writeide(0x1f7,(uint8_t)i);assert(!readide(0x1f7)&&!readide(0x3f6));assert(readidew()==0xffff);}
    assert(scheduler.next_event==ARC_RPC_EVENT_COUNT&&!(iomd.irqb.status&2));
    start(true);write_command(256,true);assert(iomd.irqb.status&2);assert(readide(0x3f6)==0x50);assert(iomd.irqb.status&2);assert(readide(0x1f7)==0x50);assert(!(iomd.irqb.status&2));
    start(true);write_command(3,false);
    start(true);writeide(0x3f6,2);writeide(0x1f7,0xec);assert(scheduler.next_ns==1000000);tick();assert(!(iomd.irqb.status&2));writeide(0x3f6,0);assert(iomd.irqb.status&2);
    uint16_t words[256];for(unsigned i=0;i<256;++i)words[i]=readidew();assert(words[1]==256&&words[3]==16&&words[6]==63&&words[49]==0x200&&words[53]==1);
    assert(words[54]==256&&words[55]==16&&words[56]==63&&((uint32_t)words[60]|(uint32_t)words[61]<<16)==258048);
    assert(words[63]==0&&words[64]==0&&words[88]==0&&words[27]==0x4172);
    for(unsigned command=0;command<256;++command){start(true);lba(0,1);if(command==0x91){writeide(0x1f6,0xaf);writeide(0x1f2,63);}writeide(0x1f7,(uint8_t)command);
        bool allowed=(command&0xf0)==0x10||(command&0xf0)==0x70||command==0x20||command==0x21||command==0x30||command==0x31||command==0x40||command==0x41||command==0x91||command==0xec||(command>=0xe0&&command<=0xe3)||command==0xe5;
        if(allowed){if(command==0x30||command==0x31)assert(readide(0x3f6)&8);else{tick();assert(!(readide(0x3f6)&1));}}
        else assert((readide(0x3f6)&1)&&readide(0x1f1)==4);
    }
    start(true);lba(258047,1);writeide(0x1f7,0x20);tick();for(unsigned i=0;i<512;++i)assert(readide(0x1f0)==(uint8_t)(258047+i));
    lba(258047,2);writeide(0x1f7,0x20);assert(readide(0x1f1)==0x10&&readide(0x3f6)&1);
    start(true);writeide(0x1f6,0xaf);writeide(0x1f5,0);writeide(0x1f4,255);writeide(0x1f3,63);writeide(0x1f2,1);writeide(0x1f7,0x20);tick();
    for(unsigned i=0;i<256;++i){uint16_t value=readidew();assert((value&255)==(uint8_t)(258047+i*2)&&value>>8==(uint8_t)(258047+i*2+1));}
    writeide(0x1f6,0xaf);writeide(0x1f4,255);writeide(0x1f3,63);writeide(0x1f2,2);writeide(0x1f7,0x20);assert(readide(0x1f1)==0x10);
    start(true);writeide(0x1f3,0);writeide(0x1f7,0x20);assert(readide(0x1f1)==0x10);
    start(true);lba(20,1);writeide(0x1f7,0x30);for(unsigned i=0;i<511;++i)writeide(0x1f0,0x99);resetide();assert(!writes&&scheduler.next_event==ARC_RPC_EVENT_COUNT);
    start(true);lba(20,1);writeide(0x1f7,0x30);for(unsigned i=0;i<512;++i)writeide(0x1f0,0x99);writeide(0x3f6,4);assert(!writes&&scheduler.next_event==ARC_RPC_EVENT_COUNT);
    writeide(0x3f6,0);assert(scheduler.next_ns==500000);tick();assert(readide(0x1f2)==1&&readide(0x1f3)==1&&!readide(0x1f4)&&!readide(0x1f5));assert(!(iomd.irqb.status&2));
    start(true);writeide(0x1f6,0xf0);writeide(0x3f6,4);writeide(0x3f6,0);tick();assert(readide(0x1f7)==0);writeide(0x1f6,0xe0);assert(readide(0x3f6)==0x50&&!arc_rpc_ide_pending());
    start(true);lba(20,1);fail_read=true;writeide(0x1f7,0x20);tick();assert(readide(0x1f1)==0x10);
    start(true);lba(20,1);fail_write=true;writeide(0x1f7,0x30);for(unsigned i=0;i<256;++i)writeidew(0x1234);tick();assert(writes==1&&readide(0x1f1)==4);resetide();lba(20,1);writeide(0x1f7,0x30);assert(readide(0x1f1)==4&&writes==1);
    start(true);lba(20,1);writeide(0x1f7,0x20);writeide(0x1f6,0xf0);assert(readide(0x1f7)==0&&readidew()==0xffff&&scheduler.next_event==ARC_RPC_EVENT_COUNT);
    start(true);writeide(0x1f7,0xec);arc_rpc_schedule(&scheduler,ARC_RPC_EVENT_FDC,1000000);arc_rpc_schedule(&scheduler,ARC_RPC_EVENT_SOUND,1000000);
    ArcRpcEvent e;assert(arc_rpc_take_event(&scheduler,1000000,&e)&&e==ARC_RPC_EVENT_FDC);assert(arc_rpc_take_event(&scheduler,1000000,&e)&&e==ARC_RPC_EVENT_IDE);callbackide();assert(arc_rpc_take_event(&scheduler,1000000,&e)&&e==ARC_RPC_EVENT_SOUND);
    arc_rpc_ide_set_backend(NULL);callbackide();assert(!(iomd.irqb.status&2));puts("rpc-ide-registers: pass");return 0;
}
