/* Observable store, checkpoint, population, cap and failure boundaries.
 * Project-authored GPL-2.0-only. */
#include "rpc_hd_store.h"
#include "install_disc.h"
#include "arcdroid_core.h"
#include "rpc_hd_vfs.h"
#include <time.h>
#include <inttypes.h>
static const char *id="0123456789012345678901234567890123456789";
static char error[256];
#ifdef ARCDROID_HD_TEST_ALLOC
static bool out_of_memory;
static void *store_allocation;
static size_t store_allocation_bytes;
void *arc_riscpc_hd_test_calloc(size_t count,size_t size)
{if(out_of_memory){out_of_memory=false;return NULL;}
 store_allocation=calloc(count,size);store_allocation_bytes=count*size;return store_allocation;}
#endif
static ArcRpcHdStore *open_store(bool blank)
{ArcRpcHdStore *s=arc_riscpc_hd_open("/save",id,blank,error,sizeof(error));assert(s);return s;}
static void idle(ArcRpcHdStore *s)
{ArcRpcHdStats a;unsigned bound=0;do{arc_riscpc_hd_tick(s);arc_riscpc_hd_stats(s,&a);assert(!a.read_only&&++bound<4096);}while(a.state!=ARC_RPC_HD_IDLE);}
static void until_phase(ArcRpcHdStore *s,unsigned state,unsigned step)
{
 ArcRpcHdStats a;for(unsigned t=0;t<4096;++t){arc_riscpc_hd_stats(s,&a);assert(!a.read_only);
  if(a.state==state&&a.phase_step==step)return;arc_riscpc_hd_tick(s);}assert(0);
}
static void fill(uint8_t *p,unsigned count,uint8_t value) {memset(p,value,count*512);}
static void check_sector(ArcRpcHdStore *s,uint32_t at,uint8_t value)
{uint8_t p[512];assert(arc_riscpc_hd_read(s,at,1,p));for(unsigned i=0;i<512;++i){if(p[i]!=value)fprintf(stderr,"LBA %u byte %u actual=%u expected=%u\n",at,i,p[i],value);assert(p[i]==value);}}
static void bounds(ArcRpcHdStore *s)
{
    ArcRpcHdStats a;arc_riscpc_hd_stats(s,&a);assert(a.allocation_high<=4889964&&a.max_chunk_files<=1008&&a.journal_high<=8388608);
    assert(a.max_tick_bytes<=9437712&&a.max_tick_operations<=4096&&a.max_tick_metadata_operations<=4&&a.max_tick_namespace_operations<=1);
    assert(a.max_tick_hash_bytes<=18948908&&a.max_tick_copied_bytes<=2097152);
    unsigned manifests=0,journals=0,chunks=0;uint64_t journal_bytes=0,temp=0;
    for(unsigned i=0;i<TEST_FILES;++i)if(files[i].present){const char *p=strrchr(files[i].path,'/')+1;
        if(!strncmp(p,"manifest.",9))++manifests;else if(!strncmp(p,"journal.",8)){++journals;journal_bytes+=files[i].size;}else if(p[0]=='c'&&p[1]!='h')++chunks;
        if(!strcmp(p,"chunk.tmp")||!strcmp(p,"manifest.tmp"))temp+=files[i].size;}
    assert(manifests<=2&&journals<=2&&chunks<=1008&&journal_bytes<=8388608&&temp<=286600);
}
static int64_t position_seek(struct retro_vfs_file_handle *f,int64_t at,int origin)
{ return test_seek(f,at,origin)<0?-1:(int64_t)f->at; }
static int64_t misplaced_seek(struct retro_vfs_file_handle *f,int64_t at,int origin)
{ (void)f;(void)at;(void)origin;return before(4)?0:-1; }
static void vfs_conformance(void)
{
    fresh_files();uint8_t p[512];fill(p,1,0x5a);
    struct retro_vfs_file_handle *a=test_open("/save/probe",2,0);assert(a);
    assert(test_write(a,p,16)==16&&test_size(a)==16);
    struct retro_vfs_file_handle *b=test_open("/save/probe",1,0);assert(b&&test_size(b)==16);
    assert(test_read(a,p,1)==-1);
    assert(test_seek(a,16,0)==0&&test_tell(a)==16);
    assert(test_write(a,p,8)==8&&test_size(a)==24&&test_size(b)==16);
    int32_t length;assert(test_stat("/save/probe",&length)==1&&length==24);
    struct retro_vfs_file_handle *c=test_open("/save/probe",1,0);assert(c&&test_size(c)==24);
    assert(test_truncate(a,8)==0&&test_size(a)==8&&test_size(b)==16&&test_size(c)==24);
    struct retro_vfs_file_handle *update=test_open("/save/probe",6,0);assert(update);
    assert(test_read(update,p,8)==8&&test_close(update)==0);
    assert(test_close(a)==0&&test_close(b)==0&&test_close(c)==0);
    fresh_files();ArcRpcHdStore *s=open_store(true);idle(s);
    /* This first nonzero seek refused the command before D-150 remediation. */
    assert(arc_riscpc_hd_write(s,8192,1,p));check_sector(s,8192,0x5a);
    arc_riscpc_hd_close(s);s=open_store(false);idle(s);check_sector(s,8192,0x5a);
    arc_riscpc_hd_close(s);
    /* Position-return VFSs remain supported. A dishonest successful seek must
     * fail the independent tell check before transferring a record byte. */
    for(unsigned mode=0;mode<2;++mode){fresh_files();s=open_store(true);idle(s);
        vfs.seek=mode?misplaced_seek:position_seek;bool ok=arc_riscpc_hd_write(s,8192,1,p);
        assert(ok==!mode);if(ok)check_sector(s,8192,0x5a);
        else{const char *diagnostic=arc_riscpc_hd_diagnostic(s);assert(diagnostic&&strstr(diagnostic,"operation=tell "));
            int j=find("/save/arcdroid/hd-riscpc/0123456789012345678901234567890123456789/journal.a",false);
            assert(j>=0&&files[j].size==256);}
        vfs.seek=test_seek;arc_riscpc_hd_close(s);}
    /* Every append failure reports its actual step, even after close cleanup.
     * A short write is injected at each header/payload/trailer separately. */
    const char *operations[]={"open","seek","tell","write","write","write","flush","close","stat"};
    for(unsigned at=1;at<=sizeof(operations)/sizeof(*operations);++at){
        fresh_files();s=open_store(true);idle(s);reset_fault();fail_at=at;
        assert(!arc_riscpc_hd_write(s,8192,1,p));const char *diagnostic=arc_riscpc_hd_diagnostic(s);
        char expected[32];snprintf(expected,sizeof(expected),"operation=%s ",operations[at-1]);
        assert(diagnostic&&strstr(diagnostic,expected));assert(arc_riscpc_hd_notice(s));
        ArcRpcHdStats a;arc_riscpc_hd_stats(s,&a);assert(a.read_only);reset_fault();arc_riscpc_hd_close(s);}
    fresh_files();s=open_store(true);idle(s);vfs.tell=NULL;
    assert(!arc_riscpc_hd_write(s,8192,1,p));const char *diagnostic=arc_riscpc_hd_diagnostic(s);
    assert(diagnostic&&strstr(diagnostic,"operation=tell "));vfs.tell=test_tell;arc_riscpc_hd_close(s);
    /* Smallest records maximize replay callbacks. Confirm tell accounting
     * still fits the accepted open bound at the aggregate journal cap. */
    fresh_files();s=open_store(true);idle(s);unsigned commands=0;
    while(arc_riscpc_hd_write(s,8192,1,p)){++commands;assert(commands<14000);}
    assert(commands>13000);arc_riscpc_hd_close(s);s=open_store(false);
    ArcRpcHdStats near_cap;arc_riscpc_hd_stats(s,&near_cap);
    assert(!near_cap.read_only&&near_cap.open_bytes<=8437256&&near_cap.open_operations-near_cap.advisory_operations<=49152);
    printf("minimum-record near-cap open: commands=%u bytes=%" PRIu64 " operations=%" PRIu64 "\n",
        commands,near_cap.open_bytes,near_cap.open_operations-near_cap.advisory_operations);
    check_sector(s,8192,0x5a);arc_riscpc_hd_close(s);
    puts("RetroArch VFS: seek-zero/position command/replay; per-handle size isolation; exact tell and first-failure diagnostics");
}
static void bridge_accounting(void)
{
    /* Independent VFS callback count includes make_path's internal metadata,
     * advisory directory callbacks and first-publication I/O. */
    for(unsigned blank=0;blank<2;++blank){
        fresh_files();reset_fault();ArcRpcHdStore *s=open_store(blank!=0);ArcRpcHdStats a;
        arc_riscpc_hd_stats(s,&a);assert(a.operations==steps);
        if(!blank)assert(a.open_operations==steps);
        else{idle(s);uint8_t p[512];fill(p,1,0x75);assert(arc_riscpc_hd_write(s,800,1,p));
            arc_riscpc_hd_stats(s,&a);assert(a.operations==steps);}
        arc_riscpc_hd_close(s);
    }
    puts("bridge accounting: all normal VFS callbacks, including directory creation, counted exactly");
}
static void store(void)
{
    fresh_files();ArcRpcHdStore *s=open_store(true);idle(s);bounds(s);
    uint8_t in[131072],out[131072];memset(in,0,512);assert(arc_riscpc_hd_read(s,1024,1,out)&&!memcmp(in,out,512));
    fill(in,256,0xa5);assert(arc_riscpc_hd_write(s,511,256,in));assert(arc_riscpc_hd_read(s,511,256,out)&&!memcmp(in,out,sizeof(in)));
    assert(!arc_riscpc_hd_write(s,258047,2,in));assert(arc_riscpc_hd_write(s,258047,1,in));check_sector(s,258047,0xa5);
    for(unsigned i=0;i<10;++i){fill(in,1,(uint8_t)(i+1));assert(arc_riscpc_hd_write(s,1024+i*512,1,in));check_sector(s,1024+i*512,(uint8_t)(i+1));}
    for(unsigned i=0;i<10;++i)check_sector(s,1024+i*512,(uint8_t)(i+1));
    /* Trigger, freeze, write to an already materialized chunk, then publish. */
    fill(in,256,0x33);for(unsigned i=0;i<8;++i)assert(arc_riscpc_hd_write(s,10000+i*256,256,in));
    until_phase(s,ARC_RPC_HD_MATERIALIZE,0);fill(in,1,0x77);assert(arc_riscpc_hd_write(s,511,1,in));idle(s);check_sector(s,511,0x77);check_sector(s,512,0xa5);bounds(s);
    assert(arc_riscpc_hd_read(s,122368,1,out)&&out[4]==9);
    arc_riscpc_hd_close(s);s=open_store(false);idle(s);check_sector(s,511,0x77);check_sector(s,512,0xa5);for(unsigned i=0;i<10;++i)check_sector(s,1024+i*512,(uint8_t)(i+1));bounds(s);arc_riscpc_hd_close(s);
    /* Missing referenced chunk is damage; an explicit hole remains zero. */
    fresh_files();s=open_store(true);idle(s);arc_riscpc_hd_close(s);
    int chunk=-1;for(unsigned i=0;i<TEST_FILES;++i)if(files[i].present&&strstr(files[i].path,"/c000.g"))chunk=(int)i;
    assert(chunk>=0);files[chunk].present=false;s=open_store(false);assert(!arc_riscpc_hd_read(s,6,1,out));assert(arc_riscpc_hd_notice(s));ArcRpcHdStats a;arc_riscpc_hd_stats(s,&a);assert(a.read_only);arc_riscpc_hd_close(s);
    fresh_files();s=open_store(true);idle(s);arc_riscpc_hd_close(s);chunk=-1;for(unsigned i=0;i<TEST_FILES;++i)if(files[i].present&&strstr(files[i].path,"/c000.g"))chunk=(int)i;assert(chunk>=0);files[chunk].data[128+30000]^=1;
    s=open_store(false);assert(!arc_riscpc_hd_read(s,6,1,out));arc_riscpc_hd_close(s);
    /* The checkpoint is exactly the frozen prefix, even when the successor
     * overwrites a sector before its first reconstruction (QA S21). */
    fresh_files();s=open_store(true);idle(s);fill(in,256,0x35);
    for(unsigned i=0;i<9;++i)assert(arc_riscpc_hd_write(s,2048+i*512,256,in));
    until_phase(s,ARC_RPC_HD_MATERIALIZE,0);fill(in,1,0x93);
    assert(arc_riscpc_hd_write(s,2048,1,in));idle(s);
    int frozen=find("/save/arcdroid/hd-riscpc/0123456789012345678901234567890123456789/c004.g0000000000000001",false);
    assert(frozen>=0);for(unsigned b=0;b<512;++b)assert(files[frozen].data[128+b]==0x35);
    check_sector(s,2048,0x93);check_sector(s,2049,0x35);bounds(s);arc_riscpc_hd_close(s);
    s=open_store(false);idle(s);check_sector(s,2048,0x93);check_sector(s,2049,0x35);
    for(unsigned i=1;i<9;++i)check_sector(s,2048+i*512,0x35);arc_riscpc_hd_close(s);
    puts("rpc-hd-store: frozen prefix before reconstruction, successor overwrite and reopen pass");
}
static void population(void)
{
    fresh_files();ArcRpcHdStore *s=open_store(true);idle(s);uint8_t in[512];fill(in,1,0x41);assert(arc_riscpc_hd_write(s,1000,1,in));
    assert(arc_riscpc_hd_population_begin(s));assert(!arc_riscpc_hd_population_begin(s));assert(!arc_riscpc_hd_write(s,1000,1,in));
    ArcRpcHdStats before,after;arc_riscpc_hd_stats(s,&before);arc_riscpc_hd_tick(s);arc_riscpc_hd_stats(s,&after);assert(before.state==after.state&&before.generation==after.generation);
    fill(in,1,0x52);assert(arc_riscpc_hd_population_write(s,5000,1,in));assert(arc_riscpc_hd_population_seal(s));assert(arc_riscpc_hd_population_publish(s));
    check_sector(s,1000,0x41);check_sector(s,5000,0x52);idle(s);arc_riscpc_hd_close(s);s=open_store(false);idle(s);check_sector(s,1000,0x41);check_sector(s,5000,0x52);
    assert(arc_riscpc_hd_population_begin(s));fill(in,1,0x63);assert(arc_riscpc_hd_population_write(s,1000,1,in));arc_riscpc_hd_population_abort(s);idle(s);check_sector(s,1000,0x41);
    uint8_t large[131072];fill(large,256,0x21);for(unsigned i=0;i<8;++i)assert(arc_riscpc_hd_write(s,6000+i*256,256,large));
    assert(!arc_riscpc_hd_population_begin(s));arc_riscpc_hd_tick(s);assert(!arc_riscpc_hd_population_begin(s));arc_riscpc_hd_tick(s);assert(!arc_riscpc_hd_population_begin(s));idle(s);bounds(s);arc_riscpc_hd_close(s);
    s=open_store(false);idle(s);assert(arc_riscpc_hd_population_begin(s));
    fill(large,256,0x45);assert(arc_riscpc_hd_population_write(s,163840,256,large));
    fill(large,256,0);assert(arc_riscpc_hd_population_write(s,163840,256,large));
    assert(arc_riscpc_hd_population_seal(s)&&arc_riscpc_hd_population_publish(s));idle(s);check_sector(s,163840,0);
    unsigned physical=0;for(unsigned i=0;i<TEST_FILES;++i)if(files[i].present&&strstr(files[i].path,"/c")&&!strstr(files[i].path,"/chunk.tmp"))++physical;
    ArcRpcHdStats final;arc_riscpc_hd_stats(s,&final);assert(physical==final.chunk_files);bounds(s);arc_riscpc_hd_close(s);
    puts("rpc-hd-population: pass");
}
static void faults(void)
{
    fresh_files();ArcRpcHdStore *s=open_store(true);idle(s);uint8_t p[512];fill(p,1,0x34);assert(arc_riscpc_hd_write(s,800,1,p));arc_riscpc_hd_close(s);snapshot();
    s=open_store(false);idle(s);reset_fault();fill(p,1,0x56);assert(arc_riscpc_hd_write(s,900,1,p));unsigned operations=steps;arc_riscpc_hd_close(s);
    /* Every command invocation; short writes, seek/count/size/flush/close errors,
     * and termination between append and memory publication. */
    for(unsigned kind=0;kind<3;++kind)for(unsigned at=1;at<=operations;++at){restore();s=open_store(false);idle(s);check_sector(s,900,0);reset_fault();full_storage=kind==2;if(kind==1)kill_at=at;else fail_at=at;
        bool acknowledged=arc_riscpc_hd_write(s,900,1,p);if(kind!=1)assert(!acknowledged);
        ArcRpcHdStats committed;arc_riscpc_hd_stats(s,&committed);assert(committed.sequence==(acknowledged?2u:1u));if(!acknowledged){assert(arc_riscpc_hd_notice(s));check_sector(s,900,0);}arc_riscpc_hd_close(s);reset_fault();s=open_store(false);check_sector(s,800,0x34);
        if(acknowledged)check_sector(s,900,0x56);bounds(s);arc_riscpc_hd_close(s);}
    restore();s=open_store(false);idle(s);arc_riscpc_hd_close(s);int j=-1;for(unsigned i=0;i<TEST_FILES;++i)if(files[i].present&&strstr(files[i].path,"/journal.a"))j=(int)i;assert(j>=0);
    files[j].data[files[j].size-5]^=1;assert(!arc_riscpc_hd_open("/save",id,true,error,sizeof(error))&&error[0]);
    restore();int m=-1;for(unsigned i=0;i<TEST_FILES;++i)if(files[i].present&&strstr(files[i].path,"/manifest.a"))m=(int)i;assert(m>=0);files[m].data[160]^=1;
    assert(!arc_riscpc_hd_open("/save",id,true,error,sizeof(error))&&error[0]);
    restore();vfs.flush=NULL;s=open_store(false);ArcRpcHdStats a;arc_riscpc_hd_stats(s,&a);assert(a.read_only);assert(!arc_riscpc_hd_write(s,900,1,p));check_sector(s,800,0x34);arc_riscpc_hd_close(s);vfs.flush=test_flush;
    /* Cap admission is atomic and occurs before any append. */
    restore();s=open_store(false);idle(s);uint8_t large[131072];fill(large,256,0x87);unsigned commits=0;
    while(arc_riscpc_hd_write(s,(commits*256)%258048,256,large)){++commits;assert(commits<100);bounds(s);}
    arc_riscpc_hd_stats(s,&a);assert(a.read_only&&a.journal_high<=8388608&&arc_riscpc_hd_notice(s));arc_riscpc_hd_close(s);
    s=open_store(false);arc_riscpc_hd_stats(s,&a);assert(a.open_bytes<=8437256&&a.open_operations-a.advisory_operations<=49152);arc_riscpc_hd_close(s);
    puts("rpc-hd-store-faults: command fault boundaries and corruption/cap pass");
}
static int named(const char *leaf)
{for(unsigned i=0;i<TEST_FILES;++i)if(files[i].present&&!strcmp(strrchr(files[i].path,'/')+1,leaf))return (int)i;assert(0);return -1;}
static void put32(uint8_t *p,uint32_t value)
{for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(value>>(8*i));}
static void put64(uint8_t *p,uint64_t value)
{for(unsigned i=0;i<8;++i)p[i]=(uint8_t)(value>>(8*i));}
static void repair(uint8_t *p,unsigned bytes)
{put32(p+bytes-4,arc_crc32(p,bytes-4));}
static void trust_boundaries(void)
{
    fresh_files();ArcRpcHdStore *s=open_store(true);idle(s);uint8_t p[131072];fill(p,1,0x71);
    assert(arc_riscpc_hd_write(s,800,1,p));arc_riscpc_hd_close(s);snapshot();
    const unsigned manifest_fields[]={8,16,56,60,64,88,96,104,112};
    for(unsigned i=0;i<sizeof(manifest_fields)/sizeof(*manifest_fields);++i){restore();int m=named("manifest.a");
        files[m].data[manifest_fields[i]]^=1;repair(files[m].data,24324);
        assert(!arc_riscpc_hd_open("/save",id,true,error,sizeof(error))&&error[0]);}
    const unsigned header_fields[]={8,16,56,60,64,88,96,192,196};
    for(unsigned i=0;i<sizeof(header_fields)/sizeof(*header_fields);++i){restore();int j=named("journal.a");
        files[j].data[header_fields[i]]^=1;repair(files[j].data,256);
        assert(!arc_riscpc_hd_open("/save",id,true,error,sizeof(error))&&error[0]);}
    for(unsigned i=0;i<3;++i){restore();int j=named("journal.a");uint8_t *record=files[j].data+256;
        if(i<2)put32(record+68,i?257:0);else{put32(record+64,258047);put32(record+68,2);put32(record+72,1024);}
        repair(record,80);assert(!arc_riscpc_hd_open("/save",id,false,error,sizeof(error))&&error[0]);}
    /* Two CRC-valid roots that bind the same journal are a conflict, not a
     * license to choose by generation and delete the unexplained inventory. */
    restore();int m=named("manifest.a"),other=find("/save/arcdroid/hd-riscpc/0123456789012345678901234567890123456789/manifest.b",true);
    files[other].data=malloc(24324);assert(files[other].data);files[other].size=files[other].capacity=24324;
    memcpy(files[other].data,files[m].data,24324);files[other].data[88]=1;repair(files[other].data,24324);
    assert(!arc_riscpc_hd_open("/save",id,false,error,sizeof(error))&&error[0]);
    /* Refuse a generation rollover, preserving the exact last valid root. */
    restore();m=named("manifest.a");int jmax=named("journal.a");
    put64(files[m].data+88,UINT64_MAX);put64(files[m].data+96,UINT64_MAX);repair(files[m].data,24324);
    uint8_t *h=files[jmax].data;files[jmax].size=256;
    put64(h+88,UINT64_MAX);put64(h+96,UINT64_MAX-1);put64(h+104,UINT64_MAX-1);
    put64(h+152,256);put32(h+192,2);repair(h,256);
    char *suffix=strrchr(files[m].path,'.');suffix[1]='b';suffix=strrchr(files[jmax].path,'.');suffix[1]='b';
    s=open_store(false);assert(!arc_riscpc_hd_population_begin(s));
    fill(p,256,0x62);for(unsigned i=0;i<8;++i)assert(arc_riscpc_hd_write(s,2048+i*256,256,p));
    arc_riscpc_hd_tick(s);arc_riscpc_hd_tick(s);ArcRpcHdStats a;arc_riscpc_hd_stats(s,&a);
    assert(a.read_only&&a.generation==UINT64_MAX&&arc_riscpc_hd_notice(s));
    ArcIdeBackend backend;arc_riscpc_hd_backend(s,&backend);backend.failed(backend.opaque,"again");assert(!arc_riscpc_hd_notice(s));
    const char *diagnostic=arc_riscpc_hd_diagnostic(s);assert(diagnostic&&strstr(diagnostic,"phase=ROTATE")&&strstr(diagnostic,"journals="));
    arc_riscpc_hd_close(s);
    /* Case-folded native lookup, advisory errors, and truthful foreign aliases. */
    restore();case_folded=true;silent_readdir=true;s=open_store(false);idle(s);check_sector(s,800,0x71);arc_riscpc_hd_close(s);
    restore();silent_readdir=false;m=named("manifest.a");char *leaf=strrchr(files[m].path,'/')+1;leaf[0]='M';
    assert(!arc_riscpc_hd_open("/save",id,false,error,sizeof(error))&&error[0]);
    restore();silent_readdir=false;find("/save/arcdroid/hd-riscpc/0123456789012345678901234567890123456789/c000.g00000000000000fe",true);
    assert(!arc_riscpc_hd_open("/save",id,false,error,sizeof(error))&&error[0]);
    silent_readdir=true;s=open_store(false);idle(s);check_sector(s,800,0x71);arc_riscpc_hd_close(s);
#ifdef ARCDROID_HD_TEST_ALLOC
    restore();out_of_memory=true;assert(!arc_riscpc_hd_open("/save",id,true,error,sizeof(error))&&error[0]);
    s=open_store(false);check_sector(s,800,0x71);arc_riscpc_hd_close(s);
#endif
    restore();vfs.truncate=NULL;int j=named("journal.a");files[j].size-=8;s=open_store(false);arc_riscpc_hd_stats(s,&a);
    assert(a.read_only&&arc_riscpc_hd_notice(s));check_sector(s,800,0);arc_riscpc_hd_close(s);vfs.truncate=test_truncate;
    puts("trust boundaries: key/revisions/geometry, range, conflicting roots, generation wrap, OOM, aliases, failed listing and missing truncate pass");
}
static void pre_use_verification(void)
{
    uint8_t p[131072];fill(p,256,0x74);ArcRpcHdStats a;
    for(unsigned full=0;full<2;++full){fresh_files();ArcRpcHdStore *s=open_store(true);idle(s);
        int c=named("c000.g0000000000000000");files[c].data[128+30000]^=1;
        assert(arc_riscpc_hd_write(s,0,full?256:1,p));
        if(full)assert(arc_riscpc_hd_write(s,256,256,p));
        for(unsigned i=0;i<8;++i)assert(arc_riscpc_hd_write(s,2048+i*256,256,p));
        until_phase(s,ARC_RPC_HD_MATERIALIZE,0);arc_riscpc_hd_tick(s);arc_riscpc_hd_stats(s,&a);
        assert(a.state==ARC_RPC_HD_DAMAGED&&a.read_only&&arc_riscpc_hd_notice(s));arc_riscpc_hd_close(s);}
    fresh_files();ArcRpcHdStore *s=open_store(true);idle(s);assert(arc_riscpc_hd_population_begin(s));
    assert(arc_riscpc_hd_population_write(s,2048,256,p));int c=named("c000.g0000000000000000");files[c].data[128+30000]^=1;
    assert(!arc_riscpc_hd_population_seal(s));arc_riscpc_hd_stats(s,&a);assert(a.state==ARC_RPC_HD_DAMAGED);
    arc_riscpc_hd_population_abort(s);arc_riscpc_hd_close(s);
    fresh_files();s=open_store(true);idle(s);assert(arc_riscpc_hd_population_begin(s));
    assert(arc_riscpc_hd_population_write(s,2048,256,p)&&arc_riscpc_hd_population_seal(s));
    int m=named("manifest.a");files[m].data[160]^=1;
    assert(!arc_riscpc_hd_population_publish(s));arc_riscpc_hd_stats(s,&a);assert(a.state==ARC_RPC_HD_DAMAGED&&a.generation==0);
    arc_riscpc_hd_population_abort(s);arc_riscpc_hd_close(s);
    puts("pre-use integrity: partial/full overwrite and population source corruption refuse before publication");
}
static void open_and_creation_faults(void)
{
    fresh_files();silent_readdir=true;ArcRpcHdStore *s=open_store(true);idle(s);uint8_t p[512];fill(p,1,0x71);
    assert(arc_riscpc_hd_write(s,800,1,p));arc_riscpc_hd_close(s);snapshot();
    reset_fault();s=open_store(false);unsigned operations=steps;select_callbacks(operations);arc_riscpc_hd_close(s);
    for(unsigned mode=0;mode<3;++mode)for(unsigned at=1;at<=operations;++at){if(!callback_selected[at])continue;restore();silent_readdir=true;
        full_storage=mode==2;if(mode==1)kill_at=at;else fail_at=at;
        s=arc_riscpc_hd_open("/save",id,false,error,sizeof(error));if(!s)assert(error[0]);else arc_riscpc_hd_close(s);
        reset_fault();s=open_store(false);check_sector(s,800,0x71);bounds(s);arc_riscpc_hd_close(s);
    }
    printf("open matrix: %u invocations, all earlier acknowledgements retained\n",operations);fflush(stdout);
    fresh_files();silent_readdir=true;snapshot();reset_fault();s=open_store(true);unsigned creation_steps=steps;select_callbacks(creation_steps);arc_riscpc_hd_close(s);
    for(unsigned mode=0;mode<3;++mode)for(unsigned at=1;at<=creation_steps;++at){if(!callback_selected[at])continue;restore();silent_readdir=true;
        full_storage=mode==2;if(mode==1)kill_at=at;else fail_at=at;
        s=arc_riscpc_hd_open("/save",id,true,error,sizeof(error));if(!s)assert(error[0]);else arc_riscpc_hd_close(s);
        reset_fault();s=open_store(true);idle(s);uint8_t boot[512];assert(arc_riscpc_hd_read(s,6,1,boot)&&boot[3]==0x20);
        bounds(s);arc_riscpc_hd_close(s);
    }
    printf("authored creation matrix: %u invocations, short/full-storage/failure/termination recover complete blank or unpublished candidate\n",creation_steps);fflush(stdout);
}
/* The integrity columns apply to retained objects at every state, including
 * the retiring root. Private candidate corruption is covered by publication
 * readbacks and the repeated-operation failure sweeps. */
