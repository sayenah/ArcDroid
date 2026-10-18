/* Zero-byte RiscPC sentinel locks, D-193's sole native file-I/O exception.
 * Copyright 2026 ArcDroid contributors. GPL-2.0-only. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include "arc_lock.h"
#include "arc_io.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <sys/file.h>
#elif !defined(F_OFD_SETLK)
#error "RiscPC ownership requires F_OFD_SETLK (D-192); no classic-lock fallback"
#endif
#endif

#if defined(_WIN32)
static unsigned native_open(void *opaque, const char *path, intptr_t *handle)
{
    (void)opaque;
    WCHAR wide[4096];
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, 4096))
        return GetLastError();
    HANDLE file = CreateFileW(wide, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return GetLastError();
    *handle = (intptr_t)file;
    return 0;
}
static unsigned native_try_lock(void *opaque, intptr_t handle)
{
    (void)opaque;
    OVERLAPPED overlap = {0};
    if (LockFileEx((HANDLE)handle, LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &overlap)) return 0;
    return GetLastError();
}
static bool native_identity(void *opaque, intptr_t handle, const char *path)
{
    /* No FILE_SHARE_DELETE: replacement/deletion is refused by Windows. */
    (void)opaque; (void)handle; (void)path;
    return true;
}
static void native_unlock(void *opaque, intptr_t handle)
{
    (void)opaque;
    OVERLAPPED overlap = {0};
    (void)UnlockFileEx((HANDLE)handle, 0, 1, 0, &overlap);
}
static void native_close(void *opaque, intptr_t handle)
{ (void)opaque; (void)CloseHandle((HANDLE)handle); }
#define NATIVE_PLATFORM ARC_LOCK_WINDOWS
#else
static unsigned native_open(void *opaque, const char *path, intptr_t *handle)
{
    (void)opaque;
    int fd = open(path, O_RDWR|O_CREAT|O_CLOEXEC, 0600);
    if (fd < 0) return (unsigned)errno;
    *handle = fd;
    return 0;
}
static unsigned native_try_lock(void *opaque, intptr_t handle)
{
    (void)opaque;
#if defined(__APPLE__)
    if (flock((int)handle, LOCK_EX|LOCK_NB) == 0) return 0;
#else
    struct flock lock = {0};
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    if (fcntl((int)handle, F_OFD_SETLK, &lock) == 0) return 0;
#endif
    return (unsigned)errno;
}
static bool native_identity(void *opaque, intptr_t handle, const char *path)
{
    (void)opaque;
    struct stat descriptor, named;
    return fstat((int)handle, &descriptor) == 0 && stat(path, &named) == 0 &&
        descriptor.st_nlink >= 1 && named.st_nlink >= 1 &&
        descriptor.st_dev == named.st_dev && descriptor.st_ino == named.st_ino;
}
static void native_unlock(void *opaque, intptr_t handle)
{
    (void)opaque;
#if defined(__APPLE__)
    (void)flock((int)handle, LOCK_UN);
#else
    struct flock lock = {0};
    lock.l_type = F_UNLCK;
    lock.l_whence = SEEK_SET;
    (void)fcntl((int)handle, F_OFD_SETLK, &lock);
#endif
}
static void native_close(void *opaque, intptr_t handle)
{
    /* Never retry close after EINTR: the fd may already have been released. */
    (void)opaque; (void)close((int)handle);
}
#if defined(__APPLE__)
#define NATIVE_PLATFORM ARC_LOCK_FLOCK
#else
#define NATIVE_PLATFORM ARC_LOCK_OFD
#endif
#endif
static const ArcLockBackend native_backend = {
    NATIVE_PLATFORM, native_open, native_try_lock, native_identity,
    native_unlock, native_close
};

