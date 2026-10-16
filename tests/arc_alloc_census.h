/* Exact requested-byte census for the dedicated session test core.
 * Force-included in every object linked into that core, including libraries.
 * The harness and the CRT/VFS implementation keep their own allocators.
 * GPL-2.0-only. */
#ifndef ARC_ALLOC_CENSUS_H
#define ARC_ALLOC_CENSUS_H
#include <stddef.h>
#include <stdlib.h>

typedef struct { size_t bytes, blocks; } ArcAllocCensus;
void *arc_alloc_census_malloc(size_t size, const char *file, unsigned line);
void *arc_alloc_census_calloc(size_t count, size_t size, const char *file, unsigned line);
void *arc_alloc_census_realloc(void *ptr, size_t size, const char *file, unsigned line);
void arc_alloc_census_free(void *ptr);
ArcAllocCensus arc_alloc_census_snapshot(void);
/* Live blocks allocated by RPC glue or RPCEmu, independent of session reset. */
ArcAllocCensus arc_alloc_census_rpc_snapshot(void);
void arc_alloc_census_dump(void);

#if defined(ARC_ALLOC_CENSUS_INTERCEPT)
#define malloc(n) arc_alloc_census_malloc((n), __FILE__, __LINE__)
#define calloc(n, s) arc_alloc_census_calloc((n), (s), __FILE__, __LINE__)
#define realloc(p, n) arc_alloc_census_realloc((p), (n), __FILE__, __LINE__)
#define free(p) arc_alloc_census_free(p)
#endif
#endif
