/* Authored RPC envelope, oracle and publication interruption tests. GPL-2.0-only. */
#include "rpc_cmos_store.h"
#include "arc_io.h"
#include "arcdroid_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct { char path[1200];uint8_t bytes[1024];size_t size;bool exists; } File;
static File files[64], baseline[64];
struct retro_vfs_file_handle { unsigned slot;bool used,write,read;size_t cached_size; };
struct retro_vfs_dir_handle { unsigned at,current,unrelated; };
static struct retro_vfs_file_handle handles[16];
static struct retro_vfs_dir_handle directory;
static unsigned unrelated_entries,listed_entries;
static unsigned step,fail_step,kill_step,write_open_step;static unsigned listing_failures;static bool unreadable;static bool corrupt_readback;static bool killed;
/* A simulated kill stops every subsequent VFS operation; restart below uses
 * only persistent bytes and a new store, never the interrupted store state. */
static bool before(void){++step;return !killed && step!=fail_step;}
static void after(void){if(step==kill_step)killed=true;}
static int index_of(const char *p,bool create)
{ for(unsigned i=0;i<64;++i)if(files[i].exists&&!strcmp(files[i].path,p))return (int)i;
  if(create)for(unsigned i=0;i<64;++i)if(!files[i].exists){snprintf(files[i].path,sizeof(files[i].path),"%s",p);return (int)i;}
  return -1; }