static ArcLockResult mapped_error(ArcLockPlatform platform, bool opening, unsigned error)
{
    if (platform == ARC_LOCK_WINDOWS) {
        /* WinError.h constants, kept numeric so flag-free tests can inject
         * every platform's results on every host. */
        if (!opening && error == 33) return ARC_LOCK_CONTENDED; /* ERROR_LOCK_VIOLATION */
        if (opening && error == 32) return ARC_LOCK_TRANSIENT; /* ERROR_SHARING_VIOLATION */
        if (error == 1 || error == 50) return ARC_LOCK_UNSUPPORTED;
    } else {
        if (!opening && ((platform == ARC_LOCK_OFD && error == EAGAIN) ||
                         (platform == ARC_LOCK_FLOCK && error == EWOULDBLOCK)))
            return ARC_LOCK_CONTENDED;
        if (error == EINTR || (opening && error == EEXIST)) return ARC_LOCK_TRANSIENT;
        if (error == ENOSYS || error == ENOLCK || error == EOPNOTSUPP || error == ENOTSUP ||
            (!opening && platform == ARC_LOCK_OFD && error == EINVAL))
            return ARC_LOCK_UNSUPPORTED;
    }
    return opening ? ARC_LOCK_CREATE_ERROR : ARC_LOCK_ERROR;
}

static bool separator(const ArcLock *lock, char c)
{ return c == '/' || (lock->backend->platform == ARC_LOCK_WINDOWS && c == '\\'); }

/* Returns first creatable prefix; UNC server/share and drive root already exist.
 * Win32 device namespaces are not save roots. POSIX backslashes are ordinary. */
static size_t root_cursor(const ArcLock *lock, const char *save)
{
    size_t n = save ? strlen(save) : 0;
    if (!n || strstr(save, "://")) return 0;
    if (lock->backend->platform != ARC_LOCK_WINDOWS) return save[0] == '/' ? 1 : 0;
    if (n >= 3 && ((save[0] >= 'A' && save[0] <= 'Z') ||
                  (save[0] >= 'a' && save[0] <= 'z')) && save[1] == ':' && separator(lock, save[2]))
        return 3;
    if (n < 5 || !separator(lock, save[0]) || !separator(lock, save[1]) ||
        separator(lock, save[2]) || save[2] == '?' || save[2] == '.') return 0;
    size_t at = 2;
    while (at < n && !separator(lock, save[at])) ++at;
    if (at == n || ++at == n || separator(lock, save[at])) return 0;
    while (at < n && !separator(lock, save[at])) ++at;
    return at < n ? at + 1 : at;
}

static bool hex_key(const char *key, size_t n, char *out)
{
    if (!key || strlen(key) != n) return false;
    for (size_t i = 0; i < n; ++i) {
        char c = key[i];
        if (c >= 'A' && c <= 'F') c += 'a' - 'A';
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
        out[i] = c;
    }
    out[n] = 0;
    return true;
}

static ArcLockResult initialize(ArcLock *lock, const char *save, const char *key,
                                const ArcLockBackend *backend, void *opaque)
{
    if (!lock) return ARC_LOCK_ROOT_REFUSED;
    memset(lock, 0, sizeof(*lock));
    lock->backend = backend ? backend : &native_backend;
    lock->opaque = opaque;
    if (!lock->backend->open_handle || !lock->backend->try_lock || !lock->backend->identity ||
        !lock->backend->unlock || !lock->backend->close_handle || (unsigned)lock->backend->platform > ARC_LOCK_WINDOWS)
        return ARC_LOCK_ROOT_REFUSED;
    lock->cursor = root_cursor(lock, save);
    if (!lock->cursor || !key) return ARC_LOCK_ROOT_REFUSED;
    size_t n = strlen(save);
    if (n >= sizeof(lock->path)) return ARC_LOCK_ROOT_REFUSED;
    while (n > lock->cursor && separator(lock, save[n - 1])) --n;
    const char *slash = separator(lock, save[n - 1]) ? "" : "/";
    int length = snprintf(lock->path, sizeof(lock->path), "%.*s%sarcdroid/locks-riscpc/%s",
                          (int)n, save, slash, key);
    if (length < 0 || (size_t)length >= sizeof(lock->path)) return ARC_LOCK_ROOT_REFUSED;
    lock->directory_length = (size_t)(strrchr(lock->path, '/') - lock->path);
    while (lock->cursor < lock->directory_length && separator(lock, lock->path[lock->cursor])) ++lock->cursor;
    lock->valid = true;
    return ARC_LOCK_TRANSIENT;
}

ArcLockResult arc_lock_content(ArcLock *lock, const char *save, const char *content,
                               const ArcLockBackend *backend, void *opaque)
{
    char hex[41], key[48];
    bool valid = hex_key(content, 40, hex);
    if (valid) snprintf(key, sizeof(key), "c-%s.lock", hex);
    return initialize(lock, save, valid ? key : NULL, backend, opaque);
}

