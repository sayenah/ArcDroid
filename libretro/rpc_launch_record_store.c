/* Bounded cache publication with recovery exclusion. GPL-2.0-only. */
#include "rpc_launch_record_store.h"
#include "install_store.h"
#include <stdio.h>
#include <string.h>
bool arc_riscpc_launch_record_begin(ArcRpcLaunchRecordStore *s,const char *save,const char *content,ArcLock *owner)
{
    if(!s || !arc_lock_held(owner) || !save || !arc_install_store_native_path(save) || !content || strlen(content)!=40)return false;
    for(unsigned i=0;i<40;++i)if(!((content[i]>='0'&&content[i]<='9')||(content[i]>='a'&&content[i]<='f')))return false;
    memset(s,0,sizeof(*s));s->owner=owner;int n=snprintf(s->directory,sizeof(s->directory),"%s/arcdroid/hd-riscpc/%s",save,content);
    if(n<0 || (size_t)n>=sizeof(s->directory))return false;
    snprintf(s->path,sizeof(s->path),"%s/launch.observation",s->directory);
    snprintf(s->temporary,sizeof(s->temporary),"%s.tmp",s->path);return true;
}
static bool held(ArcRpcLaunchRecordStore *s)
{return s && arc_lock_held(s->owner);}
static bool owned(ArcRpcLaunchRecordStore *s)
{return s && arc_lock_held(s->owner) && arc_lock_verify(s->owner)==ARC_LOCK_HELD;}
bool arc_riscpc_launch_record_namespace(const ArcRpcLaunchRecordStore *s)
{return s && (s->phase==5 || s->phase==7 || s->phase==15 || s->phase==16);}
void arc_riscpc_launch_record_close(ArcRpcLaunchRecordStore *s)
{
    if(!s)return;if(s->scan){arc_io_close_directory(s->scan);s->scan=NULL;}
    if(s->file){arc_io_close_file(s->file);s->file=NULL;}
}
static bool fail(ArcRpcLaunchRecordStore *s)
{arc_riscpc_launch_record_close(s);s->failed=true;s->have_old=false;return false;}
bool arc_riscpc_launch_record_recover(ArcRpcLaunchRecordStore *s,bool *done)
{
    if(!s || !done || s->failed || !held(s))return false;*done=false;
    switch(s->phase){
    case 0:
        s->scan=arc_io_open_directory(s->directory);
        if(!s->scan){
            /* A genuinely new store has no directory/cache. An existing but
             * unreadable namespace must not be allowed to clear recovery. */
            if(arc_io_existing_directory(s->directory))return fail(s);
            s->ready=true;s->phase=6;*done=true;return true;
        }s->phase=1;break;
    case 1:{
        const char *name=NULL;bool regular=false;
        uint64_t operations=0;int r=arc_io_read_directory(s->scan,&name,&regular,&operations);
        if(r<0 || ++s->entries>4096)return fail(s);
        if(!r)s->phase=2;
        else if(!strcmp(name,"launch.observation")){if(!regular)return fail(s);s->found=true;}
        else if(!strcmp(name,"launch.observation.tmp")){if(!regular)return fail(s);s->found_temporary=true;}
        break;
    }
    case 2:
        if(!arc_io_close_directory(s->scan)){s->scan=NULL;return fail(s);}s->scan=NULL;s->phase=s->found?3:s->found_temporary?7:6;break;
    case 3:
        s->file=arc_io_open_file(s->path,false);if(!s->file)return fail(s);s->phase=4;break;
    case 4:{
        int64_t n=arc_io_file_length(s->file);
        if(n==ARC_RPC_BOOT_RECORD_BYTES && arc_io_read_file(s->file,s->bytes,sizeof(s->bytes))==sizeof(s->bytes))
            s->have_old=arc_riscpc_boot_record_decode(s->bytes,sizeof(s->bytes),&s->old);
        if(!arc_io_close_file(s->file)){s->file=NULL;return fail(s);}s->file=NULL;s->phase=5;break;
    }
    case 5:
        ++s->namespace_operations;if(!arc_io_remove(s->path))return fail(s);s->phase=s->found_temporary?7:6;break;
    case 6:s->ready=true;*done=true;break;
    case 7:
        ++s->namespace_operations;if(!arc_io_remove(s->temporary))return fail(s);s->phase=6;break;
    default:return false;
    }return true;
}
void arc_riscpc_launch_record_discard(ArcRpcLaunchRecordStore *s)
{if(s)s->have_old=false;}
bool arc_riscpc_launch_record_publish_begin(ArcRpcLaunchRecordStore *s,const ArcRpcBootRecord *record)
{
    if(!s || !owned(s) || !s->ready || s->failed || s->file || s->phase!=6 ||
        !arc_riscpc_boot_record_encode(record,s->bytes,sizeof(s->bytes)))return false;
    s->phase=10;return true;
}
bool arc_riscpc_launch_record_publish_step(ArcRpcLaunchRecordStore *s,bool valid,bool *done)
{
    if(!s || !done || s->failed || s->phase<10)return false;*done=false;
    if(!held(s))return fail(s);
    if(!valid){s->have_old=false;arc_riscpc_launch_record_close(s);s->phase=16;}
    switch(s->phase){
    case 10:s->file=arc_io_create_file(s->temporary);if(!s->file)return fail(s);s->phase=11;break;
    case 11:{
        bool ok=arc_io_write_file(s->file,s->bytes,sizeof(s->bytes))==sizeof(s->bytes);
        bool flushed=arc_io_flush_file(s->file);
        bool closed=arc_io_close_file(s->file);s->file=NULL;
        if(!ok || !flushed || !closed)return fail(s);s->phase=12;break;
    }
    case 12:s->phase=13;break;
    case 13:s->phase=15;break;
    case 15:++s->namespace_operations;if(!arc_io_rename(s->temporary,s->path))return fail(s);s->published=true;s->phase=6;*done=true;break;
    case 16:++s->namespace_operations;(void)arc_io_remove(s->temporary);s->phase=6;*done=true;break;
    default:return fail(s);
    }return true;
}

bool arc_riscpc_launch_record_invalidate(ArcRpcLaunchRecordStore *s)
{
    if(!owned(s))return false;
    if(!s->published)return true;
    ++s->namespace_operations;
    if(!arc_io_remove(s->path))return false;
    s->published=false;return true;
}
