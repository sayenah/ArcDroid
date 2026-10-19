/* Real file VFS process-termination oracle. Authored GPL-2.0-only. */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#endif
#include "rpc_native_store_owner.h"
#include "rpc_hd_store.h"
#include "arc_io.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#endif
#define vfs_open base_open
#define vfs_close base_close
#define vfs_size base_size
#define vfs_seek base_seek
#define vfs_tell base_tell
#define vfs_read base_read
#define vfs_write base_write
#define vfs_flush base_flush
#define vfs_stat base_stat
#define vfs_remove base_remove
#define vfs_rename base_rename
#include "../tools/rpc_runner_vfs.h"
#undef vfs_open
#undef vfs_close
#undef vfs_size
#undef vfs_seek
#undef vfs_tell
#undef vfs_read
#undef vfs_write
#undef vfs_flush
#undef vfs_stat
#undef vfs_remove
#undef vfs_rename

static uint64_t metadata_calls,namespace_calls;
static struct retro_vfs_file_handle *rotation_handle;
static unsigned boundary,cut;
static bool tracing,trace_active;
static ArcRpcHdStore *observed_store;
static const char *operation="commit", *kind="memory", *side="resident";
static struct {struct retro_vfs_file_handle *handle;const char *kind;} tracked[16];
static const char *file_kind(const char *p)
{ const char *leaf=strrchr(p,'/');leaf=leaf?leaf+1:p;
  if(!strcmp(leaf,"chunk.tmp"))return "chunk-candidate";
  if(strlen(leaf)==22&&leaf[0]=='c'&&leaf[4]=='.'&&leaf[5]=='g')return "chunk";
  if(!strcmp(leaf,"manifest.tmp"))return "manifest-candidate";
  if(!strncmp(leaf,"manifest",8))return "manifest";
  if(!strncmp(leaf,"journal",7))return "journal";
  return "other"; }
static const char *handle_kind(struct retro_vfs_file_handle *h)
{for(unsigned i=0;i<16;++i)if(tracked[i].handle==h)return tracked[i].kind;assert(false);return "unknown";}
static void mark(const char *op,const char *file,const char *when)
{operation=op;kind=file;side=when;}

static void point(void)
{
    ++boundary;
    if(tracing&&trace_active){ArcRpcHdStats a={0};if(observed_store)arc_riscpc_hd_stats(observed_store,&a);
        printf("boundary=%u %u/%u/%u/%s/%s/%s\n",boundary,a.state,a.phase_step,a.population,kind,operation,side);}
    if(cut==boundary){
#ifdef _WIN32
        TerminateProcess(GetCurrentProcess(),77);
#else
        raise(SIGKILL);
#endif
        abort();
    }
}
void arc_riscpc_hd_test_point(unsigned stage) {assert(stage>=1&&stage<=3);const char *stages[]={"", "durable", "indexed", "cached"};mark(stages[stage],"memory","resident");point();}
#define WRAP(name,type,params,args) static type observed_##name params {const char *file=handle_kind(h);mark(#name,file,"before");point();type result=base_##name args;mark(#name,file,"after");point();return result;}
static struct retro_vfs_file_handle *observed_open(const char *p,unsigned m,unsigned h)
{++metadata_calls;if((m&RETRO_VFS_FILE_ACCESS_WRITE)&&!(m&RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING))++namespace_calls;const char *file=file_kind(p);const char *op=m&RETRO_VFS_FILE_ACCESS_WRITE?"open-write":"open-read";
 mark(op,file,"before");point();struct retro_vfs_file_handle *f=base_open(p,m,h);mark(op,file,"after");point();
 if(f){unsigned i=0;while(i<16&&tracked[i].handle)++i;assert(i<16);tracked[i].handle=f;tracked[i].kind=file;}
 if(f&&m==RETRO_VFS_FILE_ACCESS_WRITE&&f->cached_size==0&&strstr(p,"/journal."))rotation_handle=f;return f;}
static int observed_close(struct retro_vfs_file_handle *h)
{++metadata_calls;const char *file=handle_kind(h);mark("close",file,"before");point();if(h==rotation_handle)rotation_handle=NULL;
 for(unsigned i=0;i<16;++i)if(tracked[i].handle==h)tracked[i].handle=NULL;
 int result=base_close(h);mark("close",file,"after");point();return result;}

