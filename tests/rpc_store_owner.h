/* Pre-acquired token fixture for store-only tests; acquisition is tested by the
 * ownership and preparation harnesses. No production bypass. GPL-2.0-only. */
#ifndef ARC_RPC_STORE_TEST_OWNER_H
#define ARC_RPC_STORE_TEST_OWNER_H
#include "arc_lock.h"
#include "arc_io.h"
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
static bool rpc_fixture_lost;
static unsigned rpc_fixture_open_error,rpc_fixture_try_error;
static bool rpc_fixture_identity(void *p,intptr_t handle,const char *path)
{(void)p;(void)handle;(void)path;return !rpc_fixture_lost;}
static void rpc_fixture_noop(void *opaque,intptr_t handle){(void)opaque;(void)handle;}
static const ArcLockBackend rpc_fixture_backend={.platform=ARC_LOCK_OFD,.identity=rpc_fixture_identity,.unlock=rpc_fixture_noop,.close_handle=rpc_fixture_noop};
static inline ArcLock *rpc_store_test_owner(void)
{
    static ArcLock owner={.backend=&rpc_fixture_backend,.valid=true,.held=true};
    return &owner;
}
/* Injected native backend for the in-memory VFS preparation harness. Each
 * independent description conflicts; zero-byte sentinel data uses the VFS. */
typedef struct {char path[4096];bool used,held;} RpcFixtureLock;
static RpcFixtureLock rpc_fixture_locks[16];
static bool (*rpc_fixture_create_sentinel)(const char *);
static unsigned rpc_fixture_open(void *p,const char *path,intptr_t *handle)
{
    (void)p;if(rpc_fixture_open_error)return rpc_fixture_open_error;
    for(unsigned i=0;i<16;++i)if(!rpc_fixture_locks[i].used){
        if(rpc_fixture_create_sentinel){if(!rpc_fixture_create_sentinel(path))return EIO;}
        else{ArcIoFile *file=arc_io_create_file(path);if(!file)return EIO;
            if(!arc_io_close_file(file))return EIO;}
        rpc_fixture_locks[i]=(RpcFixtureLock){.used=true};
        snprintf(rpc_fixture_locks[i].path,sizeof(rpc_fixture_locks[i].path),"%s",path);
        *handle=(intptr_t)i;return 0;
    }
    return EMFILE;
}
static unsigned rpc_fixture_try(void *p,intptr_t handle)
{
    (void)p;if(rpc_fixture_try_error)return rpc_fixture_try_error;
    for(unsigned i=0;i<16;++i)if(i!=(unsigned)handle && rpc_fixture_locks[i].held &&
        !strcmp(rpc_fixture_locks[i].path,rpc_fixture_locks[handle].path))return EAGAIN;
    rpc_fixture_locks[handle].held=true;return 0;
}
static void rpc_fixture_unlock(void *p,intptr_t handle)
{(void)p;rpc_fixture_locks[handle].held=false;}
static void rpc_fixture_close(void *p,intptr_t handle)
{(void)p;rpc_fixture_locks[handle].used=false;}
static const ArcLockBackend rpc_preparation_test_backend={ARC_LOCK_OFD,rpc_fixture_open,rpc_fixture_try,
    rpc_fixture_identity,rpc_fixture_unlock,rpc_fixture_close};
#endif
