/* Execute each production native branch against syscall contracts. GPL-2.0-only. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include "arc_lock.h"
#include "arc_io.h"
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <unistd.h>

static unsigned calls, raw_error, close_calls, unlock_calls;
#if ARC_LOCK_NATIVE_KIND == 2
#include "lock-shims/windows.h"
static bool conversion_error;
int MultiByteToWideChar(unsigned cp, DWORD flags, const char *path, int n, WCHAR *wide, int cap)
{
    assert(cp == CP_UTF8 && flags == MB_ERR_INVALID_CHARS && !strcmp(path, "/sentinel") && n == -1 && cap == 4096);
    if (conversion_error) return 0;
    wcscpy(wide, L"sentinel"); return 9;
}
DWORD GetLastError(void) { return raw_error; }
HANDLE CreateFileW(const WCHAR *path, DWORD access, DWORD sharing, void *security, DWORD create, DWORD flags, HANDLE template_file)
{
    ++calls;
    assert(!wcscmp(path, L"sentinel") && access == (GENERIC_READ|GENERIC_WRITE));
    assert(sharing == (FILE_SHARE_READ|FILE_SHARE_WRITE) && security == NULL);
    assert(create == OPEN_ALWAYS && flags == FILE_ATTRIBUTE_NORMAL && template_file == NULL);
    return raw_error ? INVALID_HANDLE_VALUE : (HANDLE)(intptr_t)17;
}
static void overlap_contract(HANDLE file, DWORD reserved, DWORD lo, DWORD hi, OVERLAPPED *overlap)
{
    assert(file == (HANDLE)(intptr_t)17 && !reserved && lo == 1 && !hi);
    assert(!overlap->Internal && !overlap->InternalHigh && !overlap->Offset && !overlap->OffsetHigh && !overlap->hEvent);
}
BOOL LockFileEx(HANDLE file, DWORD flags, DWORD reserved, DWORD lo, DWORD hi, OVERLAPPED *overlap)
{
    ++calls; overlap_contract(file, reserved, lo, hi, overlap);
    assert(flags == (LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY));
    return !raw_error;
}
BOOL UnlockFileEx(HANDLE file, DWORD reserved, DWORD lo, DWORD hi, OVERLAPPED *overlap)
{ ++unlock_calls; overlap_contract(file, reserved, lo, hi, overlap); return 1; }
BOOL CloseHandle(HANDLE file)
{ ++close_calls; assert(file == (HANDLE)(intptr_t)17); return 1; }
#define _WIN32 1
#else
static struct stat descriptor, named;
static bool fail_fstat, fail_stat;
static int syscall_open(const char *path, int flags, ...)
{
    va_list args; va_start(args, flags); int mode = va_arg(args, int); va_end(args);
    assert(!strcmp(path, "/sentinel") && flags == (O_RDWR|O_CREAT|O_CLOEXEC) && mode == 0600);
    ++calls; errno = (int)raw_error; return raw_error ? -1 : 17;
}
static int syscall_close(int fd)
{ assert(fd == 17); ++close_calls; errno = EINTR; return -1; }
static int syscall_fstat(int fd, struct stat *out)
{ assert(fd == 17); *out = descriptor; return fail_fstat ? -1 : 0; }
static int syscall_stat(const char *path, struct stat *out)
{ assert(!strcmp(path, "/sentinel")); *out = named; return fail_stat ? -1 : 0; }
#if ARC_LOCK_NATIVE_KIND == 0
static int syscall_fcntl(int fd, int command, ...)
{
    va_list args; va_start(args, command); struct flock *lock = va_arg(args, struct flock *); va_end(args);
    assert(fd == 17 && command == F_OFD_SETLK);
    assert(lock->l_type == F_WRLCK || lock->l_type == F_UNLCK);
    assert(lock->l_whence == SEEK_SET && !lock->l_start && !lock->l_len && !lock->l_pid);
    if (lock->l_type == F_UNLCK) ++unlock_calls; else ++calls;
    errno = (int)raw_error; return raw_error ? -1 : 0;
}
#define fcntl(...) syscall_fcntl(__VA_ARGS__)
#ifdef __APPLE__
#undef __APPLE__
#endif
#else
static int syscall_flock(int fd, int operation)
{
    assert(fd == 17 && (operation == (LOCK_EX|LOCK_NB) || operation == LOCK_UN));
    if (operation == LOCK_UN) ++unlock_calls; else ++calls;
    errno = (int)raw_error; return raw_error ? -1 : 0;
}
#define flock(...) syscall_flock(__VA_ARGS__)
#ifndef __APPLE__
#define __APPLE__ 1
#endif
#endif
#define open(...) syscall_open(__VA_ARGS__)
#define close(...) syscall_close(__VA_ARGS__)
#define stat(...) syscall_stat(__VA_ARGS__)
#define fstat(...) syscall_fstat(__VA_ARGS__)
#endif
/* Include the shipping translation unit unchanged, not a reimplementation. */
#include "../libretro/arc_lock.c"

int main(void)
{
    intptr_t handle = 0;
    assert(native_open(NULL, "/sentinel", &handle) == 0 && handle == 17 && calls == 1);
    assert(native_try_lock(NULL, handle) == 0 && calls == 2);
    raw_error = 5;
    assert(native_try_lock(NULL, handle) == 5 && calls == 3);
    assert(native_open(NULL, "/sentinel", &handle) == 5);
#if ARC_LOCK_NATIVE_KIND == 2
    conversion_error = true;
    unsigned before = calls;
    assert(native_open(NULL, "/sentinel", &handle) == 5 && calls == before);
    conversion_error = false;
#endif
    raw_error = 0;
#if ARC_LOCK_NATIVE_KIND != 2
    descriptor.st_dev = named.st_dev = 7; descriptor.st_ino = named.st_ino = 9;
    descriptor.st_nlink = named.st_nlink = 1;
    assert(native_identity(NULL, 17, "/sentinel"));
    ++named.st_dev; assert(!native_identity(NULL, 17, "/sentinel")); --named.st_dev;
    ++named.st_ino; assert(!native_identity(NULL, 17, "/sentinel")); --named.st_ino;
    named.st_nlink = 0; assert(!native_identity(NULL, 17, "/sentinel")); named.st_nlink = 1;
    descriptor.st_nlink = 0; assert(!native_identity(NULL, 17, "/sentinel")); descriptor.st_nlink = 1;
    fail_fstat = true; assert(!native_identity(NULL, 17, "/sentinel")); fail_fstat = false;
    fail_stat = true; assert(!native_identity(NULL, 17, "/sentinel")); fail_stat = false;
#else
    assert(native_identity(NULL, 17, "/sentinel"));
#endif
    native_unlock(NULL, handle); native_close(NULL, handle);
    assert(unlock_calls == 1 && close_calls == 1);
    printf("rpc-lock-native-contract: branch=%d passed\n", ARC_LOCK_NATIVE_KIND);
    return 0;
}
