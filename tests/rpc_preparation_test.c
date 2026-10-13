/* R3b-2a independent outcome, no-mutation and operation-budget oracle.
 * GPL-2.0-only. */
#include "arcdroid_core.h"
#include "rpc_hd_store.h"
#include "rpc_hd_vfs.h"
static const char *id = "0123456789012345678901234567890123456789";
static char error[256];
static void disk_digest(uint8_t out[32]) {
    ArcSha256 h;
    arc_sha256_init(&h);
    for (unsigned i = 0; i < TEST_FILES; ++i)
        if (files[i].present) {
            arc_sha256_update(&h, files[i].path, strlen(files[i].path));
            arc_sha256_update(&h, &files[i].size, sizeof(files[i].size));
            arc_sha256_update(&h, files[i].data, files[i].size);
        }
    arc_sha256_update(&h, &dir_count, sizeof(dir_count));
    arc_sha256_final(&h, out);
}
static void budget(ArcRpcHdStore *s, uint64_t ops, uint64_t meta, uint64_t ns) {
    ArcRpcHdStats a;
    arc_riscpc_hd_stats(s, &a);
    assert(a.tick_operations == steps - ops);
    assert(a.tick_metadata_operations == metadata_calls - meta);
    assert(a.tick_namespace_operations == namespace_calls - ns);
    assert(a.tick_operations <= ARC_RPC_HD_STEP_OPERATIONS);
    assert(a.tick_metadata_operations <= 4 && a.tick_namespace_operations <= 1);
    assert(a.tick_bytes <= ARC_RPC_HD_STEP_BYTES &&
           a.tick_hash_bytes <= ARC_RPC_HD_STEP_HASH_BYTES);
    assert(a.tick_copied_bytes <= ARC_RPC_HD_STEP_COPY_BYTES);
}
static ArcRpcHdOutcome open_steps(ArcRpcHdStore **out) {
    *out = arc_riscpc_hd_open_begin("/save", id, error, sizeof(error));
    assert(*out);
    assert(!arc_riscpc_hd_population_begin(*out));
    ArcRpcHdOutcome result = ARC_RPC_HD_OPEN_PENDING;
    for (unsigned i = 0; result == ARC_RPC_HD_OPEN_PENDING; ++i) {
        assert(i < 10000);
        uint64_t ops = steps, meta = metadata_calls, ns = namespace_calls;
        result = arc_riscpc_hd_open_step(*out);
        budget(*out, ops, meta, ns);
    }
    return result;
}
static void idle(ArcRpcHdStore *s) {
    ArcRpcHdStats a;
    for (unsigned i = 0;; ++i) {
        assert(i < 10000);
        uint64_t ops = steps, meta = metadata_calls, ns = namespace_calls;
        arc_riscpc_hd_tick(s);
        budget(s, ops, meta, ns);
        arc_riscpc_hd_stats(s, &a);
        if (a.state == ARC_RPC_HD_IDLE)
            break;
    }
}
static void receipt_store(void) {
    ArcRpcHdStore *s;
    assert(open_steps(&s) == ARC_RPC_HD_UNPUBLISHED);
    assert(arc_riscpc_hd_population_begin(s));
    uint8_t payload[131072];
    memset(payload, 0x73, sizeof(payload));
    assert(arc_riscpc_hd_population_io_begin(s, 511, 256, payload, true));
    bool done = false;
    while (!done) {
        uint64_t ops = steps, meta = metadata_calls, ns = namespace_calls;
        assert(arc_riscpc_hd_population_io_step(s, &done));
        budget(s, ops, meta, ns);
    }
    ArcRpcHdReceipt r = {.files = 7, .bytes = 6470};
    memset(r.packages, 0x82, 64);
    assert(arc_riscpc_hd_population_receipt(s, &r));
    done = false;
    while (!done) {
        uint64_t ops = steps, meta = metadata_calls, ns = namespace_calls;
        assert(arc_riscpc_hd_population_seal_step(s, &done));
        budget(s, ops, meta, ns);
    }
    done = false;
    while (!done) {
        uint64_t ops = steps, meta = metadata_calls, ns = namespace_calls;
        assert(arc_riscpc_hd_population_publish_step(s, &done));
        budget(s, ops, meta, ns);
    }
    idle(s);
    arc_riscpc_hd_close(s);
    assert(open_steps(&s) == ARC_RPC_HD_RECEIPTED);
    uint8_t read[131072];
    assert(arc_riscpc_hd_read(s, 511, 256, read) &&
           !memcmp(read, payload, sizeof(payload)));
    ArcRpcHdReceipt actual;
    assert(arc_riscpc_hd_receipt(s, &actual) &&
           !memcmp(&r, &actual, sizeof(r)));
    arc_riscpc_hd_close(s);
    snapshot();
}
static int manifest(void) {
    char p[1100];
    snprintf(p, sizeof(p), "/save/arcdroid/hd-riscpc/%s/manifest.a", id);
    int i = find(p, false);
    assert(i >= 0);
    return i;
}
static void fix_crc(TestFile *f) {
    uint32_t crc = arc_crc32(f->data, f->size - 4);
    for (unsigned i = 0; i < 4; ++i)
        f->data[f->size - 4 + i] = (uint8_t)(crc >> (i * 8));
}
static void outcomes(void) {
    const unsigned offsets[] = {24328, 24332, 24336, 8, 0, 16, 24448};
    for (unsigned k = 0; k < sizeof(offsets) / sizeof(*offsets); ++k) {
        restore();
        TestFile *f = &files[manifest()];
        f->data[offsets[k]] ^= 2;
        if (k != 6)
            fix_crc(f);
        uint8_t before[32], after[32];
        disk_digest(before);
        uint64_t ns = namespace_calls;
        ArcRpcHdStore *s;
        ArcRpcHdOutcome expected =
            k < 5 ? ARC_RPC_HD_UNSUPPORTED : ARC_RPC_HD_CORRUPT;
        assert(open_steps(&s) == expected);
        ArcIdeBackend backend;arc_riscpc_hd_backend(s,&backend);assert(!backend.sectors);
        assert(!arc_riscpc_hd_receipt(s,NULL));
        assert(namespace_calls == ns);
        const char *notice = arc_riscpc_hd_outcome_notice(expected);
        assert(notice);
        if (expected == ARC_RPC_HD_UNSUPPORTED)
            assert(strstr(notice, "created by a newer version of ArcDroid"));
        arc_riscpc_hd_close(s);
        disk_digest(after);
        assert(!memcmp(before, after, 32));
        test_vfs_no_open_handles();
    }
    /* Integrity wins over a future receipt revision in a known envelope. */
    restore();
    files[manifest()].data[24328] = 2;
    uint8_t before[32], after[32];
    disk_digest(before);
    ArcRpcHdStore *s;
    assert(open_steps(&s) == ARC_RPC_HD_CORRUPT);
    arc_riscpc_hd_close(s);
    disk_digest(after);
    assert(!memcmp(before, after, 32));
    /* Unknown receipt fields stay opaque after authenticating the known envelope. */
    restore();TestFile *future=&files[manifest()];future->data[24328]=2;
    future->data[24424]=0x71;fix_crc(future);disk_digest(before);
    assert(open_steps(&s)==ARC_RPC_HD_UNSUPPORTED);arc_riscpc_hd_close(s);
    disk_digest(after);assert(!memcmp(before,after,32));
    /* An unfamiliar unbound short journal is never treated as creation debris. */
    restore();char foreign_name[1100];snprintf(foreign_name,sizeof(foreign_name),"/save/arcdroid/hd-riscpc/%s/journal.b",id);
    int foreign=find(foreign_name,true);files[foreign].data=malloc(8);
    files[foreign].capacity=files[foreign].size=8;memcpy(files[foreign].data,"FOREIGN!",8);
    disk_digest(before);assert(open_steps(&s)==ARC_RPC_HD_UNSUPPORTED);arc_riscpc_hd_close(s);
    disk_digest(after);assert(!memcmp(before,after,32));
    /* An unreadable length-mismatched slot is I/O unavailability, not a
     * successful integrity inspection. */
    bool failed_prefix=false;
    for(unsigned at=1;at<100&&!failed_prefix;++at){
        restore();files[manifest()].size-=1;disk_digest(before);fault=5;fail_at=at;
        ArcRpcHdOutcome r=open_steps(&s);assert(r==ARC_RPC_HD_CORRUPT||r==ARC_RPC_HD_UNAVAILABLE);
        failed_prefix=r==ARC_RPC_HD_UNAVAILABLE;arc_riscpc_hd_close(s);disk_digest(after);assert(!memcmp(before,after,32));
    }
    assert(failed_prefix);
    /* Mixed supported and opaque slots cannot silently fall back. */
    restore();
    int source = manifest();
    char name[1100];
    snprintf(name, sizeof(name), "/save/arcdroid/hd-riscpc/%s/manifest.b", id);
    int other = find(name, true);
    files[other].data = malloc(24452);
    files[other].capacity = files[other].size = 24452;
    memcpy(files[other].data, files[source].data, 24452);
    files[other].data[8] = 3;
    files[other].data[88] = 1;
    fix_crc(&files[other]);
    disk_digest(before);
    assert(open_steps(&s) == ARC_RPC_HD_UNSUPPORTED);
    arc_riscpc_hd_close(s);
    disk_digest(after);
    assert(!memcmp(before, after, 32));
    /* Every reported read failure is unavailable, not damage or absence. */
    bool unavailable = false;
    for (unsigned at = 1; at < 100; ++at) {
        restore();
        fault = 5;
        fail_at = at;
        disk_digest(before);
        ArcRpcHdOutcome r = open_steps(&s);
        arc_riscpc_hd_close(s);
        disk_digest(after);
        assert(!memcmp(before, after, 32));
        assert(r == ARC_RPC_HD_RECEIPTED || r == ARC_RPC_HD_UNAVAILABLE);
        if (r == ARC_RPC_HD_UNAVAILABLE) {
            assert(steps >= at);
            unavailable = true;
        }
    }
    assert(unavailable);
}

