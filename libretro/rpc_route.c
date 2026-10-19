/* No content or firmware identity policy lives here. GPL-2.0-only. */
#include "rpc_route.h"
#include "arc_io.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

ArcRpcStorePresence arc_riscpc_store_presence(const char *save,const char *content)
{
    char path[4096];
    int n=snprintf(path,sizeof(path),"%s/arcdroid/hd-riscpc/%s",save?save:"",content?content:"");
    if(!save || !*save || !content || strlen(content)!=40 || n<0 || (size_t)n>=sizeof(path))return ARC_RPC_STORE_UNKNOWN;
    ArcIoMetadata metadata=arc_io_metadata(path,NULL);
    if(metadata==ARC_IO_METADATA_DIRECTORY)return ARC_RPC_STORE_EXISTS;
    /* Climb to a stat-confirmed ancestor, then resolve missing components.
     * Every VFS operation is charged, including EOF and directory close. */
    size_t ends[64];unsigned depth=0,entries=0,ops=1;
    while(metadata!=ARC_IO_METADATA_DIRECTORY){
        if(depth==64 || ops>=16380)return ARC_RPC_STORE_UNKNOWN;
        char *slash=strrchr(path,'/');if(!slash || slash==path)return ARC_RPC_STORE_UNKNOWN;
        ends[depth++]=(size_t)(slash-path);*slash=0;
        metadata=arc_io_metadata(path,NULL);++ops;
    }
    while(depth){
        size_t split=ends[--depth];const char *wanted=path+split+1;
        if(ops>=16380)return ARC_RPC_STORE_UNKNOWN;
        struct retro_vfs_dir_handle *dir=arc_io_open_directory(path);++ops;
        if(!dir)return ARC_RPC_STORE_UNKNOWN;
        bool found=false,ok=true;
        for(;;){
            const char *name=NULL;bool regular=false;uint64_t used=0;
            if(++entries>4096 || ops>=16380){ok=false;break;}
            int result=arc_io_read_directory(dir,&name,&regular,&used);
            ops+=(unsigned)used;if(result<0){ok=false;break;}if(!result)break;
            if(!name || strchr(name,'/') || strchr(name,'\\')){ok=false;break;}
            if(strcmp(name,".") && strcmp(name,"..")){
                char member[4096];int length=snprintf(member,sizeof(member),"%s/%s",path,name);
                if(length<0 || (size_t)length>=sizeof(member) || arc_io_metadata(member,NULL)==ARC_IO_METADATA_UNKNOWN){ok=false;break;}
                ++ops;
            }
            if(!strcmp(name,wanted))found=true;
        }
        if(!arc_io_close_directory(dir))ok=false;++ops;
        if(!ok)return ARC_RPC_STORE_UNKNOWN;
        if(!found)return ARC_RPC_STORE_ABSENT;
        if(ops>=16384)return ARC_RPC_STORE_UNKNOWN;
        path[split]='/';
        if(arc_io_metadata(path,NULL)!=ARC_IO_METADATA_DIRECTORY)return ARC_RPC_STORE_UNKNOWN;
        ++ops;
    }
    return ARC_RPC_STORE_EXISTS;
}

bool arc_riscpc_route_epoch_with_clock(time_t (*clock)(time_t *),uint64_t *out)
{
    if(!out || !clock)return false;
    time_t now=clock(NULL);if(now==(time_t)-1)return false;
    struct tm *p=localtime(&now);if(!p)return false;
    struct tm t=*p;
    if(t.tm_year<70 || t.tm_year>=500)return false;
    unsigned year=(unsigned)t.tm_year+1900;
    if(t.tm_mon<0 || t.tm_mon>11 || t.tm_mday<1 || t.tm_mday>31)return false;
    uint64_t days=0;
    for(unsigned y=1970;y<year;++y)days+=365+(y%4==0 && (y%100!=0 || y%400==0));
    static const unsigned lengths[]={31,28,31,30,31,30,31,31,30,31,30,31};
    for(int m=0;m<t.tm_mon;++m)days+=lengths[m]+(m==1 && year%4==0 && (year%100!=0 || year%400==0));
    *out=(days+(unsigned)t.tm_mday-1)*86400+(unsigned)t.tm_hour*3600+(unsigned)t.tm_min*60+(unsigned)t.tm_sec;
    return true;
}

bool arc_riscpc_route_epoch(uint64_t *out){return arc_riscpc_route_epoch_with_clock(time,out);}