static void phase_corruption_matrix(void)
{
    unsigned cases=0;
    for(unsigned phase=0;phase<10;++phase)for(unsigned object=0;object<3;++object){
        fresh_files();silent_readdir=true;ArcRpcHdStore *s=open_store(true);idle(s);
        uint8_t p[131072];fill(p,256,0x79);ArcRpcHdStats a;
        if(phase<7){
            for(unsigned i=0;i<8;++i)assert(arc_riscpc_hd_write(s,0,256,p));
            unsigned ticks=0;do{arc_riscpc_hd_stats(s,&a);if(a.state==phase)break;
                assert(++ticks<1000&&!a.read_only);arc_riscpc_hd_tick(s);}while(1);
            assert(arc_riscpc_hd_write(s,800,1,p));
        }else{
            assert(arc_riscpc_hd_write(s,800,1,p));
            assert(arc_riscpc_hd_population_begin(s));
            assert(arc_riscpc_hd_population_write(s,0,256,p));
            if(phase>=8)assert(arc_riscpc_hd_population_seal(s));
            if(phase==9)assert(arc_riscpc_hd_population_publish(s));
        }
        arc_riscpc_hd_stats(s,&a);unsigned slot=(unsigned)(a.generation&1);
        char root[16];snprintf(root,sizeof(root),"manifest.%c",'a'+slot);
        arc_riscpc_hd_close(s);int m=named(root),damaged=-1;
        if(object==0){
            const uint8_t *e=files[m].data+128;assert(e[0]==1);uint64_t generation=0;
            for(unsigned i=0;i<8;++i)generation|=(uint64_t)e[8+i]<<(i*8);
            char leaf[23];snprintf(leaf,sizeof(leaf),"c000.g%016" PRIx64,generation);
            damaged=named(leaf);files[damaged].data[128+30000]^=1;
        }else if(object==1){damaged=m;files[m].data[160]^=1;}
        else{
            for(unsigned i=0;i<TEST_FILES;++i)if(files[i].present&&files[i].size>256&&
                strstr(files[i].path,"/journal.")){damaged=(int)i;break;}
            assert(damaged>=0);files[damaged].data[256+80]^=1;
        }
        size_t physical=files[damaged].size;uint8_t value=files[damaged].data[object==0?128+30000:object==1?160:256+80];
        reset_fault();s=arc_riscpc_hd_open("/save",id,false,error,sizeof(error));
        if(object==0){
            assert(s);uint8_t sector[512];assert(!arc_riscpc_hd_read(s,0,1,sector));
            arc_riscpc_hd_stats(s,&a);assert(a.read_only&&a.state==ARC_RPC_HD_DAMAGED&&arc_riscpc_hd_notice(s));
            arc_riscpc_hd_close(s);
        }else assert(!s&&error[0]);
        assert(files[damaged].present&&files[damaged].size==physical&&
            files[damaged].data[object==0?128+30000:object==1?160:256+80]==value);
        ++cases;
    }
    printf("integrity matrix: %u checkpoint/population phase/object cases preserve damaged files and refuse use\n",cases);
}
static bool population_action(ArcRpcHdStore *s)
{
    uint8_t p[131072];fill(p,256,0x42);
    return arc_riscpc_hd_population_begin(s)&&arc_riscpc_hd_population_write(s,2048,256,p)&&
        arc_riscpc_hd_population_write(s,2048+256,256,p)&&arc_riscpc_hd_population_write(s,4096,256,p)&&
        arc_riscpc_hd_population_seal(s)&&arc_riscpc_hd_population_publish(s);
}
static void population_fault_matrix(void)
{
    fresh_files();ArcRpcHdStore *s=open_store(true);idle(s);uint8_t p[512];fill(p,1,0x73);
    assert(arc_riscpc_hd_write(s,70000,1,p));arc_riscpc_hd_close(s);snapshot();
    s=open_store(false);reset_fault();assert(population_action(s));unsigned operations=steps;select_callbacks(operations);arc_riscpc_hd_close(s);
    for(unsigned mode=0;mode<3;++mode)for(unsigned at=1;at<=operations;++at){
        if(!callback_selected[at])continue;restore();silent_readdir=true;s=open_store(false);reset_fault();full_storage=mode==2;if(mode==1)kill_at=at;else fail_at=at;
        bool published=population_action(s);if(!published)arc_riscpc_hd_population_abort(s);
        arc_riscpc_hd_close(s);reset_fault();s=open_store(false);idle(s);check_sector(s,70000,0x73);
        ArcRpcHdStats a;arc_riscpc_hd_stats(s,&a);unsigned value=a.generation?0x42:0;
        check_sector(s,2048,value);check_sector(s,2304,value);check_sector(s,4096,value);
        bounds(s);arc_riscpc_hd_close(s);
    }
    printf("population matrix: %u invocations, failure and termination, source overlays retained\n",operations);
}
static void binding_fault(void)
{
    /* Corrupt successor parent binding with a valid header CRC. */
    fresh_files();ArcRpcHdStore *s=open_store(true);idle(s);uint8_t p[131072];fill(p,256,0x65);for(unsigned i=0;i<9;++i)assert(arc_riscpc_hd_write(s,2048+i*512,256,p));until_phase(s,ARC_RPC_HD_MATERIALIZE,0);arc_riscpc_hd_close(s);
    int j=-1;for(unsigned i=0;i<TEST_FILES;++i)if(files[i].present&&strstr(files[i].path,"/journal.b"))j=(int)i;
    assert(j>=0);files[j].data[120]^=1;uint32_t crc=arc_crc32(files[j].data,252);
    for(unsigned i=0;i<4;++i)files[j].data[252+i]=(uint8_t)(crc>>(i*8));
    assert(!arc_riscpc_hd_open("/save",id,false,error,sizeof(error))&&error[0]);
}
static void checkpoint_fault_matrix(void)
{
    fresh_files();ArcRpcHdStore *s=open_store(true);idle(s);uint8_t p[131072];fill(p,256,0x65);
    for(unsigned i=0;i<9;++i)assert(arc_riscpc_hd_write(s,2048+i*512,256,p));
    arc_riscpc_hd_close(s);snapshot();s=open_store(false);reset_fault();idle(s);
    unsigned operations=steps;select_callbacks(operations);arc_riscpc_hd_close(s);
    for(unsigned mode=0;mode<3;++mode)for(unsigned at=1;at<=operations;++at){
        if(!callback_selected[at])continue;restore();silent_readdir=true;s=open_store(false);reset_fault();full_storage=mode==2;if(mode==1)kill_at=at;else fail_at=at;
        ArcRpcHdStats a;for(unsigned t=0;t<100;++t){arc_riscpc_hd_tick(s);arc_riscpc_hd_stats(s,&a);if(killed||a.read_only||a.state==ARC_RPC_HD_IDLE)break;}
        if(a.read_only)assert(arc_riscpc_hd_notice(s));arc_riscpc_hd_close(s);reset_fault();
        s=open_store(false);idle(s);for(unsigned i=0;i<9;++i)check_sector(s,2048+i*512,0x65);
        bounds(s);arc_riscpc_hd_close(s);
    }
    printf("checkpoint matrix: %u invocations, failure and termination, all earlier commits retained\n",operations);
    /* A genuinely incomplete record recovers its prefix; complete bad CRC does not. */
    restore();s=open_store(false);idle(s);fill(p,1,0x81);assert(arc_riscpc_hd_write(s,60000,1,p));arc_riscpc_hd_close(s);
    int j=-1;for(unsigned i=0;i<TEST_FILES;++i)if(files[i].present&&strstr(files[i].path,"/journal.b"))j=(int)i;assert(j>=0);
    files[j].size-=8;s=open_store(false);assert(arc_riscpc_hd_notice(s));check_sector(s,60000,0);idle(s);arc_riscpc_hd_close(s);
}
/* D-147: terminate after each removal while the old root is an inventory.
 * Reopen has no listing; already removed old chunks must not be diagnosed as
 * selected-root damage. Both checkpoint and population use this sequence. */
