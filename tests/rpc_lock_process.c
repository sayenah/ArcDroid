/* Real OS ownership oracle and P10b device binary. No firmware. GPL-2.0-only. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include "arc_lock.h"
#include "arc_io.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#include <direct.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <poll.h>
#endif

static const char content[] = "0123456789abcdef0123456789abcdef01234567";
static ArcLock first, second;
static const char *program;
static uint64_t maximum_attempt_ns;
static const ArcLockBackend *probe_backend;
#if defined(__APPLE__) && defined(F_OFD_SETLK)
/* P10b's optional macOS OFD probe. Production on macOS always uses flock. */
static ArcLockBackend ofd_probe_backend;
static unsigned probe_ofd_lock(void *opaque, intptr_t handle)
{
    (void)opaque; struct flock lock = {0}; lock.l_type = F_WRLCK; lock.l_whence = SEEK_SET;
    return fcntl((int)handle, F_OFD_SETLK, &lock) == 0 ? 0 : (unsigned)errno;
}
static void probe_ofd_unlock(void *opaque, intptr_t handle)
{
    (void)opaque; struct flock lock = {0}; lock.l_type = F_UNLCK; lock.l_whence = SEEK_SET;
    (void)fcntl((int)handle, F_OFD_SETLK, &lock);
}
#endif
static const char *result_name(ArcLockResult result)
{
    static const char *const names[] = {"held", "contended", "transient", "unsupported",
        "root-refused", "create-error", "lock-error", "identity-lost"};
    return names[result];
}
static uint64_t clock_ns(void)
{
#if defined(_WIN32)
    LARGE_INTEGER count, frequency;
    assert(QueryPerformanceCounter(&count) && QueryPerformanceFrequency(&frequency));
    return (uint64_t)(count.QuadPart / frequency.QuadPart) * UINT64_C(1000000000) +
        (uint64_t)(count.QuadPart % frequency.QuadPart) * UINT64_C(1000000000) / (uint64_t)frequency.QuadPart;
#else
    struct timespec ts; assert(clock_gettime(CLOCK_MONOTONIC, &ts) == 0);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
#endif
}
#if defined(_WIN32)
static void wide_path(const char *path, WCHAR *out)
{ assert(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, out, 4096)); }
static VOID CALLBACK attempt_expired(PVOID arg, BOOLEAN fired)
{ (void)arg; (void)fired; ExitProcess(75); }
#else
static void attempt_expired(int signal_number)
{ (void)signal_number; _Exit(75); }
#endif
static ArcLockResult bounded_attempt(ArcLock *lock)
{
    /* Hard watchdog applies to EACH native attempt, not just the CTest process.
     * A blocking-lock mutant exits 75 within 1 s. Completed calls also have a
     * measured 250 ms bound. This is a test bound, not a filesystem guarantee. */
    bool native = !arc_lock_next_namespace(lock);
#if defined(_WIN32)
    HANDLE timer = NULL;
    if (native) assert(CreateTimerQueueTimer(&timer, NULL, attempt_expired, NULL, 1000, 0, WT_EXECUTEONLYONCE));
#else
    struct itimerval timer = {.it_value = {.tv_sec = 1}};
    if (native) { signal(SIGALRM, attempt_expired); assert(setitimer(ITIMER_REAL, &timer, NULL) == 0); }
#endif
    uint64_t start = clock_ns();
    ArcLockResult result = arc_lock_acquire(lock);
    uint64_t elapsed = clock_ns() - start;
#if defined(_WIN32)
    if (native) assert(DeleteTimerQueueTimer(NULL, timer, INVALID_HANDLE_VALUE));
#else
    timer.it_value.tv_sec = 0;
    if (native) assert(setitimer(ITIMER_REAL, &timer, NULL) == 0);
#endif
    if (native) {
        assert(elapsed < UINT64_C(250000000));
        if (elapsed > maximum_attempt_ns) maximum_attempt_ns = elapsed;
    }
    return result;
}
static int32_t vfs_stat(const char *path, int32_t *size)
{
#if defined(_WIN32)
    WCHAR wide[4096]; wide_path(path, wide);
    WIN32_FILE_ATTRIBUTE_DATA s;
    if (!GetFileAttributesExW(wide, GetFileExInfoStandard, &s)) return 0;
    if (size) *size = s.nFileSizeHigh || s.nFileSizeLow > INT32_MAX ? INT32_MAX : (int32_t)s.nFileSizeLow;
    return RETRO_VFS_STAT_IS_VALID | ((s.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? RETRO_VFS_STAT_IS_DIRECTORY : 0);
#else
    struct stat s; if (stat(path, &s)) return 0;
    if (size) *size = s.st_size > INT32_MAX ? INT32_MAX : (int32_t)s.st_size;
    return RETRO_VFS_STAT_IS_VALID | (S_ISDIR(s.st_mode) ? RETRO_VFS_STAT_IS_DIRECTORY :
        S_ISREG(s.st_mode) ? 0 : RETRO_VFS_STAT_IS_CHARACTER_SPECIAL);
#endif
}
static int vfs_mkdir(const char *path)
{
#if defined(_WIN32)
    WCHAR wide[4096]; wide_path(path, wide); int result = _wmkdir(wide);
#else
    int result = mkdir(path, 0700);
#endif
    return result == 0 ? 0 : errno == EEXIST ? -2 : -1;
}
static struct retro_vfs_interface vfs = {.stat = vfs_stat, .mkdir = vfs_mkdir};
static ArcLockResult initialize(ArcLock *lock, const char *save, const char *key)
{
    ArcLockResult result = arc_lock_content(lock, save, key, probe_backend, NULL);
    if (result != ARC_LOCK_TRANSIENT) return result;
    unsigned steps = 0;
    while (arc_lock_next_namespace(lock)) {
        result = arc_lock_acquire(lock);
        if (result != ARC_LOCK_TRANSIENT) return result;
        assert(++steps < 4096);
    }
    return bounded_attempt(lock);
}
static void zero_file(const char *path)
{
    int32_t n = -1;
    assert(vfs_stat(path, &n) == RETRO_VFS_STAT_IS_VALID && n == 0);
}
static void empty_file(const char *path)
{
#if defined(_WIN32)
    WCHAR wide[4096]; wide_path(path, wide); FILE *file = _wfopen(wide, L"wb");
#else
    FILE *file = fopen(path, "wb");
#endif
    assert(file && fclose(file) == 0);
}
static void info(const char *save)
{
#if defined(_WIN32)
    printf("platform=LockFileEx pid=%lu\n", (unsigned long)GetCurrentProcessId());
#else
    struct utsname version; assert(uname(&version) == 0);
    printf("platform=%s pid=%ld uid=%ld os=%s kernel=%s machine=%s\n",
#if defined(__APPLE__)
        probe_backend ? "OFD-test-only" : "flock",
#else
        "OFD",
#endif
        (long)getpid(), (long)getuid(), version.sysname, version.release, version.machine);
#ifdef F_OFD_SETLK
    printf("sdk-F_OFD_SETLK=%d\n", F_OFD_SETLK);
#else
    puts("sdk-F_OFD_SETLK=absent (macOS uses flock)");
#endif
#if defined(__linux__)
    /* Test diagnostics only, never called by shipping code. */
    FILE *mounts = fopen("/proc/self/mountinfo", "r"); assert(mounts);
    char line[8192]; while (fgets(line, sizeof(line), mounts)) fputs(line, stdout);
    assert(!ferror(mounts) && fclose(mounts) == 0);
    if (save && initialize(&first, save, content) == ARC_LOCK_HELD) {
        errno = 0; int result = flock((int)first.handle, LOCK_EX|LOCK_NB), error = errno;
        printf("diagnostic-flock=%d errno=%d (not the production mechanism)\n", result, error);
        if (!result) assert(flock((int)first.handle, LOCK_UN) == 0);
        arc_lock_release(&first);
    }
#endif
#endif
    (void)save;
    fflush(stdout);
}

/* Two independent executable processes. Commands use a pipe handshake, never
 * sleeps to guess acquisition. Every killed child is reaped before retry. */
#if defined(_WIN32)
typedef struct { HANDLE process, command, reply; } Child;
static Child spawn_child(const char *mode, const char *save)
{
    SECURITY_ATTRIBUTES security = {sizeof(security), NULL, TRUE};
    HANDLE input_read, input_write, output_read, output_write;
    assert(CreatePipe(&input_read, &input_write, &security, 0));
    assert(CreatePipe(&output_read, &output_write, &security, 0));
    assert(SetHandleInformation(input_write, HANDLE_FLAG_INHERIT, 0));
    assert(SetHandleInformation(output_read, HANDLE_FLAG_INHERIT, 0));
    WCHAR executable[4096], root[4096], command[9000];
    assert(GetModuleFileNameW(NULL, executable, 4096)); wide_path(save, root);
    assert(swprintf(command, 9000, L"\"%ls\" %hs \"%ls\"", executable, mode, root) > 0);
    STARTUPINFOW startup = {0}; startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input_read; startup.hStdOutput = output_write;
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION process;
    assert(CreateProcessW(executable, command, NULL, NULL, TRUE, 0, NULL, NULL, &startup, &process));
    assert(CloseHandle(input_read) && CloseHandle(output_write) && CloseHandle(process.hThread));
    return (Child){process.hProcess, input_write, output_read};
}
static void send_command(Child *child, char command)
{ DWORD wrote; assert(WriteFile(child->command, &command, 1, &wrote, NULL) && wrote == 1); }
static void reply(Child *child, const char *expected)
{
    char line[128]; size_t n = 0; uint64_t start = clock_ns();
    while (n + 1 < sizeof(line)) {
        DWORD available, got;
        assert(PeekNamedPipe(child->reply, NULL, 0, NULL, &available, NULL));
        if (!available) { assert(clock_ns() - start < UINT64_C(3000000000)); Sleep(1); continue; }
        assert(ReadFile(child->reply, line + n, 1, &got, NULL) && got == 1);
        if (line[n++] == '\n') break;
    }
    line[n] = 0;
    /* Windows CRT stdout adds CR to LF on the pipe. */
    if (n >= 2 && line[n-2] == '\r') { line[n-2] = '\n'; line[n-1] = 0; }
    if (strcmp(line, expected)) fprintf(stderr, "child reply: %s expected: %s", line, expected);
    assert(!strcmp(line, expected));
}
static void reap(Child *child, bool killed, int expected_exit)
{
    if (killed) assert(TerminateProcess(child->process, 98));
    assert(WaitForSingleObject(child->process, 5000) == WAIT_OBJECT_0);
    DWORD code; assert(GetExitCodeProcess(child->process, &code));
    assert(code == (DWORD)(killed ? 98 : expected_exit));
    assert(CloseHandle(child->process) && CloseHandle(child->command) && CloseHandle(child->reply));
}
#else
typedef struct { pid_t pid; int command, reply; } Child;
static Child spawn_child(const char *mode, const char *save)
{
    int input[2], output[2]; assert(pipe(input) == 0 && pipe(output) == 0);
    pid_t pid = fork(); assert(pid >= 0);
    if (!pid) {
        assert(dup2(input[0], STDIN_FILENO) >= 0 && dup2(output[1], STDOUT_FILENO) >= 0);
        close(input[0]); close(input[1]); close(output[0]); close(output[1]);
        const char *child_mode = probe_backend ? (!strcmp(mode, "try") ? "ofd-try" : "ofd-hold") : mode;
        execl(program, program, child_mode, save, (char *)NULL); _Exit(76);
    }
    close(input[0]); close(output[1]);
    return (Child){pid, input[1], output[0]};
}
static void send_command(Child *child, char command)
{ assert(write(child->command, &command, 1) == 1); }
static void reply(Child *child, const char *expected)
{
    char line[128]; size_t n = 0;
    while (n + 1 < sizeof(line)) {
        struct pollfd fd = {.fd = child->reply, .events = POLLIN};
        assert(poll(&fd, 1, 3000) > 0);
        assert(read(child->reply, line + n, 1) == 1);
        if (line[n++] == '\n') break;
    }
    line[n] = 0;
    if (strcmp(line, expected)) fprintf(stderr, "child reply: %s expected: %s", line, expected);
    assert(!strcmp(line, expected));
}
static void reap(Child *child, bool killed, int expected_exit)
{
    if (killed) assert(kill(child->pid, SIGKILL) == 0);
    int status; assert(waitpid(child->pid, &status, 0) == child->pid);
    assert(killed ? WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL :
        WIFEXITED(status) && WEXITSTATUS(status) == expected_exit);
    close(child->command); close(child->reply);
}
#endif

static void replacement(const char *save, bool remove_it)
{
    assert(initialize(&first, save, content) == ARC_LOCK_HELD);
    char replacement_path[4200]; snprintf(replacement_path, sizeof(replacement_path), "%s.replacement", first.path);
    empty_file(replacement_path);
#if defined(_WIN32)
    WCHAR held[4096], replacement[4096]; wide_path(first.path, held); wide_path(replacement_path, replacement);
    BOOL result = remove_it ? DeleteFileW(held) : MoveFileExW(replacement, held, MOVEFILE_REPLACE_EXISTING);
    assert(!result && GetLastError() == ERROR_SHARING_VIOLATION);
    assert(arc_lock_verify(&first) == ARC_LOCK_HELD);
    assert(DeleteFileW(replacement));
#else
    assert((remove_it ? unlink(first.path) : rename(replacement_path, first.path)) == 0);
    assert(arc_lock_verify(&first) == ARC_LOCK_IDENTITY_LOST && !arc_lock_held(&first));
    if (remove_it) assert(unlink(replacement_path) == 0);
#endif
    arc_lock_release(&first);
    assert(initialize(&first, save, content) == ARC_LOCK_HELD);
    arc_lock_release(&first); zero_file(first.path);
    printf("%s: passed\n", remove_it ? "delete-identity" : "rename-over-identity");
}
static void self_test(const char *save, const char *alias)
{
    assert(initialize(&first, save, content) == ARC_LOCK_HELD);
    zero_file(first.path);
#if defined(_WIN32)
    DWORD flags; assert(GetHandleInformation((HANDLE)first.handle, &flags) && !(flags & HANDLE_FLAG_INHERIT));
#else
    assert(fcntl((int)first.handle, F_GETFD) & FD_CLOEXEC);
#endif
    assert(initialize(&second, save, content) == ARC_LOCK_CONTENDED && !arc_lock_held(&second));
    arc_lock_release(&second);
    Child contender = spawn_child("try", save); reply(&contender, "contended\n"); reap(&contender, false, 10);
    /* Closing a non-owner never releases the first description. */
    assert(initialize(&second, save, content) == ARC_LOCK_CONTENDED); arc_lock_release(&second);
    if (alias) {
        assert(initialize(&second, alias, content) == ARC_LOCK_CONTENDED); arc_lock_release(&second);
    }
#if !defined(_WIN32)
    char dotted[4200]; snprintf(dotted, sizeof(dotted), "%s/.", save);
    assert(initialize(&second, dotted, content) == ARC_LOCK_CONTENDED); arc_lock_release(&second);
    char symlink_path[4200]; snprintf(symlink_path, sizeof(symlink_path), "%s/p10b-root-alias", save);
    int linked = symlink(".", symlink_path), link_error = errno;
    if (!linked || link_error == EEXIST) {
        assert(initialize(&second, symlink_path, content) == ARC_LOCK_CONTENDED); arc_lock_release(&second);
        puts("symlink-alias: passed");
    } else {
        /* Emulated Android shared storage can prohibit symlinks. The dot-root
         * alias above still exercises two spellings of the same sentinel. */
        assert(link_error == EPERM || link_error == EACCES || link_error == ENOTSUP || link_error == EOPNOTSUPP);
        printf("symlink-alias: unavailable errno=%d; dot-root alias passed\n", link_error);
    }
#endif
    arc_lock_release(&first); zero_file(first.path);
    assert(initialize(&first, save, content) == ARC_LOCK_HELD); arc_lock_release(&first);
    puts("same-process, two-process, descriptor isolation, alias and clean reacquire: passed");
#if defined(_WIN32)
    /* Model an indexer/sync/antivirus handle that temporarily denies opening.
     * The real OS returns ERROR_SHARING_VIOLATION before LockFileEx is called. */
    WCHAR transient_path[4096]; wide_path(first.path, transient_path);
    HANDLE indexer = CreateFileW(transient_path, GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    assert(indexer != INVALID_HANDLE_VALUE);
    assert(initialize(&first, save, content) == ARC_LOCK_TRANSIENT && !first.opened);
    assert(CloseHandle(indexer));
    assert(bounded_attempt(&first) == ARC_LOCK_HELD); arc_lock_release(&first);
    puts("transient-open-sharing-violation and retry: passed");
#endif
    for (unsigned crash = 0; crash < 2; ++crash) {
        Child holder = spawn_child("hold", save); reply(&holder, "held\n");
        assert(initialize(&second, save, content) == ARC_LOCK_CONTENDED);
        if (crash) send_command(&holder, 'x');
        uint64_t start = clock_ns(); reap(&holder, !crash, 97);
        unsigned attempts = 0; ArcLockResult result;
        do {
            result = bounded_attempt(&second); ++attempts;
#if defined(_WIN32)
            if (result == ARC_LOCK_CONTENDED) Sleep(1);
#endif
        } while (result == ARC_LOCK_CONTENDED && clock_ns() - start < UINT64_C(10000000000));
        assert(result == ARC_LOCK_HELD);
#if !defined(_WIN32)
        assert(attempts == 1);
#endif
        printf("%s-release: attempts=%u elapsed-ns=%llu (reaped before acquire)\n",
            crash ? "crash" : "kill", attempts, (unsigned long long)(clock_ns() - start));
        arc_lock_release(&second); zero_file(second.path);
    }
    replacement(save, false); replacement(save, true);
    /* Untrusted sentinel contents are refused, never truncated or repaired by
     * the production descriptor. Only the test fixture resets them. */
    empty_file(first.path);
#if defined(_WIN32)
    WCHAR held_path[4096]; wide_path(first.path, held_path);
    FILE *bad = _wfopen(held_path, L"ab");
#else
    FILE *bad = fopen(first.path, "ab");
#endif
    assert(bad && fputc(1, bad) == 1 && fclose(bad) == 0);
    assert(initialize(&first, save, content) == ARC_LOCK_ROOT_REFUSED);
    int32_t n; assert(vfs_stat(first.path, &n) == RETRO_VFS_STAT_IS_VALID && n == 1);
    arc_lock_release(&first); empty_file(first.path);
#if defined(_WIN32)
    assert(SetFileAttributesW(held_path, FILE_ATTRIBUTE_READONLY));
    assert(initialize(&first, save, content) == ARC_LOCK_CREATE_ERROR);
    assert(SetFileAttributesW(held_path, FILE_ATTRIBUTE_NORMAL));
#else
    int changed = chmod(first.path, 0400), mode_error = errno;
    if (!changed) {
        /* A successful chmod can be ignored by FUSE; root/ACLs may also grant
         * write access. Qualify the actual native-open failure, not mode bits. */
        int permission_probe = open(first.path, O_RDWR|O_CLOEXEC);
        if (permission_probe < 0) {
            assert(errno == EACCES || errno == EROFS);
            assert(initialize(&first, save, content) == ARC_LOCK_CREATE_ERROR);
            puts("read-only-open: refused as create-error");
        } else {
            assert(close(permission_probe) == 0);
            puts("read-only-open: not qualified (filesystem/UID/ACL permits write-open)");
        }
        assert(chmod(first.path, 0600) == 0);
    } else {
        assert(mode_error == EPERM || mode_error == EACCES || mode_error == ENOTSUP || mode_error == EOPNOTSUPP);
        printf("read-only-open: not qualified (chmod unavailable errno=%d)\n", mode_error);
    }
#endif
    arc_lock_release(&first); zero_file(first.path);
    char blocked[4200]; snprintf(blocked, sizeof(blocked), "%s/not-a-directory", save);
    empty_file(blocked);
    assert(initialize(&first, blocked, content) == ARC_LOCK_CREATE_ERROR); arc_lock_release(&first);
    assert(arc_lock_content(&first, "relative", content, NULL, NULL) == ARC_LOCK_ROOT_REFUSED);
    assert(arc_lock_content(&first, "saf://not-native", content, NULL, NULL) == ARC_LOCK_ROOT_REFUSED);
    puts("nonzero, non-directory, relative and scheme refusal paths: passed (read-only qualification reported above)");
    printf("rpc-lock-process: passed; maximum-native-attempt-ns=%llu\n", (unsigned long long)maximum_attempt_ns);
}
int main(int argc, char **argv)
{
    if (argc < 3 || argc > 4) {
        fputs("usage: rpc_lock_process self-test|info|try|hold SAVE [ALIAS for self-test; CONTENT40 for try/hold]\n"
              "hold stdin: v=verify, i=identity/mountinfo, q=release+exit, x=crash-without-cleanup\n", stderr); return 2;
    }
    program = argv[0]; arc_io_set_vfs(&vfs);
    const char *mode = argv[1];
#if defined(__APPLE__) && defined(F_OFD_SETLK)
    if (!strncmp(mode, "ofd-", 4)) {
        ArcLock prototype;
        assert(arc_lock_content(&prototype, argv[2], content, NULL, NULL) == ARC_LOCK_TRANSIENT);
        ofd_probe_backend = *prototype.backend;
        ofd_probe_backend.platform = ARC_LOCK_OFD;
        ofd_probe_backend.try_lock = probe_ofd_lock; ofd_probe_backend.unlock = probe_ofd_unlock;
        probe_backend = &ofd_probe_backend; mode += 4;
    }
#endif
    if (!strcmp(mode, "info")) { info(argv[2]); return 0; }
    if (!strcmp(mode, "self-test")) {
        if (probe_backend) {
            ArcLockResult support = initialize(&first, argv[2], content); arc_lock_release(&first);
            if (support != ARC_LOCK_HELD) { puts(result_name(support)); return 20 + support; }
            puts("test-only macOS OFD backend selected");
        }
        info(argv[2]); self_test(argv[2], argc == 4 ? argv[3] : NULL); return 0;
    }
    ArcLockResult result = initialize(&first, argv[2], argc == 4 ? argv[3] : content);
    puts(result_name(result)); fflush(stdout);
    if (result != ARC_LOCK_HELD) { arc_lock_release(&first); return result == ARC_LOCK_CONTENDED ? 10 : 20 + result; }
    if (!strcmp(mode, "hold")) {
        int command;
        while ((command = getchar()) != EOF) {
            if (command == 'x') _Exit(97);
            if (command == 'q') break;
            if (command == 'i') info(NULL);
            if (command == 'v') { puts(result_name(arc_lock_verify(&first))); fflush(stdout); }
        }
    } else if (strcmp(mode, "try")) { arc_lock_release(&first); return 2; }
    arc_lock_release(&first); zero_file(first.path); return 0;
}