const char *arc_riscpc_route_notice(const ArcRpcSelection *s,const ArcRpcFirmwareInventory *i,char *out,size_t n)
{
    const char *id="N-1",*text=NULL;
    switch(s->reason){
    case ARC_RPC_SELECTION_CONFLICTING_DISCS:id="N-4";text="The discs in this set ask for different machines. Load them separately, or choose a machine in core options.";break;
    case ARC_RPC_SELECTION_UNSUPPORTED:id="N-3";text="This title asks for hardware ArcDroid does not emulate (unsupported CPU or RISC OS requirement).";break;
    case ARC_RPC_SELECTION_UNQUALIFIED:id="N-2";text="This title needs StrongARM with RISC OS 3.50 or 3.60, which ArcDroid does not support yet. You can choose an Archimedes in core options to try it anyway.";break;
    case ARC_RPC_SELECTION_INCOMPLETE_SCAN:id="N-5 incomplete-scan";text="The firmware check reached its entry, byte, depth or path limit. Reduce files or nesting in <system>/arcdroid/firmware/, then close and reload this content.";break;
    default:
        if(i && !i->bounded && i->resource_failure){id="N-1 resource";text="ArcDroid ran out of memory while checking RiscPC firmware. Close other apps, then close and reload this content.";}
        else if(s->reason==ARC_RPC_SELECTION_UNREADABLE_INVENTORY){id="N-5";text="ArcDroid cannot find or read <system>/arcdroid/firmware/. Create that folder and add your RiscPC firmware, or check that it is readable.";}
        break;
    }
    if(text)snprintf(out,n,"%s",text);
    else{
        char families[32]={0};
        for(unsigned os=1;os<=4;++os)if(s->needed & ARC_RPC_OS_BIT(os)){
            if(*families)strcat(families,",");strcat(families,arc_riscpc_selection_os_name((ArcRpcSelectionOs)os));
        }
        snprintf(out,n,"Needs RiscPC RISC OS %s in <system>/arcdroid/firmware/. Or try an Archimedes in core options.",*families?families:"3.50–3.71");
    }
    return id;
}

static uint64_t pair_generation(const unsigned char *record)
{
    uint64_t value=0;for(unsigned k=0;k<8;++k)value|=(uint64_t)record[8+k]<<(8*k);return value;
}
static bool pair_valid(const unsigned char *record,size_t size)
{
    static const unsigned char magic[8]={'A','R','C','P','A','I','R',1};
    if(size!=32 || memcmp(record,magic,8) || !pair_generation(record) || record[16]>=4 || record[17]<1 || record[17]>4)return false;
    for(unsigned k=18;k<28;++k)if(record[k])return false;
    uint32_t checksum=0;for(unsigned k=0;k<4;++k)checksum|=(uint32_t)record[28+k]<<(8*k);
    return checksum==arc_crc32(record,28);
}
bool arc_riscpc_pair_admitted(const char *save,const char *content,unsigned model,unsigned os,ArcLock *owner)
{
    char directory[1100],path[1210],temporary[1210],base[1200];
    if(!arc_lock_held(owner) || arc_lock_verify(owner)!=ARC_LOCK_HELD)return false;
    int n=snprintf(directory,sizeof(directory),"%s/arcdroid/rpc-pairs",save);
    if(n<0 || (size_t)n>=sizeof(directory))return false;
    n=snprintf(base,sizeof(base),"%s/%s.pair",directory,content);
    if(n<0 || (size_t)n>=sizeof(base) || model>=4 || os<1 || os>4)return false;
    unsigned char latest[32]={0};int best=-1;
    for(unsigned slot=0;slot<2;++slot){
        void *old=NULL;size_t length=0;snprintf(path,sizeof(path),"%s.%u",base,slot);
        if(arc_io_read_all_limit(path,&old,&length,32,NULL) && pair_valid(old,length) &&
           (best<0 || pair_generation(old)>pair_generation(latest))){memcpy(latest,old,32);best=(int)slot;}
        free(old);
    }
    bool changed=best>=0 && (latest[16]!=model || latest[17]!=os);
    if(best>=0 && !changed)return false;
    uint64_t generation=best<0?1:pair_generation(latest)+1;
    if(!generation)return changed; /* Advisory exhaustion never refuses admission. */
    unsigned char record[32]={'A','R','C','P','A','I','R',1};
    for(unsigned k=0;k<8;++k)record[8+k]=(unsigned char)(generation>>(8*k));
    record[16]=(unsigned char)model;record[17]=(unsigned char)os;
    uint32_t checksum=arc_crc32(record,28);for(unsigned k=0;k<4;++k)record[28+k]=(unsigned char)(checksum>>(8*k));
    snprintf(temporary,sizeof(temporary),"%s.tmp",base);
    snprintf(path,sizeof(path),"%s.%u",base,best<0?0u:(unsigned)best^1u);
    /* Same two-slot rule as CMOS: only retire the inactive slot. Windows VFS
     * rename cannot replace; the last complete generation survives every gap. */
    if(arc_lock_verify(owner)==ARC_LOCK_HELD && arc_io_make_path(directory) &&
       arc_lock_verify(owner)==ARC_LOCK_HELD && arc_io_write_all(temporary,record,32) && arc_lock_verify(owner)==ARC_LOCK_HELD){
        (void)arc_io_remove(path);
        if(arc_lock_verify(owner)==ARC_LOCK_HELD)(void)arc_io_rename(temporary,path);
    }
    return changed;
}