#define META_WRAP(name,type,params,args) static type observed_##name params {++metadata_calls;const char *file=handle_kind(h);mark(#name,file,"before");point();type result=base_##name args;mark(#name,file,"after");point();return result;}
WRAP(size,int64_t,(struct retro_vfs_file_handle *h),(h))
WRAP(tell,int64_t,(struct retro_vfs_file_handle *h),(h))
WRAP(seek,int64_t,(struct retro_vfs_file_handle *h,int64_t n,int o),(h,n,o))
WRAP(read,int64_t,(struct retro_vfs_file_handle *h,void *p,uint64_t n),(h,p,n))
WRAP(write,int64_t,(struct retro_vfs_file_handle *h,const void *p,uint64_t n),(h,p,n))
META_WRAP(flush,int,(struct retro_vfs_file_handle *h),(h))
static int32_t observed_stat(const char *p,int32_t *n)
{++metadata_calls;const char *file=file_kind(p);mark("stat",file,"before");point();int32_t result=base_stat(p,n);mark("stat",file,"after");point();return result;}
static int observed_remove(const char *p)
{++namespace_calls;++metadata_calls;const char *file=file_kind(p);mark("remove",file,"before");point();int result=base_remove(p);mark("remove",file,"after");point();return result;}
static int observed_rename(const char *a,const char *b)
{++namespace_calls;++metadata_calls;const char *file=file_kind(b);mark("rename",file,"before");point();int result=base_rename(a,b);mark("rename",file,"after");point();return result;}
static void process_rotation_writer(void)
{assert(vfs_open_handles==1&&vfs_open_directories==0&&rotation_handle&&rotation_handle->writable&&!rotation_handle->readable&&rotation_handle->cached_size==0);}
#define rpc_hd_test_rotation_writer process_rotation_writer
#define rpc_hd_test_metadata_calls() metadata_calls
#define rpc_hd_test_namespace_calls() namespace_calls
#define rpc_hd_test_no_open_handles vfs_assert_no_open_handles
#include "rpc_hd_handle_checks.h"
static bool no_listing(struct retro_vfs_dir_handle *d){(void)d;return false;}
static void idle(ArcRpcHdStore *s)
{ArcRpcHdStats a;for(unsigned n=0;n<4096;++n){arc_riscpc_hd_tick(s);arc_riscpc_hd_stats(s,&a);assert(!a.read_only);if(a.state==ARC_RPC_HD_IDLE)return;}assert(0);}
static const char *id="0123456789012345678901234567890123456789";
static ArcRpcHdStore *open_store(bool blank)
{char error[256];observed_store=NULL;ArcRpcHdStore *s=arc_riscpc_hd_open(save_directory,id,blank,rpc_native_test_owner(save_directory,id),error,sizeof(error));if(!s)fprintf(stderr,"open: %s\n",error);assert(s);observed_store=s;return s;}
static void sector(ArcRpcHdStore *s,unsigned lba,unsigned value)
{uint8_t p[512];assert(arc_riscpc_hd_read(s,lba,1,p));for(unsigned i=0;i<512;++i)assert(p[i]==value);}
int main(int argc,char **argv)
{
    assert(argc==5);assert(canonical(argv[1],save_directory));arc_io_set_vfs(&vfs);vfs.readdir=no_listing;
    if(!strcmp(argv[2],"ownership-hold") || !strcmp(argv[2],"ownership-try")){
        ArcLock owner;assert(arc_lock_content(&owner,save_directory,argv[3],NULL,NULL)==ARC_LOCK_TRANSIENT);
        ArcLockResult result;unsigned attempts=0;
        do{result=arc_lock_acquire(&owner);assert(++attempts<32);}while(result==ARC_LOCK_TRANSIENT);
        if(!strcmp(argv[2],"ownership-try")){assert(result==ARC_LOCK_CONTENDED);puts("contended");}
        else{assert(result==ARC_LOCK_HELD);puts("held");fflush(stdout);char command[32];assert(fgets(command,sizeof(command),stdin));}
        arc_lock_release(&owner);vfs_assert_no_open_handles();return 0;
    }
    vfs.open=observed_open;vfs.close=observed_close;vfs.size=observed_size;vfs.seek=observed_seek;vfs.tell=observed_tell;
    vfs.read=observed_read;vfs.write=observed_write;vfs.flush=observed_flush;vfs.stat=observed_stat;
    vfs.remove=observed_remove;vfs.rename=observed_rename;
    tracing=!strcmp(argv[2],"trace");const char *mode=tracing?"write":argv[2];bool population=!strcmp(argv[3],"population"),interleaved=!strcmp(argv[3],"interleaved");
    if(!strcmp(argv[3],"creation")){
        if(!strcmp(mode,"write")){
            boundary=0;trace_active=true;cut=(unsigned)strtoul(argv[4],NULL,10);
            ArcRpcHdStore *s=open_store(true);idle(s);
            printf("boundaries=%u\n",boundary);arc_riscpc_hd_close(s,false);return 0;
        }
        assert(!strcmp(mode,"verify"));
        ArcRpcHdStore *s=open_store(false);ArcRpcHdStats a;arc_riscpc_hd_stats(s,&a);
        uint8_t p[512];
        if(a.published)assert(arc_riscpc_hd_read(s,6,1,p)&&p[3]==0x20);
        else assert(!arc_riscpc_hd_read(s,6,1,p));
        arc_riscpc_hd_close(s,false);s=open_store(true);idle(s);arc_riscpc_hd_stats(s,&a);
        assert(a.published&&arc_riscpc_hd_read(s,6,1,p)&&p[3]==0x20);
        arc_riscpc_hd_close(s,false);return 0;
    }
    if(!strcmp(mode,"prepare")){
        ArcRpcHdStore *s=open_store(true);idle(s);uint8_t p[131072];memset(p,0x31,sizeof(p));
        assert(arc_riscpc_hd_population_begin(s));for(unsigned i=0;i<3;++i)assert(arc_riscpc_hd_population_write(s,2048+i*512,256,p));
        assert(arc_riscpc_hd_population_seal(s)&&arc_riscpc_hd_population_publish(s));idle(s);
        memset(p,0x73,512);assert(arc_riscpc_hd_write(s,70000,1,p));arc_riscpc_hd_close(s,false);return 0;
    }
    if(!strcmp(mode,"verify")){
        ArcRpcHdStore *s=open_store(false);ArcRpcHdStats recovered;arc_riscpc_hd_stats(s,&recovered);
        if(recovered.read_only)assert(arc_riscpc_hd_notice(s));else idle(s);
        sector(s,70000,0x73);uint8_t p[131072];
        unsigned acknowledged=(unsigned)strtoul(argv[4],NULL,10);
        for(unsigned i=0;i<3;++i){assert(arc_riscpc_hd_read(s,2048+i*512,256,p));unsigned value=p[0];assert(value==0x31||value==0x42);
            if(interleaved&&(acknowledged&(1u<<i)))assert(value==0x42);
            if(population)assert(value==(recovered.generation==1?0x31:0x42));
            for(unsigned j=0;j<sizeof(p);++j)assert(p[j]==value);}
        if(interleaved){
            const unsigned lbas[2]={80000,90000},values[2]={0x7b,0x58};
            for(unsigned i=0;i<2;++i){assert(arc_riscpc_hd_read(s,lbas[i],1,p));unsigned value=p[0];
                assert(value==0||value==values[i]);if(acknowledged&(8u<<i))assert(value==values[i]);
                for(unsigned j=0;j<512;++j)assert(p[j]==value);}
        }
        ArcRpcHdStats a;arc_riscpc_hd_stats(s,&a);assert(a.chunk_files<=1008&&a.journal_high<=8388608);
        assert(a.max_tick_operations<=4096);arc_riscpc_hd_close(s,false);return 0;
    }
    ArcRpcHdStore *s=open_store(false);idle(s);boundary=0;trace_active=true;cut=(unsigned)strtoul(argv[4],NULL,10);
    uint8_t p[131072];memset(p,0x42,sizeof(p));
    if(population){assert(arc_riscpc_hd_population_begin(s));for(unsigned i=0;i<3;++i)assert(arc_riscpc_hd_population_write(s,2048+i*512,256,p));
        assert(arc_riscpc_hd_population_seal(s)&&arc_riscpc_hd_population_publish(s));idle(s);}
    else if(interleaved){
        unsigned mask=0;
        for(unsigned cycle=0;cycle<2;++cycle){
            ArcRpcHdStats before;arc_riscpc_hd_stats(s,&before);
            for(unsigned i=0;i<8;++i){unsigned chunk=i%3;assert(arc_riscpc_hd_write(s,2048+chunk*512,256,p));
                mask|=1u<<chunk;printf("ack_mask=%u\n",mask);fflush(stdout);}
            bool prepared=false,materialized=false,published=false;uint8_t sector_data[512];
            for(unsigned tick=0;tick<100;++tick){arc_riscpc_hd_tick(s);ArcRpcHdStats a;arc_riscpc_hd_stats(s,&a);assert(!a.read_only);
                if(!prepared&&a.state==ARC_RPC_HD_ROTATE&&a.phase_step==1){
                    memset(sector_data,0x7b,sizeof(sector_data));assert(arc_riscpc_hd_write(s,80000,1,sector_data));
                    prepared=true;mask|=8;printf("ack_mask=%u\n",mask);fflush(stdout);}
                if(a.state>=ARC_RPC_HD_MATERIALIZE&&a.state<=ARC_RPC_HD_RETIRE){
                    memset(sector_data,0x58,sizeof(sector_data));assert(arc_riscpc_hd_write(s,90000,1,sector_data));
                    mask|=16;printf("ack_mask=%u\n",mask);fflush(stdout);}
                if(!materialized&&a.state==ARC_RPC_HD_MATERIALIZE&&a.materialized>before.materialized){
                    assert(arc_riscpc_hd_write(s,2048,256,p));materialized=true;printf("ack_mask=%u\n",mask);fflush(stdout);}
                if(!published&&a.state==ARC_RPC_HD_RETIRE){
                    assert(arc_riscpc_hd_write(s,2560,256,p));published=true;printf("ack_mask=%u\n",mask);fflush(stdout);}
                if(a.state==ARC_RPC_HD_IDLE){assert(prepared&&materialized&&published);break;}assert(tick<99);
            }
        }
    }else{
        for(unsigned cycle=0;cycle<2;++cycle){for(unsigned i=0;i<8;++i)assert(arc_riscpc_hd_write(s,2048+(i%3)*512,256,p));idle(s);}
    }
    printf("boundaries=%u\n",boundary);arc_riscpc_hd_close(s,false);return 0;
}