static void retirement(bool populate)
{
    fresh_files();ArcRpcHdStore *s=open_store(true);idle(s);uint8_t p[131072];
    assert(arc_riscpc_hd_population_begin(s));fill(p,256,0x31);
    for(unsigned i=0;i<20;++i)assert(arc_riscpc_hd_population_write(s,2048+i*512,256,p));
    assert(arc_riscpc_hd_population_seal(s)&&arc_riscpc_hd_population_publish(s));idle(s);
    if(populate){assert(arc_riscpc_hd_population_begin(s));fill(p,256,0x42);
        for(unsigned i=0;i<20;++i)assert(arc_riscpc_hd_population_write(s,2048+i*512,256,p));
        assert(arc_riscpc_hd_population_seal(s)&&arc_riscpc_hd_population_publish(s));}
    else{fill(p,256,0x42);for(unsigned i=0;i<20;++i)assert(arc_riscpc_hd_write(s,2048+i*512,256,p));
        ArcRpcHdStats a;do{arc_riscpc_hd_tick(s);arc_riscpc_hd_stats(s,&a);}while(a.state!=ARC_RPC_HD_RETIRE);}
    arc_riscpc_hd_close(s);snapshot();
    s=open_store(false);reset_fault();idle(s);unsigned operations=steps;arc_riscpc_hd_close(s);
    /* Every retirement operation, including all 20 old-only removals, both
     * batches, manifest removal, and the following journal removal. */
    for(unsigned at=1;at<=operations;++at){restore();silent_readdir=true;s=open_store(false);reset_fault();kill_at=at;
        for(unsigned t=0;t<100&&!killed;++t)arc_riscpc_hd_tick(s);assert(killed);
        arc_riscpc_hd_close(s);reset_fault();s=open_store(false);idle(s);
        for(unsigned i=0;i<20;++i)check_sector(s,2048+i*512,0x42);
        uint8_t boot[512];assert(arc_riscpc_hd_read(s,6,1,boot)&&boot[3]==0x20);bounds(s);
        ArcRpcHdStats a;arc_riscpc_hd_stats(s,&a);assert(a.chunk_files==22);
        arc_riscpc_hd_close(s);
    }
    restore();silent_readdir=true;s=open_store(false);idle(s);bounds(s);arc_riscpc_hd_close(s);
    printf("retirement %s: %u operation kills recovered without listing\n",populate?"population":"checkpoint",operations);
}
static uint8_t expected[258048],touched[258048];
static void verify_stream(ArcRpcHdStore *s)
{
    uint8_t p[131072];
    for(unsigned lba=0;lba<258048;lba+=256){bool used=false;for(unsigned i=0;i<256;++i)used|=touched[lba+i]!=0;
        if(!used)continue;assert(arc_riscpc_hd_read(s,lba,256,p));
        for(unsigned i=0;i<256;++i)if(touched[lba+i])for(unsigned b=0;b<512;++b)assert(p[i*512+b]==expected[lba+i]);}
}
static void dump_stream(const char *filename)
{
    FILE *f=fopen(filename,"wb");assert(f);assert(fwrite(expected,1,sizeof(expected),f)==sizeof(expected));
    assert(fwrite(touched,1,sizeof(touched),f)==sizeof(touched));
    for(unsigned i=0;i<TEST_FILES;++i)if(files[i].present){uint32_t length=(uint32_t)strlen(files[i].path),bytes=(uint32_t)files[i].size;
        assert(fwrite(&length,4,1,f)==1&&fwrite(&bytes,4,1,f)==1&&fwrite(files[i].path,1,length,f)==length&&fwrite(files[i].data,1,bytes,f)==bytes);}
    uint32_t end=0;assert(fwrite(&end,4,1,f)==1&&fclose(f)==0);
}
static void reopen_stream(const char *filename)
{
    fresh_files();silent_readdir=true;FILE *f=fopen(filename,"rb");assert(f);
    assert(fread(expected,1,sizeof(expected),f)==sizeof(expected)&&fread(touched,1,sizeof(touched),f)==sizeof(touched));
    uint32_t length;while(fread(&length,4,1,f)==1&&length){uint32_t bytes;char path[1100];assert(length<sizeof(path)&&fread(&bytes,4,1,f)==1&&bytes<=8388608);
        assert(fread(path,1,length,f)==length);path[length]=0;int i=find(path,true);assert(i>=0);files[i].data=malloc(bytes);files[i].size=files[i].capacity=bytes;
        assert(fread(files[i].data,1,bytes,f)==bytes);}
    assert(fclose(f)==0);ArcRpcHdStore *s=open_store(false);idle(s);verify_stream(s);bounds(s);arc_riscpc_hd_close(s);
    puts("stream fresh process: all retained command bytes recovered without listing");
}
static void stream(bool scatter,bool silent,const char *dump,bool sanitizer)
{
    fresh_files();memset(expected,0,sizeof(expected));memset(touched,0,sizeof(touched));silent_readdir=silent;
    ArcRpcHdStore *s=open_store(true);idle(s);uint8_t p[131072],q[131072];
    /* The sanitizer profile shortens duration and starts near the disc end to
     * retain clipping/wrap coverage. Commands, burst and assertions are shared. */
    uint64_t bytes=0,target=(sanitizer?(scatter?UINT64_C(12):UINT64_C(16)):
        (scatter?UINT64_C(64):UINT64_C(512)))*1048576;
    uint32_t at=scatter?0:(sanitizer?ARC_RPC_HD_SECTORS-17:17),random=0x13579bdf;
    unsigned frames=0,burst_start=UINT32_MAX,burst_commands=0,wraps=0;
    uint64_t checkpoint=0,burst_bytes=0,window_bytes=0,window[600]={0};clock_t begin=clock();
    unsigned cycle_ticks=0,max_cycle_ticks=0;uint64_t materialized_before=0;ArcRpcHdStats previous;arc_riscpc_hd_stats(s,&previous);
    while(bytes<target){unsigned commands=scatter?8:1;uint64_t frame_bytes=0;
        /* Start the exact 2 MiB burst while the inactive ROTATE writer is
         * prepared. Sixteen whole commands are distributed over sixty frames. */
        if(burst_start==UINT32_MAX&&previous.state==ARC_RPC_HD_ROTATE&&previous.phase_step==1)burst_start=frames;
        unsigned elapsed=frames-burst_start;
        bool extra=burst_start!=UINT32_MAX&&burst_commands<16&&
            ((burst_commands<15&&elapsed==4*burst_commands)||(burst_commands==15&&elapsed==59));
        for(unsigned c=0;c<commands+(unsigned)extra;++c){bool burst=c==commands;
            unsigned count=burst?(burst_commands<13?256:255):(scatter?1:32);
            if(scatter){random=random*1664525u+1013904223u;at=burst?(random%80)*512:random%(80*512);}
            if(at+count>ARC_RPC_HD_SECTORS)count=ARC_RPC_HD_SECTORS-at;
            uint8_t value=(uint8_t)(frames+c+1);fill(p,count,value);
            assert(arc_riscpc_hd_write(s,at,count,p));assert(arc_riscpc_hd_read(s,at,count,q)&&!memcmp(p,q,count*512));
            for(unsigned i=0;i<count;++i){expected[at+i]=value;touched[at+i]=1;}
            bytes+=count*512;frame_bytes+=96+count*512;
            if(burst){burst_bytes+=96+count*512;++burst_commands;}
            if(at+count==ARC_RPC_HD_SECTORS)++wraps;at=(at+count)%ARC_RPC_HD_SECTORS;
        }
        window_bytes-=window[frames%600];window[frames%600]=frame_bytes;window_bytes+=frame_bytes;
        if(frames>=599)assert(window_bytes>=UINT64_C(4300)*600);
        arc_riscpc_hd_tick(s);bounds(s);ArcRpcHdStats current;arc_riscpc_hd_stats(s,&current);
        assert(current.journal_high<=(scatter?UINT64_C(6961664):UINT64_C(7700864)));
        if(previous.state==ARC_RPC_HD_IDLE&&current.state==ARC_RPC_HD_ROTATE){cycle_ticks=1;materialized_before=current.materialized;}
        else if(cycle_ticks)++cycle_ticks;
        if(cycle_ticks&&current.state==ARC_RPC_HD_IDLE){unsigned n=current.materialized-(unsigned)materialized_before;
            assert(n<=(scatter?80u:25u)&&cycle_ticks<=6*n+16);
            if(cycle_ticks>max_cycle_ticks)max_cycle_ticks=cycle_ticks;cycle_ticks=0;}
        if(current.checkpoints!=checkpoint){verify_stream(s);checkpoint=current.checkpoints;}
        previous=current;++frames;
    }
    assert(burst_commands==16&&burst_bytes==2097152);idle(s);verify_stream(s);bounds(s);if(dump)dump_stream(dump);
    ArcRpcHdStats a;arc_riscpc_hd_stats(s,&a);arc_riscpc_hd_close(s);s=open_store(false);idle(s);bounds(s);arc_riscpc_hd_close(s);
    printf("stream %s bytes=%" PRIu64 " frames=%u burst_journal=%" PRIu64 " max_cycle_ticks=%u journal_high=%" PRIu64 " allocation=%" PRIu64 " files=%u physical_files=%u physical_bytes=%" PRIu64 " temp_bytes=%" PRIu64 " tick_bytes=%" PRIu64 " tick_ops=%" PRIu64 " tick_metadata=%" PRIu64 " tick_hash=%" PRIu64 " tick_copy=%" PRIu64 " checkpoints=%" PRIu64 " wraps=%u cpu_s=%.3f\n",scatter?"scatter":"sequential",bytes,frames,burst_bytes,max_cycle_ticks,a.journal_high,a.allocation_high,a.max_chunk_files,physical_chunks_high,physical_high,temp_high,a.max_tick_bytes,a.max_tick_operations,a.max_tick_metadata_operations,a.max_tick_hash_bytes,a.max_tick_copied_bytes,a.checkpoints,wraps,(double)(clock()-begin)/CLOCKS_PER_SEC);fflush(stdout);
}
static void qa_integrity(void)
{
    uint8_t p[131072];fill(p,256,0x42);ArcRpcHdStore *s;ArcRpcHdStats a;
    /* Q04: the payload CRC remains valid; only SHA detects this change. */
    fresh_files();s=open_store(true);idle(s);arc_riscpc_hd_close(s);
    int c=named("c000.g0000000000000000");files[c].data[128+3000]^=0x40;
    put32(files[c].data+128+ARC_RPC_HD_CHUNK,arc_crc32(files[c].data+128,ARC_RPC_HD_CHUNK));
    s=open_store(false);assert(!arc_riscpc_hd_read(s,6,1,p));assert(arc_riscpc_hd_notice(s));arc_riscpc_hd_close(s);
    /* Q05: repair both sequence copies and header CRC, leaving a true gap. */
    fresh_files();s=open_store(true);idle(s);assert(arc_riscpc_hd_write(s,800,1,p));arc_riscpc_hd_close(s);
    int j=named("journal.a");uint8_t *r=files[j].data+256;put64(r+56,2);put64(r+80+512,2);repair(r,80);
    assert(!arc_riscpc_hd_open("/save",id,false,error,sizeof(error))&&error[0]);
    /* Q11: a well-framed successor bound to the wrong frozen prefix. */
    fresh_files();s=open_store(true);idle(s);for(unsigned i=0;i<8;++i)assert(arc_riscpc_hd_write(s,800+i*256,256,p));
    until_phase(s,ARC_RPC_HD_MATERIALIZE,0);arc_riscpc_hd_close(s);
    j=named("journal.b");files[j].data[160]^=1;repair(files[j].data,256);
    assert(!arc_riscpc_hd_open("/save",id,false,error,sizeof(error))&&error[0]);
    /* Q12: future chunk generation, with a valid manifest envelope. */
    fresh_files();s=open_store(true);idle(s);arc_riscpc_hd_close(s);int m=named("manifest.a");
    put64(files[m].data+128+8,1);repair(files[m].data,24324);
    c=named("c000.g0000000000000000");char *generation=strrchr(files[c].path,'g');generation[16]='1';
    put64(files[c].data+72,1);repair(files[c].data,128);
    assert(!arc_riscpc_hd_open("/save",id,false,error,sizeof(error))&&error[0]);
    /* Q16: interrupted private work, both with and without a published root.
     * Closing aborts ownership but does not run the queued GC tick. */
    for(unsigned published=0;published<2;++published){fresh_files();silent_readdir=true;s=open_store(published!=0);idle(s);
        assert(arc_riscpc_hd_population_begin(s));assert(arc_riscpc_hd_population_write(s,5120,1,p));
        arc_riscpc_hd_close(s);const char *leaf=published?"c00a.g0000000000000001":"c00a.g0000000000000000";
        (void)named(leaf);s=open_store(false);
        for(unsigned i=0;i<TEST_FILES;++i)assert(!files[i].present||strcmp(strrchr(files[i].path,'/')+1,leaf));
        arc_riscpc_hd_stats(s,&a);assert(!a.read_only&&a.published==(published!=0));arc_riscpc_hd_close(s);}
    puts("Q04/Q05/Q11/Q12/Q16: CRC-valid trust-boundary rejection and fixed-path debris cleanup pass");
}
static uint8_t import_pattern(uint32_t lba,unsigned byte)
{unsigned chunk=lba/512;return chunk%7==3?0:(uint8_t)(lba*31+byte*7+chunk+1);}
static void import_verify(ArcRpcHdStore *s,unsigned stride)
{uint8_t p[512];for(uint32_t lba=0;lba<ARC_RPC_HD_SECTORS;lba+=stride){assert(arc_riscpc_hd_read(s,lba,1,p));
    for(unsigned b=0;b<512;++b)assert(p[b]==import_pattern(lba,b));}}