ArcLockResult arc_lock_defaults(ArcLock *lock, const char *save, const char *firmware,
                                unsigned model, unsigned procedure, unsigned timing,
                                const ArcLockBackend *backend, void *opaque)
{
    char hex[65], key[112];
    bool valid = model < 4 && hex_key(firmware, 64, hex);
    if (valid) snprintf(key, sizeof(key), "d-%s-m%u-p%u-t%u.lock", hex, model + 1, procedure, timing);
    return initialize(lock, save, valid ? key : NULL, backend, opaque);
}

bool arc_lock_next_namespace(const ArcLock *lock)
{ return lock && lock->valid && !lock->parents_done; }

bool arc_lock_held(const ArcLock *lock)
{ return lock && lock->valid && lock->held && !lock->lost; }

void arc_lock_release(ArcLock *lock)
{
    if (!lock) return;
    if (lock->held) lock->backend->unlock(lock->opaque, lock->handle);
    if (lock->opened) lock->backend->close_handle(lock->opaque, lock->handle);
    lock->held = lock->opened = lock->lost = false;
}

static bool identity(ArcLock *lock)
{
    if(lock->identity_observer)lock->identity_observer(lock->identity_opaque,true);
    bool ok=lock->backend->identity(lock->opaque,lock->handle,lock->path);
    if(lock->identity_observer)lock->identity_observer(lock->identity_opaque,false);
    return ok;
}

ArcLockResult arc_lock_verify(ArcLock *lock)
{
    if (!lock || !lock->valid) return ARC_LOCK_ROOT_REFUSED;
    if (lock->lost) return ARC_LOCK_IDENTITY_LOST;
    if (!lock->held) return ARC_LOCK_ERROR;
    if (!identity(lock)) {
        arc_lock_release(lock);
        lock->lost = true;
        return ARC_LOCK_IDENTITY_LOST;
    }
    return ARC_LOCK_HELD;
}

ArcLockResult arc_lock_acquire(ArcLock *lock)
{
    if (!lock || !lock->valid) return ARC_LOCK_ROOT_REFUSED;
    if (lock->lost) return ARC_LOCK_IDENTITY_LOST;
    if (lock->held) return arc_lock_verify(lock);
    if (!lock->parents_done) {
        size_t at = lock->cursor;
        while (at < lock->directory_length && !separator(lock, lock->path[at])) ++at;
        /* Repeated separators do not denote an additional mkdir unit. */
        if (at > lock->cursor) {
            char saved = lock->path[at]; lock->path[at] = 0;
            ArcIoMkdirResult result = arc_io_mkdir(lock->path);
            bool ok = result == ARC_IO_MKDIR_CREATED ||
                (result == ARC_IO_MKDIR_EXISTS && arc_io_metadata(lock->path, NULL) == ARC_IO_METADATA_DIRECTORY);
            lock->path[at] = saved;
            if (!ok) return ARC_LOCK_CREATE_ERROR;
        }
        lock->cursor = at + 1;
        while (lock->cursor < lock->directory_length && separator(lock, lock->path[lock->cursor])) ++lock->cursor;
        lock->parents_done = at == lock->directory_length;
        return ARC_LOCK_TRANSIENT;
    }
    unsigned error;
    if (!lock->opened) {
        error = lock->backend->open_handle(lock->opaque, lock->path, &lock->handle);
        if (error) return mapped_error(lock->backend->platform, true, error);
        lock->opened = true;
    }
    error = lock->backend->try_lock(lock->opaque, lock->handle);
    if (error) {
        ArcLockResult result = mapped_error(lock->backend->platform, false, error);
        if (result != ARC_LOCK_CONTENDED && result != ARC_LOCK_TRANSIENT) arc_lock_release(lock);
        return result;
    }
    lock->held = true;
    if (!identity(lock)) {
        arc_lock_release(lock);
        return ARC_LOCK_TRANSIENT;
    }
    size_t size;
    if (arc_io_metadata(lock->path, &size) != ARC_IO_METADATA_FILE || size != 0) {
        arc_lock_release(lock);
        return ARC_LOCK_ROOT_REFUSED;
    }
    return ARC_LOCK_HELD;
}
