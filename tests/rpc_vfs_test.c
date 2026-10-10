/* RetroArch buffered VFS conformance for the non-shipping runner.
 * Authored test; GPL-2.0-only. */
#if !defined(_WIN32)
#define _XOPEN_SOURCE 700
#endif
#include "libretro.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif
#include "../tools/rpc_runner_vfs.h"

int main(int argc,char **argv)
{
    assert(argc==2);
#if defined(_WIN32)
    (void)_mkdir(argv[1]);
#else
    (void)mkdir(argv[1],0700);
#endif
    assert(canonical(argv[1],save_directory));
    char path[4096];int n=snprintf(path,sizeof(path),"%s/size-test",argv[1]);
    assert(n>0&&(size_t)n<sizeof(path));
    uint8_t data[32]={0};
    struct retro_vfs_file_handle *a=vfs.open(path,2,0);assert(a&&vfs.size(a)==0);
    assert(vfs.write(a,data,16)==16&&vfs.flush(a)==0&&vfs.size(a)==16);
    struct retro_vfs_file_handle *b=vfs.open(path,1,0);assert(b&&vfs.size(b)==16);
    assert(vfs.read(a,data,1)==-1);
    assert(vfs.seek(a,16,0)==0&&vfs.tell(a)==16);
    assert(vfs.write(a,data,8)==8&&vfs.flush(a)==0&&vfs.size(a)==24&&vfs.size(b)==16);
    int32_t size;assert(vfs.stat(path,&size)==1&&size==24);
    struct retro_vfs_file_handle *c=vfs.open(path,1,0);assert(c&&vfs.size(c)==24);
    /* Independent file replacement/truncation must not update any old cache. */
    FILE *external=fopen(path,"wb");assert(external);
    assert(fwrite(data,1,6,external)==6&&fclose(external)==0);
    assert(vfs.size(a)==24&&vfs.size(b)==16&&vfs.size(c)==24);
    struct retro_vfs_file_handle *d=vfs.open(path,1,0);assert(d&&vfs.size(d)==6);
    assert(vfs.stat(path,&size)==1&&size==6);
    /* Native buffered WRITE|UPDATE_EXISTING uses r+b, not write-only wb. */
    struct retro_vfs_file_handle *update=vfs.open(path,6,0);assert(update);
    assert(vfs.read(update,data,6)==6&&vfs.close(update)==0);
    assert(vfs.close(a)==0&&vfs.close(b)==0&&vfs.close(c)==0&&vfs.close(d)==0);
    assert(vfs.remove(path)==0);
    puts("runner VFS: buffered seek-zero, tell, independent per-handle size caches");
    vfs_assert_no_open_handles();return 0;
}
