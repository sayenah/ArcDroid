/* Test-core allocation instrumentation; no allocator-size rounding, CRT
 * internals, metadata allocations or freed blocks enter the totals.
 * Calls retain their original sizes and alignment. Single core thread only.
 * GPL-2.0-only. */
#include "arc_alloc_census.h"
#undef malloc
#undef calloc
#undef realloc
#undef free
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#ifndef __has_feature
#define __has_feature(x) 0
#endif

/* Fixed metadata avoids perturbing the allocator with bookkeeping blocks.
 * Exhaustion is a hard test failure, never an uncounted allocation. */
#define SLOTS 65536u
typedef struct { void *ptr; size_t size; const char *file; unsigned line; } Entry;
static Entry entries[SLOTS];
static ArcAllocCensus live;
static size_t slot(void *ptr)
{
    size_t at = ((uintptr_t)ptr >> 4) % SLOTS;
    while (entries[at].ptr && entries[at].ptr != ptr) at = (at + 1) % SLOTS;
    return at;
}
static void add(void *ptr, size_t size, const char *file, unsigned line)
{
    if (!ptr) return;
    assert(live.blocks < SLOTS / 2 && size <= SIZE_MAX - live.bytes);
    size_t at = slot(ptr); assert(!entries[at].ptr);
    entries[at] = (Entry){ptr, size, file, line};
    live.bytes += size; ++live.blocks;
}
static Entry remove_entry(void *ptr)
{
    size_t at = slot(ptr); Entry old = entries[at]; assert(old.ptr);
    live.bytes -= old.size; --live.blocks;
    entries[at] = (Entry){0};
    /* Reinsert the rest of this probe chain after deleting its first gap. */
    for (at = (at + 1) % SLOTS; entries[at].ptr; at = (at + 1) % SLOTS) {
        Entry e = entries[at]; entries[at] = (Entry){0}; entries[slot(e.ptr)] = e;
    }
    return old;
}
void *arc_alloc_census_malloc(size_t size, const char *file, unsigned line)
{
    void *ptr = malloc(size); add(ptr, size, file, line); return ptr;
}
void *arc_alloc_census_calloc(size_t count, size_t size, const char *file, unsigned line)
{
    if (size && count > SIZE_MAX / size) { errno = ENOMEM; return NULL; }
    void *ptr = calloc(count, size); add(ptr, count * size, file, line); return ptr;
}
void arc_alloc_census_free(void *ptr)
{
    if (ptr) remove_entry(ptr);
    free(ptr);
}
void *arc_alloc_census_realloc(void *ptr, size_t size, const char *file, unsigned line)
{
    Entry old = {0};
    if (ptr) old = remove_entry(ptr);
    void *grown = realloc(ptr, size);
    if (grown) add(grown, size, file, line);
    /* Native Darwin retains the old object on any realloc failure, including
     * its minimum-size allocation for size zero. Windows/glibc and our ASan
     * runtime free a nonnull old object when size is zero and return NULL. */
#if defined(__APPLE__) && !defined(__SANITIZE_ADDRESS__) && !__has_feature(address_sanitizer)
    else if (ptr) add(old.ptr, old.size, old.file, old.line);
#else
    else if (size && ptr) add(old.ptr, old.size, old.file, old.line);
#endif
    return grown;
}
#if defined(_WIN32)
__declspec(dllexport)
#endif
ArcAllocCensus arc_alloc_census_snapshot(void) { return live; }
#if defined(_WIN32)
__declspec(dllexport)
#endif
void arc_alloc_census_dump(void)
{
    for (size_t i = 0; i < SLOTS; ++i) if (entries[i].ptr)
        fprintf(stderr, "live-allocation: bytes=%zu site=%s:%u\n",
                entries[i].size, entries[i].file, entries[i].line);
}
