/* RPC records never use Arculator's envelope or normalisation. GPL-2.0-only. */
#include "rpc_cmos_store.h"
#include "arc_io.h"
#include "arcdroid_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static bool fail(char *error, size_t n, const char *message)
{ if(error && n)snprintf(error,n,"RPC CMOS: %s",message);return false; }
static void put(uint8_t *p, uint64_t v, unsigned n)
{ for(unsigned i=0;i<n;++i)p[i]=(uint8_t)(v>>(8*i)); }
static uint64_t get(const uint8_t *p,unsigned n)
{ uint64_t v=0;for(unsigned i=0;i<n;++i)v|=(uint64_t)p[i]<<(8*i);return v; }
unsigned arc_riscpc_cmos_layout(const char *s)
{ const char *names[]={"", "ro350", "ro360", "ro370"};for(unsigned i=1;s && i<4;++i)if(!strcmp(s,names[i]))return i;return 0; }
bool arc_riscpc_cmos_hex(const char *s,size_t n,char *out)
{
    if(!s || strlen(s)!=n)return false;
    for(size_t i=0;i<n;++i){char c=s[i];if(c>='A'&&c<='F')c+=(char)('a'-'A');if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;out[i]=c;}out[n]=0;return true;
}
static unsigned nibble(char c){return c<='9'?(unsigned)(c-'0'):(unsigned)(c-'a'+10);}
static void digest_bytes(const char *s,uint8_t *p)
{ for(unsigned i=0;i<32;++i)p[i]=(uint8_t)(nibble(s[2*i])*16+nibble(s[2*i+1])); }
bool arc_riscpc_cmos_checksum(const uint8_t p[240])
{ unsigned sum=1;for(unsigned i=0;i<240;++i)if(i!=47)sum+=p[i];return (uint8_t)sum==p[47]; }
/* Physical address masks, deliberately data rather than firmware recognisers. */
static const uint8_t required[3][32]={
 {0,0,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,243,255,255,255,255,255,255,255},
 {0,0,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255},
 {0,0,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255}};
void arc_riscpc_cmos_required(unsigned layout,uint8_t mask[32])
{ if(layout>=1 && layout<=3)memcpy(mask,required[layout-1],32);else memset(mask,0,32); }
bool arc_riscpc_cmos_oracle(unsigned layout,const uint8_t p[240],const ArcRpcCmosEvidence *e)
{
    if(layout<1 || layout>3 || !e || !e->writes || e->last_write>ARC_RPC_CMOS_CAPTURE_NS-ARC_RPC_CMOS_STABILITY_NS || !arc_riscpc_cmos_checksum(p))return false;
    for(unsigned i=0;i<32;++i)if((e->mask[i]&required[layout-1][i])!=required[layout-1][i])return false;
    return true;
}
static void header(const ArcRpcCmosKey *k,uint8_t p[512],uint64_t gen)
{
    memset(p,0,512);memcpy(p,k->defaults?"ARCRPCD1":"ARCRPCM1",8);
    put(p+8,1,4);put(p+12,512,4);put(p+16,k->model+1,4);put(p+20,0x10000u|k->layout,4);put(p+24,gen,8);
    if(!k->defaults)memcpy(p+32,k->content,40);
    digest_bytes(k->firmware,p+72);
}
static void encode(ArcRpcCmosStore *s,const uint8_t payload[240],const ArcRpcCmosEvidence *e,uint8_t p[512])
{
    header(&s->key,p,s->generation+1);memcpy(p+104,payload,240);
    if(s->key.defaults){
        put(p+344,s->key.procedure,4);put(p+348,s->key.timing,4);put(p+352,e->rate,4);put(p+356,1,4);
        put(p+360,e->epoch,8);put(p+368,0,8);put(p+376,ARC_RPC_CMOS_DELETE_NS,8);
        put(p+384,ARC_RPC_CMOS_CAPTURE_NS,8);put(p+392,ARC_RPC_CMOS_STABILITY_NS,8);
        put(p+400,e->writes,8);put(p+408,e->last_write,8);memcpy(p+416,e->mask,32);
        char digest[65];arc_sha256_hex(payload,240,digest);digest_bytes(digest,p+448);
    }
    put(p+508,arc_crc32(p,508),4);
}
static bool decode(ArcRpcCmosStore *s,const uint8_t *p,size_t size,ArcRpcCmosEvidence *e)
{
    uint8_t expected[512];header(&s->key,expected,1);
    if(size!=512 || memcmp(p,expected,24) || memcmp(p+32,expected+32,72) || !get(p+24,8) || get(p+508,4)!=arc_crc32(p,508))return false;
    memset(e,0,sizeof(*e));
    if(s->key.defaults){
        if(get(p+344,4)!=s->key.procedure || get(p+348,4)!=s->key.timing || get(p+356,4)!=1 ||
           get(p+368,8)!=0 || get(p+376,8)!=ARC_RPC_CMOS_DELETE_NS || get(p+384,8)!=ARC_RPC_CMOS_CAPTURE_NS || get(p+392,8)!=ARC_RPC_CMOS_STABILITY_NS)return false;
        static const unsigned rates[]={20,35,100,35};
        e->rate=(unsigned)get(p+352,4);e->epoch=get(p+360,8);e->writes=get(p+400,8);e->last_write=get(p+408,8);memcpy(e->mask,p+416,32);
        /* Payload-only evidence cannot claim more distinct written addresses
         * than writes, or include RTC registers. Validate cached provenance. */
        unsigned covered=0;
        for(unsigned i=2;i<32;++i)for(unsigned bit=0;bit<8;++bit)covered+=(e->mask[i]>>bit)&1u;
        if(e->mask[0] || e->mask[1] || e->writes<covered)return false;
        char digest[65];uint8_t bytes[32];arc_sha256_hex(p+104,240,digest);digest_bytes(digest,bytes);
        if(e->rate!=rates[s->key.model] || e->epoch>UINT64_C(13569465599) || memcmp(p+448,bytes,32) || !arc_riscpc_cmos_oracle(s->key.layout,p+104,e))return false;
        for(unsigned i=480;i<508;++i)if(p[i])return false;
    }else for(unsigned i=344;i<508;++i)if(p[i])return false;
    return true;
}
static bool path(const ArcRpcCmosStore *s,const char *suffix,char out[1040])
{ int n=snprintf(out,1040,"%s%s",s->base,suffix);return n>=0 && n<1040; }

