/* Bounded read-only preflight and non-authoritative pair advice. GPL-2.0-only. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#endif
#include "rpc_route.h"
#include "arc_io.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#endif
#include "../tools/rpc_runner_vfs.h"
static const char content[]="0123456789abcdef0123456789abcdef01234567";
static struct retro_vfs_interface real_vfs;
static unsigned calls,closed;
static bool endless,bad_close,bad_stat;
static bool read_dir(struct retro_vfs_dir_handle *d){++calls;return endless?true:real_vfs.readdir(d);}
static const char *name_dir(struct retro_vfs_dir_handle *d){return endless?"entry":real_vfs.dirent_get_name(d);}
static int close_dir(struct retro_vfs_dir_handle *d){++closed;int result=real_vfs.closedir(d);return bad_close?-1:result;}
static int32_t stat_path(const char *p,int32_t *size){if(bad_stat && strstr(p,"/entry"))return 0;return real_vfs.stat(p,size);}
static bool is_dir(struct retro_vfs_dir_handle *d){return endless?false:real_vfs.dirent_is_dir(d);}
static int rename_without_replace(const char *a,const char *b){int32_t size=0;if(real_vfs.stat(b,&size)&RETRO_VFS_STAT_IS_VALID)return -1;return real_vfs.rename(a,b);}
static int no_publish(const char *a,const char *b){(void)a;(void)b;return -1;}
static time_t failed_clock(time_t *p){(void)p;return (time_t)-1;}
static unsigned clock_calls;
static time_t fixed_clock(time_t *p){(void)p;++clock_calls;return (time_t)946684800;}
int main(int argc,char **argv)
{
    assert((argc==2 || argc==3) && canonical(argv[1],save_directory));
    uint64_t epoch=0;assert(!arc_riscpc_route_epoch_with_clock(failed_clock,&epoch));
    assert(arc_riscpc_route_epoch_with_clock(fixed_clock,&epoch) && clock_calls==1);
    if(argc==3)assert(epoch==strtoull(argv[2],NULL,10));
    uint64_t same=0;assert(arc_riscpc_route_epoch_with_clock(fixed_clock,&same) && epoch==same);real_vfs=vfs;
    vfs.readdir=read_dir;vfs.dirent_get_name=name_dir;vfs.closedir=close_dir;vfs.stat=stat_path;vfs.dirent_is_dir=is_dir;vfs.rename=rename_without_replace;arc_io_set_vfs(&vfs);
    assert(arc_riscpc_store_presence(save_directory,content)==ARC_RPC_STORE_ABSENT);
    bad_close=true;assert(arc_riscpc_store_presence(save_directory,content)==ARC_RPC_STORE_UNKNOWN);bad_close=false;
    char entry[4096];snprintf(entry,sizeof(entry),"%s/entry",save_directory);assert(arc_io_write_all(entry,"",0));
    endless=true;calls=0;assert(arc_riscpc_store_presence(save_directory,content)==ARC_RPC_STORE_UNKNOWN);assert(calls<=4096);endless=false;
    endless=bad_stat=true;assert(arc_riscpc_store_presence(save_directory,content)==ARC_RPC_STORE_UNKNOWN);endless=bad_stat=false;
    char notice[512];ArcRpcSelection selection={.reason=ARC_RPC_SELECTION_MISSING_FIRMWARE,.needed=ARC_RPC_OS_BIT(ARC_RPC_OS_350)|ARC_RPC_OS_BIT(ARC_RPC_OS_371)};
    ArcRpcFirmwareInventory inventory={0};assert(!strcmp(arc_riscpc_route_notice(&selection,&inventory,notice,sizeof(notice)),"N-1"));
    assert(!strcmp(notice,"Needs RiscPC RISC OS 3.50,3.71 in <system>/arcdroid/firmware/. Or try an Archimedes in core options.") && strlen(notice)<=120);
    inventory.resource_failure=true;assert(!strcmp(arc_riscpc_route_notice(&selection,&inventory,notice,sizeof(notice)),"N-1 resource") && strstr(notice,"ran out of memory"));
    inventory.bounded=true;selection.reason=ARC_RPC_SELECTION_INCOMPLETE_SCAN;
    assert(!strcmp(arc_riscpc_route_notice(&selection,&inventory,notice,sizeof(notice)),"N-5 incomplete-scan") && !strstr(notice,"memory"));
    inventory=(ArcRpcFirmwareInventory){0};selection.reason=ARC_RPC_SELECTION_UNREADABLE_INVENTORY;
    assert(!strcmp(arc_riscpc_route_notice(&selection,&inventory,notice,sizeof(notice)),"N-5"));
    assert(!strcmp(notice,"ArcDroid cannot find or read <system>/arcdroid/firmware/. Create that folder and add your RiscPC firmware, or check that it is readable."));
    selection.reason=ARC_RPC_SELECTION_UNQUALIFIED;assert(!strcmp(arc_riscpc_route_notice(&selection,&inventory,notice,sizeof(notice)),"N-2") && strstr(notice,"StrongARM with RISC OS 3.50 or 3.60"));
    selection.reason=ARC_RPC_SELECTION_UNSUPPORTED;assert(!strcmp(arc_riscpc_route_notice(&selection,&inventory,notice,sizeof(notice)),"N-3"));
    assert(!strcmp(notice,"This title asks for hardware ArcDroid does not emulate (unsupported CPU or RISC OS requirement)."));
    selection.reason=ARC_RPC_SELECTION_CONFLICTING_DISCS;assert(!strcmp(arc_riscpc_route_notice(&selection,&inventory,notice,sizeof(notice)),"N-4"));
    assert(!strcmp(notice,"The discs in this set ask for different machines. Load them separately, or choose a machine in core options."));
    char deep[4096];snprintf(deep,sizeof(deep),"%s",save_directory);for(unsigned i=0;i<65;++i)strcat(deep,"/x");
    assert(arc_riscpc_store_presence(deep,content)==ARC_RPC_STORE_UNKNOWN);
    char directory[4096];snprintf(directory,sizeof(directory),"%s/arcdroid/hd-riscpc/%s",save_directory,content);
    assert(arc_io_make_path(directory));assert(arc_riscpc_store_presence(save_directory,content)==ARC_RPC_STORE_EXISTS);
    ArcLock owner={0};assert(arc_lock_content(&owner,save_directory,content,NULL,NULL)==ARC_LOCK_TRANSIENT);
    while(arc_lock_next_namespace(&owner))assert(arc_lock_acquire(&owner)==ARC_LOCK_TRANSIENT);
    assert(arc_lock_acquire(&owner)==ARC_LOCK_HELD);
    assert(!arc_riscpc_pair_admitted(save_directory,content,0,1,&owner));
    assert(!arc_riscpc_pair_admitted(save_directory,content,0,1,&owner));
    assert(arc_riscpc_pair_admitted(save_directory,content,2,4,&owner));
    assert(!arc_riscpc_pair_admitted(save_directory,content,2,4,&owner));
    assert(arc_riscpc_pair_admitted(save_directory,content,0,1,&owner));
    vfs.rename=no_publish;assert(arc_riscpc_pair_admitted(save_directory,content,2,4,&owner));vfs.rename=rename_without_replace;
    assert(!arc_riscpc_pair_admitted(save_directory,content,0,1,&owner)); /* failed publication retained earlier pair */
    char sidecar[4096];snprintf(sidecar,sizeof(sidecar),"%s/arcdroid/rpc-pairs/%s.pair.0",save_directory,content);
    assert(arc_io_write_all(sidecar,"corrupt",7));snprintf(sidecar,sizeof(sidecar),"%s/arcdroid/rpc-pairs/%s.pair.1",save_directory,content);assert(arc_io_write_all(sidecar,"corrupt",7));assert(!arc_riscpc_pair_admitted(save_directory,content,2,4,&owner));
    assert(arc_riscpc_store_presence(save_directory,content)==ARC_RPC_STORE_EXISTS);
    arc_lock_release(&owner);assert(closed);vfs_assert_no_open_handles();puts("rpc-route-policy: bounds, uncertainty, sidecar drift and interrupted publication pass");return 0;
}
