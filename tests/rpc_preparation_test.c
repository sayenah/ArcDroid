/* R3b-2a independent outcome, no-mutation and operation-budget oracle.
 * GPL-2.0-only. */
#include "arcdroid_core.h"
#include "rpc_hd_store.h"
#include "rpc_hd_vfs.h"
static const char *id = "0123456789012345678901234567890123456789";
static char error[256];
static uint64_t max_vfs_bytes,max_vfs_operations,max_vfs_metadata,max_vfs_namespace;
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
static void budget(ArcRpcHdStore *s, uint64_t ops, uint64_t meta, uint64_t ns, uint64_t bytes) {
    ArcRpcHdStats a;
    arc_riscpc_hd_stats(s, &a);
    if(vfs_requested_bytes-bytes>max_vfs_bytes)max_vfs_bytes=vfs_requested_bytes-bytes;
    if(steps-ops>max_vfs_operations)max_vfs_operations=steps-ops;
    if(metadata_calls-meta>max_vfs_metadata)max_vfs_metadata=metadata_calls-meta;
    if(namespace_calls-ns>max_vfs_namespace)max_vfs_namespace=namespace_calls-ns;
    assert(a.tick_bytes == vfs_requested_bytes - bytes);
    assert(vfs_requested_bytes - bytes <= ARC_RPC_HD_STEP_BYTES);
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
        uint64_t ops = steps, meta = metadata_calls, ns = namespace_calls, bytes = vfs_requested_bytes;
        result = arc_riscpc_hd_open_step(*out);
        budget(*out, ops, meta, ns, bytes);
    }
    return result;
}
static void idle(ArcRpcHdStore *s) {
    ArcRpcHdStats a;
    for (unsigned i = 0;; ++i) {
        assert(i < 10000);
        uint64_t ops = steps, meta = metadata_calls, ns = namespace_calls, bytes = vfs_requested_bytes;
        arc_riscpc_hd_tick(s);
        budget(s, ops, meta, ns, bytes);
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
        uint64_t ops = steps, meta = metadata_calls, ns = namespace_calls, bytes = vfs_requested_bytes;
        assert(arc_riscpc_hd_population_io_step(s, &done));
        budget(s, ops, meta, ns, bytes);
    }
    ArcRpcHdReceipt r = {.files = 7, .bytes = 6470};
    memset(r.packages, 0x82, 64);
    assert(arc_riscpc_hd_population_receipt(s, &r));
    done = false;
    while (!done) {
        uint64_t ops = steps, meta = metadata_calls, ns = namespace_calls, bytes = vfs_requested_bytes;
        assert(arc_riscpc_hd_population_seal_step(s, &done));
        budget(s, ops, meta, ns, bytes);
    }
    done = false;
    while (!done) {
        uint64_t ops = steps, meta = metadata_calls, ns = namespace_calls, bytes = vfs_requested_bytes;
        assert(arc_riscpc_hd_population_publish_step(s, &done));
        budget(s, ops, meta, ns, bytes);
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

static int journal(void) {
    char path[1100];
    snprintf(path,sizeof(path),"/save/arcdroid/hd-riscpc/%s/journal.a",id);
    int i=find(path,false);assert(i>=0);return i;
}
static void set32(uint8_t *p,unsigned n) {
    for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(n>>(8*i));
}
static void refusal(ArcRpcHdOutcome expected) {
    uint8_t before[32],after[32];disk_digest(before);
    uint64_t ns=namespace_calls;ArcRpcHdStore *s;
    assert(open_steps(&s)==expected);
    assert(namespace_calls==ns && !arc_riscpc_hd_receipt(s,NULL));
    ArcIdeBackend backend;arc_riscpc_hd_backend(s,&backend);assert(!backend.sectors);
    arc_riscpc_hd_close(s);disk_digest(after);assert(!memcmp(before,after,32));
    test_vfs_no_open_handles();
}
static void future_envelopes(void) {
    /* Authentication precedes revision, length and revision-specific geometry.
     * Include the QA reproduction: v3, 24580-byte manifest with valid CRC. */
    for(unsigned kind=0;kind<2;++kind)
        for(unsigned variant=0;variant<5;++variant)
            for(unsigned damage=0;damage<3;++damage){
                restore();TestFile *f=&files[kind?journal():manifest()];
                unsigned length=kind?384:24580;
                if(variant<2){
                    uint8_t *p=realloc(f->data,length);assert(p);f->data=p;
                    memset(p+f->size,0,length-f->size);f->size=f->capacity=length;
                    set32(p+12,length);
                }
                if(variant==0 || variant==2)set32(f->data+8,3);
                if(variant==3 || variant==4){
                    set32(f->data+(variant==3?56:60),2);
                    set32(f->data+64,1024); /* geometry belongs to a future revision */
                }
                fix_crc(f);
                if(damage==1)f->data[f->size-1]^=1;
                if(damage==2){f->data[16]^=1;fix_crc(f);}
                refusal(damage?ARC_RPC_HD_CORRUPT:ARC_RPC_HD_UNSUPPORTED);
            }
    /* Smaller complete future headers are authenticated too, including when
     * the current manifest names that journal. */
    for(unsigned damage=0;damage<2;++damage){
        restore();TestFile *f=&files[journal()];f->size=128;
        set32(f->data+8,3);set32(f->data+12,128);fix_crc(f);
        if(damage)f->data[127]^=1;
        refusal(damage?ARC_RPC_HD_CORRUPT:ARC_RPC_HD_UNSUPPORTED);
    }
    /* Bounded inspection authenticates exactly through the scratch limit. */
    for(unsigned kind=0;kind<2;++kind)
        for(unsigned damage=0;damage<2;++damage){
            restore();TestFile *f=&files[kind?journal():manifest()];
            uint8_t *p=realloc(f->data,131168);assert(p);f->data=p;
            memset(p+f->size,0,131168-f->size);f->size=f->capacity=131168;
            set32(p+8,3);set32(p+12,131168);fix_crc(f);
            if(damage)p[131167]^=1;
            refusal(damage?ARC_RPC_HD_CORRUPT:ARC_RPC_HD_UNSUPPORTED);
        }
    /* A claimed length inconsistent with the available authenticated frame is damage. */
    for(unsigned kind=0;kind<2;++kind){
        restore();TestFile *f=&files[kind?journal():manifest()];
        set32(f->data+8,3);set32(f->data+12,(unsigned)f->size+128);fix_crc(f);
        refusal(ARC_RPC_HD_CORRUPT);
    }
    restore();TestFile *f=&files[journal()];
    set32(f->data+8,3);set32(f->data+12,131169);fix_crc(f);
    /* An impossible advertised size is damaged framing, even beyond the cap. */
    refusal(ARC_RPC_HD_CORRUPT);

}
static void publish_cancellation(void) {
    for(unsigned cut=0;cut<=6;++cut){
        fresh_files();ArcRpcHdStore *s;assert(open_steps(&s)==ARC_RPC_HD_UNPUBLISHED);
        assert(arc_riscpc_hd_population_begin(s));
        uint8_t payload[512];memset(payload,0x59,sizeof(payload));
        assert(arc_riscpc_hd_population_write(s,1000,1,payload));
        ArcRpcHdReceipt r={.files=23,.bytes=512};memset(r.packages,0x45,64);
        assert(arc_riscpc_hd_population_receipt(s,&r));
        assert(arc_riscpc_hd_population_seal(s));bool done=false;
        for(unsigned i=0;i<cut;++i){
            uint64_t ops=steps,meta=metadata_calls,ns=namespace_calls,bytes=vfs_requested_bytes;
            assert(arc_riscpc_hd_population_publish_step(s,&done));
            budget(s,ops,meta,ns,bytes);
        }
        assert(done==(cut==6));
        arc_riscpc_hd_population_abort(s);arc_riscpc_hd_close(s);
        assert(open_steps(&s)==(cut>=5?ARC_RPC_HD_RECEIPTED:ARC_RPC_HD_UNPUBLISHED));
        ArcRpcHdStats stats;arc_riscpc_hd_stats(s,&stats);assert(stats.published==(cut>=5));
        if(cut>=5){ArcRpcHdReceipt actual;uint8_t read[512];
            assert(arc_riscpc_hd_receipt(s,&actual)&&!memcmp(&r,&actual,sizeof(r)));
            assert(arc_riscpc_hd_read(s,1000,1,read)&&!memcmp(read,payload,512));
        }else assert(!arc_riscpc_hd_receipt(s,NULL));
        arc_riscpc_hd_close(s);test_vfs_no_open_handles();
    }
    restore();
}
static void reconstruction_failure(void) {
    /* Start a real checkpoint, then fail its contributing-record read after
     * rotation. Verify the local reconstruction handle closes immediately. */
    for(unsigned corrupt=0;corrupt<2;++corrupt){
        restore();ArcRpcHdStore *s;assert(open_steps(&s)==ARC_RPC_HD_RECEIPTED);
        uint8_t payload[131072];memset(payload,0x51,sizeof(payload));
        for(unsigned i=0;i<8;++i)assert(arc_riscpc_hd_write(s,1024+i*256,256,payload));
        ArcRpcHdStats a;
        for(unsigned i=0;;++i){assert(i<100);arc_riscpc_hd_stats(s,&a);
            if(a.state==ARC_RPC_HD_MATERIALIZE&&a.phase_step==0)break;
            arc_riscpc_hd_tick(s);
        }
        if(corrupt){TestFile *f=&files[journal()];assert(f->size>256+96);f->data[256+80]^=1;}
        else{fault=5;fail_at=steps+4;} /* open, seek, tell, framed-record read */
        uint64_t ops=steps,meta=metadata_calls,ns=namespace_calls,bytes=vfs_requested_bytes;
        arc_riscpc_hd_tick(s);budget(s,ops,meta,ns,bytes);
        arc_riscpc_hd_stats(s,&a);assert(a.read_only);
        test_vfs_no_open_handles();arc_riscpc_hd_close(s);
    }
}
static void torn_write_refusal(void) {
    for(unsigned legacy=0;legacy<2;++legacy)
      for(unsigned cause=0;cause<2;++cause){
        restore();
        if(legacy){TestFile *m=&files[manifest()];m->size=24324;
            set32(m->data+8,1);set32(m->data+12,24324);fix_crc(m);}
        TestFile *f=&files[journal()];size_t size=f->size;
        uint8_t *p=realloc(f->data,size+7);assert(p);f->data=p;
        memset(p+size,0x63,7);f->size=f->capacity=size+7;
        uint8_t before[32],after[32];disk_digest(before);
        if(cause)refuse_update_open=true;else refuse_truncate=true;
        ArcRpcHdStore *s;assert(open_steps(&s)==(legacy?ARC_RPC_HD_LEGACY:ARC_RPC_HD_RECEIPTED));
        ArcRpcHdStats a;arc_riscpc_hd_stats(s,&a);assert(a.read_only && a.published);
        assert(strstr(arc_riscpc_hd_notice(s),"recovered; the hard disc is read-only"));
        uint8_t read[512];assert(arc_riscpc_hd_read(s,511,1,read));
        for(unsigned i=0;i<512;++i)assert(read[i]==0x73);
        ArcRpcHdReceipt r;assert(arc_riscpc_hd_receipt(s,&r)==!legacy);if(!legacy)assert(r.files==7);
        assert(!arc_riscpc_hd_write(s,0,1,read));arc_riscpc_hd_close(s);
        disk_digest(after);assert(!memcmp(before,after,32));test_vfs_no_open_handles();
        refuse_update_open=refuse_truncate=false;
    }
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
    /* Reopen before checkpoint: the >5 MiB journal spans many replay steps. */
    arc_riscpc_hd_close(s);
    assert(open_steps(&s)==ARC_RPC_HD_RECEIPTED);
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
    future_envelopes();
    publish_cancellation();
    reconstruction_failure();
    torn_write_refusal();
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
    printf("HD independent VFS maxima: bytes=%llu operations=%llu metadata=%llu namespace=%llu\n",
        (unsigned long long)max_vfs_bytes,(unsigned long long)max_vfs_operations,
        (unsigned long long)max_vfs_metadata,(unsigned long long)max_vfs_namespace);
    puts("R3b-2a: outcomes, immutable refusal, receipt, crossing write, all "
         "step counters pass");
    return 0;
}
