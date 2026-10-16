/* Observable allocation accounting, including allocator failures. GPL-2.0-only. */
#include <assert.h>
#include <stdlib.h>
/* Compare the bridge with the actual allocator, including Darwin's live
 * minimum-size object versus Windows/glibc/ASan's free-and-NULL result. */
static int native_zero_realloc_has_block(void)
{
    void *p = malloc(17); assert(p);
    void *q = realloc(p, 0);
    int result = q != NULL;
    free(q);
    return result;
}
#include "arc_alloc_census.h"
#include <stdint.h>
#include <string.h>
static void expect(size_t bytes, size_t blocks)
{
    ArcAllocCensus s = arc_alloc_census_snapshot();
    assert(s.bytes == bytes && s.blocks == blocks);
}
int main(void)
{
    expect(0, 0);
    free(NULL); expect(0, 0);
    unsigned char *p = malloc(7), *q = calloc(3, 5);
    assert(p && q); expect(22, 2);
    for (unsigned i = 0; i < 15; ++i) assert(!q[i]);
    memset(p, 0x5a, 7); p = realloc(p, 123456); assert(p); expect(123471, 2);
    for (unsigned i = 0; i < 7; ++i) assert(p[i] == 0x5a);
    p = realloc(p, 3); assert(p && p[0] == 0x5a); expect(18, 2);
    size_t volatile huge = SIZE_MAX;
    assert(!calloc(huge, 2)); expect(18, 2);
#if !defined(ARCDROID_SESSION_SANITIZER_PROFILE)
    assert(!malloc(huge)); expect(18, 2);
    assert(!realloc(p, huge)); assert(p[0] == 0x5a); expect(18, 2);
#endif
    free(q); expect(3, 1);
    void *zero = realloc(p, 0);
    assert((zero != NULL) == native_zero_realloc_has_block());
    expect(0, zero ? 1 : 0); free(zero); expect(0, 0);
    p = realloc(NULL, 17); assert(p); expect(17, 1); free(p);
    p = malloc(0); expect(0, p ? 1 : 0); free(p); expect(0, 0);
    /* Exercise probe-chain deletion, reuse and table wrap without allocating
     * bookkeeping from the CRT. */
    void *many[4096];
    for (size_t i = 0; i < 4096; ++i) { many[i] = malloc(1); assert(many[i]); }
    expect(4096, 4096);
    for (size_t i = 0; i < 4096; i += 2) free(many[i]);
    expect(2048, 2048);
    for (size_t i = 1; i < 4096; i += 2) free(many[i]);
    expect(0, 0);
    /* The RPC view survives loss of the owner's pointer/reset of its logical
     * counter, and tracks actual frees rather than session teardown. */
    void *rpc = arc_alloc_census_malloc(32, "/src/engine/rpc/rpc_session.c", 1);
    void *vendored = arc_alloc_census_malloc(7, "C:\\src\\engine\\rpcemu\\src\\mem.c", 1);
    void *frontend = malloc(5);
    ArcAllocCensus r = arc_alloc_census_rpc_snapshot();
    assert(r.bytes == 39 && r.blocks == 2);
    free(rpc); r = arc_alloc_census_rpc_snapshot();
    assert(r.bytes == 7 && r.blocks == 1);
    free(vendored); r = arc_alloc_census_rpc_snapshot();
    assert(!r.bytes && !r.blocks);
    free(frontend); expect(0, 0);
    return 0;
}
