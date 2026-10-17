/* Flag-free ownership, error mapping and namespace-boundary tests. GPL-2.0-only. */
#include "arc_lock.h"
#include "arc_io.h"
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static const char content[] = "0123456789abcdef0123456789abcdef01234567";
static const char firmware[] = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
static ArcLock a, b;
static uint64_t now, lock_cost;
static unsigned opens, tries, identities, unlocks, closes, mkdirs, stats;
static unsigned open_error, try_error, live, owner;
static bool mismatch, handle_live[32];
static int mkdir_result = -2;
static int32_t directory_flags = 3, file_flags = 1, file_size;
static char mkdir_path[4096];
static unsigned fake_open(void *opaque, const char *path, intptr_t *handle)
{
    assert(opaque == &now && strstr(path, "/arcdroid/locks-riscpc/"));
    ++opens; now += 7;
    if (open_error) return open_error;
    for (unsigned i = 1; i < 32; ++i) if (!handle_live[i]) {
        handle_live[i] = true; ++live; *handle = i; return 0;
    }
    assert(0); return ENOMEM;
}
static unsigned fake_try(void *opaque, intptr_t handle)
{
    assert(opaque == &now && handle_live[handle]); ++tries; now += lock_cost;
    if (try_error) return try_error;
    if (owner && owner != (unsigned)handle) return EAGAIN;
    owner = (unsigned)handle; return 0;
}
static bool fake_identity(void *opaque, intptr_t handle, const char *path)
{
    assert(opaque == &now && handle_live[handle] && path[0]);
    ++identities; now += 3; return !mismatch;
}
static void fake_unlock(void *opaque, intptr_t handle)
{
    assert(opaque == &now && handle_live[handle] && owner == (unsigned)handle);
    ++unlocks; owner = 0;
}
static void fake_close(void *opaque, intptr_t handle)
{
    assert(opaque == &now && handle_live[handle]);
    ++closes; handle_live[handle] = false; --live;
}
static ArcLockBackend backend = {ARC_LOCK_OFD, fake_open, fake_try, fake_identity, fake_unlock, fake_close};
static int mkdir_file(const char *path)
{ ++mkdirs; snprintf(mkdir_path, sizeof(mkdir_path), "%s", path); return mkdir_result; }
static int32_t stat_file(const char *path, int32_t *size)
{
    ++stats;
    if (strstr(path, ".lock")) { if (size) *size = file_size; return file_flags; }
    /* Every directory stat must follow a mkdir of exactly that path. */
    assert(!strcmp(path, mkdir_path)); return directory_flags;
}
static struct retro_vfs_interface vfs = {.stat = stat_file, .mkdir = mkdir_file};
static void fresh(void)
{
    assert(!live && !owner);
    now = 0; lock_cost = 11;
    opens = tries = identities = unlocks = closes = mkdirs = stats = 0;
    open_error = try_error = 0; mismatch = false;
    mkdir_result = -2; directory_flags = 3; file_flags = 1; file_size = 0;
    backend.platform = ARC_LOCK_OFD; arc_io_set_vfs(&vfs);
}
static ArcLockResult attempt(ArcLock *lock)
{
    uint64_t start = now; unsigned before = tries, opened = opens;
    ArcLockResult result = arc_lock_acquire(lock);
    /* The injected clock observes every backend call. One attempt, no spin. */
    assert(tries - before <= 1 && opens - opened <= 1 && now - start <= 25);
    return result;
}
static void directories(ArcLock *lock)
{
    unsigned calls = 0;
    while (arc_lock_next_namespace(lock)) {
        unsigned before = mkdirs, opened = opens;
        assert(attempt(lock) == ARC_LOCK_TRANSIENT && !arc_lock_held(lock));
        assert(mkdirs == before + 1 && opens == opened && ++calls < 64);
    }
}
static void init(ArcLock *lock)
{
    assert(arc_lock_content(lock, "/save", content, &backend, &now) == ARC_LOCK_TRANSIENT);
    directories(lock);
}
static void mapping(ArcLockPlatform platform, bool opening, unsigned error, ArcLockResult expected)
{
    fresh(); backend.platform = platform;
    assert(arc_lock_content(&a, platform == ARC_LOCK_WINDOWS ? "C:/save" : "/save",
                            content, &backend, &now) == ARC_LOCK_TRANSIENT);
    directories(&a);
    if (opening) open_error = error; else try_error = error;
    assert(attempt(&a) == expected && !arc_lock_held(&a));
    assert(opens == 1 && tries == (opening ? 0u : 1u) && !identities && !unlocks);
    bool retained = !opening && (expected == ARC_LOCK_CONTENDED || expected == ARC_LOCK_TRANSIENT);
    assert(live == (unsigned)retained && closes == (opening || retained ? 0u : 1u));
    arc_lock_release(&a); assert(!live && !owner);
}
static void basic(void)
{
    fresh(); init(&a);
    assert(mkdirs == 3 && stats == 3 && !strcmp(mkdir_path, "/save/arcdroid/locks-riscpc"));
    assert(!strcmp(a.path, "/save/arcdroid/locks-riscpc/c-0123456789abcdef0123456789abcdef01234567.lock"));
    assert(attempt(&a) == ARC_LOCK_HELD && arc_lock_held(&a));
    assert(opens == 1 && tries == 1 && identities == 1 && live == 1);
    init(&b); assert(attempt(&b) == ARC_LOCK_CONTENDED && !arc_lock_held(&b));
    assert(opens == 2 && live == 2 && !unlocks);
    /* A menu pause longer than W never expires or breaks a holder. */
    now += UINT64_C(121000000000);
    for (unsigned i = 0; i < 200; ++i) assert(attempt(&b) == ARC_LOCK_CONTENDED);
    assert(opens == 2 && !unlocks && arc_lock_verify(&a) == ARC_LOCK_HELD);
    arc_lock_release(&b); assert(arc_lock_held(&a) && live == 1);
    init(&b); assert(attempt(&b) == ARC_LOCK_CONTENDED); /* leaked first description */
    arc_lock_release(&a); assert(!arc_lock_held(&a) && !owner);
    assert(attempt(&b) == ARC_LOCK_HELD && opens == 3); /* reuse contender's open */
    arc_lock_release(&b);
    assert(attempt(&a) == ARC_LOCK_HELD); /* same token, no directory replay */
    unsigned count = tries;
    assert(attempt(&a) == ARC_LOCK_HELD && tries == count); /* idempotent */
    arc_lock_release(&a); arc_lock_release(&a); arc_lock_release(NULL);
    assert(closes == opens && unlocks == 3 && !live);
}
static void errors(void)
{
    mapping(ARC_LOCK_OFD, false, EAGAIN, ARC_LOCK_CONTENDED);
    mapping(ARC_LOCK_OFD, false, EACCES, ARC_LOCK_ERROR);
    mapping(ARC_LOCK_OFD, true, EACCES, ARC_LOCK_CREATE_ERROR);
    mapping(ARC_LOCK_OFD, true, EROFS, ARC_LOCK_CREATE_ERROR);
    mapping(ARC_LOCK_OFD, false, EBADF, ARC_LOCK_ERROR);
    mapping(ARC_LOCK_OFD, true, ENOENT, ARC_LOCK_CREATE_ERROR);
    mapping(ARC_LOCK_OFD, false, EINVAL, ARC_LOCK_UNSUPPORTED);
    mapping(ARC_LOCK_OFD, true, EINVAL, ARC_LOCK_CREATE_ERROR);
    mapping(ARC_LOCK_FLOCK, false, EINVAL, ARC_LOCK_ERROR);
    mapping(ARC_LOCK_FLOCK, false, EWOULDBLOCK, ARC_LOCK_CONTENDED);
    for (unsigned p = ARC_LOCK_OFD; p <= ARC_LOCK_FLOCK; ++p) {
        mapping((ArcLockPlatform)p, false, EINTR, ARC_LOCK_TRANSIENT);
        mapping((ArcLockPlatform)p, true, EINTR, ARC_LOCK_TRANSIENT);
        mapping((ArcLockPlatform)p, true, EEXIST, ARC_LOCK_TRANSIENT);
        mapping((ArcLockPlatform)p, false, EEXIST, ARC_LOCK_ERROR);
        const unsigned unsupported[] = {ENOSYS, ENOLCK, EOPNOTSUPP, ENOTSUP};
        for (unsigned i = 0; i < 4; ++i) for (unsigned o = 0; o < 2; ++o)
            mapping((ArcLockPlatform)p, o != 0, unsupported[i], ARC_LOCK_UNSUPPORTED);
    }
    mapping(ARC_LOCK_WINDOWS, false, 33, ARC_LOCK_CONTENDED);
    mapping(ARC_LOCK_WINDOWS, true, 33, ARC_LOCK_CREATE_ERROR);
    mapping(ARC_LOCK_WINDOWS, true, 32, ARC_LOCK_TRANSIENT);
    mapping(ARC_LOCK_WINDOWS, false, 32, ARC_LOCK_ERROR);
    mapping(ARC_LOCK_WINDOWS, true, 5, ARC_LOCK_CREATE_ERROR);
    mapping(ARC_LOCK_WINDOWS, false, 5, ARC_LOCK_ERROR);
    for (unsigned o = 0; o < 2; ++o) {
        mapping(ARC_LOCK_WINDOWS, o != 0, 1, ARC_LOCK_UNSUPPORTED);
        mapping(ARC_LOCK_WINDOWS, o != 0, 50, ARC_LOCK_UNSUPPORTED);
    }
    fresh(); init(&a); try_error = EINTR;
    assert(attempt(&a) == ARC_LOCK_TRANSIENT && live == 1);
    try_error = 0; assert(attempt(&a) == ARC_LOCK_HELD && opens == 1);
    arc_lock_release(&a);
}
static void identity_and_vfs(void)
{
    fresh(); init(&a); mismatch = true;
    assert(attempt(&a) == ARC_LOCK_TRANSIENT && !live && !owner && unlocks == 1 && closes == 1);
    mismatch = false; assert(attempt(&a) == ARC_LOCK_HELD && opens == 2);
    mismatch = true; assert(arc_lock_verify(&a) == ARC_LOCK_IDENTITY_LOST && !arc_lock_held(&a));
    assert(!live && !owner && unlocks == 2 && closes == 2);
    mismatch = false;
    assert(arc_lock_verify(&a) == ARC_LOCK_IDENTITY_LOST && attempt(&a) == ARC_LOCK_IDENTITY_LOST);
    assert(opens == 2); arc_lock_release(&a);
    assert(arc_lock_verify(&a) == ARC_LOCK_ERROR);
    assert(attempt(&a) == ARC_LOCK_HELD); arc_lock_release(&a);
    const int32_t flags[] = {0, -1, 2, 3, 5};
    for (unsigned i = 0; i < 5; ++i) {
        fresh(); init(&a); file_flags = flags[i];
        assert(attempt(&a) == ARC_LOCK_ROOT_REFUSED && !live && !owner && closes == 1 && unlocks == 1);
    }
    for (int32_t size = -1; size <= 1; size += 2) {
        fresh(); init(&a); file_size = size;
        assert(attempt(&a) == ARC_LOCK_ROOT_REFUSED && !live && !owner);
    }
    fresh(); init(&a); arc_io_set_vfs(NULL);
    assert(attempt(&a) == ARC_LOCK_ROOT_REFUSED && !live);
    fresh(); mkdir_result = -1;
    assert(arc_lock_content(&a, "/save", content, &backend, &now) == ARC_LOCK_TRANSIENT);
    assert(attempt(&a) == ARC_LOCK_CREATE_ERROR && !opens && mkdirs == 1 && !stats);
    const int32_t dirs[] = {0, -1, 1, 2, 5};
    for (unsigned i = 0; i < 5; ++i) {
        fresh(); directory_flags = dirs[i];
        assert(arc_lock_content(&a, "/save", content, &backend, &now) == ARC_LOCK_TRANSIENT);
        assert(attempt(&a) == ARC_LOCK_CREATE_ERROR && !opens && stats == 1);
    }
    fresh(); mkdir_result = 0; init(&a); assert(!stats && mkdirs == 3);
    assert(attempt(&a) == ARC_LOCK_HELD); arc_lock_release(&a);
    fresh(); arc_io_set_vfs(NULL); assert(arc_io_mkdir("/x") == ARC_IO_MKDIR_ERROR);
    struct retro_vfs_interface unavailable = {0}; arc_io_set_vfs(&unavailable);
    assert(arc_io_mkdir("/x") == ARC_IO_MKDIR_ERROR);
    arc_io_set_vfs(&vfs); assert(arc_io_mkdir(NULL) == ARC_IO_MKDIR_ERROR);
    assert(arc_io_mkdir("") == ARC_IO_MKDIR_ERROR && !mkdirs);
    arc_io_measure_begin(); assert(arc_io_mkdir("/x") == ARC_IO_MKDIR_EXISTS);
    ArcIoMeasurement m = arc_io_measure_end();
    assert(m.operations == 1 && m.namespace_operations == 1 && !m.bytes && !stats);
}
static void paths(void)
{
    fresh();
    const char *roots[] = {NULL, "", "save", "./save", "saf://save", "/saf://save", "C:save", "C:/save"};
    for (unsigned i = 0; i < sizeof(roots)/sizeof(*roots); ++i) {
        assert(arc_lock_content(&a, roots[i], content, &backend, &now) == ARC_LOCK_ROOT_REFUSED);
        assert(attempt(&a) == ARC_LOCK_ROOT_REFUSED && !arc_lock_next_namespace(&a) && !arc_lock_held(&a));
        assert(arc_lock_verify(&a) == ARC_LOCK_ROOT_REFUSED); arc_lock_release(&a);
    }
    assert(!arc_lock_held(NULL) && !arc_lock_next_namespace(NULL));
    assert(arc_lock_acquire(NULL) == ARC_LOCK_ROOT_REFUSED && arc_lock_verify(NULL) == ARC_LOCK_ROOT_REFUSED);
    const char *bad_keys[] = {NULL, "", "a", "0123456789abcdef0123456789abcdef012345678", "g123456789abcdef0123456789abcdef01234567", "../3456789abcdef0123456789abcdef01234567"};
    for (unsigned i = 0; i < sizeof(bad_keys)/sizeof(*bad_keys); ++i)
        assert(arc_lock_content(&a, "/save", bad_keys[i], &backend, &now) == ARC_LOCK_ROOT_REFUSED);
    char huge[4200]; memset(huge, 'x', sizeof(huge)); huge[0] = '/'; huge[sizeof(huge)-1] = 0;
    assert(arc_lock_content(&a, huge, content, &backend, &now) == ARC_LOCK_ROOT_REFUSED);
    huge[4060] = 0;
    assert(arc_lock_content(&a, huge, content, &backend, &now) == ARC_LOCK_ROOT_REFUSED);
    assert(arc_lock_defaults(&a, "/save", firmware, 4, 1, 2, &backend, &now) == ARC_LOCK_ROOT_REFUSED);
    assert(arc_lock_defaults(&a, "/save", "bad", 0, 1, 2, &backend, &now) == ARC_LOCK_ROOT_REFUSED);
    assert(arc_lock_content(&a, "/save///", "ABCDEF0123456789ABCDEF0123456789ABCDEF01", &backend, &now) == ARC_LOCK_TRANSIENT);
    assert(!strcmp(a.path, "/save/arcdroid/locks-riscpc/c-abcdef0123456789abcdef0123456789abcdef01.lock"));
    assert(arc_lock_defaults(&a, "/save", firmware, 3, UINT_MAX, UINT_MAX, &backend, &now) == ARC_LOCK_TRANSIENT);
    char expected[4096]; snprintf(expected, sizeof(expected), "/save/arcdroid/locks-riscpc/d-%s-m4-p%u-t%u.lock", firmware, UINT_MAX, UINT_MAX);
    assert(!strcmp(a.path, expected));
    assert(arc_lock_content(&a, "/", content, &backend, &now) == ARC_LOCK_TRANSIENT);
    assert(!strncmp(a.path, "/arcdroid/", 10) && a.cursor == 1);
    backend.platform = ARC_LOCK_WINDOWS;
    const char *bad_windows[] = {"/save", "C:save", "C:", "x\\server\\share", "\\\\server", "\\\\server\\", "\\\\server\\\\share", "\\\\?\\C:\\save", "\\\\.\\pipe\\save"};
    for (unsigned i = 0; i < sizeof(bad_windows)/sizeof(*bad_windows); ++i)
        assert(arc_lock_content(&a, bad_windows[i], content, &backend, &now) == ARC_LOCK_ROOT_REFUSED);
    assert(arc_lock_content(&a, "C:\\save\\", content, &backend, &now) == ARC_LOCK_TRANSIENT);
    assert(!strncmp(a.path, "C:\\save/arcdroid/", 17) && a.cursor == 3);
    assert(arc_lock_content(&a, "\\\\server\\share", content, &backend, &now) == ARC_LOCK_TRANSIENT);
    directories(&a); assert(mkdirs == 2 && !strncmp(mkdir_path, "\\\\server\\share/arcdroid/", 24));
    for (unsigned i = 0; i < 7; ++i) {
        ArcLockBackend invalid = backend;
        if (i == 0) invalid.open_handle = NULL;
        if (i == 1) invalid.try_lock = NULL;
        if (i == 2) invalid.identity = NULL;
        if (i == 3) invalid.unlock = NULL;
        if (i == 4) invalid.close_handle = NULL;
        if (i == 5) invalid.platform = (ArcLockPlatform)3;
        if (i == 6) invalid.platform = (ArcLockPlatform)-1;
        assert(arc_lock_content(&a, "C:/save", content, &invalid, &now) == ARC_LOCK_ROOT_REFUSED);
        assert(arc_lock_content(&a, "/save", content, &invalid, &now) == ARC_LOCK_ROOT_REFUSED);
    }
    assert(!opens && !tries);
}
int main(void)
{
    fresh();
    assert(arc_lock_content(NULL, "/save", content, &backend, &now) == ARC_LOCK_ROOT_REFUSED);
    assert(arc_lock_defaults(NULL, "/save", firmware, 0, 1, 2, &backend, &now) == ARC_LOCK_ROOT_REFUSED);
    assert(arc_lock_acquire(NULL) == ARC_LOCK_ROOT_REFUSED);
    assert(arc_lock_verify(NULL) == ARC_LOCK_ROOT_REFUSED);
    assert(!arc_lock_held(NULL) && !arc_lock_next_namespace(NULL));
    arc_lock_release(NULL);
    assert(!opens && !tries && !identities && !unlocks && !closes && !mkdirs && !stats);
    basic(); errors(); identity_and_vfs(); paths();
    puts("rpc-lock-unit: ownership, error domains, identity, paths and mkdir boundaries passed");
    return 0;
}
