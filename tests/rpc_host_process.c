/* Flag-free real-process host ownership and crash boundaries. GPL-2.0-only. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#endif
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#endif
/* Embed the unchanged primitive only in this executable so the injected
 * decorator can forward the actual OS backend, including identity checks. */
#include "../libretro/arc_lock.c"
#include "rpc_preparation_host.h"
#include "rpc_launch_record_store.h"
#include "rpc/rpc_rtc.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../tools/rpc_runner_vfs.h"
static const char *stop_at,*mode;
static bool releasing,point_reached;
static intptr_t content_handle=-1;
static unsigned native_attempts;
static uint64_t injected_time;
static ArcRpcFrontendHost *session;
static struct {struct retro_vfs_file_handle *file;char path[4096];} files[32];
static void checkpoint(const char *name)
{
    if(point_reached || strcmp(name,stop_at))return;
    point_reached=true;printf("point:%s\n",name);fflush(stdout);
    assert(getchar()!=EOF); /* Parent either resumes this exact boundary or kills/reaps us. */
}
static unsigned owner_open(void *opaque,const char *path,intptr_t *handle)
{
    unsigned result=native_open(opaque,path,handle);
    if(!result && strstr(path,"/c-"))content_handle=*handle;
    return result;
}
static unsigned owner_try(void *opaque,intptr_t handle)
{
    ++native_attempts;unsigned result=native_try_lock(opaque,handle);
    if(!result && handle==content_handle)checkpoint("lock");
    if(result && handle!=content_handle)checkpoint("defaults-wait");
    return result;
}
static void owner_unlock(void *opaque,intptr_t handle)
{if(releasing && handle==content_handle)checkpoint("release");native_unlock(opaque,handle);}
static const ArcLockBackend owner_backend={NATIVE_PLATFORM,owner_open,owner_try,native_identity,owner_unlock,native_close};
static struct retro_vfs_file_handle *observed_open(const char *path,unsigned flags,unsigned hints)
{
    struct retro_vfs_file_handle *file=vfs_open(path,flags,hints);
    if(file){unsigned i=0;while(i<32 && files[i].file)++i;assert(i<32);files[i].file=file;snprintf(files[i].path,sizeof(files[i].path),"%s",path);}
    return file;
}
static const char *file_path(struct retro_vfs_file_handle *file)
{for(unsigned i=0;i<32;++i)if(files[i].file==file)return files[i].path;abort();}
static int observed_close(struct retro_vfs_file_handle *file)
{for(unsigned i=0;i<32;++i)if(files[i].file==file)files[i].file=NULL;return vfs_close(file);}
static int64_t observed_write(struct retro_vfs_file_handle *file,const void *data,uint64_t size)
{
    int64_t written=vfs_write(file,data,size);const char *path=file_path(file);
    if(written==(int64_t)size){
        if(strstr(path,"/cmos-riscpc/") && strstr(path,".tmp"))checkpoint(strstr(path,"/defaults/")?"defaults":"cmos");
        if(strstr(path,"/chunk.tmp"))checkpoint("chunk");
        if(strstr(path,"/manifest.tmp"))checkpoint("manifest");
        if(strstr(path,"/launch.observation.tmp"))checkpoint("observation");
    }
    return written;
}
static uint64_t clock_read(void *opaque){(void)opaque;return injected_time+=1000;}
static void log_line(void *opaque,enum retro_log_level level,const char *text)
{(void)opaque;(void)level;if(!strncmp(text,"N-14:",5)||!strncmp(text,"N-16:",5))puts(text);}
static void pending(void)
{
    for(unsigned frame=0;arc_riscpc_host_pending(session);++frame){assert(frame<100000);
        injected_time+=1000000000;
        arc_riscpc_host_frame_begin(session);arc_riscpc_host_pending_frame(session);arc_riscpc_host_frame_end(session);
        if(arc_riscpc_preparation_state(arc_riscpc_host_preparation(session))==ARC_RPC_PREP_FAILED)break;
    }
}
static void observation(const char *content)
{
    ArcLock owner;arc_lock_content(&owner,save_directory,content,&owner_backend,NULL);
    ArcLockResult result;do{result=arc_lock_acquire(&owner);}while(result==ARC_LOCK_TRANSIENT);
    assert(result==ARC_LOCK_HELD);
    ArcRpcLaunchRecordStore store;assert(arc_riscpc_launch_record_begin(&store,save_directory,content,&owner));
    bool done=false;while(!done)assert(arc_riscpc_launch_record_recover(&store,&done));
    ArcRpcBootRecord record={.observation=ARC_RPC_OUTCOME_RETURNED,.key={.cpu=3,.plan_revision=2,.profile_revision=2,.action=1}};
    memset(record.key.firmware,1,32);memset(record.key.configuration,2,32);memset(record.key.content,3,32);
    memset(record.key.media,4,32);memset(record.key.store,5,32);memset(record.key.plan,6,32);memset(record.key.helper,7,32);
    assert(arc_riscpc_launch_record_publish_begin(&store,&record));done=false;
    while(!done)assert(arc_riscpc_launch_record_publish_step(&store,true,&done));
    arc_riscpc_launch_record_close(&store);arc_lock_release(&owner);
}
int main(int argc,char **argv)
{
    assert(argc==7);mode=argv[4];stop_at=argv[5];const char *content=argv[6];
    assert(canonical(argv[3],save_directory));
    struct retro_vfs_interface observed=vfs;observed.open=observed_open;observed.close=observed_close;observed.write=observed_write;
    arc_io_set_vfs(&observed);
    FILE *file=fopen(argv[1],"rb");assert(file);uint8_t *rom=malloc(2097152);assert(rom && fread(rom,1,2097152,file)==2097152);assert(!fclose(file));
    ArcDigest digest=arc_digest(rom,2097152);ArcRpcFirmwareFingerprint fingerprint={ARC_RPC_OS_371,2097152,digest.crc32,digest.sha1};
    uint8_t seed[256]={0};seed[63]=1;
    ArcRpcPreparationRequest request={.firmware=rom,.firmware_size=2097152,.fingerprint=&fingerprint,
        .selection={.reason=ARC_RPC_SELECTION_SELECTED,.model=ARC_RPC_SELECT_SA110,.os=ARC_RPC_OS_371},
        .save=save_directory,.system=argv[2],.seed=(!strcmp(mode,"generate")||!strcmp(mode,"defaults-contender"))?NULL:seed,.epoch=0,.hard_disc=true,.populate=true,.session_id=1};
    request.key.model=2;request.key.layout=3;snprintf(request.key.content,sizeof(request.key.content),"%s",content);
    ArcRpcFrontendOps ops={.kind=ARC_SESSION_KIND_RISCPC,.module={.clock=clock_read,.lock_backend=&owner_backend},.log=log_line};
    char error[512]={0};session=arc_riscpc_host_begin(&request,&ops,error,sizeof(error));assert(session);
    pending();ArcRpcPreparation *preparation=arc_riscpc_host_preparation(session);
    if(!strcmp(mode,"contender")||!strcmp(mode,"defaults-contender")){
        assert(arc_riscpc_preparation_state(preparation)==ARC_RPC_PREP_FAILED &&
            strstr(arc_riscpc_preparation_error(preparation),!strcmp(mode,"contender")?"another RetroArch":"preparing RiscPC settings"));
    }else{
        assert(arc_riscpc_preparation_state(preparation)==ARC_RPC_PREP_RUNNING);
        ArcRpcPreparationStats stats;arc_riscpc_preparation_stats(preparation,&stats);
        printf("acquired: attempts=%llu\n",(unsigned long long)stats.lock_attempts);fflush(stdout);
        if(!strcmp(stop_at,"cmos")){
            uint8_t latest[256];arc_rpc_cmos_snapshot(latest);latest[100]^=1;arc_rpc_cmos_seed(latest);
            for(unsigned i=0;i<60 && !point_reached;++i)arc_riscpc_host_maintenance(session);
        }
        if(!strcmp(stop_at,"chunk")||!strcmp(stop_at,"manifest")){
            ArcRpcHdStore *disc=arc_riscpc_preparation_disc(preparation);
            for(unsigned i=0;i<60;++i)arc_riscpc_host_maintenance(session);
            uint8_t bytes[131072];memset(bytes,0x5b,sizeof(bytes));
            for(unsigned i=0;i<9;++i)assert(arc_riscpc_hd_write(disc,100000,256,bytes));
            for(unsigned i=0;i<100 && !point_reached;++i)arc_riscpc_host_maintenance(session);
        }
        if(!strcmp(mode,"fault")){
            uint8_t latest[256];arc_rpc_cmos_snapshot(latest);latest[100]^=1;arc_rpc_cmos_seed(latest);
            assert(!arc_engine_rpc_advance(10000));arc_riscpc_host_maintenance(session);assert(arc_riscpc_preparation_draining(preparation));
            arc_riscpc_host_pending_frame(session);checkpoint("drain");
        }
        checkpoint("running");
    }
    releasing=true;assert(arc_riscpc_host_free(session,error,sizeof(error)));session=NULL;
    if(!strcmp(stop_at,"observation"))observation(content);
    assert(!strcmp(stop_at,"none") || point_reached);free(rom);vfs_assert_no_open_handles();
    puts("clean");return 0;
}