static void import_roundtrip(void)
{
    fresh_files();int raw=find("/save/raw.img",true);assert(raw>=0);
    files[raw].data=malloc(ARC_RPC_HD_BYTES);assert(files[raw].data);files[raw].size=files[raw].capacity=ARC_RPC_HD_BYTES;
    for(uint32_t lba=0;lba<ARC_RPC_HD_SECTORS;++lba)for(unsigned b=0;b<512;++b)files[raw].data[(size_t)lba*512+b]=import_pattern(lba,b);
    uint8_t before[32],after[32];ArcSha256 hash;arc_sha256_init(&hash);arc_sha256_update(&hash,files[raw].data,ARC_RPC_HD_BYTES);arc_sha256_final(&hash,before);
    ArcRpcHdStore *s=open_store(false);assert(arc_riscpc_hd_import(s,"/save/raw.img"));idle(s);import_verify(s,1);
    uint8_t p[131072];for(unsigned k=0;k<12;++k){uint32_t lba=k*20000+5;
        for(unsigned b=0;b<sizeof(p);++b)p[b]=import_pattern(lba+b/512,b%512);
        assert(arc_riscpc_hd_write(s,lba,256,p));arc_riscpc_hd_tick(s);}
    idle(s);ArcRpcHdStats a;arc_riscpc_hd_stats(s,&a);assert(a.checkpoints>=2);bounds(s);arc_riscpc_hd_close(s);
    s=open_store(false);idle(s);import_verify(s,1);assert(!arc_riscpc_hd_import(s,"/save/raw.img"));import_verify(s,97);arc_riscpc_hd_close(s);
    arc_sha256_init(&hash);arc_sha256_update(&hash,files[raw].data,ARC_RPC_HD_BYTES);arc_sha256_final(&hash,after);assert(!memcmp(before,after,32));
    /* Pop-abort GC must advance even when its inventory begins with hundreds
     * of retained chunks, checking no more than 16 entries in a tick. */
    s=open_store(false);idle(s);assert(arc_riscpc_hd_population_begin(s));assert(arc_riscpc_hd_population_write(s,512,1,p));
    arc_riscpc_hd_population_abort(s);unsigned ticks=0;do{arc_riscpc_hd_tick(s);arc_riscpc_hd_stats(s,&a);assert(++ticks<100&&!a.read_only);}
    while(a.state!=ARC_RPC_HD_IDLE);assert(ticks>=27);import_verify(s,97);arc_riscpc_hd_close(s);
    puts("nonzero import: every sector, checkpoint, reopen, second import refusal, unchanged source and bounded abort GC pass");
}
static void reconstruction_failures(void)
{
    /* Reopen clears the resident cache; these sectors exist only in indexed
     * journal records. Exercise cleanup immediately, before close/reset can
     * obscure an open handle, with both adjacent and gapped records. */
    unsigned cases=0;
    for(unsigned gapped=0;gapped<2;++gapped){
        fresh_files();ArcRpcHdStore *s=open_store(true);idle(s);uint8_t p[512],out[512];fill(p,1,0x51);
        assert(arc_riscpc_hd_write(s,5120,1,p));
        if(gapped)assert(arc_riscpc_hd_write(s,51200,1,p));
        assert(arc_riscpc_hd_write(s,5121,1,p));arc_riscpc_hd_close(s);snapshot();
        s=open_store(false);reset_fault();assert(arc_riscpc_hd_read(s,5121,1,out));
        unsigned callbacks=steps;arc_riscpc_hd_close(s);
        for(unsigned at=1;at<=callbacks;++at){
            restore();s=open_store(false);reset_fault();fail_at=at;
            assert(!arc_riscpc_hd_read(s,5121,1,out));test_vfs_no_open_handles();
            assert(arc_riscpc_hd_notice(s));arc_riscpc_hd_close(s);++cases;
        }
        /* Each frame's integrity check must close the already-open journal.
         * Corrupt after open so recovery validation is not the refusal site. */
        for(unsigned record=0;record<2;++record){
            restore();s=open_store(false);int j=named("journal.a");
            unsigned offset=256+(record?(gapped?2:1)*608:0);files[j].data[offset+80]^=1;
            assert(!arc_riscpc_hd_read(s,5121,1,out));test_vfs_no_open_handles();
            assert(arc_riscpc_hd_notice(s));arc_riscpc_hd_close(s);++cases;
        }
    }
    /* The same reconstruction failure during a MATERIALIZE tick. */
    fresh_files();ArcRpcHdStore *s=open_store(true);idle(s);uint8_t p[131072];fill(p,256,0x62);
    assert(arc_riscpc_hd_write(s,5120,1,p));
    for(unsigned i=0;i<8;++i)assert(arc_riscpc_hd_write(s,51200+i*256,256,p));
    until_phase(s,ARC_RPC_HD_MATERIALIZE,0);
    int j=named("journal.a");files[j].data[256+80]^=1;
    arc_riscpc_hd_tick(s);test_vfs_no_open_handles();ArcRpcHdStats a;arc_riscpc_hd_stats(s,&a);
    assert(a.read_only&&arc_riscpc_hd_notice(s));arc_riscpc_hd_close(s);++cases;
    printf("handle cleanup: %u failed indexed reads/materialization cases, zero file/directory handles at every return and close\n",cases);
}
static void reconstruction_budget(void)
{
    fresh_files();ArcRpcHdStore *s=open_store(true);idle(s);uint8_t p[131072],out[512];fill(p,256,0x5a);
    for(unsigned i=0;i<512;++i)assert(arc_riscpc_hd_write(s,5120+i,1,p));
    for(unsigned i=0;i<9;++i)assert(arc_riscpc_hd_write(s,51200+i*256,256,p));
    for(unsigned i=0;i<8;++i)check_sector(s,(300+i)*512,0);
    ArcRpcHdStats a,b;arc_riscpc_hd_stats(s,&a);unsigned callbacks=steps;
    assert(arc_riscpc_hd_read(s,5121,1,out));arc_riscpc_hd_stats(s,&b);
    assert(b.operations-a.operations==steps-callbacks&&b.operations-a.operations<=516);
    for(unsigned i=0;i<512;++i)assert(out[i]==0x5a);
    uint64_t miss=b.operations-a.operations;uint64_t worst=0;do{arc_riscpc_hd_tick(s);arc_riscpc_hd_stats(s,&b);
        if(b.tick_operations>worst)worst=b.tick_operations;
        assert(b.tick_operations<=550&&!b.read_only);
    }while(b.state!=ARC_RPC_HD_IDLE);
    printf("512 single-sector reconstruction: miss_ops=%" PRIu64 " worst_tick_ops=%" PRIu64 "\n",miss,worst);
    arc_riscpc_hd_close(s);
    /* Nonadjacent indexed frames require seek/tell, but still only one open.
     * A live miss after ROTATE must retain sectors from both journal slots. */
    fresh_files();s=open_store(true);idle(s);
    for(unsigned i=0;i<512;++i){assert(arc_riscpc_hd_write(s,5120+i,1,p));
        assert(arc_riscpc_hd_write(s,51200,1,p));}
    for(unsigned i=0;i<8;++i)check_sector(s,(300+i)*512,0);
    arc_riscpc_hd_stats(s,&a);assert(arc_riscpc_hd_read(s,5121,1,out));arc_riscpc_hd_stats(s,&b);
    assert(b.operations-a.operations<=1540);uint64_t gaps=b.operations-a.operations;
    for(unsigned i=0;i<9;++i)assert(arc_riscpc_hd_write(s,51200+i*256,256,p));
    until_phase(s,ARC_RPC_HD_MATERIALIZE,0);
    fill(p,1,0x91);assert(arc_riscpc_hd_write(s,5121,1,p));
    for(unsigned i=0;i<8;++i)check_sector(s,(300+i)*512,0);
    arc_riscpc_hd_stats(s,&a);check_sector(s,5121,0x91);check_sector(s,5120,0x5a);arc_riscpc_hd_stats(s,&b);
    assert(b.operations-a.operations<=1550);
    do{arc_riscpc_hd_tick(s);arc_riscpc_hd_stats(s,&b);assert(b.tick_operations<=1600&&!b.read_only);}
    while(b.state!=ARC_RPC_HD_IDLE);
    check_sector(s,5121,0x91);check_sector(s,5120,0x5a);
    printf("gapped record miss_ops=%" PRIu64 "; two-journal effective view retained\n",gaps);
    arc_riscpc_hd_close(s);
    /* A populated base plus the maximally gapped frozen chunk exercises both
     * read handles: full base verification and all 512 contributing records. */
    fresh_files();s=open_store(true);idle(s);fill(p,256,0x61);
    assert(arc_riscpc_hd_population_begin(s));assert(arc_riscpc_hd_population_write(s,5120,256,p));
    assert(arc_riscpc_hd_population_seal(s)&&arc_riscpc_hd_population_publish(s));idle(s);fill(p,256,0x5a);
    for(unsigned i=0;i<512;++i){assert(arc_riscpc_hd_write(s,5120+i,1,p));assert(arc_riscpc_hd_write(s,51200,1,p));}
    for(unsigned i=0;i<8;++i)check_sector(s,(300+i)*512,0);
    arc_riscpc_hd_stats(s,&a);assert(arc_riscpc_hd_read(s,5121,1,out));arc_riscpc_hd_stats(s,&b);
    assert(b.operations-a.operations<=1600&&b.metadata_operations-a.metadata_operations==4);
    for(unsigned i=0;i<9;++i)assert(arc_riscpc_hd_write(s,51200+i*256,256,p));
    until_phase(s,ARC_RPC_HD_MATERIALIZE,0);arc_riscpc_hd_tick(s);arc_riscpc_hd_stats(s,&a);
    assert(!a.read_only&&a.tick_operations<=1600&&a.tick_metadata_operations==4);
    printf("512 gapped records plus verified base: tick_ops=%" PRIu64 " metadata=%" PRIu64 "\n",a.tick_operations,a.tick_metadata_operations);
    idle(s);for(unsigned half=0;half<2;++half){assert(arc_riscpc_hd_read(s,5120+half*256,256,p));
        for(unsigned i=0;i<131072;++i)assert(p[i]==0x5a);}
    bounds(s);arc_riscpc_hd_close(s);
}
/* D-158: interleaving at every persistent split, plus independent VFS budget. */
static void split_steps(void)
{
    fresh_files();ArcRpcHdStore *s=open_store(true);idle(s);uint8_t p[131072];fill(p,256,0x65);
    for(unsigned i=0;i<9;++i)assert(arc_riscpc_hd_write(s,2048+i*512,256,p));
    until_phase(s,ARC_RPC_HD_ROTATE,1);
    ArcRpcHdStats a;arc_riscpc_hd_stats(s,&a);assert(files[named("journal.b")].size==0);
    fill(p,1,0x7b);assert(arc_riscpc_hd_write(s,60000,1,p));arc_riscpc_hd_stats(s,&a);uint64_t snapshot_sequence=a.sequence;
    arc_riscpc_hd_tick(s);int j=named("journal.b");uint64_t bound=0;
    for(unsigned i=0;i<8;++i)bound|=(uint64_t)files[j].data[112+i]<<(8*i);
    assert(bound==snapshot_sequence); /* preparation must not bind S */
    unsigned seen[7]={0};uint64_t maxima[7][5]={{0}},namespace_maxima[7][5]={{0}};bool wrote=false;
    for(unsigned tick=0;tick<1000;++tick){arc_riscpc_hd_stats(s,&a);assert(!a.read_only);
        if(a.state==ARC_RPC_HD_IDLE)break;assert(a.state<7&&a.phase_step<5);seen[a.state]|=1u<<a.phase_step;
        /* Effective-cache eviction plus a miss at each step must never mutate
         * the frozen payload. The successor can overwrite that same sector. */
        if(a.state==ARC_RPC_HD_MATERIALIZE){
            for(unsigned i=0;i<8;++i)check_sector(s,(300+i)*512,0);
            if(a.phase_step==1&&!wrote){fill(p,1,0x92);assert(arc_riscpc_hd_write(s,2048,1,p));wrote=true;}
        }
        arc_riscpc_hd_tick(s);ArcRpcHdStats after;arc_riscpc_hd_stats(s,&after);
        if(after.tick_metadata_operations>maxima[a.state][a.phase_step])maxima[a.state][a.phase_step]=after.tick_metadata_operations;
        if(after.tick_namespace_operations>namespace_maxima[a.state][a.phase_step])namespace_maxima[a.state][a.phase_step]=after.tick_namespace_operations;
        bounds(s);assert(tick<999);
    }
    assert(wrote&&seen[ARC_RPC_HD_MATERIALIZE]==31&&seen[ARC_RPC_HD_MANIFEST]==3&&seen[ARC_RPC_HD_PUBLISH]==3&&seen[ARC_RPC_HD_RETIRE]==31);
    int c=named("c004.g0000000000000001");for(unsigned i=0;i<512;++i)assert(files[c].data[128+i]==0x65);
    check_sector(s,2048,0x92);check_sector(s,60000,0x7b);arc_riscpc_hd_close(s);
    s=open_store(false);idle(s);check_sector(s,2048,0x92);check_sector(s,60000,0x7b);arc_riscpc_hd_close(s);
    for(unsigned phase=2;phase<7;++phase)for(unsigned step=0;step<5;++step)if(seen[phase]&(1u<<step))
        printf("split phase=%u step=%u maximum_metadata=%" PRIu64 " maximum_namespace=%" PRIu64 "\n",phase,step,maxima[phase][step],namespace_maxima[phase][step]);
    /* Corruption between steps must be caught at the intended readback,
     * before the next publication or selected-root update. */
    for(unsigned which=0;which<4;++which){
        fresh_files();s=open_store(true);idle(s);fill(p,256,0x65);
        for(unsigned i=0;i<9;++i)assert(arc_riscpc_hd_write(s,2048+i*512,256,p));
        unsigned phase=which<2?ARC_RPC_HD_MATERIALIZE:which==2?ARC_RPC_HD_MANIFEST:ARC_RPC_HD_PUBLISH;
        unsigned step=which==0?2:which==1?4:1;until_phase(s,phase,step);
        const char *leaf=which<2?"chunk.tmp":which==2?"manifest.tmp":"manifest.b";int at=named(leaf);
        if(which<2){files[at].data[128+30000]^=1;put32(files[at].data+128+ARC_RPC_HD_CHUNK,arc_crc32(files[at].data+128,ARC_RPC_HD_CHUNK));}
        else files[at].data[160]^=1;
        arc_riscpc_hd_tick(s);arc_riscpc_hd_stats(s,&a);assert(a.read_only&&arc_riscpc_hd_notice(s));
        if(which==0)assert(files[at].present&&!strcmp(strrchr(files[at].path,'/')+1,"chunk.tmp"));
        assert(a.generation==0);arc_riscpc_hd_close(s);
    }
    /* Two old-only leaves expose a budget error if GC tries two removals and
     * the second stat fails: stat/remove + stat/open/close would be five. */
    fresh_files();s=open_store(true);idle(s);fill(p,256,0x31);assert(arc_riscpc_hd_population_begin(s));
    for(unsigned i=0;i<3;++i)assert(arc_riscpc_hd_population_write(s,2048+i*512,256,p));
    assert(arc_riscpc_hd_population_seal(s)&&arc_riscpc_hd_population_publish(s));idle(s);fill(p,256,0x42);
    for(unsigned i=0;i<9;++i)assert(arc_riscpc_hd_write(s,2048+(i%3)*512,256,p));
    until_phase(s,ARC_RPC_HD_GC,0);fail_at=steps+3;fault=10;
    arc_riscpc_hd_tick(s);arc_riscpc_hd_tick(s);arc_riscpc_hd_stats(s,&a);assert(a.read_only);
    arc_riscpc_hd_close(s);reset_fault();s=open_store(false);idle(s);check_sector(s,2048,0x42);arc_riscpc_hd_close(s);
    /* The new retirement boundary retains the parent alone. Writes between
     * the two removals must recover for checkpoint and population roots. */
    for(unsigned populate=0;populate<2;++populate){
        fresh_files();s=open_store(true);idle(s);fill(p,256,0x37);
        if(populate){assert(arc_riscpc_hd_population_begin(s));
            for(unsigned i=0;i<3;++i)assert(arc_riscpc_hd_population_write(s,2048+i*512,256,p));
            assert(arc_riscpc_hd_population_seal(s)&&arc_riscpc_hd_population_publish(s));}
        else for(unsigned i=0;i<9;++i)assert(arc_riscpc_hd_write(s,2048+(i%3)*512,256,p));
        until_phase(s,ARC_RPC_HD_RETIRE,3);arc_riscpc_hd_tick(s);arc_riscpc_hd_stats(s,&a);
        assert(a.state==ARC_RPC_HD_RETIRE&&a.phase_step==4&&a.tick_namespace_operations==1);
        unsigned roots=0,journals=0;for(unsigned i=0;i<TEST_FILES;++i)if(files[i].present){
            const char *leaf=strrchr(files[i].path,'/')+1;
            if(!strncmp(leaf,"manifest.",9))++roots;if(!strncmp(leaf,"journal.",8))++journals;}
        assert(roots==1&&journals==2);
        fill(p,1,0x94);assert(arc_riscpc_hd_write(s,90000,1,p));arc_riscpc_hd_close(s);
        s=open_store(false);idle(s);check_sector(s,90000,0x94);
        for(unsigned i=0;i<3;++i)check_sector(s,2048+i*512,0x37);bounds(s);arc_riscpc_hd_close(s);
    }
    /* Unload and a guest failure can occur while the empty writer is held. */
    for(unsigned failing=0;failing<2;++failing){
        fresh_files();s=open_store(true);idle(s);fill(p,256,0x65);
        for(unsigned i=0;i<9;++i)assert(arc_riscpc_hd_write(s,2048+i*512,256,p));
        until_phase(s,ARC_RPC_HD_ROTATE,1);
        if(failing){fault=7;fail_at=steps+7;fill(p,1,0x91);
            assert(!arc_riscpc_hd_write(s,70000,1,p));arc_riscpc_hd_stats(s,&a);
            assert(a.read_only&&arc_riscpc_hd_notice(s));}
        arc_riscpc_hd_close(s);reset_fault();s=open_store(false);idle(s);
        for(unsigned i=0;i<9;++i)check_sector(s,2048+i*512,0x65);
        arc_riscpc_hd_close(s);
    }
    puts("split boundaries: late S, private frozen payload, full readbacks, separate retirement removals, failed-stat GC budget, prepared-writer cleanup and recovery pass");
}
/* QA 3: several private leaves must be retired through ordinary GC ticks. */
static void abort_gc_leaves(void)
{
    fresh_files();silent_readdir=true;ArcRpcHdStore *s=open_store(true);idle(s);
    ArcRpcHdStats before,a;arc_riscpc_hd_stats(s,&before);uint8_t p[131072];fill(p,256,0x5c);
    assert(arc_riscpc_hd_population_begin(s));
    for(unsigned c=0;c<6;++c)assert(arc_riscpc_hd_population_write(s,2048+c*512,256,p));
    arc_riscpc_hd_stats(s,&a);assert(a.chunk_files==before.chunk_files+6);
    arc_riscpc_hd_population_abort(s);unsigned ticks=0,removed=0;
    do{unsigned leaves=a.chunk_files;arc_riscpc_hd_tick(s);arc_riscpc_hd_stats(s,&a);
        assert(!a.read_only&&++ticks<100&&a.chunk_files<=leaves&&leaves-a.chunk_files<=1);
        removed+=leaves-a.chunk_files;bounds(s);
    }while(a.state!=ARC_RPC_HD_IDLE);
    assert(ticks>=6&&removed==6&&a.chunk_files==before.chunk_files&&a.generation==before.generation);
    for(unsigned c=0;c<6;++c){char leaf[1100];snprintf(leaf,sizeof(leaf),
        "/save/arcdroid/hd-riscpc/%s/c%03x.g0000000000000001",id,c+4);
        assert(find(leaf,false)<0);check_sector(s,2048+c*512,0);}
    arc_riscpc_hd_close(s);s=open_store(false);idle(s);
    for(unsigned c=0;c<6;++c)check_sector(s,2048+c*512,0);bounds(s);arc_riscpc_hd_close(s);
    printf("abort GC: six private leaves, %u ticks, <=one removal per tick, unchanged source and reopen pass\n",ticks);
}
static ArcRpcHdStore *checkpoint_seed(void)
{
    fresh_files();ArcRpcHdStore *s=open_store(true);idle(s);uint8_t p[131072];fill(p,256,0x65);
    for(unsigned i=0;i<9;++i)assert(arc_riscpc_hd_write(s,2048+i*512,256,p));return s;
}
static void checkpoint_reopen(ArcRpcHdStore *s)
{
    arc_riscpc_hd_close(s);s=open_store(false);idle(s);
    for(unsigned i=0;i<9;++i)check_sector(s,2048+i*512,0x65);bounds(s);arc_riscpc_hd_close(s);
}
static unsigned switch_read_corruptions;
static int64_t corrupt_switch_read(struct retro_vfs_file_handle *f,void *p,uint64_t n)
{
    size_t at=f->at;int64_t got=test_read(f,p,n);
    const char *leaf=strrchr(files[f->file].path,'/')+1;
    if(!strcmp(leaf,"journal.b")&&at==0&&n==256&&got==256){((uint8_t *)p)[252]^=1;++switch_read_corruptions;}
    return got;
}
#ifdef ARCDROID_HD_TEST_ALLOC
/* Inject a resident-header fault through the existing test allocator. Match
 * the unique cached envelope, rather than duplicating the opaque struct or
 * guessing its offsets. RETIRE's binding check compares this cached envelope
 * with the independently retained parent model, not another disk read. */
