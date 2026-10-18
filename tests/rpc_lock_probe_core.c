/* Test-only P10b app-context probe; never installed. GPL-2.0-only. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include "arc_lock.h"
#include "arc_io.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#include <direct.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#if defined(__ANDROID__)
#include <sys/system_properties.h>
#endif

#define CAP 4096
#define MAX_LEGS 16
static const char key[] = "0123456789abcdef0123456789abcdef01234567";
typedef enum { INFO, TRY, SAME, FORK, REPLACE_LEG, DELETE_LEG, HOLD } Leg;
static const char *const leg_names[] = {"info", "try", "same-process", "fork-contend", "replace", "delete", "hold"};
static const char *const outcomes[] = {"held", "contended", "transient", "unsupported",
    "root-refused", "create-error", "lock-error", "identity-lost"};
static struct {
    char root[CAP], output[CAP];
    Leg legs[MAX_LEGS];
    unsigned count, next;
    uint64_t frame;
    FILE *log;
    ArcLock holder;
    bool holding, failed;
    unsigned mutation_frame;
    Leg mutation_leg;
    bool mutation_succeeded;
    ArcLockResult last;
} probe;
static retro_video_refresh_t video;
static retro_audio_sample_batch_t audio;
static retro_input_poll_t poll_input;
static retro_environment_t environment;

#if defined(_WIN32)
static bool wide_path(const char *path, WCHAR *wide)
{ return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, CAP) != 0; }
#endif
static FILE *path_file(const char *path, bool write_file)
{
#if defined(_WIN32)
    WCHAR wide[CAP];
    return wide_path(path, wide) ? _wfopen(wide, write_file ? L"wb" : L"rb") : NULL;
#else
    return fopen(path, write_file ? "wb" : "rb");
#endif
}
static bool line(const char *format, ...)
{
    if (!probe.log || probe.failed) return false;
    va_list args; va_start(args, format);
    int result = vfprintf(probe.log, format, args); va_end(args);
    if (result < 0 || fputc('\n', probe.log) == EOF || fflush(probe.log)) {
        probe.failed = true;
        arc_lock_release(&probe.holder);
        return false;
    }
    return true;
}
/* Canonical separators and no traversal, device namespace or alternate streams.
 * Root aliases themselves are allowed; descendants must not redirect writes. */