/* Step counters include failed calls and cleanup, without a production clock.
 */
static void begin_count(ArcRpcCmosStore *s) {
    s->stats.step_operations = s->stats.operations;
    s->stats.step_bytes = s->stats.bytes;
    s->stats.step_metadata = s->stats.metadata;
    s->stats.step_namespace = s->stats.namespace_operations;
}
static void end_count(ArcRpcCmosStore *s) {
#define FINISH(field, total, max)                                              \
    s->stats.field = s->stats.total - s->stats.field;                          \
    if (s->stats.field > s->stats.max)                                         \
    s->stats.max = s->stats.field
    FINISH(step_operations, operations, max_operations);
    FINISH(step_bytes, bytes, max_bytes);
    FINISH(step_metadata, metadata, max_metadata);
    FINISH(step_namespace, namespace_operations, max_namespace);
#undef FINISH
}
static void meta(ArcRpcCmosStore *s) {
    ++s->stats.operations;
    ++s->stats.metadata;
}
static ArcIoFile *open_record(ArcRpcCmosStore *s, const char *p, bool create) {
    meta(s);
    if (create)
        ++s->stats.namespace_operations;
    uint64_t before = s->stats.operations;
    ArcIoFile *f =
        create ? arc_io_create_file_counted(p, &s->stats.operations)
               : arc_io_open_stream_counted(p, false, &s->stats.operations);
    s->stats.metadata += s->stats.operations - before;
    return f;
}
static bool close_record(ArcRpcCmosStore *s, ArcIoFile *f) {
    meta(s);
    return arc_io_close_file(f);
}
static bool read_record(ArcRpcCmosStore *s, const char *p, uint8_t out[512],
                        bool *invalid) {
    *invalid = false;
    ArcIoFile *f = open_record(s, p, false);
    if (!f)
        return false;
    ++s->stats.operations;
    int64_t n = arc_io_file_length(f);
    bool ok = n >= 0;
    if (n >= 0 && n != 512)
        *invalid = true;
    if (n == 512) {
        ++s->stats.operations;
        s->stats.bytes += 512;
        ok = arc_io_read_file(f, out, 512) == 512;
    }
    bool closed = close_record(s, f);
    return ok && closed;
}
static ArcRpcCmosResult result(const ArcRpcCmosStore *s) {
    return !s->reconciled ? (s->exhausted ? ARC_RPC_CMOS_UNAVAILABLE
                                          : ARC_RPC_CMOS_INSPECTING)
           : s->present   ? ARC_RPC_CMOS_FOUND
           : s->refused   ? ARC_RPC_CMOS_REFUSED
                          : ARC_RPC_CMOS_MISSING;
}
void arc_riscpc_cmos_store_close(ArcRpcCmosStore *s) {
    if (s->directory_handle) {
        meta(s);
        (void)arc_io_close_directory(s->directory_handle);
        s->directory_handle = NULL;
    }
}
static void retry(ArcRpcCmosStore *s, char *e, size_t n) {
    arc_riscpc_cmos_store_close(s);
    s->reconciled = false;
    s->putting = false;
    /* Never clear a corruption refusal, including during later reconciliation.
     */
    s->phase = 1;
    s->slot = 0;
    memset(s->slot_found, 0, sizeof(s->slot_found));
    if (++s->attempts >= 3)
        s->exhausted = true;
    fail(e, n,
         "record inspection unavailable; no seed or publication admitted");
}
ArcRpcCmosResult arc_riscpc_cmos_store_begin(ArcRpcCmosStore *s,
                                             const char *save,
                                             const ArcRpcCmosKey *k, char *e,
                                             size_t n) {
    memset(s, 0, sizeof(*s));
    if (e && n)
        *e = 0;
    if (!k) {
        s->refused = s->reconciled = true;
        fail(e, n, "invalid key");
        return ARC_RPC_CMOS_REFUSED;
    }
    s->key = *k;
    char content[41], firmware[65];
    if (!save || !*save || k->model >= 4 || k->layout < 1 || k->layout > 3 ||
        !arc_riscpc_cmos_hex(k->firmware, 64, firmware) ||
        (!k->defaults && !arc_riscpc_cmos_hex(k->content, 40, content))) {
        s->refused = s->reconciled = true;
        fail(e, n, "invalid key or save directory");
        return ARC_RPC_CMOS_REFUSED;
    }
    memcpy(s->key.firmware, firmware, 65);
    if (!k->defaults)
        memcpy(s->key.content, content, 41);
    int len = snprintf(s->directory, sizeof(s->directory),
                       "%s/arcdroid/cmos-riscpc%s", save,
                       k->defaults ? "/defaults" : "");
    if (len < 0 || len >= (int)sizeof(s->directory))
        goto invalid;
    len = k->defaults ? snprintf(s->base, sizeof(s->base), "%s/%s-m%u-p%u-t%u",
                                 s->directory, firmware, k->model + 1,
                                 k->procedure, k->timing)
                      : snprintf(s->base, sizeof(s->base), "%s/%s-m%u-%s",
                                 s->directory, content, k->model + 1, firmware);
    if (len < 0 || len >= (int)sizeof(s->base))
        goto invalid;
    return ARC_RPC_CMOS_INSPECTING;
invalid:
    s->refused = s->reconciled = true;
    fail(e, n, "save path too long");
    return ARC_RPC_CMOS_REFUSED;
}
ArcRpcCmosResult arc_riscpc_cmos_store_step(ArcRpcCmosStore *s, char *e,
                                            size_t n) {
    if (s->reconciled || s->exhausted || s->putting) {
        begin_count(s);
        end_count(s);
        return result(s);
    }
    begin_count(s);
    char filename[1040];
    bool done, invalid;
    size_t bytes;
    switch (s->phase) {
    case 0: {
        uint64_t before = s->stats.operations;
        bool ok = arc_io_make_path_step(s->directory, &s->path_cursor, &done,
                                        &s->stats.operations,
                                        &s->stats.namespace_operations);
        s->stats.metadata += s->stats.operations - before;
        if (!ok) {
            retry(s, e, n);
            s->phase = 0;
            s->path_cursor = 0;
        } else if (done)
            s->phase = 1;
        break;
    }
    case 1:
        meta(s);
        s->directory_handle = arc_io_open_directory(s->directory);
        if (!s->directory_handle)
            retry(s, e, n);
        else {
            s->phase = 2;
            s->slot = 0;
        }
        break;
    case 2: {
        const char *name;
        bool regular;
        uint64_t before = s->stats.operations;
        int r = arc_io_read_directory(s->directory_handle, &name, &regular,
                                      &s->stats.operations);
        s->stats.metadata += s->stats.operations - before;
        if (r < 0 || ++s->slot > 4096) {
            retry(s, e, n);
            break;
        }
        if (!r) {
            s->phase = 3;
            break;
        }
        for (unsigned i = 0; i < 2; ++i) {
            path(s, i ? ".1" : ".0", filename);
            if (!strcmp(name, strrchr(filename, '/') + 1))
                s->slot_found[i] = true;
        }
        break;
    }
    case 3: {
        meta(s);
        bool ok = arc_io_close_directory(s->directory_handle);
        s->directory_handle = NULL;
        if (!ok)
            retry(s, e, n);
        else {
            s->slot = 0;
            s->phase = 4;
        }
        break;
    }
    case 4:
        path(s, s->slot ? ".1" : ".0", filename);
        meta(s);
        if (arc_io_file_size(filename, &bytes))
            s->slot_found[s->slot] = true;
        /* A silent/partial listing cannot turn an existing unreadable record
         * into absence. Exact-path open supplements stat under the same
         * truthful-path assumption as the HD store (contract section 10). */
        if (!s->slot_found[s->slot]) {
            ArcIoFile *f = open_record(s, filename, false);
            if (f) {
                (void)close_record(s, f);
                s->slot_found[s->slot] = true;
            }
        }
        s->phase = 5;
        break;
    case 5:
        if (s->slot_found[s->slot]) {
            path(s, s->slot ? ".1" : ".0", filename);
            if (!read_record(s, filename, s->inspected, &invalid)) {
                retry(s, e, n);
                break;
            }
            ArcRpcCmosEvidence evidence;
            if (invalid || !decode(s, s->inspected, 512, &evidence) ||
                (get(s->inspected + 24, 8) & 1) != s->slot) {
                s->refused = true;
                fail(e, n,
                     "refused corrupt, foreign or wrong-key slot; preserved on "
                     "disk");
            } else if (get(s->inspected + 24, 8) > s->generation) {
                s->generation = get(s->inspected + 24, 8);
                s->present = true;
                memcpy(s->payload, s->inspected + 104, 240);
                s->evidence = evidence;
            }
        }
        if (++s->slot < 2)
            s->phase = 4;
        else {
            s->reconciled = true;
            s->publication_pending = false;
            s->attempts = 0;
            if (!s->refused && e && n)
                *e = 0;
        }
        break;
    default:
        retry(s, e, n);
        break;
    }
    end_count(s);
    return result(s);
}
ArcRpcCmosResult arc_riscpc_cmos_store_load(ArcRpcCmosStore *s,
                                            const char *save,
                                            const ArcRpcCmosKey *k, char *e,
                                            size_t n) {
    ArcRpcCmosResult r = arc_riscpc_cmos_store_begin(s, save, k, e, n);
    while (r == ARC_RPC_CMOS_INSPECTING)
        r = arc_riscpc_cmos_store_step(s, e, n);
    return r;
}
bool arc_riscpc_cmos_store_put_begin(ArcRpcCmosStore *s,
                                     const uint8_t payload[240],
                                     const ArcRpcCmosEvidence *e, char *error,
                                     size_t n) {
    if (s->refused || !s->reconciled || s->publication_pending || s->putting ||
        s->generation == UINT64_MAX)
        return fail(error, n,
                    "publication refused while inspection is incomplete or "
                    "data is refused");
    if (s->present && !memcmp(s->payload, payload, 240)) {
        s->phase = 100;
        return true;
    }
    if (s->key.defaults &&
        (!e || !arc_riscpc_cmos_oracle(s->key.layout, payload, e)))
        return fail(error, n, "default completion oracle failed");
    encode(s, payload, e, s->candidate);
    s->putting = true;
    s->phase = 10;
    return true;
}
bool arc_riscpc_cmos_store_put_step(ArcRpcCmosStore *s, bool *done, char *e,
                                    size_t n) {
    *done = false;
    if (s->phase == 100) {
        begin_count(s);
        end_count(s);
        *done = true;
        return true;
    }
    if (!s->putting || s->refused || !s->reconciled)
        return false;
    begin_count(s);
    char tmp[1040], final[1040];
    path(s, ".tmp", tmp);
    path(s, (s->generation + 1) & 1 ? ".1" : ".0", final);
    bool ok = true, invalid;
    size_t length;
    switch (s->phase) {
    case 10:
        meta(s);
        s->destination_found = arc_io_file_size(final, &length);
        if (!s->destination_found) {
            ArcIoFile *f = open_record(s, final, false);
            if (f) {
                s->destination_found = true;
                ok = close_record(s, f);
            }
        }
        ++s->phase;
        break;
    case 11:
        if (s->destination_found) {
            ok = read_record(s, final, s->inspected, &invalid);
            ArcRpcCmosEvidence old;
            if (ok && (invalid || !decode(s, s->inspected, 512, &old) ||
                       get(s->inspected + 24, 8) >= s->generation ||
                       (get(s->inspected + 24, 8) & 1) !=
                           ((s->generation + 1) & 1))) {
                s->refused = true;
                ok = false;
                fail(e, n, "destination changed or refused; preserved on disk");
            }
        }
        ++s->phase;
        break;
    case 12: {
        s->publication_pending = true;
        ArcIoFile *f = open_record(s, tmp, true);
        if (!f) {
            ok = false;
            break;
        }
        ++s->stats.operations;
        s->stats.bytes += 512;
        ok = arc_io_write_file(f, s->candidate, 512) == 512;
        if (ok) {
            meta(s);
            ok = arc_io_flush_available() && arc_io_flush_file(f);
        }
        bool closed = close_record(s, f);
        ok &= closed;
        ++s->phase;
        break;
    }
    case 13:
        ok = read_record(s, tmp, s->inspected, &invalid) && !invalid &&
             !memcmp(s->candidate, s->inspected, 512);
        if (!ok)
            fail(e, n, "candidate readback verification failed");
        ++s->phase;
        break;
    case 14:
        if (s->destination_found) {
            meta(s);
            ++s->stats.namespace_operations;
            ok = arc_io_remove(final);
        }
        ++s->phase;
        break;
    case 15:
        meta(s);
        ++s->stats.namespace_operations;
        ok = arc_io_rename(tmp, final);
        ++s->phase;
        break;
    case 16:
        ok = read_record(s, final, s->inspected, &invalid) && !invalid &&
             !memcmp(s->candidate, s->inspected, 512);
        if (ok) {
            ArcRpcCmosEvidence evidence;
            ok = decode(s, s->candidate, 512, &evidence);
            if (ok) {
                ++s->generation;
                s->present = true;
                s->publication_pending = s->putting = false;
                memcpy(s->payload, s->candidate + 104, 240);
                s->evidence = evidence;
                s->phase = 100;
                *done = true;
            }
        } else
            fail(e, n, "published-slot verification failed");
        break;
    default:
        ok = false;
        break;
    }
    if (!ok) {
        s->reconciled = false;
        s->phase = 1;
        s->slot = s->attempts = 0;
        s->putting = false;
        memset(s->slot_found, 0, sizeof(s->slot_found));
        if (!e || !n || !*e)
            fail(e, n, "publication I/O unavailable; reconcile before retry");
    }
    end_count(s);
    return ok;
}
bool arc_riscpc_cmos_store_put(ArcRpcCmosStore *s, const uint8_t payload[240],
                               const ArcRpcCmosEvidence *e, char *error,
                               size_t n) {
    if (!s->reconciled && !s->exhausted) {
        ArcRpcCmosResult r;
        do {
            r = arc_riscpc_cmos_store_step(s, error, n);
        } while (r == ARC_RPC_CMOS_INSPECTING);
    }
    if (!arc_riscpc_cmos_store_put_begin(s, payload, e, error, n))
        return false;
    bool done = false;
    while (!done)
        if (!arc_riscpc_cmos_store_put_step(s, &done, error, n))
            return false;
    return true;
}
bool arc_riscpc_cmos_store_seed(ArcRpcCmosStore *s, const uint8_t payload[240],
                                char *e, size_t n) {
    if (!s->reconciled || s->present || s->refused || s->publication_pending)
        return fail(e, n, "seed requires verified record absence");
    return arc_riscpc_cmos_store_put(s, payload, NULL, e, n);
}
