/* Host measurement only; never compiled into shipping/derived code. GPL-2.0-only. */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdint.h>
#if defined(_WIN32)
#include <windows.h>
uint64_t arc_test_clock_ns(void)
{
    LARGE_INTEGER value, frequency;
    QueryPerformanceCounter(&value); QueryPerformanceFrequency(&frequency);
    uint64_t v = (uint64_t)value.QuadPart, f = (uint64_t)frequency.QuadPart;
    return v / f * UINT64_C(1000000000) + v % f * UINT64_C(1000000000) / f;
}
#else
#include <time.h>
uint64_t arc_test_clock_ns(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) return 0;
    return (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
}
#endif