static struct retro_vfs_file_handle *open_file(const char *p,unsigned mode,unsigned hints)
{
    (void)hints;if(!before())return NULL;bool write=(mode&2)!=0;int i=index_of(p,write);if(i<0)return NULL;
    if(write){files[i].exists=true;files[i].size=0;write_open_step=step;}
    for(unsigned h=0;h<16;++h)if(!handles[h].used){handles[h]=(struct retro_vfs_file_handle){(unsigned)i,true,write,(mode&1)!=0||(write&&(mode&4)),files[i].size};after();return &handles[h];}
    assert(0);return NULL;
}
static int close_file(struct retro_vfs_file_handle *h){bool ok=before();h->used=false;after();return ok?0:-1;}
static int64_t size_file(struct retro_vfs_file_handle *h){if(!before())return -1;int64_t n=(int64_t)h->cached_size;after();return n;}
static int64_t read_file(struct retro_vfs_file_handle *h,void *p,uint64_t n)
{ if(!before()||!h->read||unreadable)return -1;if(n>files[h->slot].size)n=files[h->slot].size;memcpy(p,files[h->slot].bytes,(size_t)n);if(corrupt_readback && strstr(files[h->slot].path,".tmp") && n>104){((uint8_t *)p)[104]^=1;corrupt_readback=false;}after();return (int64_t)n; }
static int64_t write_file(struct retro_vfs_file_handle *h,const void *p,uint64_t n)
{ if(!before()){if(killed)return -1;assert(n<=1024);if((size_t)n/2>files[h->slot].size)files[h->slot].size=(size_t)n/2;if((size_t)n/2>h->cached_size)h->cached_size=(size_t)n/2;memcpy(files[h->slot].bytes,p,(size_t)n/2);return (int64_t)n/2;}assert(n<=1024);memcpy(files[h->slot].bytes,p,(size_t)n);if((size_t)n>files[h->slot].size)files[h->slot].size=(size_t)n;if((size_t)n>h->cached_size)h->cached_size=(size_t)n;after();return (int64_t)n; }
static int flush_file(struct retro_vfs_file_handle *h){(void)h;if(!before())return -1;after();return 0;}
static int remove_file(const char *p){if(!before())return -1;int i=index_of(p,false);if(i<0)return -1;files[i].exists=false;after();return 0;}
static int rename_file(const char *a,const char *b)
{ if(!before())return -1;int i=index_of(a,false);if(i<0 || index_of(b,false)>=0)return -1;snprintf(files[i].path,sizeof(files[i].path),"%s",b);after();return 0; }
static int32_t stat_file(const char *p,int32_t *n)
{ if(!before())return 0;int i=index_of(p,false);if(i>=0){if(n)*n=(int32_t)files[i].size;after();return RETRO_VFS_STAT_IS_VALID;}after();return RETRO_VFS_STAT_IS_VALID|RETRO_VFS_STAT_IS_DIRECTORY; }
static int mkdir_file(const char *p){(void)p;if(!before())return -1;after();return 0;}
static struct retro_vfs_dir_handle *opendir_file(const char *p,bool hidden){(void)p;(void)hidden;if(!before())return NULL;if(listing_failures){--listing_failures;return NULL;}memset(&directory,0,sizeof(directory));after();return &directory;}
static bool readdir_file(struct retro_vfs_dir_handle *d){if(!before())return false;if(d->unrelated<unrelated_entries){++d->unrelated;++listed_entries;d->current=64;after();return true;}while(d->at<64){unsigned i=d->at++;if(files[i].exists){d->current=i;after();return true;}}after();return false;}
static const char *name_file(struct retro_vfs_dir_handle *d){if(!before())return NULL;static char filler[32];if(d->current==64){snprintf(filler,sizeof(filler),"unrelated%u",d->unrelated);after();return filler;}const char *name=strrchr(files[d->current].path,'/')+1;after();return name;}
static bool isdir_file(struct retro_vfs_dir_handle *d){(void)d;if(!before())return true;after();return false;}
static int closedir_file(struct retro_vfs_dir_handle *d){(void)d;if(!before())return -1;after();return 0;}
static struct retro_vfs_interface vfs={.open=open_file,.close=close_file,.size=size_file,.read=read_file,.write=write_file,.flush=flush_file,.remove=remove_file,.rename=rename_file,.stat=stat_file,.mkdir=mkdir_file,.opendir=opendir_file,.readdir=readdir_file,.dirent_get_name=name_file,.dirent_is_dir=isdir_file,.closedir=closedir_file};
static char error[256];
static void fresh(void){step=fail_step=kill_step=write_open_step=0;killed=false;corrupt_readback=false;memset(handles,0,sizeof(handles));}
static void crc(File *f){uint32_t crc=arc_crc32(f->bytes,508);for(unsigned i=0;i<4;++i)f->bytes[508+i]=(uint8_t)(crc>>(i*8));}
int main(void)
{
    for(unsigned layout=1;layout<=3;++layout)for(unsigned field=0;field<256;++field){
        uint8_t before[240],after[240];unsigned sum=1;
        for(unsigned i=0;i<240;++i)before[i]=(uint8_t)(i*13u+layout);
        before[183]=(uint8_t)field;
        for(unsigned i=0;i<240;++i)if(i!=47)sum+=before[i];before[47]=(uint8_t)sum;
        memcpy(after,before,240);assert(arc_riscpc_cmos_two_floppies(layout,after));
        assert(after[183]==((field&248u)|2u) && arc_riscpc_cmos_checksum(after));
        for(unsigned i=0;i<240;++i)if(i!=47 && i!=183)assert(after[i]==before[i]);
        assert(!arc_riscpc_cmos_two_floppies(0,before));
        assert(!arc_riscpc_cmos_two_floppies(4,before));
        before[47]^=1;assert(!arc_riscpc_cmos_two_floppies(layout,before));
    }
    assert(!arc_riscpc_cmos_two_floppies(1,NULL));
    arc_io_set_vfs(&vfs);ArcRpcCmosKey key={0};memset(key.content,'a',40);memset(key.firmware,'b',64);key.layout=1;
    ArcRpcCmosStore s,t;uint8_t p[240]={0},q[240]={0},r[240]={0};p[47]=1;q[0]=7;q[47]=8;r[0]=9;r[47]=10;
    assert(arc_riscpc_cmos_store_load(&s,"/save",&key,error,sizeof(error))==ARC_RPC_CMOS_MISSING);
    assert(arc_riscpc_cmos_store_put(&s,p,NULL,error,sizeof(error))&&s.generation==1);
    unsigned writes=step;assert(arc_riscpc_cmos_store_put(&s,p,NULL,error,sizeof(error))&&step==writes);
    assert(arc_riscpc_cmos_store_put(&s,q,NULL,error,sizeof(error))&&s.generation==2);
    assert(arc_riscpc_cmos_store_load(&t,"/save",&key,error,sizeof(error))==ARC_RPC_CMOS_FOUND&&!memcmp(t.payload,q,240));
    for(unsigned isolate=0;isolate<3;++isolate){ArcRpcCmosKey other=key;if(!isolate)other.content[0]='c';else if(isolate==1)other.model=1;else other.firmware[0]='c';assert(arc_riscpc_cmos_store_load(&t,"/save",&other,error,sizeof(error))==ARC_RPC_CMOS_MISSING);}
    memcpy(baseline,files,sizeof(files));fresh();assert(arc_riscpc_cmos_store_put(&s,p,NULL,error,sizeof(error)));unsigned operations=step,first_write=write_open_step;assert(operations>=14&&first_write);
    for(unsigned kind=0;kind<2;++kind)for(unsigned at=1;at<=operations;++at){
        memcpy(files,baseline,sizeof(files));fresh();assert(arc_riscpc_cmos_store_load(&s,"/save",&key,error,sizeof(error))==ARC_RPC_CMOS_FOUND);fresh();
        if(kind)kill_step=at;else fail_step=at;(void)arc_riscpc_cmos_store_put(&s,p,NULL,error,sizeof(error));
        File interrupted[64];memcpy(interrupted,files,sizeof(files));
        /* Every write/flush/readback/retirement/rename/publication boundary
         * recovers in the live session as well as after restart. */
        fresh();if(at>=first_write)assert(arc_riscpc_cmos_store_put(&s,r,NULL,error,sizeof(error))&&!s.refused);
        memcpy(files,interrupted,sizeof(files));fresh();
        assert(arc_riscpc_cmos_store_load(&t,"/save",&key,error,sizeof(error))==ARC_RPC_CMOS_FOUND&&!t.refused);
        assert(!memcmp(t.payload,p,240)||!memcmp(t.payload,q,240));assert(t.generation==2||t.generation==3);
        assert(arc_riscpc_cmos_store_put(&t,r,NULL,error,sizeof(error))&&!t.refused);
        fresh();assert(arc_riscpc_cmos_store_load(&s,"/save",&key,error,sizeof(error))==ARC_RPC_CMOS_FOUND&&!memcmp(s.payload,r,240));
    }
    /* A CRC-valid destination with the wrong slot parity is refused even
     * when it replaces an older record while the session remains live. */
    memcpy(files,baseline,sizeof(files));fresh();assert(arc_riscpc_cmos_store_load(&s,"/save",&key,error,sizeof(error))==ARC_RPC_CMOS_FOUND);
    assert(arc_riscpc_cmos_store_put(&s,p,NULL,error,sizeof(error))&&s.generation==3);
    char destination[1200];snprintf(destination,sizeof(destination),"%s.0",s.base);int replaced=index_of(destination,false);assert(replaced>=0);
    memset(files[replaced].bytes+24,0,8);files[replaced].bytes[24]=1;crc(&files[replaced]);
    assert(!arc_riscpc_cmos_store_put(&s,q,NULL,error,sizeof(error))&&s.refused&&files[replaced].bytes[24]==1);
    /* QA B: first publication, including the empty/torn candidate cases.
     * Discover the operation count from a successful write, independently of
     * implementation step numbering; every later publication must succeed. */
    memset(files,0,sizeof(files));fresh();assert(arc_riscpc_cmos_store_load(&s,"/save",&key,error,sizeof(error))==ARC_RPC_CMOS_MISSING);
    ArcRpcCmosStore empty=s;fresh();assert(arc_riscpc_cmos_store_put(&s,p,NULL,error,sizeof(error)));
    operations=step;first_write=write_open_step;
    for(unsigned kind=0;kind<2;++kind)for(unsigned at=first_write;at<=operations;++at){
        memset(files,0,sizeof(files));fresh();s=empty;if(kind)kill_step=at;else fail_step=at;
        (void)arc_riscpc_cmos_store_put(&s,p,NULL,error,sizeof(error));
        File interrupted[64];memcpy(interrupted,files,sizeof(files));fresh();
        assert(arc_riscpc_cmos_store_put(&s,r,NULL,error,sizeof(error))&&!s.refused);
        memcpy(files,interrupted,sizeof(files));fresh();
        ArcRpcCmosResult result=arc_riscpc_cmos_store_load(&t,"/save",&key,error,sizeof(error));
        assert(result!=ARC_RPC_CMOS_REFUSED&&!t.refused);
        assert(arc_riscpc_cmos_store_put(&t,r,NULL,error,sizeof(error)));
    }
    /* Any invalid unpublished candidate is disposable, including a foreign
     * Arculator envelope; identical bytes in published slots remain refused. */
    for(unsigned length=0;length<=512;++length){
        memcpy(files,baseline,sizeof(files));fresh();assert(arc_riscpc_cmos_store_load(&s,"/save",&key,error,sizeof(error))==ARC_RPC_CMOS_FOUND);
        char pending[1200];snprintf(pending,sizeof(pending),"%s.tmp",s.base);int candidate=index_of(pending,true);assert(candidate>=0);
        files[candidate].exists=true;files[candidate].size=length;memset(files[candidate].bytes,0,512);memcpy(files[candidate].bytes,"ARCCMOS2",8);
        assert(arc_riscpc_cmos_store_load(&t,"/save",&key,error,sizeof(error))==ARC_RPC_CMOS_FOUND&&!t.refused&&!error[0]);
        assert(arc_riscpc_cmos_store_put(&s,r,NULL,error,sizeof(error))&&!s.refused);
    }
    /* C11: wrong candidate readback cannot retire or replace either slot. */
    memcpy(files,baseline,sizeof(files));fresh();assert(arc_riscpc_cmos_store_load(&s,"/save",&key,error,sizeof(error))==ARC_RPC_CMOS_FOUND);
    corrupt_readback=true;assert(!arc_riscpc_cmos_store_put(&s,p,NULL,error,sizeof(error)));
    assert(strstr(error,"readback"));
    for(unsigned i=0;i<64;++i)if(baseline[i].exists){int j=index_of(baseline[i].path,false);assert(j>=0&&files[j].size==baseline[i].size&&!memcmp(files[j].bytes,baseline[i].bytes,baseline[i].size));}
    fresh();assert(arc_riscpc_cmos_store_put(&s,r,NULL,error,sizeof(error)));
    /* C05/C07/C10: independent wire values, with a recomputed valid CRC. */
    memcpy(files,baseline,sizeof(files));fresh();
    char parity_path[1200];snprintf(parity_path,sizeof(parity_path),"%s.0",s.base);int parity=index_of(parity_path,false);assert(parity>=0);
    memset(files[parity].bytes+24,0,8);files[parity].bytes[24]=1;crc(&files[parity]);
    assert(arc_riscpc_cmos_store_load(&t,"/save",&key,error,sizeof(error))==ARC_RPC_CMOS_FOUND&&t.refused);
    for(unsigned offset=344;offset<508;offset+=163){
        memcpy(files,baseline,sizeof(files));fresh();int i=index_of(parity_path,false);assert(i>=0);files[i].bytes[offset]=1;crc(&files[i]);
        assert(arc_riscpc_cmos_store_load(&t,"/save",&key,error,sizeof(error))==ARC_RPC_CMOS_FOUND&&t.refused);
    }
    for(unsigned cpu=0;cpu<4;++cpu){
        memset(files,0,sizeof(files));fresh();ArcRpcCmosKey other=key;other.model=cpu;
        assert(arc_riscpc_cmos_store_load(&t,"/save",&other,error,sizeof(error))==ARC_RPC_CMOS_MISSING);
        assert(arc_riscpc_cmos_store_put(&t,p,NULL,error,sizeof(error)));
        char name[1200];snprintf(name,sizeof(name),"%s.1",t.base);int i=index_of(name,false);assert(i>=0);
        assert(files[i].bytes[16]==cpu+1&&!files[i].bytes[17]&&!files[i].bytes[18]&&!files[i].bytes[19]);
    }
    /* Each byte, including reserved/header/provenance, participates in CRC. */
    for(unsigned byte=0;byte<512;++byte){memcpy(files,baseline,sizeof(files));fresh();int i=index_of(strcat(strcpy((char[1200]){0},s.base),".0"),false);assert(i>=0);files[i].bytes[byte]^=1;
        assert(arc_riscpc_cmos_store_load(&t,"/save",&key,error,sizeof(error))==ARC_RPC_CMOS_FOUND&&t.generation==1&&t.refused&&error[0]);
        assert(!arc_riscpc_cmos_store_put(&t,q,NULL,error,sizeof(error)));}
    for(unsigned kind=0;kind<6;++kind){memcpy(files,baseline,sizeof(files));fresh();for(unsigned i=0;i<64;++i)if(files[i].exists){if(!kind)memcpy(files[i].bytes,"ARCCMOS2",8);else if(kind==1)files[i].bytes[8]=2;else if(kind==2)files[i].bytes[32]='c';else if(kind==3)files[i].bytes[72]^=1;else if(kind==4)files[i].bytes[16]=4;else files[i].bytes[20]=2;crc(&files[i]);}
        assert(arc_riscpc_cmos_store_load(&t,"/save",&key,error,sizeof(error))==ARC_RPC_CMOS_REFUSED&&error[0]);}
    for(unsigned length=0;length<512;++length){memcpy(files,baseline,sizeof(files));fresh();char path[1200];snprintf(path,sizeof(path),"%s.0",s.base);int i=index_of(path,false);assert(i>=0);files[i].size=length;
        assert(arc_riscpc_cmos_store_load(&t,"/save",&key,error,sizeof(error))==ARC_RPC_CMOS_FOUND&&t.generation==1&&t.refused);}
    memset(files,0,sizeof(files));fresh();key.defaults=true;key.procedure=ARC_RPC_CMOS_PROCEDURE_REVISION;key.timing=ARC_RPC_CMOS_TIMING_REVISION;ArcRpcCmosEvidence e={.rate=20,.epoch=946684800,.writes=240,.last_write=40000000000};arc_riscpc_cmos_required(1,e.mask);
    assert(arc_riscpc_cmos_layout("ro350")==1 && arc_riscpc_cmos_layout("ro360")==2 && arc_riscpc_cmos_layout("ro370")==3 && !arc_riscpc_cmos_layout("unknown"));
    assert(arc_riscpc_cmos_oracle(1,p,&e));
    assert(!arc_riscpc_cmos_oracle(2,p,&e) && !arc_riscpc_cmos_oracle(3,p,&e)); /* 194/195 required in both later families */
    arc_riscpc_cmos_required(2,e.mask);assert(arc_riscpc_cmos_oracle(2,p,&e) && arc_riscpc_cmos_oracle(3,p,&e));
    uint8_t bad[240];memcpy(bad,p,240);bad[47]^=1;assert(!arc_riscpc_cmos_oracle(2,bad,&e));
    arc_riscpc_cmos_required(1,e.mask);
    e.last_write++;assert(!arc_riscpc_cmos_oracle(1,p,&e));e.last_write--;e.mask[3]=0;assert(!arc_riscpc_cmos_oracle(1,p,&e));arc_riscpc_cmos_required(1,e.mask);
    assert(arc_riscpc_cmos_two_floppies(1,p));
    assert(arc_riscpc_cmos_store_load(&s,"/save",&key,error,sizeof(error))==ARC_RPC_CMOS_MISSING);assert(arc_riscpc_cmos_store_put(&s,p,&e,error,sizeof(error)));
    assert(arc_riscpc_cmos_store_load(&t,"/save",&key,error,sizeof(error))==ARC_RPC_CMOS_FOUND&&t.evidence.writes==240&&t.evidence.last_write==e.last_write);
    for(unsigned revision=0;revision<2;++revision){ArcRpcCmosKey other=key;if(revision)++other.timing;else ++other.procedure;assert(arc_riscpc_cmos_store_load(&t,"/save",&other,error,sizeof(error))==ARC_RPC_CMOS_MISSING);}
    memcpy(baseline,files,sizeof(files));
    for(unsigned i=0;i<64;++i)if(files[i].exists){
        files[i].bytes[104+183]=1;--files[i].bytes[104+47];
        char sha[65];arc_sha256_hex(files[i].bytes+104,240,sha);
        for(unsigned b=0;b<32;++b){unsigned value;assert(sscanf(sha+2*b,"%2x",&value)==1);files[i].bytes[448+b]=(uint8_t)value;}
        crc(&files[i]);
    }
    assert(arc_riscpc_cmos_store_load(&t,"/save",&key,error,sizeof(error))==ARC_RPC_CMOS_REFUSED);
    memcpy(files,baseline,sizeof(files));fresh();
    const unsigned offsets[]={344,348,352,356,368,376,384,392,400,416,416+2,448,480,507,360};
    for(unsigned at=0;at<sizeof(offsets)/sizeof(*offsets);++at){unsigned offset=offsets[at];
        memcpy(files,baseline,sizeof(files));fresh();for(unsigned i=0;i<64;++i)if(files[i].exists){if(offset==360){uint64_t outside=UINT64_C(13569465600);for(unsigned byte=0;byte<8;++byte)files[i].bytes[360+byte]=(uint8_t)(outside>>(8*byte));}else if(offset==400){memset(files[i].bytes+400,0,8);files[i].bytes[400]=1;}else files[i].bytes[offset]^=1;crc(&files[i]);}
        assert(arc_riscpc_cmos_store_load(&t,"/save",&key,error,sizeof(error))==ARC_RPC_CMOS_REFUSED);
    }

    /* LOW-A: retry listing and partial inspection failures without latching
     * corruption; seeds/publications are barred until both slots reconcile. */
    memset(files,0,sizeof(files));fresh();key.defaults=false;
    assert(arc_riscpc_cmos_store_begin(&s,"/save",&key,error,sizeof(error))==ARC_RPC_CMOS_INSPECTING);
    assert(!arc_riscpc_cmos_store_seed(&s,p,error,sizeof(error)));
    assert(!arc_riscpc_cmos_store_put_begin(&s,p,NULL,error,sizeof(error)));
    listing_failures=1;ArcRpcCmosResult state;
    do{unsigned ops=step;state=arc_riscpc_cmos_store_step(&s,error,sizeof(error));
        assert(s.stats.step_operations==step-ops&&s.stats.step_operations<=4&&s.stats.step_namespace<=1);
    }while(state==ARC_RPC_CMOS_INSPECTING);
    assert(state==ARC_RPC_CMOS_MISSING&&s.reconciled&&!s.refused&&!error[0]);
    assert(arc_riscpc_cmos_store_seed(&s,p,error,sizeof(error)));
    assert(!arc_riscpc_cmos_store_seed(&s,q,error,sizeof(error)));
    assert(arc_riscpc_cmos_store_load(&s,"/save",&key,error,sizeof(error))==ARC_RPC_CMOS_FOUND);
    listing_failures=3;assert(arc_riscpc_cmos_store_load(&t,"/save",&key,error,sizeof(error))==ARC_RPC_CMOS_UNAVAILABLE);
    assert(t.exhausted&&!t.refused&&!t.reconciled&&!arc_riscpc_cmos_store_seed(&t,q,error,sizeof(error)));
    assert(!arc_riscpc_cmos_store_put(&t,q,NULL,error,sizeof(error)));
    unreadable=true;assert(arc_riscpc_cmos_store_load(&t,"/save",&key,error,sizeof(error))==ARC_RPC_CMOS_UNAVAILABLE);
    assert(!t.reconciled&&!t.refused&&!arc_riscpc_cmos_store_seed(&t,q,error,sizeof(error)));unreadable=false;
    assert(arc_riscpc_cmos_store_load(&t,"/save",&key,error,sizeof(error))==ARC_RPC_CMOS_FOUND&&!memcmp(t.payload,p,240));
    /* Explicit publication steps obey counts independently of quantum and
     * preserve the newest published slot after rename/readback interruption. */
    assert(arc_riscpc_cmos_store_put_begin(&t,q,NULL,error,sizeof(error)));
    bool done=false;while(!done){unsigned ops=step;assert(arc_riscpc_cmos_store_put_step(&t,&done,error,sizeof(error)));
        assert(t.stats.step_operations==step-ops&&t.stats.step_operations<=4&&t.stats.step_bytes<=512&&t.stats.step_namespace<=1);}
    assert(t.generation==2&&!memcmp(t.payload,q,240));
    memcpy(baseline,files,sizeof(files));
    /* Partial-slot inspection is never a found/missing result, and an invalid
     * slot found before a transient failure remains refused after retry. */
    for(unsigned at=1;at<=40;++at){memcpy(files,baseline,sizeof(files));fresh();fail_step=at;
        state=arc_riscpc_cmos_store_begin(&t,"/save",&key,error,sizeof(error));
        while(state==ARC_RPC_CMOS_INSPECTING){assert(!t.reconciled);
            assert(!arc_riscpc_cmos_store_put_begin(&t,p,NULL,error,sizeof(error)));
            state=arc_riscpc_cmos_store_step(&t,error,sizeof(error));}
        assert(state==ARC_RPC_CMOS_FOUND&&t.generation==2&&!t.refused&&!memcmp(t.payload,q,240));
    }

    memcpy(files,baseline,sizeof(files));fresh();
    char corrupt_path[1200];snprintf(corrupt_path,sizeof(corrupt_path),"%s.0",s.base);
    int corrupt=index_of(corrupt_path,false);assert(corrupt>=0);files[corrupt].bytes[104]^=1;
    state=arc_riscpc_cmos_store_begin(&t,"/save",&key,error,sizeof(error));bool interrupted=false;
    while(state==ARC_RPC_CMOS_INSPECTING){
        if(t.phase==5&&t.slot==1&&!interrupted){assert(t.refused);unreadable=true;interrupted=true;}
        state=arc_riscpc_cmos_store_step(&t,error,sizeof(error));unreadable=false;
    }
    assert(interrupted&&t.refused&&t.reconciled&&t.generation==1&&state==ARC_RPC_CMOS_FOUND);
    assert(!arc_riscpc_cmos_store_seed(&t,q,error,sizeof(error)));
    assert(!arc_riscpc_cmos_store_put(&t,p,NULL,error,sizeof(error)));
    /* LOW-2: more than 4096 unrelated records precede both canonical slots.
     * Stop advisory enumeration, then inspect the fixed paths independently. */
    memcpy(files,baseline,sizeof(files));fresh();
    unrelated_entries=5000;listed_entries=0;
    state=arc_riscpc_cmos_store_begin(&t,"/save",&key,error,sizeof(error));
    unsigned ticks=0;
    while(state==ARC_RPC_CMOS_INSPECTING){
        assert(++ticks<4200);unsigned ops=step;
        state=arc_riscpc_cmos_store_step(&t,error,sizeof(error));
        assert(step-ops<=4 && t.stats.step_operations==step-ops);
        assert(t.stats.step_namespace<=1 && t.stats.step_bytes<=512);
    }
    assert(listed_entries==4096 && !t.attempts && state==ARC_RPC_CMOS_FOUND);
    assert(t.generation==2 && !memcmp(t.payload,q,240) && !t.refused);
    ArcRpcCmosKey absent=key;absent.content[0]='c';listed_entries=0;
    assert(arc_riscpc_cmos_store_load(&t,"/save",&absent,error,sizeof(error))==ARC_RPC_CMOS_MISSING);
    assert(listed_entries==4096 && !t.attempts);
    files[corrupt].bytes[104]^=1;
    assert(arc_riscpc_cmos_store_load(&t,"/save",&key,error,sizeof(error))==ARC_RPC_CMOS_FOUND && t.refused);
    assert(!arc_riscpc_cmos_store_seed(&t,p,error,sizeof(error)));
    unreadable=true;
    assert(arc_riscpc_cmos_store_load(&t,"/save",&key,error,sizeof(error))==ARC_RPC_CMOS_UNAVAILABLE);
    assert(!arc_riscpc_cmos_store_seed(&t,p,error,sizeof(error)));
    unreadable=false;unrelated_entries=0;
    puts("RPC CMOS: byte integrity, keys, fallback/refusal, all publication failures and simulated kills, oracle and revisions passed");return 0;
}