static void overlapping_records(void) {
    restore();
    ArcRpcHdStore *s;
    assert(open_steps(&s) == ARC_RPC_HD_RECEIPTED);
    uint8_t bytes[131072];
    memset(bytes, 0x91, sizeof(bytes));
    assert(arc_riscpc_hd_population_begin(s));
    assert(arc_riscpc_hd_population_write(s, 1024, 256, bytes));
    assert(arc_riscpc_hd_population_seal(s) &&
           arc_riscpc_hd_population_publish(s));
    idle(s);
    /* Forty maximum-size records each retain a different leading sector in
     * the final index. Reading only one frame per contributing record would
     * otherwise put >5 MiB of reconstruction in one tick. */
    for (unsigned i = 0; i < 40; ++i) {
        memset(bytes, (int)(i + 1), sizeof(bytes));
        assert(arc_riscpc_hd_write(s, 1024 + i, 256, bytes));
    }
    idle(s);
    for (unsigned i = 0; i < 40; ++i) {
        uint8_t sector[512];
        assert(arc_riscpc_hd_read(s, 1024 + i, 1, sector));
        for (unsigned j = 0; j < 512; ++j)
            assert(sector[j] == i + 1);
    }
    arc_riscpc_hd_close(s);
    assert(open_steps(&s) == ARC_RPC_HD_RECEIPTED);
    arc_riscpc_hd_close(s);
}
int main(void) {
    fresh_files();
    receipt_store();
    outcomes();
    overlapping_records();
    fresh_files();
    ArcRpcHdStore *s =
        arc_riscpc_hd_open("/save", id, true, error, sizeof(error));
    assert(s);
    arc_riscpc_hd_close(s);
    assert(open_steps(&s) == ARC_RPC_HD_LEGACY);
    arc_riscpc_hd_close(s);
    fresh_files();
    test_vfs_no_open_handles();
    puts("R3b-2a: outcomes, immutable refusal, receipt, crossing write, all "
         "step counters pass");
    return 0;
}
