/* Authored Win32 signature/constant shim for non-Windows unit builds only.
 * Values checked against Microsoft's fileapi/stringapiset docs. GPL-2.0-only. */
#ifndef ARC_LOCK_TEST_WINDOWS_H
#define ARC_LOCK_TEST_WINDOWS_H
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>
typedef void *HANDLE;
typedef wchar_t WCHAR;
typedef uint32_t DWORD;
typedef int BOOL;
typedef struct { uintptr_t Internal, InternalHigh; DWORD Offset, OffsetHigh; HANDLE hEvent; } OVERLAPPED;
#define CP_UTF8 65001u
#define MB_ERR_INVALID_CHARS 8u
#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u
#define FILE_SHARE_READ 1u
#define FILE_SHARE_WRITE 2u
#define FILE_SHARE_DELETE 4u
#define OPEN_ALWAYS 4u
#define FILE_ATTRIBUTE_NORMAL 128u
#define FILE_FLAG_OVERLAPPED 0x40000000u
#define LOCKFILE_EXCLUSIVE_LOCK 2u
#define LOCKFILE_FAIL_IMMEDIATELY 1u
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
int MultiByteToWideChar(unsigned, DWORD, const char *, int, WCHAR *, int);
DWORD GetLastError(void);
HANDLE CreateFileW(const WCHAR *, DWORD, DWORD, void *, DWORD, DWORD, HANDLE);
BOOL LockFileEx(HANDLE, DWORD, DWORD, DWORD, DWORD, OVERLAPPED *);
BOOL UnlockFileEx(HANDLE, DWORD, DWORD, DWORD, OVERLAPPED *);
BOOL CloseHandle(HANDLE);
#endif