static bool clean_path(char *path)
{
    size_t n = strlen(path);
    if (!n || n >= CAP - 128 || strstr(path, "://")) return false;
    for (size_t i = 0; i < n; ++i) {
        if ((unsigned char)path[i] < 32 || (unsigned char)path[i] == 127) return false;
#if defined(_WIN32)
        if (path[i] == '\\') path[i] = '/';
        if (path[i] == ':' && i != 1) return false;
        if (strchr("<>\"|?*", path[i])) return false;
#endif
    }
#if defined(_WIN32)
    bool drive = n >= 3 && ((path[0] >= 'A' && path[0] <= 'Z') ||
        (path[0] >= 'a' && path[0] <= 'z')) && path[1] == ':' && path[2] == '/';
    bool unc = n >= 5 && path[0] == '/' && path[1] == '/' &&
        path[2] != '?' && path[2] != '.' && strchr(path + 2, '/');
    if (!drive && !unc) return false;
#else
    if (path[0] != '/') return false;
#endif
    /* Remove repeated separators, retaining the Windows UNC prefix. */
    size_t used = 0;
    for (size_t i = 0; i < n; ++i) {
        if (path[i] == '/' && used && path[used - 1] == '/') {
#if defined(_WIN32)
            if (i != 1) continue;
#else
            continue;
#endif
        }
        path[used++] = path[i];
    }
    path[n = used] = 0;
    for (const char *part = path; *part;) {
        if (*part == '/') { ++part; continue; }
        size_t length = strcspn(part, "/");
        if ((length == 1 && *part == '.') || (length == 2 && !strncmp(part, "..", 2))) return false;
#if defined(_WIN32)
        if (part[length - 1] == '.' || part[length - 1] == ' ') return false;
#endif
        part += length;
    }
    while (n > 1 && path[n - 1] == '/' && path[n - 2] != ':') path[--n] = 0;
    return true;
}
static bool path_prefix(const char *path, const char *prefix, size_t length)
{
#if defined(_WIN32)
    return !_strnicmp(path, prefix, length);
#else
    return !strncmp(path, prefix, length);
#endif
}
static bool below_root(const char *path)
{
    size_t n = strlen(probe.root);
    return path_prefix(path, probe.root, n) && (probe.root[n - 1] == '/' || path[n] == '/');
}
static void root_path(char *out, const char *leaf)
{
    /* clean_path reserved 128 bytes for our fixed descendants. */
    size_t n = strlen(probe.root);
    memcpy(out, probe.root, n);
    if (out[n - 1] != '/') out[n++] = '/';
    strcpy(out + n, leaf);
}
static int32_t probe_stat(const char *path, int32_t *size)
{
#if defined(_WIN32)
    WCHAR wide[CAP]; WIN32_FILE_ATTRIBUTE_DATA s;
    if (!wide_path(path, wide) || !GetFileAttributesExW(wide, GetFileExInfoStandard, &s)) return 0;
    if (size) *size = s.nFileSizeHigh || s.nFileSizeLow > INT32_MAX ? INT32_MAX : (int32_t)s.nFileSizeLow;
    return RETRO_VFS_STAT_IS_VALID | ((s.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? RETRO_VFS_STAT_IS_DIRECTORY : 0);
#else
    struct stat s;
    if (stat(path, &s)) return 0;
    if (size) *size = s.st_size > INT32_MAX ? INT32_MAX : (int32_t)s.st_size;
    return RETRO_VFS_STAT_IS_VALID | (S_ISDIR(s.st_mode) ? RETRO_VFS_STAT_IS_DIRECTORY :
        S_ISREG(s.st_mode) ? 0 : RETRO_VFS_STAT_IS_CHARACTER_SPECIAL);
#endif
}
static int probe_mkdir(const char *path)
{
    /* The production stepper visits ancestors. Existing directories are fine;
     * creating missing ancestors outside the authored root is forbidden. */
    if (probe_stat(path, NULL) == (RETRO_VFS_STAT_IS_VALID | RETRO_VFS_STAT_IS_DIRECTORY)) return -2;
    if (!below_root(path)) return -1;
#if defined(_WIN32)
    WCHAR wide[CAP]; if (!wide_path(path, wide)) return -1;
    int result = _wmkdir(wide);
#else
    int result = mkdir(path, 0700);
#endif
    return !result ? 0 : errno == EEXIST ? -2 : -1;
}
static struct retro_vfs_interface vfs = {.stat = probe_stat, .mkdir = probe_mkdir};
/* Refuse pre-existing symlinks/reparse points below the root, including a
 * sentinel hard link. The authored disposable namespace must remain stable
 * except for the explicit identity-loss experiments (the production contract). */
static bool plain_entry(const char *path)
{
#if defined(_WIN32)
    WCHAR wide[CAP]; if (!wide_path(path, wide)) return false;
    DWORD attr = GetFileAttributesW(wide);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
    }
    if (attr & FILE_ATTRIBUTE_REPARSE_POINT) return false;
    if (attr & FILE_ATTRIBUTE_DIRECTORY) return true;
    HANDLE h = CreateFileW(wide, FILE_READ_ATTRIBUTES, FILE_SHARE_READ|FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    BY_HANDLE_FILE_INFORMATION s;
    bool ok = GetFileInformationByHandle(h, &s) && s.nNumberOfLinks == 1;
    CloseHandle(h);
    return ok;
#else
    struct stat s;
    if (lstat(path, &s)) return errno == ENOENT;
    return S_ISDIR(s.st_mode) || (S_ISREG(s.st_mode) && s.st_nlink == 1);
#endif
}
static bool namespace_safe(const char *sentinel)
{
    char path[CAP];
    root_path(path, "arcdroid");
    if (!plain_entry(path)) return false;
    strcat(path, "/locks-riscpc");
    return plain_entry(path) && plain_entry(sentinel);
}
static ArcLockResult prepare(ArcLock *lock)
{
    ArcLockResult result = arc_lock_content(lock, probe.root, key, NULL, NULL);
    if (result != ARC_LOCK_TRANSIENT || !namespace_safe(lock->path)) return ARC_LOCK_ROOT_REFUSED;
    /* Namespace steps are not native lock attempts. Never retry the attempt. */
    for (unsigned steps = 0; arc_lock_next_namespace(lock) && steps < CAP; ++steps) {
        result = arc_lock_acquire(lock);
        if (result != ARC_LOCK_TRANSIENT) return result;
    }
    return arc_lock_next_namespace(lock) ? ARC_LOCK_CREATE_ERROR : ARC_LOCK_TRANSIENT;
}
static ArcLockResult acquire(ArcLock *lock, unsigned *attempts)
{
    *attempts = 0;
    ArcLockResult result = prepare(lock);
    if (result != ARC_LOCK_TRANSIENT) return result;
    *attempts = 1;
    return arc_lock_acquire(lock);
}
/* stat / file-ID queries follow aliases, including parent-directory aliases.
 * Missing protected files are normal before the first leg; other uncertainty
 * refuses the plan. No namespace is created during these admission queries. */
typedef struct { uint64_t device, inode; } FileId;
static int resolved_id(const char *path, FileId *id)
{
#if defined(_WIN32)
    WCHAR wide[CAP];
    if (!wide_path(path, wide)) return -1;
    HANDLE h = CreateFileW(wide, FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? 0 : -1;
    }
    BY_HANDLE_FILE_INFORMATION s;
    bool ok = GetFileInformationByHandle(h, &s);
    CloseHandle(h);
    if (!ok) return -1;
    *id = (FileId){s.dwVolumeSerialNumber, ((uint64_t)s.nFileIndexHigh << 32) | s.nFileIndexLow};
#else
    struct stat s;
    if (stat(path, &s)) return errno == ENOENT ? 0 : -1;
    *id = (FileId){(uint64_t)s.st_dev, (uint64_t)s.st_ino};
#endif
    return 1;
}
static bool same_id(FileId a, FileId b)
{ return a.device == b.device && a.inode == b.inode; }
static bool output_safe(const char *plan)
{
    char parent[CAP], protected[CAP];
    strcpy(parent, probe.output);
    char *leaf = strrchr(parent, '/');
    if (!leaf || !leaf[1]) return false;
    const char *output_leaf = probe.output + (leaf - parent) + 1;
    /* Keep native filesystem roots intact. */
    if (leaf == parent || (leaf == parent + 2 && parent[1] == ':')) leaf[1] = 0;
    else *leaf = 0;
    FileId output_id, parent_id, id;
    int output_exists = resolved_id(probe.output, &output_id);
    if (output_exists < 0 || resolved_id(parent, &parent_id) != 1) return false;
    const char *const names[] = {"arcdroid/locks-riscpc",
        "arcdroid/locks-riscpc/c-0123456789abcdef0123456789abcdef01234567.lock",
        "p10b-flock.probe", "p10b-replace.probe"};
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        root_path(protected, names[i]);
        int exists = resolved_id(protected, &id);
        if (exists < 0 || (exists && (same_id(id, parent_id) ||
            (output_exists && same_id(id, output_id))))) return false;
    }
    /* Before diagnostic/fixture creation there is no inode to compare. Resolve
     * its parent and reserve the ASCII leaf, including a case variant. */
    if (resolved_id(probe.root, &id) != 1) return false;
    if (same_id(id, parent_id)) {
        char lower[CAP]; size_t i;
        for (i = 0; output_leaf[i]; ++i) {
            char c = output_leaf[i]; lower[i] = c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
        }
        lower[i] = 0;
        if (!strcmp(lower, "p10b-flock.probe") || !strcmp(lower, "p10b-replace.probe")) return false;
    }
    int exists = resolved_id(plan, &id);
    return exists == 1 && !(output_exists && same_id(id, output_id));
}
static void identity_info(const char *name, const char *path)
{
#if defined(_WIN32)
    WCHAR wide[CAP]; BY_HANDLE_FILE_INFORMATION s;
    if (!wide_path(path, wide)) { line("identity name=%s error=utf8", name); return; }
    HANDLE h = CreateFileW(wide, FILE_READ_ATTRIBUTES, FILE_SHARE_READ|FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (h == INVALID_HANDLE_VALUE) { line("identity name=%s error=%lu", name, (unsigned long)GetLastError()); return; }
    if (GetFileInformationByHandle(h, &s))
        line("identity name=%s device=%lu inode=%llu", name, (unsigned long)s.dwVolumeSerialNumber,
            (unsigned long long)(((uint64_t)s.nFileIndexHigh << 32) | s.nFileIndexLow));
    else line("identity name=%s error=%lu", name, (unsigned long)GetLastError());
    CloseHandle(h);
#else
    struct stat s;
    if (!stat(path, &s)) line("identity name=%s device=%llu inode=%llu", name,
        (unsigned long long)s.st_dev, (unsigned long long)s.st_ino);
    else line("identity name=%s errno=%d", name, errno);
#endif
}
static void info(void)
{
    ArcLock named = {0};
    unsigned attempts;
    ArcLockResult result = acquire(&named, &attempts);
    line("result leg=info outcome=%s attempts=%u", outcomes[result], attempts);
#if defined(_WIN32)
    OSVERSIONINFOW version = {0}; version.dwOSVersionInfoSize = sizeof(version);
    /* GetVersionEx can report a manifest compatibility version. Query the
     * running NT version instead, without adding an import dependency. */
    HMODULE module = GetModuleHandleW(L"ntdll.dll");
    FARPROC symbol = module ? GetProcAddress(module, "RtlGetVersion") : NULL;
    LONG (WINAPI *query)(OSVERSIONINFOW *) = NULL;
    _Static_assert(sizeof(query) == sizeof(symbol), "function pointer size");
    memcpy(&query, &symbol, sizeof(query));
    LONG status = query ? query(&version) : -1;
    line("info pid=%lu uid=unavailable os=Windows os-release=%lu.%lu.%lu kernel=WindowsNT version-status=%ld",
         (unsigned long)GetCurrentProcessId(), (unsigned long)version.dwMajorVersion,
         (unsigned long)version.dwMinorVersion, (unsigned long)version.dwBuildNumber, (long)status);
    line("diagnostic-flock outcome=unavailable platform=Windows");
#else
    struct utsname version;
    if (!uname(&version)) line("info pid=%ld uid=%ld os=%s kernel=%s machine=%s", (long)getpid(),
        (long)getuid(), version.sysname, version.release, version.machine);
    else line("info uname-errno=%d", errno);
#if defined(__ANDROID__)
    char release[PROP_VALUE_MAX] = {0};
    __system_property_get("ro.build.version.release", release);
    line("os-release android=%s", release);
#endif
    FILE *mounts = fopen("/proc/self/mountinfo", "rb");
    if (mounts) {
        char text[8192];
        while (fgets(text, sizeof(text), mounts)) {
            text[strcspn(text, "\r\n")] = 0;
            line("mountinfo %s", text);
        }
        if (ferror(mounts)) line("mountinfo error=read");
        fclose(mounts);
    } else line("mountinfo errno=%d", errno);
    char diagnostic[CAP];
    root_path(diagnostic, "p10b-flock.probe");
    if (plain_entry(diagnostic)) {
        int fd = open(diagnostic, O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW, 0600);
        int error = errno;
        if (fd >= 0) {
            int result = flock(fd, LOCK_EX|LOCK_NB); error = result ? errno : 0;
            line("diagnostic-flock result=%d errno=%d file=p10b-flock.probe", result, error);
            /* Closing this independent description releases only its lock. */
            close(fd);
        } else line("diagnostic-flock open-errno=%d", error);
    } else line("diagnostic-flock error=redirected-path");
#endif
    identity_info("root", probe.root);
    identity_info("sentinel", named.path);
    arc_lock_release(&named);
    line("done leg=info");
}
typedef struct { int result; unsigned error; } ChildResult;
static ChildResult mutate(Leg leg, const char *sentinel, const char *replacement)
{
#if defined(_WIN32)
    WCHAR target[CAP], source[CAP];
    if (!wide_path(sentinel, target) || (leg == REPLACE_LEG && !wide_path(replacement, source)))
        return (ChildResult){-1, ERROR_NO_UNICODE_TRANSLATION};
    BOOL ok = leg == REPLACE_LEG ? MoveFileExW(source, target, MOVEFILE_REPLACE_EXISTING) : DeleteFileW(target);
    return (ChildResult){ok ? 0 : -1, ok ? 0 : GetLastError()};
#else
    int result = leg == REPLACE_LEG ? rename(replacement, sentinel) : unlink(sentinel);
    return (ChildResult){result, result ? (unsigned)errno : 0};
#endif
}
#if !defined(_WIN32)
static bool result_pipe(int channel[2])
{
#if defined(__linux__)
    return pipe2(channel, O_CLOEXEC) == 0;
#else
    /* macOS has no pipe2. Set both flags before our fork. */
    if (pipe(channel)) return false;
    if (fcntl(channel[0], F_SETFD, FD_CLOEXEC) == 0 &&
        fcntl(channel[1], F_SETFD, FD_CLOEXEC) == 0) return true;
    int error = errno; close(channel[0]); close(channel[1]); errno = error;
    return false;
#endif
}
static bool fork_result(Leg leg, ArcLock *parent, ArcLock *second,
                        const char *replacement, const char *role, ChildResult *report)
{
    int channel[2];
    if (!result_pipe(channel)) { line("result leg=%s pipe-errno=%d", leg_names[leg], errno); return false; }
    int read_flags = fcntl(channel[0], F_GETFD), write_flags = fcntl(channel[1], F_GETFD);
    if (read_flags < 0 || write_flags < 0 || !(read_flags & FD_CLOEXEC) || !(write_flags & FD_CLOEXEC)) {
        close(channel[0]); close(channel[1]);
        line("result leg=%s error=pipe-cloexec", leg_names[leg]); return false;
    }
    if (!line("pipe leg=%s role=%s cloexec=1", leg_names[leg], role)) {
        close(channel[0]); close(channel[1]); return false;
    }
    /* Namespace preparation and fixture creation are complete. The child uses
     * only native operations / the stat-only bridge, no stdio or allocation. */
    pid_t child = fork(); int fork_error = errno;
    if (!child) {
        close(channel[0]);
        ChildResult reply;
        if (leg == FORK) {
            reply = (ChildResult){(int)arc_lock_acquire(second), 0};
            arc_lock_release(second);
            /* W9: never unlock the inherited parent description. */
        } else reply = mutate(leg, parent->path, replacement);
        ssize_t wrote;
        do { wrote = write(channel[1], &reply, sizeof(reply)); } while (wrote < 0 && errno == EINTR);
        _exit(wrote == sizeof(reply) ? 0 : 1);
    }
    close(channel[1]);
    bool ok = false;
    if (child < 0) line("result leg=%s fork-errno=%d", leg_names[leg], fork_error);
    else {
        int status = -1; ssize_t got;
        do { got = read(channel[0], report, sizeof(*report)); } while (got < 0 && errno == EINTR);
        pid_t reaped;
        do { reaped = waitpid(child, &status, 0); } while (reaped < 0 && errno == EINTR);
        ok = got == sizeof(*report) && reaped == child && WIFEXITED(status) && !WEXITSTATUS(status);
        if (!ok) line("result leg=%s error=child status=%d", leg_names[leg], status);
        else if (leg == FORK && report->result >= 0 && report->result <= ARC_LOCK_IDENTITY_LOST)
            line("result leg=fork-contend role=%s pid=%ld outcome=%s attempts=1", role, (long)child, outcomes[report->result]);
        else if (leg != FORK) line("operation leg=%s role=child pid=%ld result=%d errno=%u",
            leg_names[leg], (long)child, report->result, report->error);
        else ok = false;
    }
    close(channel[0]);
    return ok;
}
static void fork_contender(ArcLock *parent, const char *role)
{
    ArcLock second = {0};
    ArcLockResult ready = prepare(&second);
    ChildResult report;
    if (ready != ARC_LOCK_TRANSIENT) line("result leg=fork-contend role=%s prepare=%s", role, outcomes[ready]);
    else (void)fork_result(FORK, parent, &second, NULL, role, &report);
    arc_lock_release(&second);
}
#endif
static void contention(Leg leg)
{
    ArcLock first = {0}, second = {0};
    unsigned attempts;
    ArcLockResult initial = acquire(&first, &attempts);
    line("result leg=%s role=parent outcome=%s attempts=%u", leg_names[leg], outcomes[initial], attempts);
    if (initial == ARC_LOCK_HELD) {
        if (leg == SAME) {
            ArcLockResult result = acquire(&second, &attempts);
            line("result leg=same-process role=second outcome=%s attempts=%u", outcomes[result], attempts);
            arc_lock_release(&second);
        } else {
#if defined(_WIN32)
            line("result leg=fork-contend outcome=unavailable platform=Windows");
#else
            fork_contender(&first, "child");
            /* Identity alone cannot detect an inherited-description unlock.
             * A new process/description proves native exclusion after reap. */
            fork_contender(&first, "retained-proof");
#endif
        }
        line("verify leg=%s role=parent outcome=%s", leg_names[leg], outcomes[arc_lock_verify(&first)]);
    }
    arc_lock_release(&first);
    line("done leg=%s", leg_names[leg]);
}
static void mutation_begin(Leg leg)
{
    unsigned attempts;
    ArcLockResult result = acquire(&probe.holder, &attempts);
    if (!line("result leg=%s role=parent outcome=%s attempts=%u", leg_names[leg], outcomes[result], attempts)) {
        arc_lock_release(&probe.holder); return;
    }
    if (result != ARC_LOCK_HELD) { arc_lock_release(&probe.holder); line("done leg=%s", leg_names[leg]); return; }
    char replacement[CAP]; root_path(replacement, "p10b-replace.probe");
    if (leg == REPLACE_LEG) {
#if defined(_WIN32)
        WCHAR wide[CAP];
        HANDLE file = wide_path(replacement, wide) ? CreateFileW(wide, GENERIC_WRITE, 0,
            NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL) : INVALID_HANDLE_VALUE;
        unsigned error = file == INVALID_HANDLE_VALUE ? GetLastError() : 0;
        bool created = file != INVALID_HANDLE_VALUE;
        if (created) CloseHandle(file);
#else
        int fd = open(replacement, O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW, 0600);
        unsigned error = fd < 0 ? (unsigned)errno : 0;
        bool created = fd >= 0;
        if (created) close(fd);
#endif
        if (!created) {
            line("fixture leg=replace error=%u", error);
            arc_lock_release(&probe.holder); line("done leg=replace"); return;
        }
    }
    ChildResult operation = {-1, 0};
#if defined(_WIN32)
    operation = mutate(leg, probe.holder.path, replacement);
    line("operation leg=%s role=in-process result=%d error=%u", leg_names[leg], operation.result, operation.error);
    bool completed = true;
#else
    bool completed = fork_result(leg, &probe.holder, NULL, replacement, "child", &operation);
#endif
    if (leg == REPLACE_LEG && (!completed || operation.result)) {
#if defined(_WIN32)
        WCHAR wide[CAP]; bool ok = wide_path(replacement, wide) && DeleteFileW(wide);
        unsigned error = ok ? 0 : GetLastError();
#else
        bool ok = unlink(replacement) == 0;
        unsigned error = ok ? 0 : (unsigned)errno;
#endif
        line("cleanup leg=replace result=%d error=%u", ok ? 0 : -1, error);
    }
    probe.mutation_leg = leg; probe.mutation_frame = 1;
    probe.mutation_succeeded = completed && operation.result == 0;
}
static void mutation_verify(void)
{
    Leg leg = probe.mutation_leg;
    ArcLockResult result = arc_lock_verify(&probe.holder);
    line("verify leg=%s phase=%s frame=%llu outcome=%s held=%d", leg_names[leg],
        probe.mutation_frame == 1 ? "next" : "sticky", (unsigned long long)probe.frame,
        outcomes[result], (int)arc_lock_held(&probe.holder));
    if (++probe.mutation_frame == 3) {
        if (!probe.mutation_succeeded) {
            ArcLock contender = {0}; unsigned attempts;
            ArcLockResult exclusion = acquire(&contender, &attempts);
            line("result leg=%s role=retained-proof outcome=%s attempts=%u", leg_names[leg], outcomes[exclusion], attempts);
            arc_lock_release(&contender);
        }
        arc_lock_release(&probe.holder); probe.mutation_frame = 0;
        line("done leg=%s", leg_names[leg]);
    }
}
static void finish(const char *reason)
{
    if (probe.holding || probe.mutation_frame) {
        arc_lock_release(&probe.holder);
        line("done leg=%s reason=%s", probe.holding ? "hold" : leg_names[probe.mutation_leg], reason);
    }
    if (probe.log) fclose(probe.log);
    memset(&probe, 0, sizeof(probe));
}
static bool read_plan(const char *path)
{
    FILE *file = path_file(path, false);
    if (!file) return false;
    char data[16385]; size_t size = fread(data, 1, sizeof(data), file);
    bool ok = !ferror(file) && size < sizeof(data) && !memchr(data, 0, size);
    if (fclose(file)) ok = false;
    if (!ok) return false;
    data[size] = 0;
    char *cursor = data; unsigned number = 0;
    bool have_root = false, have_output = false;
    while (*cursor) {
        char *text = cursor, *end = strchr(cursor, '\n');
        if (end) { *end = 0; cursor = end + 1; } else cursor += strlen(cursor);
        size_t n = strlen(text); if (n && text[n - 1] == '\r') text[--n] = 0;
        if (++number == 1) { if (strcmp(text, "arc-lock-probe-v1")) return false; continue; }
        if (!strncmp(text, "root=", 5) && !have_root && n - 5 < CAP) {
            strcpy(probe.root, text + 5); have_root = true;
        } else if (!strncmp(text, "output=", 7) && !have_output && n - 7 < CAP) {
            strcpy(probe.output, text + 7); have_output = true;
        }
        else if (!strncmp(text, "leg=", 4) && probe.count < MAX_LEGS) {
            unsigned leg = 0; while (leg <= HOLD && strcmp(text + 4, leg_names[leg])) ++leg;
            if (leg > HOLD || (probe.count && probe.legs[probe.count - 1] == HOLD)) return false;
            probe.legs[probe.count++] = (Leg)leg;
        } else return false;
    }
    if (!number || !probe.count || !clean_path(probe.root) || !clean_path(probe.output)) return false;
    if (probe_stat(probe.root, NULL) != (RETRO_VFS_STAT_IS_VALID | RETRO_VFS_STAT_IS_DIRECTORY)) return false;
    /* Output cannot clobber a sentinel, diagnostic, or the plan itself. */
    char owned[CAP]; root_path(owned, "arcdroid");
    if (path_prefix(path, probe.output, strlen(probe.output) + 1) ||
        path_prefix(probe.root, probe.output, strlen(probe.output) + 1) ||
        path_prefix(probe.output, owned, strlen(owned)) || !plain_entry(probe.output)) return false;
    root_path(owned, "p10b-flock.probe");
    return !path_prefix(owned, probe.output, strlen(probe.output) + 1) && output_safe(path);
}

RETRO_API void retro_set_environment(retro_environment_t cb) { environment = cb; }
RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb) { video = cb; }
RETRO_API void retro_set_audio_sample(retro_audio_sample_t cb) { (void)cb; }
RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio = cb; }
RETRO_API void retro_set_input_poll(retro_input_poll_t cb) { poll_input = cb; }
RETRO_API void retro_set_input_state(retro_input_state_t cb) { (void)cb; }
RETRO_API unsigned retro_api_version(void) { return RETRO_API_VERSION; }
RETRO_API void retro_init(void) { arc_io_set_vfs(&vfs); }
RETRO_API void retro_deinit(void) { finish("deinit"); arc_io_set_vfs(NULL); }
RETRO_API void retro_get_system_info(struct retro_system_info *info)
{ *info = (struct retro_system_info){"ArcDroid P10b LOCK PROBE (test only)", "1", "arcprobe", true, true}; }
RETRO_API void retro_get_system_av_info(struct retro_system_av_info *info)
{ *info = (struct retro_system_av_info){{64, 64, 64, 64, 1.0f}, {60.0, 48000.0}}; }
RETRO_API bool retro_load_game(const struct retro_game_info *game)
{
    finish("reload");
    if (!game || !game->path || !read_plan(game->path)) { finish("invalid-plan"); return false; }
    probe.log = path_file(probe.output, true);
    if (!probe.log) { finish("output-error"); return false; }
    if (environment) {
        enum retro_pixel_format format = RETRO_PIXEL_FORMAT_XRGB8888;
        if (!environment(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &format)) { finish("pixel-format"); return false; }
    }
    if (!line("probe version=1 key=%s", key)) { finish("output-error"); return false; }
    return true;
}
RETRO_API void retro_unload_game(void) { finish("unload"); }
RETRO_API void retro_run(void)
{
    static const uint32_t pixels[64 * 64] = {0};
    static const int16_t silence[800 * 2] = {0};
    if (probe.log && !probe.failed) {
        ++probe.frame;
        if (probe.mutation_frame) mutation_verify();
        else if (probe.holding) {
            ArcLockResult result = arc_lock_verify(&probe.holder);
            if (result != probe.last) {
                line("verify leg=hold frame=%llu outcome=%s", (unsigned long long)probe.frame, outcomes[result]);
                probe.last = result;
            }
        } else if (probe.next < probe.count) {
            Leg leg = probe.legs[probe.next++];
            if (leg == INFO) info();
            else if (leg == SAME || leg == FORK) contention(leg);
            else if (leg == REPLACE_LEG || leg == DELETE_LEG) mutation_begin(leg);
            else {
                unsigned attempts;
                ArcLockResult result = acquire(&probe.holder, &attempts);
                line("result leg=%s outcome=%s attempts=%u", leg_names[leg], outcomes[result], attempts);
                if (leg == HOLD && result == ARC_LOCK_HELD && !probe.failed) {
                    probe.holding = true; probe.last = arc_lock_verify(&probe.holder);
                    line("verify leg=hold frame=%llu outcome=%s", (unsigned long long)probe.frame, outcomes[probe.last]);
                } else {
                    arc_lock_release(&probe.holder);
                    line("done leg=%s", leg_names[leg]);
                }
            }
        }
    }
    if (poll_input) poll_input();
    if (video) video(pixels, 64, 64, 64 * sizeof(uint32_t));
    if (audio) audio(silence, 800);
}
RETRO_API void retro_reset(void) { /* Reset must not release a live holder. */ }
RETRO_API bool retro_load_game_special(unsigned type, const struct retro_game_info *games, size_t count)
{ (void)type; (void)games; (void)count; return false; }
RETRO_API size_t retro_serialize_size(void) { return 0; }
RETRO_API bool retro_serialize(void *data, size_t size) { (void)data; (void)size; return false; }
RETRO_API bool retro_unserialize(const void *data, size_t size) { (void)data; (void)size; return false; }
RETRO_API void retro_cheat_reset(void) {}
RETRO_API void retro_cheat_set(unsigned index, bool enabled, const char *code) { (void)index; (void)enabled; (void)code; }
RETRO_API void retro_set_controller_port_device(unsigned port, unsigned device) { (void)port; (void)device; }
RETRO_API unsigned retro_get_region(void) { return RETRO_REGION_NTSC; }
RETRO_API void *retro_get_memory_data(unsigned id) { (void)id; return NULL; }
RETRO_API size_t retro_get_memory_size(unsigned id) { (void)id; return 0; }
