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
    /* Existing ancestors are observations, not permission to mutate outside
     * --save. The lock namespace walk relies on RetroArch's -2 result. */
    char ancestor[4096];
    assert(canonical(".",ancestor) && !below_save(ancestor));
    assert(vfs.mkdir(ancestor)==-2 && vfs.mkdir(save_directory)==-2);
    char denied[4096];
    assert(snprintf(denied,sizeof(denied),"%s/arcdroid-vfs-refused-child",ancestor)<(int)sizeof(denied));
    assert(vfs.stat(denied,NULL)==0 && vfs.mkdir(denied)==-1 && vfs.stat(denied,NULL)==0);
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
    /* Both modes preserve checked publication; RetroArch mode needs only a
     * buffered flush. A read-only handle remains an invalid flush target. */
    for(unsigned sync=0;sync<2;++sync){
        uint8_t expected[32],actual[32];
        for(unsigned i=0;i<32;++i)expected[i]=(uint8_t)(i+sync+1);
        vfs_flush_fsync=sync!=0;a=vfs.open(path,2,0);assert(a);
        assert(vfs.write(a,expected,32)==32&&vfs.flush(a)==0);
        b=vfs.open(path,1,0);assert(b&&vfs.read(b,actual,32)==32);
        assert(!memcmp(actual,expected,32)&&vfs.flush(b)==-1);
        assert(vfs.close(b)==0&&vfs.close(a)==0&&vfs.remove(path)==0);
    }
    vfs_flush_fsync=true;
    puts("runner VFS: buffered seek-zero, tell, independent per-handle size caches");
    vfs_assert_no_open_handles();return 0;
}