static void tamper_resident_header(ArcRpcHdStore *s,const uint8_t header[256],unsigned field)
{
    assert(store_allocation==(void *)s&&store_allocation_bytes>=256&&field<252);
    uint8_t *allocation=store_allocation,*cached=NULL;unsigned matches=0;
    for(size_t at=0;at<=store_allocation_bytes-256;++at)
        if(!memcmp(allocation+at,header,256)){cached=allocation+at;++matches;}
    assert(matches==1);cached[field]^=1;
}
#endif
static void split_verification(void)
{
    ArcRpcHdStats a;ArcRpcHdStore *s=checkpoint_seed();until_phase(s,ARC_RPC_HD_ROTATE,1);
    switch_read_corruptions=0;vfs.read=corrupt_switch_read;arc_riscpc_hd_tick(s);vfs.read=test_read;
    arc_riscpc_hd_stats(s,&a);assert(a.read_only&&a.generation==0&&arc_riscpc_hd_notice(s));
    assert(switch_read_corruptions==1);checkpoint_reopen(s);
    /* An occupied successor name must be refused before a truncating create. */
    s=checkpoint_seed();until_phase(s,ARC_RPC_HD_ROTATE,0);uint8_t sentinel[256];memset(sentinel,0x83,sizeof(sentinel));
    struct retro_vfs_file_handle *f=test_open("/save/arcdroid/hd-riscpc/0123456789012345678901234567890123456789/journal.b",2,0);
    assert(f&&test_write(f,sentinel,256)==256&&test_close(f)==0);int j=named("journal.b");
    arc_riscpc_hd_tick(s);arc_riscpc_hd_stats(s,&a);assert(a.read_only&&a.generation==0&&arc_riscpc_hd_notice(s));
    assert(files[j].size==256&&!memcmp(files[j].data,sentinel,256));arc_riscpc_hd_close(s);
    /* The injected foreign file is removed by its test owner before recovery. */
    assert(test_remove(files[j].path)==0);s=open_store(false);idle(s);
    for(unsigned i=0;i<9;++i)check_sector(s,2048+i*512,0x65);arc_riscpc_hd_close(s);
    /* A physical length change after the header step must block retirement. */
    for(unsigned longer=0;longer<2;++longer){s=checkpoint_seed();until_phase(s,ARC_RPC_HD_RETIRE,2);
        j=named("journal.b");size_t length=files[j].size;assert(length<files[j].capacity);
        if(longer)files[j].data[files[j].size++]=0x7d;else --files[j].size;
        arc_riscpc_hd_tick(s);arc_riscpc_hd_stats(s,&a);assert(a.read_only&&arc_riscpc_hd_notice(s));
        (void)named("manifest.a");(void)named("journal.a");files[j].size=length;checkpoint_reopen(s);}
    /* CRC-valid replacement of the successor header after publication must
     * still fail comparison with the cached header at RETIRE/header. */
    s=checkpoint_seed();until_phase(s,ARC_RPC_HD_RETIRE,1);j=named("journal.b");uint8_t original[256];
    memcpy(original,files[j].data,256);files[j].data[104]^=1;repair(files[j].data,256);
    arc_riscpc_hd_tick(s);arc_riscpc_hd_stats(s,&a);assert(a.read_only&&arc_riscpc_hd_notice(s));
    (void)named("manifest.a");(void)named("journal.a");memcpy(files[j].data,original,256);checkpoint_reopen(s);
#ifdef ARCDROID_HD_TEST_ALLOC
    const unsigned bindings[]={96,152,160};
    for(unsigned k=0;k<3;++k){s=checkpoint_seed();until_phase(s,ARC_RPC_HD_RETIRE,2);j=named("journal.b");
        tamper_resident_header(s,files[j].data,bindings[k]);arc_riscpc_hd_tick(s);arc_riscpc_hd_stats(s,&a);
        assert(a.read_only&&arc_riscpc_hd_notice(s));(void)named("manifest.a");(void)named("journal.a");checkpoint_reopen(s);}
#endif
    /* A zero reconstruction must remove an existing private alternative before
     * publishing HOLE, even without directory listings (QA S20). */
    fresh_files();silent_readdir=true;s=open_store(true);idle(s);uint8_t p[131072];fill(p,256,0);
    for(unsigned i=0;i<9;++i)assert(arc_riscpc_hd_write(s,2048,256,p));until_phase(s,ARC_RPC_HD_MATERIALIZE,1);
    f=test_open("/save/arcdroid/hd-riscpc/0123456789012345678901234567890123456789/c004.g0000000000000001",2,0);
    assert(f&&test_write(f,sentinel,256)==256&&test_close(f)==0);int debris=named("c004.g0000000000000001");
    arc_riscpc_hd_tick(s);arc_riscpc_hd_stats(s,&a);assert(!a.read_only&&!files[debris].present);
    idle(s);check_sector(s,2048,0);bounds(s);arc_riscpc_hd_close(s);s=open_store(false);idle(s);check_sector(s,2048,0);arc_riscpc_hd_close(s);
    puts("QA 3 split verification: switch readback, occupied destination, RETIRE length/header/parent bindings and zero alternative pass");
}
static void switch_cap_edge(void)
{
    fresh_files();ArcRpcHdStore *s=open_store(true);idle(s);ArcRpcHdStats a;uint8_t p[131072];fill(p,256,0x65);
    for(unsigned i=0;i<8;++i)assert(arc_riscpc_hd_write(s,2048,256,p));until_phase(s,ARC_RPC_HD_ROTATE,1);
    /* 72 records: 63*256 sectors +8*1 +234, plus 72*96 framing
     * bytes and the parent header, exactly fill the 8 MiB cap. All extra
     * writes arrive while the empty successor writer is held. */
    for(unsigned i=8;i<63;++i)assert(arc_riscpc_hd_write(s,2048,256,p));
    for(unsigned i=0;i<8;++i)assert(arc_riscpc_hd_write(s,2048,1,p));
    assert(arc_riscpc_hd_write(s,2048,234,p));arc_riscpc_hd_stats(s,&a);
    assert(a.sequence==72&&a.journal_high==ARC_RPC_HD_JOURNAL_CAP);
    int parent=named("journal.a"),writer=named("journal.b");assert(files[parent].size==ARC_RPC_HD_JOURNAL_CAP&&files[writer].size==0);
    arc_riscpc_hd_tick(s);arc_riscpc_hd_stats(s,&a);assert(a.read_only&&arc_riscpc_hd_notice(s));
    assert(files[parent].size==ARC_RPC_HD_JOURNAL_CAP&&files[writer].size==0);bounds(s);arc_riscpc_hd_close(s);
    s=open_store(false);arc_riscpc_hd_stats(s,&a);assert(!a.read_only&&a.sequence==72);
    check_sector(s,2048,0x65);check_sector(s,2303,0x65);bounds(s);arc_riscpc_hd_close(s);
    puts("switch cap edge: parent at 8 MiB after preparation, successor header refused, all 72 acknowledgements recover");
}
int main(int argc,char **argv)
{
    if(argc>1&&(!strcmp(argv[1],"bounds")||!strcmp(argv[1],"bounds-sanitizer"))){bool sanitizer=!strcmp(argv[1],"bounds-sanitizer");
        stream(false,false,argc>2?argv[2]:NULL,sanitizer);stream(true,true,argc>3?argv[3]:NULL,sanitizer);}
    else if(argc>2&&!strcmp(argv[1],"reopen"))reopen_stream(argv[2]);
    else if(argc>1&&!strcmp(argv[1],"qa-integrity"))qa_integrity();
    else if(argc>1&&!strcmp(argv[1],"import-roundtrip"))import_roundtrip();
    else if(argc>1&&!strcmp(argv[1],"reconstruction"))reconstruction_budget();
    else if(argc>1&&!strcmp(argv[1],"handles"))reconstruction_failures();
    else if(argc>1&&!strcmp(argv[1],"vfs"))vfs_conformance();
    else if(argc>1&&!strcmp(argv[1],"population")){population();abort_gc_leaves();}
    else if(argc>1&&!strcmp(argv[1],"steps")){split_steps();split_verification();switch_cap_edge();}
    else if(argc>1&&!strcmp(argv[1],"command"))faults();
    else if(argc>1&&!strcmp(argv[1],"binding"))binding_fault();
    else if(argc>1&&!strcmp(argv[1],"accounting"))bridge_accounting();
    else if(argc>1&&!strcmp(argv[1],"integrity"))phase_corruption_matrix();
    else if(argc>1&&(!strcmp(argv[1],"faults")||!strcmp(argv[1],"faults-sanitizer"))){sanitizer_faults=!strcmp(argv[1],"faults-sanitizer");bridge_accounting();faults();binding_fault();trust_boundaries();pre_use_verification();phase_corruption_matrix();open_and_creation_faults();checkpoint_fault_matrix();population_fault_matrix();retirement(false);retirement(true);}else store();
    fresh_files();for(unsigned i=0;i<TEST_FILES;++i)free(baseline[i].data);return 0;
}
