/* ArcDroid RiscPC sparse hard-disc store, D-139/D-145/D-147. GPL-2.0-only.
 * Format and publication ordering: docs/riscpc-hd-store.md. */
#include "rpc_hd_store.h"
#include "arc_io.h"
#include "install_store.h"
#include "install_disc.h"
#include "arcdroid_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#ifdef ARCDROID_HD_TEST_ALLOC
extern void *arc_riscpc_hd_test_calloc(size_t count,size_t size);
#define store_allocate arc_riscpc_hd_test_calloc
#else
#define store_allocate calloc
#endif
#ifdef ARCDROID_HD_TEST_POINTS
extern void arc_riscpc_hd_test_point(unsigned boundary);
#define commit_point(n) arc_riscpc_hd_test_point(n)
#else
#define commit_point(n) ((void)0)
#endif

#define MANIFEST_V1_BYTES 24324u
#define MANIFEST_BYTES 24452u
#define RECEIPT_AT 24320u
#define CHUNK_BYTES (ARC_RPC_HD_CHUNK + 132u)
#define RECORD_MAX 131168u
/* Unknown envelopes share the existing scratch record; no new allocation. */
#define ENVELOPE_MAX RECORD_MAX
#define ENVELOPE_MIN 60u
#define RECORDS_MAX ((ARC_RPC_HD_JOURNAL_CAP - 512u) / 608u)
#define SLOT_BIT UINT32_C(0x80000000)
#define OFFSET_MASK UINT32_C(0x7fffffff)
#define FILES_MAX 1008u
#define CACHE_COUNT 5u
_Static_assert(MANIFEST_BYTES + 1023u + 64u + 32u <= ARC_RPC_HD_IDENTITY_HASH_BYTES,
               "RPC identity acquisition hash bound");
_Static_assert(2u * sizeof(ArcSha256) + 96u + 256u <= ARC_RPC_HD_IDENTITY_WORKSPACE,
               "RPC identity local hash workspace bound");

typedef struct { uint32_t offset, lba, count, slot; } Record;
typedef struct {
    unsigned index;
    uint64_t age, generation, sequence;
    bool valid;
} CacheTag;
struct ArcRpcHdStore {
    uint32_t live[ARC_RPC_HD_SECTORS], frozen[ARC_RPC_HD_SECTORS];
    uint8_t cache[CACHE_COUNT][ARC_RPC_HD_CHUNK], scratch[ARC_RPC_HD_CHUNK], checkpoint[ARC_RPC_HD_CHUNK], record[RECORD_MAX];
    uint8_t manifests[3][MANIFEST_BYTES], headers[2][256];
    Record records[RECORDS_MAX]; unsigned records_count;
    char chunks[FILES_MAX][23]; unsigned chunk_count, gc_at, advisory_count;
    CacheTag tags[CACHE_COUNT]; uint64_t clock;
    ArcSha256 digest[2]; uint32_t lengths[2];
    uint64_t sequence, snapshot, generation, jgeneration[2];
    unsigned selected, current, parent, next_chunk, phase_step, seal_at;
    ArcRpcHdOutcome outcome;
    unsigned open_phase, open_at, replay_phase, replay_slot, replay_mode;
    uint32_t replay_at; uint64_t replay_last[2];
    size_t path_cursor;
    struct retro_vfs_dir_handle *listing;
    ArcIoFile *replay_file;
    bool partial[2], abandon, open_validated, identity_recovered;
    unsigned population_phase, population_lba, population_count;
    uint8_t *population_output; bool population_write, population_io;
    unsigned publish_step, seal_step;
    unsigned reconstruction_at, reconstruction_slot;
    bool reconstruction_active;
    uint8_t reconstruction_record[RECORD_MAX];
    ArcIoFile *rotation_writer;
    bool present[2], journal_present[2], torn[2], dirty[ARC_RPC_HD_CHUNKS];
    bool population, sealed, published, refused, damaged, requested, authored, gc_done, foreign_entry, stat_failed,
        saving_notified, damage_notified;
    bool population_changed[ARC_RPC_HD_CHUNKS];
    ArcRpcHdState state;
    char base[1024], content[41], io_leaf[23], diagnostic[192];
    const char *notice, *io_operation; bool diagnostic_pending,notice_terminal;
    ArcRpcHdStats stats;
};
/* Store buffers, index and metadata are one allocation; VFS handles and the
 * formatter's bounded stack buffers fit the contract's remaining allowance. */
_Static_assert(sizeof(ArcRpcHdStore) + 12288u <= 4889964u, "RPC store memory bound");
static void step_start(ArcRpcHdStore *,ArcRpcHdStats *);
static void step_finish(ArcRpcHdStore *,const ArcRpcHdStats *);
static bool view_ready(const ArcRpcHdStore *s)
{ return s && (s->outcome==ARC_RPC_HD_LEGACY||s->outcome==ARC_RPC_HD_RECEIPTED||
    (s->outcome==ARC_RPC_HD_UNAVAILABLE&&s->open_validated)); }
static uint64_t get(const uint8_t *p, unsigned n)
{ uint64_t v=0;for(unsigned i=0;i<n;++i)v|=(uint64_t)p[i]<<(i*8u);return v; }
static void put(uint8_t *p, uint64_t v, unsigned n)
{ for(unsigned i=0;i<n;++i)p[i]=(uint8_t)(v>>(i*8u)); }
static bool zero(const uint8_t *p,size_t n)
{
    /* Portable unaligned loads: creation checks all 126 MiB, and a checkpoint
     * checks a whole chunk. Keep those fixed scans at word granularity. */
    while(n>=sizeof(uint64_t)){
        uint64_t word;memcpy(&word,p,sizeof(word));
        if(word)return false;
        p+=sizeof(word);n-=sizeof(word);
    }
    for(size_t i=0;i<n;++i)if(p[i])return false;
    return true;
}
static void sha(ArcRpcHdStore *s,const void *p,size_t n,uint8_t out[32])
{ s->stats.hash_bytes+=n;ArcSha256 h;arc_sha256_init(&h);arc_sha256_update(&h,p,n);arc_sha256_final(&h,out); }
static uint32_t crc(ArcRpcHdStore *s,const void *p,size_t n)
{ s->stats.hash_bytes+=n;return arc_crc32(p,n); }
static void copy(ArcRpcHdStore *s,void *dst,const void *src,size_t n)
{ s->stats.copied_bytes+=n;memcpy(dst,src,n); }
static void stream_hash(ArcRpcHdStore *s,unsigned slot,const void *p,size_t n)
{ s->stats.hash_bytes+=n;arc_sha256_update(&s->digest[slot],p,n); }
static void op(ArcRpcHdStore *s) { ++s->stats.operations; }
static void metadata(ArcRpcHdStore *s) { op(s);++s->stats.metadata_operations; }
static void path(ArcRpcHdStore *s,const char *leaf,char out[1100])
{ snprintf(out,1100,"%s/%s",s->base,leaf); }
static void journal_leaf(unsigned slot,char leaf[16])
{ snprintf(leaf,16,"journal.%c",'a'+slot); }
static void manifest_leaf(unsigned slot,char leaf[16])
{ snprintf(leaf,16,"manifest.%c",'a'+slot); }
static void chunk_leaf(unsigned index,uint64_t generation,char leaf[23])
{ snprintf(leaf,23,"c%03x.g%016" PRIx64,index,generation); }
static bool failure(ArcRpcHdStore *s,bool damage)
{
    static const char *const phases[]={"IDLE","ROTATE","MATERIALIZE","MANIFEST","PUBLISH","RETIRE","GC","READ_ONLY","DAMAGED"};
    unsigned phase=s->state;
    if(s->rotation_writer){ArcIoFile *f=s->rotation_writer;s->rotation_writer=NULL;metadata(s);(void)arc_io_close_file(f);}
    s->refused=true;s->damaged|=damage;damage=s->damaged;s->state=damage?ARC_RPC_HD_DAMAGED:ARC_RPC_HD_READ_ONLY;
    s->notice_terminal=true;
    if(damage&&!s->damage_notified){s->damage_notified=true;
        s->notice="The saved RiscPC hard disc is damaged; files have been preserved.";}
    else if(!damage&&!s->saving_notified){s->saving_notified=true;
        s->notice="Hard-disc changes can no longer be saved this session.";}
    if(!s->diagnostic_pending){
        snprintf(s->diagnostic,sizeof(s->diagnostic),"RiscPC HD phase=%s operation=%s leaf=%s journals=%" PRIu64 "/%" PRIu64 " S=%" PRIu64 " reason=%s",
            phases[phase],s->io_operation?s->io_operation:"validate",s->io_leaf,
            s->jgeneration[s->parent],s->jgeneration[s->current],s->snapshot,damage?"integrity":"I/O or admission");
        s->diagnostic_pending=true;
    }
    return false;
}
static bool size(ArcRpcHdStore *s,const char *leaf,size_t *n)
{
    snprintf(s->io_leaf,sizeof(s->io_leaf),"%s",leaf);s->io_operation="stat";
    char p[1100];path(s,leaf,p);metadata(s);if(arc_io_file_size(p,n))return true;
    /* A single failed stat of an existing canonical leaf must not masquerade
     * as absence. An exact-path read-only open detects that inconsistency.
     * Both callbacks falsely reporting absence remains the documented platform
     * assumption, not something the VFS can prove. */
    metadata(s);uint64_t before=s->stats.operations;ArcIoFile *f=arc_io_open_stream_counted(p,false,&s->stats.operations);
    s->stats.metadata_operations+=s->stats.operations-before;
    if(f){metadata(s);(void)arc_io_close_file(f);s->stat_failed=true;failure(s,false);}
    return false;
}
static bool remove_leaf(ArcRpcHdStore *s,const char *leaf)
{ char p[1100];snprintf(s->io_leaf,sizeof(s->io_leaf),"%s",leaf);s->io_operation="remove";path(s,leaf,p);++s->stats.namespace_operations;metadata(s);return arc_io_remove(p); }
/* The split destination step already proved absence under exclusive ownership. */
static bool rename_empty(ArcRpcHdStore *s,const char *from,const char *to)
{ char a[1100],b[1100];snprintf(s->io_leaf,sizeof(s->io_leaf),"%s",to);s->io_operation="rename";
  path(s,from,a);path(s,to,b);++s->stats.namespace_operations;metadata(s);return arc_io_rename(a,b); }
static bool rename_leaf(ArcRpcHdStore *s,const char *from,const char *to)
{ size_t n;if(size(s,to,&n)||s->refused)return false;return rename_empty(s,from,to); }
static ArcIoFile *open_leaf(ArcRpcHdStore *s,const char *leaf,bool write,bool create)
{ char p[1100];snprintf(s->io_leaf,sizeof(s->io_leaf),"%s",leaf);s->io_operation=create?"create":"open";path(s,leaf,p);if(create)++s->stats.namespace_operations;metadata(s);
  uint64_t before=s->stats.operations;ArcIoFile *f=create?arc_io_create_file_counted(p,&s->stats.operations):arc_io_open_stream_counted(p,write,&s->stats.operations);
  s->stats.metadata_operations+=s->stats.operations-before;return f; }
/* Cleanup must not replace the operation that already failed. */
static bool close_file(ArcRpcHdStore *s,ArcIoFile *f,bool previous_ok)
{ if(previous_ok)s->io_operation="close";metadata(s);return arc_io_close_file(f); }
static bool seek(ArcRpcHdStore *s,ArcIoFile *f,uint32_t at)
{
    s->io_operation="seek";op(s);
    if(arc_io_seek_file(f,at,RETRO_VFS_SEEK_POSITION_START)<0)return false;
    s->io_operation="tell";op(s);return arc_io_tell_file(f)==at;
}
static int64_t length(ArcRpcHdStore *s,ArcIoFile *f)
{ s->io_operation="size";op(s);return arc_io_file_length(f); }
static bool read_file(ArcRpcHdStore *s,ArcIoFile *f,void *p,size_t n)
{ s->io_operation="read";op(s);s->stats.requested_bytes+=n;size_t got=arc_io_read_file(f,p,n);s->stats.bytes_read+=got;return got==n; }
static bool write_file(ArcRpcHdStore *s,ArcIoFile *f,const void *p,size_t n)
{ s->io_operation="write";op(s);s->stats.requested_bytes+=n;size_t got=arc_io_write_file(f,p,n);s->stats.bytes_written+=got;return got==n; }
static bool flush(ArcRpcHdStore *s,ArcIoFile *f)
{ s->io_operation="flush";metadata(s);return arc_io_flush_available() && arc_io_flush_file(f); }
static bool read_leaf(ArcRpcHdStore *s,const char *leaf,void *p,size_t n)
{
    ArcIoFile *f=open_leaf(s,leaf,false,false);if(!f)return false;
    bool ok=length(s,f)==(int64_t)n;
    if(ok)ok=read_file(s,f,p,n);bool closed=close_file(s,f,ok);return ok&&closed;
}
static bool write_leaf(ArcRpcHdStore *s,const char *leaf,const void *p,size_t n)
{
    ArcIoFile *f=open_leaf(s,leaf,true,true);if(!f)return false;
    bool ok=write_file(s,f,p,n)&&flush(s,f);bool closed=close_file(s,f,ok);
    return ok&&closed; /* The following readback step verifies fresh length. */
}
static uint8_t *entry(uint8_t *m,unsigned index) { return m+128u+index*48u; }
static void geometry(uint8_t *p)
{ const uint32_t values[]={512,63,16,256,262144,504};for(unsigned i=0;i<6;++i)put(p+4*i,values[i],4); }
static bool geometry_valid(const uint8_t *p)
{ uint8_t expected[24];geometry(expected);return !memcmp(p,expected,24); }
static size_t manifest_length(const uint8_t *m)
{ return get(m+8,4)==2 ? MANIFEST_BYTES : MANIFEST_V1_BYTES; }
bool arc_riscpc_hd_identity(ArcRpcHdStore *s, ArcRpcHdIdentity *out)
{
    if(!out||!view_ready(s)||!s->published||s->population||s->refused||
       s->damaged||s->identity_recovered||s->state!=ARC_RPC_HD_IDLE||
       !s->journal_present[s->current])return false;
    /* The accepted manifest commits every immutable chunk hash. The stream
     * digest includes the current header and each committed whole command,
     * including writes which have not reached a checkpoint. Never hash a
     * chunk, journal file or live sector index here. */
    uint8_t binding[64]={0},journal[32];ArcSha256 h;
    size_t root=manifest_length(s->manifests[s->selected]),name=strlen(s->base);
    memcpy(binding,"ARCRHDID",8);put(binding+8,ARC_RPC_HD_IDENTITY_REVISION,4);
    put(binding+12,name,4);put(binding+16,root,4);
    put(binding+24,s->generation,8);put(binding+32,s->sequence,8);
    put(binding+40,s->jgeneration[s->current],8);put(binding+48,s->lengths[s->current],8);
    arc_sha256_final(&s->digest[s->current],journal);
    arc_sha256_init(&h);arc_sha256_update(&h,binding,sizeof(binding));
    arc_sha256_update(&h,s->base,name);
    arc_sha256_update(&h,s->manifests[s->selected],root);
    arc_sha256_update(&h,journal,sizeof(journal));arc_sha256_final(&h,out->bytes);
    s->stats.hash_bytes+=sizeof(binding)+name+root+sizeof(journal);
    return true;
}
bool arc_riscpc_hd_identity_matches(ArcRpcHdStore *s,const ArcRpcHdIdentity *expected)
{
    ArcRpcHdIdentity current;
    return expected&&arc_riscpc_hd_identity(s,&current)&&
        !memcmp(expected->bytes,current.bytes,sizeof(current.bytes));
}
bool arc_riscpc_hd_receipt(const ArcRpcHdStore *s,ArcRpcHdReceipt *r)
{
    if(!view_ready(s)||!s->published||get(s->manifests[s->selected]+8,4)!=2)return false;
    const uint8_t *p=s->manifests[s->selected]+RECEIPT_AT;
    if(r){memset(r,0,sizeof(*r));r->receipt_revision=get(p+8,4);r->populator_revision=get(p+12,4);r->authored_revision=get(p+16,4);memcpy(r->packages,p+24,64);r->files=get(p+88,8);r->bytes=get(p+96,8);}
    return true;
}
bool arc_riscpc_hd_population_receipt(ArcRpcHdStore *s,const ArcRpcHdReceipt *r)
{
    if(!s||!r||!s->population||s->published||s->sealed||s->seal_at||s->refused)return false;
    uint8_t *m=s->manifests[2],*p=m+RECEIPT_AT;
    memset(p,0,128);memcpy(p,"ARCRBOOT",8);put(p+8,1,4);put(p+12,1,4);put(p+16,2,4);
    memcpy(p+24,r->packages,64);put(p+88,r->files,8);put(p+96,r->bytes,8);put(m+8,2,4);
    return true;
}
static void manifest_encode(ArcRpcHdStore *s,uint8_t *m,uint64_t generation,uint64_t jgen,uint64_t boundary)
{
    size_t n=manifest_length(m);unsigned version=n==MANIFEST_BYTES?2:1;
    memset(m,0,128);copy(s,m,"ARCRHDM1",8);put(m+8,version,4);put(m+12,n,4);
    copy(s,m+16,s->content,40);put(m+56,1,4);put(m+60,1,4);geometry(m+64);
    put(m+88,generation,8);put(m+96,jgen,8);put(m+104,boundary,8);
    put(m+n-4,crc(s,m,n-4),4);
}
static void header_encode(ArcRpcHdStore *s,uint8_t h[256],uint64_t gen,unsigned role)
{
    memset(h,0,256);copy(s,h,"ARCRHDJ1",8);put(h+8,1,4);put(h+12,256,4);copy(s,h+16,s->content,40);
    put(h+56,1,4);put(h+60,1,4);geometry(h+64);put(h+88,gen,8);put(h+112,s->snapshot,8);put(h+192,role,4);
    if(s->published){put(h+96,s->jgeneration[s->parent],8);put(h+104,s->generation,8);
       sha(s,s->manifests[s->selected],manifest_length(s->manifests[s->selected]),h+120);put(h+152,s->lengths[s->parent],8);
       arc_sha256_final(&s->digest[s->parent],h+160);}
    put(h+252,crc(s,h,252),4);
}
static bool header_valid(ArcRpcHdStore *s,const uint8_t h[256],unsigned slot)
{
    if(memcmp(h,"ARCRHDJ1",8)||get(h+8,4)!=1||get(h+12,4)!=256||memcmp(h+16,s->content,40)||
       get(h+56,4)!=1||get(h+60,4)!=1||!geometry_valid(h+64)||get(h+88,8)%2!=slot||
       get(h+192,4)>2||!zero(h+196,56)||get(h+252,4)!=crc(s,h,252))return false;
    if(get(h+192,4)==2&&get(h+88,8)==0)return zero(h+96,96);
    if(get(h+192,4)==0)return get(h+88,8)==0&&zero(h+96,96);
    return get(h+88,8)==get(h+96,8)+1 && get(h+152,8)>=256&&get(h+152,8)<=ARC_RPC_HD_JOURNAL_CAP;
}
static void index_record(ArcRpcHdStore *s,const Record *r,uint32_t *index)
{ (void)s;for(unsigned i=0;i<r->count;++i)index[r->lba+i]=(r->slot?SLOT_BIT:0)|(r->offset+80+i*512); }
static bool chunk_read(ArcRpcHdStore *s,const uint8_t *m,unsigned index,uint8_t *out,const char *source)
{
    const uint8_t *e=m+128+index*48;if(get(e,4)==0){memset(out,0,ARC_RPC_HD_CHUNK);return true;}
    bool damage=!(m==s->manifests[2]&&(!s->published||get(e+8,8)>s->generation));
    char leaf[23];chunk_leaf(index,get(e+8,8),leaf);ArcIoFile *f=open_leaf(s,source?source:leaf,false,false);if(!f)return failure(s,damage);
    uint8_t h[128],tail[4],digest[32];bool ok=length(s,f)==CHUNK_BYTES&&read_file(s,f,h,128)&&
        !memcmp(h,"ARCRHDC1",8)&&get(h+8,4)==1&&get(h+12,4)==CHUNK_BYTES&&!memcmp(h+16,s->content,40)&&
        get(h+56,4)==index&&get(h+60,4)==1&&get(h+64,4)==1&&get(h+68,4)==ARC_RPC_HD_CHUNK&&
        get(h+72,8)==get(e+8,8)&&!memcmp(h+80,e+16,32)&&zero(h+112,12)&&get(h+124,4)==crc(s,h,124);
    if(ok)ok=read_file(s,f,out,ARC_RPC_HD_CHUNK)&&read_file(s,f,tail,4);
    bool closed=close_file(s,f,ok);if(ok){sha(s,out,ARC_RPC_HD_CHUNK,digest);
        ok=get(tail,4)==crc(s,out,ARC_RPC_HD_CHUNK)&&!memcmp(digest,e+16,32);}
    if(!closed)return failure(s,false);
    return ok?true:failure(s,damage);
}
static bool base_chunk_into(ArcRpcHdStore *s,const uint8_t *m,unsigned index,uint8_t *out)
{ return chunk_read(s,m,index,out,NULL); }
static bool reconstruct_into(ArcRpcHdStore *s,const uint8_t *m,unsigned chunk,const uint32_t *index,uint8_t *out)
{
    if(!base_chunk_into(s,m,chunk,out))return false;
    /* One lazily opened handle per contributing journal. Each slot's record
     * index is in append-offset order; retained slots can both contribute to a
     * live miss, while frozen checkpoint reconstruction uses only its parent.
     * Adjacent records continue from the confirmed cursor without another seek.
     * Every whole frame is validated before copying indexed sectors. */
    unsigned first=chunk*512;
    for(unsigned slot=0;slot<2;++slot){ArcIoFile *f=NULL;uint32_t position=0;
        for(unsigned r=0;r<s->records_count;++r){const Record *rec=&s->records[r];
            if(rec->slot!=slot)continue;
            unsigned lo=rec->lba>first?rec->lba:first,hi=rec->lba+rec->count<first+512?rec->lba+rec->count:first+512;
            if(lo>=hi)continue;bool used=false;
            for(unsigned i=lo;i<hi;++i)if(index[i]==((slot?SLOT_BIT:0)|(rec->offset+80+(i-rec->lba)*512))){used=true;break;}
            if(!used)continue;
            if(!f){char leaf[16];journal_leaf(slot,leaf);f=open_leaf(s,leaf,false,false);
                if(!f)return failure(s,true);}
            unsigned bytes=96+rec->count*512;
            bool ok=(position==rec->offset||seek(s,f,rec->offset))&&read_file(s,f,s->record,bytes);
            position=rec->offset+bytes;
            uint8_t *p=s->record;ok=ok&&!memcmp(p,"RCJR",4)&&get(p+4,4)==1&&!memcmp(p+8,s->content,40)&&
                get(p+48,8)==s->jgeneration[slot]&&get(p+64,4)==rec->lba&&get(p+68,4)==rec->count&&get(p+72,4)==rec->count*512&&
                get(p+76,4)==crc(s,p,76)&&get(p+80+rec->count*512,8)==get(p+56,8)&&
                get(p+88+rec->count*512,4)==crc(s,p+80,rec->count*512)&&!memcmp(p+92+rec->count*512,"RCCM",4);
            if(!ok){close_file(s,f,false);return failure(s,true);}
            for(unsigned i=lo;i<hi;++i)if(index[i]==((slot?SLOT_BIT:0)|(rec->offset+80+(i-rec->lba)*512)))
                copy(s,out+(i-first)*512,p+80+(i-rec->lba)*512,512);
        }
        if(f&&!close_file(s,f,true))return failure(s,true);
    }
    return true;
}

/* Bounded frozen reconstruction. Persistent state is private to preparation /
 * maintenance, so guest reads and appends cannot overwrite its framed record.
 */
static bool reconstruction_step(ArcRpcHdStore *s, const uint8_t *m,
                                unsigned chunk, const uint32_t *index,
                                uint8_t *out, bool *done) {
    *done = false;
    if (!s->reconstruction_active) {
        if (!base_chunk_into(s, m, chunk, out))
            return false;
        s->reconstruction_active = true;
        s->reconstruction_at = s->reconstruction_slot = 0;
        /* Base verification consumes a whole chunk. Overlay work starts in a
         * later step only when there actually are contributing records. */
        bool dirty = false, slot0 = false;
        for (unsigned i = chunk * 512; i < (chunk + 1) * 512; ++i) {
            dirty |= index[i] != 0;
            slot0 |= index[i] && !(index[i] & SLOT_BIT);
        }
        if (dirty && !slot0)
            s->reconstruction_slot = 1;
        if (!dirty) {
            s->reconstruction_active = false;
            *done = true;
            return true;
        }
    }
    unsigned first = chunk * 512, inspected = 0, used_records = 0;
    uint64_t transferred = 0;
    ArcIoFile *f = NULL;
    uint32_t position = 0;
    unsigned slot = s->reconstruction_slot;
    while (s->reconstruction_at < s->records_count &&
           inspected++ < RECORDS_MAX && used_records < 512 &&
           transferred < 526336) {
        const Record *rec = &s->records[s->reconstruction_at];
        if (rec->slot != slot) {
            ++s->reconstruction_at;
            continue;
        }
        unsigned lo = rec->lba > first ? rec->lba : first,
                 hi = rec->lba + rec->count < first + 512
                          ? rec->lba + rec->count
                          : first + 512;
        bool used = false;
        for (unsigned i = lo; i < hi; ++i)
            if (index[i] == ((slot ? SLOT_BIT : 0) |
                             (rec->offset + 80 + (i - rec->lba) * 512))) {
                used = true;
                break;
            }
        if (!used) {
            ++s->reconstruction_at;
            continue;
        }
        unsigned bytes = 96 + rec->count * 512;
        if (transferred + bytes > 526336)
            break;
        if (!f) {
            char leaf[16];
            journal_leaf(slot, leaf);
            f = open_leaf(s, leaf, false, false);
            if (!f)
                return failure(s, false);
        }
        uint8_t *p = s->reconstruction_record;
        bool ok = (position == rec->offset || seek(s, f, rec->offset)) &&
                  read_file(s, f, p, bytes);
        position = rec->offset + bytes;
        if (!ok) {
            close_file(s, f, false);
            return failure(s, false);
        }
        ok = !memcmp(p, "RCJR", 4) && get(p + 4, 4) == 1 &&
             !memcmp(p + 8, s->content, 40) &&
             get(p + 48, 8) == s->jgeneration[slot] &&
             get(p + 64, 4) == rec->lba && get(p + 68, 4) == rec->count &&
             get(p + 72, 4) == rec->count * 512 &&
             get(p + 76, 4) == crc(s, p, 76) &&
             get(p + 80 + rec->count * 512, 8) == get(p + 56, 8) &&
             get(p + 88 + rec->count * 512, 4) ==
                 crc(s, p + 80, rec->count * 512) &&
             !memcmp(p + 92 + rec->count * 512, "RCCM", 4);
        if (!ok) {
            close_file(s, f, false);
            return failure(s, true);
        }
        for (unsigned i = lo; i < hi; ++i)
            if (index[i] == ((slot ? SLOT_BIT : 0) |
                             (rec->offset + 80 + (i - rec->lba) * 512)))
                copy(s, out + (i - first) * 512, p + 80 + (i - rec->lba) * 512,
                     512);
        ++s->reconstruction_at;
        ++used_records;
        transferred += bytes;
    }
    if (f && !close_file(s, f, true))
        return failure(s, false);
    if (s->reconstruction_at == s->records_count) {
        /* Frozen indexes name only the parent. Avoid scanning the other slot
         * when it cannot contribute. */
        bool another = false;
        if (slot == 0)
            for (unsigned i = first; i < first + 512; ++i)
                another |= (index[i] & SLOT_BIT) != 0;
        if (another) {
            s->reconstruction_slot = 1;
            s->reconstruction_at = 0;
        } else {
            s->reconstruction_active = false;
            *done = true;
        }
    }
    return true;
}
static bool base_chunk(ArcRpcHdStore *s,const uint8_t *m,unsigned index)
{ return base_chunk_into(s,m,index,s->scratch); }
static bool reconstruct(ArcRpcHdStore *s,const uint8_t *m,unsigned chunk,const uint32_t *index)
{ return reconstruct_into(s,m,chunk,index,s->scratch); }
static bool track_chunk(ArcRpcHdStore *s,const char *leaf)
{
    for(unsigned i=0;i<s->chunk_count;++i)if(!strcmp(s->chunks[i],leaf))return true;
    if(s->chunk_count==FILES_MAX)return false;
    strcpy(s->chunks[s->chunk_count++],leaf);if(s->chunk_count>s->stats.max_chunk_files)s->stats.max_chunk_files=s->chunk_count;
    return true;
}
static void untrack_chunk(ArcRpcHdStore *s,const char *leaf);
static bool parent_binding(ArcRpcHdStore *s,unsigned child,unsigned parent,unsigned manifest)
{
    uint8_t digest[32];const uint8_t *h=s->headers[child];
    if(get(h+96,8)!=s->jgeneration[parent]||get(h+104,8)!=get(s->manifests[manifest]+88,8)||
       get(h+152,8)!=s->lengths[parent])return false;
    arc_sha256_final(&s->digest[parent],digest);if(memcmp(digest,h+160,32))return false;
    sha(s,s->manifests[manifest],manifest_length(s->manifests[manifest]),digest);return !memcmp(digest,h+120,32);
}
static bool referenced(ArcRpcHdStore *s,const char *leaf)
{
    for(unsigned slot=0;slot<3;++slot){if(slot<2&&!s->present[slot])continue;
        if(slot==2&&!(s->population||(s->state>=ARC_RPC_HD_MATERIALIZE&&s->state<=ARC_RPC_HD_PUBLISH)))continue;
        for(unsigned i=0;i<ARC_RPC_HD_CHUNKS;++i){const uint8_t *e=entry(s->manifests[slot],i);
            if(get(e,4)!=1)continue;char name[23];chunk_leaf(i,get(e+8,8),name);if(!strcmp(name,leaf))return true;}}
    return false;
}
static void untrack_chunk(ArcRpcHdStore *s,const char *leaf)
{
    for(unsigned i=0;i<s->chunk_count;++i)if(!strcmp(s->chunks[i],leaf)){
        size_t bytes=(s->chunk_count-i-1)*23;s->stats.copied_bytes+=bytes;
        memmove(s->chunks[i],s->chunks[i+1],bytes);--s->chunk_count;return;}
}
static bool gc(ArcRpcHdStore *s)
{
    unsigned removed=0,inspected=0,old=s->selected^1;size_t n;
    bool retiring=s->published&&s->parent!=s->current&&s->journal_present[s->parent];
    if(retiring){
        /* D-147: the old manifest is an inventory, not a selectable root.
         * Its removed chunks are expected. Never discover garbage by listing. */
        while(s->gc_at<ARC_RPC_HD_CHUNKS&&removed<1){unsigned i=s->gc_at++;
            if(!s->present[old])break;
            const uint8_t *e=entry(s->manifests[old],i),*live=entry(s->manifests[s->selected],i);
            if(get(e,4)!=1||(get(live,4)==1&&get(live+8,8)==get(e+8,8)))continue;
            char leaf[23];chunk_leaf(i,get(e+8,8),leaf);
            if(size(s,leaf,&n)&&!remove_leaf(s,leaf))return failure(s,false);
            if(s->refused)return false;
            untrack_chunk(s,leaf);++removed;
        }
        if(!s->present[old]||s->gc_at==ARC_RPC_HD_CHUNKS){s->gc_at=0;s->gc_done=true;s->state=ARC_RPC_HD_RETIRE;s->phase_step=0;}
    }else{
        /* Abort debris is the bounded fixed-path inventory assembled at open
         * or by successful private publication, never advisory directory data. */
        while(s->gc_at<s->chunk_count&&removed<1&&inspected++<16){const char *leaf=s->chunks[s->gc_at];
            if(referenced(s,leaf)){++s->gc_at;continue;}
            char owned[23];copy(s,owned,leaf,23);
            if(size(s,owned,&n)&&!remove_leaf(s,owned))return failure(s,false);
            if(s->refused)return false;
            untrack_chunk(s,owned);++removed;
        }
        if(s->gc_at==s->chunk_count){s->gc_at=0;s->state=ARC_RPC_HD_IDLE;}
    }
    return true;
}
static bool read_header(ArcRpcHdStore *s,unsigned slot,uint8_t h[256])
{
    char leaf[16];journal_leaf(slot,leaf);ArcIoFile *f=open_leaf(s,leaf,false,false);if(!f)return false;
    bool ok=read_file(s,f,h,256),closed=close_file(s,f,ok);return ok&&closed;
}
static bool retire_step(ArcRpcHdStore *s)
{
    char leaf[16];uint8_t *check=s->record,h[256],digest[32];size_t n;
    switch(s->phase_step){
    case 0:
        manifest_leaf(s->selected,leaf);
        if(!read_leaf(s,leaf,check,manifest_length(s->manifests[s->selected]))||memcmp(check,s->manifests[s->selected],manifest_length(s->manifests[s->selected])))return false;
        ++s->phase_step;return true;
    case 1:
        if(!read_header(s,s->current,h)||memcmp(h,s->headers[s->current],256))return false;
        ++s->phase_step;return true;
    case 2:
        journal_leaf(s->current,leaf);
        if(!size(s,leaf,&n)||n!=s->lengths[s->current])return false;
        if(s->parent!=s->current&&s->journal_present[s->parent]){
            const uint8_t *bound=s->headers[s->current];arc_sha256_final(&s->digest[s->parent],digest);
            if(get(bound+96,8)!=s->jgeneration[s->parent]||get(bound+152,8)!=s->lengths[s->parent]||memcmp(bound+160,digest,32))return failure(s,true);
            if(!s->gc_done){s->state=ARC_RPC_HD_GC;s->gc_at=0;s->phase_step=0;return true;}
            ++s->phase_step;return true;
        }
        break;
    case 3:{
        unsigned old=s->selected^1;
        if(s->present[old]){manifest_leaf(old,leaf);if(!remove_leaf(s,leaf))return false;s->present[old]=false;}
        ++s->phase_step;return true;}
    case 4:
        journal_leaf(s->parent,leaf);if(!remove_leaf(s,leaf))return false;
        s->journal_present[s->parent]=false;s->lengths[s->parent]=0;
        unsigned keep=0;for(unsigned r=0;r<s->records_count;++r)if(s->records[r].slot==s->current)s->records[keep++]=s->records[r];s->records_count=keep;
        break;
    default:return false;
    }
    s->state=ARC_RPC_HD_IDLE;s->phase_step=0;s->gc_at=0;s->gc_done=false;return true;
}
static void materialized(ArcRpcHdStore *s)
{
    ++s->stats.materialized;++s->next_chunk;s->phase_step=0;
    while(s->next_chunk<ARC_RPC_HD_CHUNKS&&!s->dirty[s->next_chunk])++s->next_chunk;
    if(s->next_chunk==ARC_RPC_HD_CHUNKS)s->state=ARC_RPC_HD_MANIFEST;
}
static bool materialize_step(ArcRpcHdStore *s)
{
    while(s->next_chunk<ARC_RPC_HD_CHUNKS&&!s->dirty[s->next_chunk])++s->next_chunk;
    if(s->next_chunk==ARC_RPC_HD_CHUNKS){s->state=ARC_RPC_HD_MANIFEST;s->phase_step=0;return true;}
    unsigned i=s->next_chunk;uint8_t *e=entry(s->manifests[2],i);char leaf[23];size_t n;
    chunk_leaf(i,s->generation+1,leaf);
    switch(s->phase_step){
    case 0:
        {bool done;if(!reconstruction_step(s,s->manifests[s->selected],i,s->frozen,s->checkpoint,&done))return false;
        if(done)++s->phase_step;return true;}
    case 1:{
        if(zero(s->checkpoint,ARC_RPC_HD_CHUNK)){
            if(size(s,leaf,&n)){if(!remove_leaf(s,leaf))return false;untrack_chunk(s,leaf);}
            if(s->refused)return false;memset(e,0,48);materialized(s);return true;
        }
        uint8_t h[128]={0},tail[4],digest[32];sha(s,s->checkpoint,ARC_RPC_HD_CHUNK,digest);
        copy(s,h,"ARCRHDC1",8);put(h+8,1,4);put(h+12,CHUNK_BYTES,4);copy(s,h+16,s->content,40);
        put(h+56,i,4);put(h+60,1,4);put(h+64,1,4);put(h+68,ARC_RPC_HD_CHUNK,4);put(h+72,s->generation+1,8);
        copy(s,h+80,digest,32);put(h+124,crc(s,h,124),4);put(tail,crc(s,s->checkpoint,ARC_RPC_HD_CHUNK),4);
        ArcIoFile *f=open_leaf(s,"chunk.tmp",true,true);if(!f)return false;
        bool ok=write_file(s,f,h,128)&&write_file(s,f,s->checkpoint,ARC_RPC_HD_CHUNK)&&write_file(s,f,tail,4)&&flush(s,f);
        bool closed=close_file(s,f,ok);if(!ok||!closed)return false;
        memset(e,0,48);put(e,1,4);put(e+8,s->generation+1,8);copy(s,e+16,digest,32);
        ++s->phase_step;return true;}
    case 2:
        if(!chunk_read(s,s->manifests[2],i,s->checkpoint,"chunk.tmp"))return false;
        ++s->phase_step;return true;
    case 3:
        if(size(s,leaf,&n)){
            for(unsigned slot=0;slot<2;++slot)if(s->present[slot]&&get(entry(s->manifests[slot],i),4)==1&&get(entry(s->manifests[slot],i)+8,8)==s->generation+1)return false;
            if(!remove_leaf(s,leaf))return false;untrack_chunk(s,leaf);
        }
        if(s->refused)return false;++s->phase_step;return true;
    case 4:
        if(!rename_empty(s,"chunk.tmp",leaf)||!track_chunk(s,leaf)||!base_chunk_into(s,s->manifests[2],i,s->checkpoint))return false;
        materialized(s);return true;
    default:return false;
    }
}
static bool rotate_step(ArcRpcHdStore *s)
{
    unsigned next=s->current^1;char leaf[16];journal_leaf(next,leaf);
    if(s->phase_step==0){
        size_t n;if(size(s,leaf,&n)||s->refused)return false;
        if(s->jgeneration[s->current]==UINT64_MAX||s->generation==UINT64_MAX||
            (uint64_t)s->lengths[0]+s->lengths[1]+256>ARC_RPC_HD_JOURNAL_CAP)return false;
        /* Empty file only: S and the parent remain live throughout preparation. */
        s->rotation_writer=open_leaf(s,leaf,true,true);if(!s->rotation_writer)return false;
        s->phase_step=1;return true;
    }
    if(s->phase_step!=1||!s->rotation_writer||
        (uint64_t)s->lengths[0]+s->lengths[1]+256>ARC_RPC_HD_JOURNAL_CAP)return false;
    s->parent=s->current;s->snapshot=s->sequence;copy(s,s->frozen,s->live,sizeof(s->live));
    copy(s,s->manifests[2],s->manifests[s->selected],MANIFEST_BYTES);s->next_chunk=0;
    memset(s->dirty,0,sizeof(s->dirty));for(unsigned i=0;i<ARC_RPC_HD_SECTORS;++i)if(s->frozen[i])s->dirty[i/512]=true;
    uint8_t h[256],check[256];header_encode(s,h,s->jgeneration[s->parent]+1,1);
    ArcIoFile *f=s->rotation_writer;s->rotation_writer=NULL;
    bool ok=write_file(s,f,h,256)&&flush(s,f);bool closed=close_file(s,f,ok);
    if(!ok||!closed||!read_leaf(s,leaf,check,256)||memcmp(h,check,256))return false;
    copy(s,s->headers[next],h,256);s->lengths[next]=256;s->jgeneration[next]=s->jgeneration[s->parent]+1;s->journal_present[next]=true;
    arc_sha256_init(&s->digest[next]);stream_hash(s,next,h,256);
    s->current=next;s->state=ARC_RPC_HD_MATERIALIZE;s->phase_step=0;return true;
}
void arc_riscpc_hd_tick(ArcRpcHdStore *s)
{
    if(!s)return;
    s->stats.tick_bytes=s->stats.tick_operations=s->stats.tick_hash_bytes=s->stats.tick_copied_bytes=s->stats.tick_metadata_operations=s->stats.tick_namespace_operations=0;
    if(s->outcome==ARC_RPC_HD_OPEN_PENDING||s->population||s->refused)return;
    uint64_t bytes=s->stats.requested_bytes,ops=s->stats.operations,
        hashes=s->stats.hash_bytes,copies=s->stats.copied_bytes,meta=s->stats.metadata_operations,namespace=s->stats.namespace_operations;
    bool ok=true;switch(s->state){
    case ARC_RPC_HD_IDLE:if(s->requested){s->requested=false;s->state=ARC_RPC_HD_ROTATE;s->phase_step=0;}break;
    case ARC_RPC_HD_ROTATE:ok=rotate_step(s);break;
    case ARC_RPC_HD_MATERIALIZE:ok=materialize_step(s);break;
    case ARC_RPC_HD_MANIFEST:
        if(s->phase_step==0){manifest_encode(s,s->manifests[2],s->generation+1,s->jgeneration[s->current],s->snapshot);
            /* Fresh readback in the next step establishes independent length. */
            ArcIoFile *f=open_leaf(s,"manifest.tmp",true,true);if(!f){ok=false;break;}
            ok=write_file(s,f,s->manifests[2],manifest_length(s->manifests[2]))&&flush(s,f);bool closed=close_file(s,f,ok);ok&=closed;
            if(ok)s->phase_step=1;
        }else{ok=read_leaf(s,"manifest.tmp",s->record,manifest_length(s->manifests[2]))&&!memcmp(s->record,s->manifests[2],manifest_length(s->manifests[2]));
            if(ok){s->state=ARC_RPC_HD_PUBLISH;s->phase_step=0;}}
        break;
    case ARC_RPC_HD_PUBLISH:
        if(s->phase_step==0){char leaf[16];manifest_leaf((unsigned)(get(s->manifests[2]+88,8)&1),leaf);
            ok=rename_leaf(s,"manifest.tmp",leaf);if(ok)s->phase_step=1;
        }else{
            unsigned slot=(unsigned)(get(s->manifests[2]+88,8)&1);char leaf[16];manifest_leaf(slot,leaf);
            ok=read_leaf(s,leaf,s->record,manifest_length(s->manifests[2]))&&!memcmp(s->record,s->manifests[2],manifest_length(s->manifests[2]));
            if(ok){copy(s,s->manifests[slot],s->record,MANIFEST_BYTES);s->present[slot]=true;s->selected=slot;
                s->generation=get(s->record+88,8);s->published=true;s->gc_done=false;
                for(unsigned i=0;i<ARC_RPC_HD_SECTORS;++i)if(s->live[i]&&((s->live[i]&SLOT_BIT)!=0)==(s->parent!=0))s->live[i]=0;
                memset(s->tags,0,sizeof(s->tags));s->state=ARC_RPC_HD_RETIRE;s->phase_step=0;++s->stats.checkpoints;}}
        break;
    case ARC_RPC_HD_RETIRE:ok=retire_step(s);break;
    case ARC_RPC_HD_GC:ok=gc(s);break;
    default:break;
    }
    if(!ok&&!s->refused)failure(s,false);
    if(s->state==ARC_RPC_HD_IDLE&&s->lengths[s->current]>=ARC_RPC_HD_TRIGGER)s->requested=true;
    s->stats.tick_bytes=s->stats.requested_bytes-bytes;s->stats.tick_operations=s->stats.operations-ops;
    s->stats.tick_hash_bytes=s->stats.hash_bytes-hashes;s->stats.tick_copied_bytes=s->stats.copied_bytes-copies;
    s->stats.tick_metadata_operations=s->stats.metadata_operations-meta;
    s->stats.tick_namespace_operations=s->stats.namespace_operations-namespace;
    if(s->stats.tick_bytes>s->stats.max_tick_bytes)s->stats.max_tick_bytes=s->stats.tick_bytes;
    if(s->stats.tick_operations>s->stats.max_tick_operations)s->stats.max_tick_operations=s->stats.tick_operations;
    if(s->stats.tick_hash_bytes>s->stats.max_tick_hash_bytes)s->stats.max_tick_hash_bytes=s->stats.tick_hash_bytes;
    if(s->stats.tick_copied_bytes>s->stats.max_tick_copied_bytes)s->stats.max_tick_copied_bytes=s->stats.tick_copied_bytes;
    if(s->stats.tick_metadata_operations>s->stats.max_tick_metadata_operations)s->stats.max_tick_metadata_operations=s->stats.tick_metadata_operations;
    if(s->stats.tick_namespace_operations>s->stats.max_tick_namespace_operations)s->stats.max_tick_namespace_operations=s->stats.tick_namespace_operations;
}
static bool range(uint32_t lba,uint32_t count)
{ return count&&count<=256&&lba<ARC_RPC_HD_SECTORS&&count<=ARC_RPC_HD_SECTORS-lba; }
bool arc_riscpc_hd_read(ArcRpcHdStore *s,uint32_t lba,uint32_t count,uint8_t *out)
{
    if(!view_ready(s)||!out||!s->published||s->population||s->damaged||!range(lba,count))return false;
    while(count){unsigned chunk=lba/512,offset=lba%512,take=512-offset;if(take>count)take=count;
        unsigned at=CACHE_COUNT;for(unsigned i=0;i<CACHE_COUNT;++i)if(s->tags[i].valid&&s->tags[i].index==chunk&&
            s->tags[i].generation==s->generation&&s->tags[i].sequence==s->sequence){at=i;break;}
        if(at==CACHE_COUNT){at=0;for(unsigned i=0;i<CACHE_COUNT;++i){if(!s->tags[i].valid){at=i;break;}if(s->tags[i].age<s->tags[at].age)at=i;}
            if(!reconstruct(s,s->manifests[s->selected],chunk,s->live))return false;
            copy(s,s->cache[at],s->scratch,ARC_RPC_HD_CHUNK);s->tags[at].valid=true;s->tags[at].index=chunk;
            s->tags[at].generation=s->generation;s->tags[at].sequence=s->sequence;}
        s->tags[at].age=++s->clock;copy(s,out,s->cache[at]+offset*512,take*512);out+=take*512;lba+=take;count-=take;
    }return true;
}
bool arc_riscpc_hd_write(ArcRpcHdStore *s,uint32_t lba,uint32_t count,const uint8_t *in)
{
    if(!s||s->outcome==ARC_RPC_HD_OPEN_PENDING||!in||!s->published||s->population||s->refused||!range(lba,count))return false;
    s->io_operation="append-admission";journal_leaf(s->current,s->io_leaf);
    uint32_t bytes=96+count*512;uint64_t aggregate=(uint64_t)s->lengths[0]+s->lengths[1];
    if(aggregate+bytes>ARC_RPC_HD_JOURNAL_CAP||s->sequence==UINT64_MAX||s->records_count==RECORDS_MAX)return failure(s,false);
    uint8_t *p=s->record;memset(p,0,80);copy(s,p,"RCJR",4);put(p+4,1,4);copy(s,p+8,s->content,40);
    put(p+48,s->jgeneration[s->current],8);put(p+56,s->sequence+1,8);put(p+64,lba,4);put(p+68,count,4);put(p+72,count*512,4);put(p+76,crc(s,p,76),4);
    copy(s,p+80,in,count*512);put(p+80+count*512,s->sequence+1,8);put(p+88+count*512,crc(s,in,count*512),4);copy(s,p+92+count*512,"RCCM",4);
    char leaf[16];journal_leaf(s->current,leaf);ArcIoFile *f=open_leaf(s,leaf,true,false);if(!f)return failure(s,false);
    uint32_t tail=s->lengths[s->current];bool ok=seek(s,f,tail)&&write_file(s,f,p,80)&&write_file(s,f,p+80,count*512)&&write_file(s,f,p+80+count*512,16)&&flush(s,f);
    bool closed=close_file(s,f,ok);size_t actual;
    if(!ok||!closed||!size(s,leaf,&actual)||actual!=tail+bytes)return failure(s,false);
    commit_point(1); /* Durable record, before resident publication. */
    /* A1: no per-record reopen/readback. Only successful publication advances
     * the index, cache and acknowledgement. */
    stream_hash(s,s->current,p,bytes);s->lengths[s->current]+=bytes;++s->sequence;
    Record *r=&s->records[s->records_count++];*r=(Record){tail,lba,count,s->current};index_record(s,r,s->live);
    commit_point(2); /* Index updated, before cache publication. */
    for(unsigned i=0;i<CACHE_COUNT;++i)if(s->tags[i].valid){unsigned first=s->tags[i].index*512;
        unsigned lo=lba>first?lba:first,hi=lba+count<first+512?lba+count:first+512;
        if(lo<hi)copy(s,s->cache[i]+(lo-first)*512,in+(lo-lba)*512,(hi-lo)*512);
        s->tags[i].sequence=s->sequence;}
    commit_point(3); /* Cache updated, before backend acknowledgement. */
    ++s->stats.commands;aggregate+=bytes;if(aggregate>s->stats.journal_high)s->stats.journal_high=aggregate;
    if(s->state==ARC_RPC_HD_IDLE&&s->lengths[s->current]>=ARC_RPC_HD_TRIGGER)s->requested=true;return true;
}
static bool backend_read(void *p,uint32_t lba,uint32_t count,uint8_t *out) { return arc_riscpc_hd_read(p,lba,count,out); }
static bool backend_write(void *p,uint32_t lba,uint32_t count,const uint8_t *in) { return arc_riscpc_hd_write(p,lba,count,in); }
static void backend_failed(void *p,const char *reason) { (void)reason;failure(p,false); }
void arc_riscpc_hd_backend(ArcRpcHdStore *s,ArcIdeBackend *out)
{ memset(out,0,sizeof(*out));if(view_ready(s)&&s->published&&!s->damaged){out->opaque=s;out->read=backend_read;out->write=backend_write;out->failed=backend_failed;out->sectors=ARC_RPC_HD_SECTORS;out->writable=!s->refused;} }
void arc_riscpc_hd_stats(const ArcRpcHdStore *s,ArcRpcHdStats *out)
{ *out=s->stats;out->phase_step=s->phase_step;out->chunk_files=s->chunk_count;out->sequence=s->sequence;out->generation=s->generation;out->state=s->state;out->published=s->published;out->population=s->population;out->read_only=s->refused; }
const char *arc_riscpc_hd_notice(ArcRpcHdStore *s) { if(!s)return NULL;const char *n=s->notice;s->notice=NULL;return n; }
bool arc_riscpc_hd_notice_is_terminal(const ArcRpcHdStore *s) { return s && s->notice_terminal; }
const char *arc_riscpc_hd_diagnostic(ArcRpcHdStore *s)
{ if(!s||!s->diagnostic_pending)return NULL;s->diagnostic_pending=false;return s->diagnostic; }
static bool directory_visit(const char *leaf,bool regular,void *opaque)
{
    ArcRpcHdStore *s=opaque;if(!regular){s->foreign_entry=true;return false;}
    if(!strcmp(leaf,"manifest.a")||!strcmp(leaf,"manifest.b")||!strcmp(leaf,"manifest.tmp")||
       !strcmp(leaf,"journal.a")||!strcmp(leaf,"journal.b")||!strcmp(leaf,"chunk.tmp"))return true;
    unsigned index;uint64_t generation;char canonical[23];
    if(strlen(leaf)!=22||sscanf(leaf,"c%3x.g%16" SCNx64,&index,&generation)!=2||index>=504){s->foreign_entry=true;return false;}
    chunk_leaf(index,generation,canonical);
    if(strcmp(leaf,canonical)||s->advisory_count==FILES_MAX){s->foreign_entry=true;return false;}
    /* Advisory names share the inventory buffer only until root validation.
     * They never enter reference tracking or resource counters. */
    copy(s,s->chunks[s->advisory_count++],canonical,23);return true;
}
static bool key_valid(const char *id)
{ if(!id||strlen(id)!=40)return false;for(unsigned i=0;i<40;++i)if(!((id[i]>='0'&&id[i]<='9')||(id[i]>='a'&&id[i]<='f')))return false;return true; }

const char *arc_riscpc_hd_outcome_notice(ArcRpcHdOutcome outcome) {
    switch (outcome) {
    case ARC_RPC_HD_UNSUPPORTED:
        return "The saved RiscPC hard disc was created by a newer version of "
               "ArcDroid; files have been preserved.";
    case ARC_RPC_HD_UNAVAILABLE:
        return "The saved RiscPC hard disc is unavailable; files have been "
               "preserved. Check the save folder and try again.";
    case ARC_RPC_HD_CORRUPT:
        return "The saved RiscPC hard disc is damaged; files have been "
               "preserved.";
    case ARC_RPC_HD_LEGACY:
        return "The existing RiscPC hard disc is without a creation receipt; it was "
               "left unchanged.";
    case ARC_RPC_HD_RECEIPTED:
        return "The saved RiscPC hard disc has a verified creation receipt.";
    case ARC_RPC_HD_UNPUBLISHED:
        return "The RiscPC hard disc needs boot-resource population.";
    default:
        return NULL;
    }
}
ArcRpcHdOutcome arc_riscpc_hd_outcome(const ArcRpcHdStore *s) {
    return s ? s->outcome : ARC_RPC_HD_UNAVAILABLE;
}
static bool open_failure(ArcRpcHdStore *s, ArcRpcHdOutcome outcome) {
    if (s->replay_file) {
        ArcIoFile *f = s->replay_file;
        s->replay_file = NULL;
        (void)close_file(s, f, false);
    }
    s->outcome = outcome;
    s->refused = true;
    s->damaged = outcome == ARC_RPC_HD_CORRUPT;
    s->notice = arc_riscpc_hd_outcome_notice(outcome);
    s->notice_terminal = true;
    s->state = s->damaged ? ARC_RPC_HD_DAMAGED : ARC_RPC_HD_READ_ONLY;
    return false;
}
/* Known envelopes authenticate framing, key and CRC before revision refusal.
 * Unknown envelopes are opaque: no entries, bindings or cleanup are
 * interpreted. */
static ArcRpcHdOutcome manifest_outcome(ArcRpcHdStore *s, const uint8_t *m,
                                        size_t n, unsigned slot) {
    if (n < 8)
        return ARC_RPC_HD_CORRUPT;
    if (memcmp(m, "ARCRHDM1", 8))
        return ARC_RPC_HD_UNSUPPORTED;
    if (n < ENVELOPE_MIN || n != get(m + 12, 4) || memcmp(m + 16, s->content, 40) ||
        get(m + n - 4, 4) != crc(s, m, n - 4))
        return ARC_RPC_HD_CORRUPT;
    unsigned revision = (unsigned)get(m + 8, 4);
    if ((revision != 1 && revision != 2) || n != manifest_length(m))
        return ARC_RPC_HD_UNSUPPORTED;
    if (get(m + 56, 4) != 1 || get(m + 60, 4) != 1)
        return ARC_RPC_HD_UNSUPPORTED;
    if (!geometry_valid(m + 64) || !zero(m + 112, 16) ||
        get(m + 88, 8) % 2 != slot)
        return ARC_RPC_HD_CORRUPT;
    /* Validate the entry framing before classifying a future receipt. */
    for (unsigned i = 0; i < 504; ++i) {
        const uint8_t *e = entry((uint8_t *)m, i);
        if (get(e, 4) > 1 || !zero(e + 4, 4) ||
            (get(e, 4) == 0 && !zero(e + 8, 40)) ||
            (get(e, 4) == 1 && get(e + 8, 8) > get(m + 88, 8)))
            return ARC_RPC_HD_CORRUPT;
    }
    if (revision == 2) {
        const uint8_t *r = m + RECEIPT_AT;
        if (memcmp(r, "ARCRBOOT", 8))
            return ARC_RPC_HD_CORRUPT;
        if (get(r + 8, 4) != 1 || get(r + 12, 4) != 1 || (get(r + 16, 4) != 1 && get(r + 16, 4) != 2))
            return ARC_RPC_HD_UNSUPPORTED;
        if (!zero(r + 20, 4) || !zero(r + 104, 24))
            return ARC_RPC_HD_CORRUPT;
    }
    return ARC_RPC_HD_OPEN_PENDING;
}
static bool recovered_read_only(ArcRpcHdStore *s) {
    failure(s, false);
    s->outcome = get(s->manifests[s->selected] + 8, 4) == 2
                     ? ARC_RPC_HD_RECEIPTED : ARC_RPC_HD_LEGACY;
    s->stats.open_bytes = s->stats.bytes_read + s->stats.bytes_written;
    s->stats.open_operations = s->stats.operations;
    s->notice = "An interrupted hard-disc write was recovered; the "
                "hard disc is read-only.";
    s->notice_terminal = false;
    return true;
}
static void step_start(ArcRpcHdStore *s, ArcRpcHdStats *before) {
    *before = s->stats;
    s->stats.tick_bytes = s->stats.tick_operations = s->stats.tick_hash_bytes =
        s->stats.tick_copied_bytes = s->stats.tick_metadata_operations =
            s->stats.tick_namespace_operations = 0;
}
static void step_finish(ArcRpcHdStore *s, const ArcRpcHdStats *before) {
#define COUNT(tick, total, max)                                                \
    s->stats.tick = s->stats.total - before->total;                            \
    if (s->stats.tick > s->stats.max)                                          \
    s->stats.max = s->stats.tick
    COUNT(tick_bytes, requested_bytes, max_tick_bytes);
    COUNT(tick_operations, operations, max_tick_operations);
    COUNT(tick_hash_bytes, hash_bytes, max_tick_hash_bytes);
    COUNT(tick_copied_bytes, copied_bytes, max_tick_copied_bytes);
    COUNT(tick_metadata_operations, metadata_operations,
          max_tick_metadata_operations);
    COUNT(tick_namespace_operations, namespace_operations,
          max_tick_namespace_operations);
#undef COUNT
}
static bool range(uint32_t, uint32_t);
/* Recovery's retained cursor consumes a bounded record batch, independent of
 * journal capacity. A complete invalid record is damage; failed I/O is
 * unavailable. */
static bool replay_step(ArcRpcHdStore *s, unsigned slot, uint32_t *index,
                        bool *done) {
    *done = false;
    char leaf[16];
    journal_leaf(slot, leaf);
    if (!s->replay_file) {
        s->replay_file = open_leaf(s, leaf, false, false);
        if (!s->replay_file)
            return open_failure(s, ARC_RPC_HD_UNAVAILABLE);
        s->replay_at = 256;
        s->replay_last[slot] = get(s->headers[slot] + 112, 8);
        arc_sha256_init(&s->digest[slot]);
        stream_hash(s, slot, s->headers[slot], 256);
        if (s->lengths[slot] != 256 && !seek(s, s->replay_file, 256))
            return open_failure(s, ARC_RPC_HD_UNAVAILABLE);
        return true;
    }
    uint64_t transferred = 0;
    unsigned records = 0;
    while (s->replay_at < s->lengths[slot] && records++ < 32 &&
           transferred < 131072) {
        uint32_t at = s->replay_at, left = s->lengths[slot] - at;
        if (left < 80) {
            s->identity_recovered = true;
            s->torn[slot] = true;
            s->lengths[slot] = at;
            break;
        }
        if (!read_file(s, s->replay_file, s->record, 80))
            return open_failure(s, ARC_RPC_HD_UNAVAILABLE);
        uint8_t *p = s->record;
        uint32_t count = (uint32_t)get(p + 68, 4),
                 lba = (uint32_t)get(p + 64, 4);
        uint64_t sequence = get(p + 56, 8);
        if (memcmp(p, "RCJR", 4) || get(p + 4, 4) != 1 ||
            memcmp(p + 8, s->content, 40) ||
            get(p + 48, 8) != s->jgeneration[slot] ||
            s->replay_last[slot] == UINT64_MAX ||
            sequence != s->replay_last[slot] + 1 || !range(lba, count) ||
            get(p + 72, 4) != count * 512 || get(p + 76, 4) != crc(s, p, 76))
            return open_failure(s, ARC_RPC_HD_CORRUPT);
        uint32_t total = 96 + count * 512;
        if (left < total) {
            s->identity_recovered = true;
            s->torn[slot] = true;
            s->lengths[slot] = at;
            break;
        }
        if (!read_file(s, s->replay_file, p + 80, total - 80))
            return open_failure(s, ARC_RPC_HD_UNAVAILABLE);
        if (get(p + 80 + count * 512, 8) != sequence ||
            get(p + 88 + count * 512, 4) != crc(s, p + 80, count * 512) ||
            memcmp(p + 92 + count * 512, "RCCM", 4) ||
            s->records_count == RECORDS_MAX)
            return open_failure(s, ARC_RPC_HD_CORRUPT);
        Record *r = &s->records[s->records_count++];
        *r = (Record){at, lba, count, slot};
        if (index)
            index_record(s, r, index);
        stream_hash(s, slot, p, total);
        s->replay_last[slot] = sequence;
        s->replay_at += total;
        transferred += total;
    }
    if (s->replay_at >= s->lengths[slot]) {
        ArcIoFile *f = s->replay_file;
        s->replay_file = NULL;
        if (!close_file(s, f, true))
            return open_failure(s, ARC_RPC_HD_UNAVAILABLE);
        *done = true;
    }
    return true;
}
static bool recover_step(ArcRpcHdStore *s) {
    char leaf[23];
    size_t n;
    bool done;
    unsigned other = s->current ^ 1;
    switch (s->open_phase) {
    case 0: {
        uint64_t ops = s->stats.operations;
        if (!arc_io_make_path_step(s->base, &s->path_cursor, &done,
                                   &s->stats.operations,
                                   &s->stats.namespace_operations))
            return open_failure(s, ARC_RPC_HD_UNAVAILABLE);
        s->stats.metadata_operations += s->stats.operations - ops;
        if (done)
            ++s->open_phase;
        return true;
    }
    case 1:
        op(s);
        ++s->stats.advisory_operations;
        s->listing = arc_io_open_directory(s->base);
        ++s->open_phase;
        return true;
    case 2:
        if (s->listing) {
            const char *name;
            bool regular;
            uint64_t before = s->stats.operations;
            int r = arc_io_read_directory(s->listing, &name, &regular,
                                          &s->stats.operations);
            s->stats.advisory_operations += s->stats.operations - before;
            if (r > 0) {
                if (strcmp(name, ".") && strcmp(name, ".."))
                    directory_visit(name, regular, s);
                if (s->foreign_entry)
                    s->open_phase = 3;
                return true;
            }
        }
        ++s->open_phase;
        return true;
    case 3:
        if (s->listing) {
            op(s);
            ++s->stats.advisory_operations;
            (void)arc_io_close_directory(s->listing);
            s->listing = NULL;
        }
        if (s->foreign_entry)
            return open_failure(s, ARC_RPC_HD_CORRUPT);
        ++s->open_phase;
        s->open_at = 0;
        return true;
    case 4:
        manifest_leaf(s->open_at, leaf);
        if (size(s, leaf, &n)) {
            if (n > ENVELOPE_MAX) {
                ArcIoFile *f = open_leaf(s, leaf, false, false);
                if (!f)
                    return open_failure(s, ARC_RPC_HD_UNAVAILABLE);
                uint8_t prefix[16];
                bool ok = read_file(s, f, prefix, sizeof(prefix));
                bool closed = close_file(s, f, ok);
                if (!ok || !closed)
                    return open_failure(s, ARC_RPC_HD_UNAVAILABLE);
                /* Same-magic framing is inspected before treating the remainder as opaque. */
                ArcRpcHdOutcome outcome = !memcmp(prefix,"ARCRHDM1",8) && n != get(prefix+12,4)
                    ? ARC_RPC_HD_CORRUPT : ARC_RPC_HD_UNSUPPORTED;
                return open_failure(s, outcome);
            }
            uint8_t *m = (n == MANIFEST_BYTES || n == MANIFEST_V1_BYTES)
                             ? s->manifests[s->open_at] : s->record;
            if (!read_leaf(s, leaf, m, n))
                return open_failure(s, ARC_RPC_HD_UNAVAILABLE);
            ArcRpcHdOutcome outcome = manifest_outcome(s, m, n, s->open_at);
            if (outcome != ARC_RPC_HD_OPEN_PENDING)
                return open_failure(s, outcome);
            s->present[s->open_at] = true;
        } else if (s->stat_failed)
            return open_failure(s, ARC_RPC_HD_UNAVAILABLE);
        if (++s->open_at == 2) {
            ++s->open_phase;
            s->open_at = 0;
        }
        return true;
    case 5:
        if (s->present[0] && s->present[1] &&
            get(s->manifests[0] + 88, 8) == get(s->manifests[1] + 88, 8))
            return open_failure(s, ARC_RPC_HD_CORRUPT);
        s->published = s->present[0] || s->present[1];
        s->selected = s->present[1] && (!s->present[0] ||
                                        get(s->manifests[1] + 88, 8) >
                                            get(s->manifests[0] + 88, 8))
                          ? 1
                          : 0;
        if (s->published) {
            s->generation = get(s->manifests[s->selected] + 88, 8);
            s->current = (unsigned)(get(s->manifests[s->selected] + 96, 8) & 1);
        }
        ++s->open_phase;
        return true;
    case 6: {
        unsigned i = s->open_at;
        journal_leaf(i, leaf);
        if (size(s, leaf, &n)) {
            if (n < 256) {
                if(n){
                    if(!read_leaf(s,leaf,s->headers[i],n))return open_failure(s,ARC_RPC_HD_UNAVAILABLE);
                    uint8_t *h=s->headers[i];
                    if(memcmp(h,"ARCRHDJ1",n<8?n:8))return open_failure(s,ARC_RPC_HD_UNSUPPORTED);
                    size_t header_bytes=n>=16?(size_t)get(h+12,4):0;
                    if(n>=ENVELOPE_MIN && header_bytes>=ENVELOPE_MIN && header_bytes<=n){
                        if(memcmp(h+16,s->content,40)||get(h+header_bytes-4,4)!=crc(s,h,header_bytes-4))
                            return open_failure(s,ARC_RPC_HD_CORRUPT);
                        return open_failure(s,ARC_RPC_HD_UNSUPPORTED);
                    }
                    if(n>=16&&header_bytes!=256)return open_failure(s,ARC_RPC_HD_CORRUPT);
                    if(n>=56&&memcmp(h+16,s->content,40))return open_failure(s,ARC_RPC_HD_CORRUPT);
                    if(n>=12&&get(h+8,4)!=1)return open_failure(s,ARC_RPC_HD_UNSUPPORTED);
                }
                for (unsigned m = 0; m < 2; ++m)
                    if (s->present[m] &&
                        (get(s->manifests[m] + 96, 8) & 1) == i)
                        return open_failure(s, ARC_RPC_HD_CORRUPT);
                s->partial[i] = true;
                s->identity_recovered = true;
            } else {
                s->lengths[i] = (uint32_t)n;
                ArcIoFile *f = open_leaf(s, leaf, false, false);
                if (!f)
                    return open_failure(s, ARC_RPC_HD_UNAVAILABLE);
                uint8_t *h = s->record;
                bool ok = read_file(s, f, h, 256);
                size_t header_bytes = ok ? (size_t)get(h + 12, 4) : 0;
                if (ok && !memcmp(h, "ARCRHDJ1", 8) &&
                    header_bytes >= ENVELOPE_MIN && header_bytes <= ENVELOPE_MAX &&
                    header_bytes <= n && header_bytes > 256)
                    ok = read_file(s, f, h + 256, header_bytes - 256);
                bool closed = close_file(s, f, ok);
                if (!ok || !closed)
                    return open_failure(s, ARC_RPC_HD_UNAVAILABLE);
                if (memcmp(h, "ARCRHDJ1", 8))
                    return open_failure(s, ARC_RPC_HD_UNSUPPORTED);
                if (header_bytes < ENVELOPE_MIN || header_bytes > n ||
                    memcmp(h + 16, s->content, 40))
                    return open_failure(s, ARC_RPC_HD_CORRUPT);
                if (header_bytes > ENVELOPE_MAX)
                    return open_failure(s, ARC_RPC_HD_UNSUPPORTED);
                if (get(h + header_bytes - 4, 4) != crc(s, h, header_bytes - 4))
                    return open_failure(s, ARC_RPC_HD_CORRUPT);
                if (get(h + 8, 4) != 1 || header_bytes != 256 ||
                    get(h + 56, 4) != 1 || get(h + 60, 4) != 1)
                    return open_failure(s, ARC_RPC_HD_UNSUPPORTED);
                copy(s, s->headers[i], h, 256);
                if (n > ARC_RPC_HD_JOURNAL_CAP)
                    return open_failure(s, ARC_RPC_HD_CORRUPT);
                if (!header_valid(s, h, i))
                    return open_failure(s, ARC_RPC_HD_CORRUPT);
                s->jgeneration[i] = get(h + 88, 8);
                s->journal_present[i] = true;
            }
        } else if (s->stat_failed)
            return open_failure(s, ARC_RPC_HD_UNAVAILABLE);
        if (++s->open_at == 2) {
            ++s->open_phase;
            s->open_at = 0;
        }
        return true;
    }
    case 7:
        if ((uint64_t)s->lengths[0] + s->lengths[1] > ARC_RPC_HD_JOURNAL_CAP)
            return open_failure(s, ARC_RPC_HD_CORRUPT);
        for (unsigned m = 0; m < 2; ++m)
            if (s->present[m]) {
                unsigned j = (unsigned)(get(s->manifests[m] + 96, 8) & 1);
                if (!s->journal_present[j] ||
                    get(s->manifests[m] + 96, 8) != s->jgeneration[j] ||
                    get(s->manifests[m] + 104, 8) !=
                        get(s->headers[j] + 112, 8))
                    return open_failure(s, ARC_RPC_HD_CORRUPT);
                uint64_t gen = get(s->manifests[m] + 88, 8),
                         parent = get(s->headers[j] + 104, 8);
                if (s->jgeneration[j] == 0
                        ? gen != 0
                        : parent == UINT64_MAX || gen != parent + 1)
                    return open_failure(s, ARC_RPC_HD_CORRUPT);
            }
        if (s->present[0] && s->present[1] &&
            (!s->journal_present[other] ||
             s->jgeneration[other] >= s->jgeneration[s->current] ||
             get(s->manifests[s->selected ^ 1] + 96, 8) !=
                 s->jgeneration[other]))
            return open_failure(s, ARC_RPC_HD_CORRUPT);
        if (!s->published) {
            for (unsigned i = 0; i < 2; ++i)
                if (s->journal_present[i] &&
                    (s->lengths[i] != 256 || get(s->headers[i] + 88, 8) != 0 ||
                     get(s->headers[i] + 112, 8) != 0))
                    return open_failure(s, ARC_RPC_HD_CORRUPT);
            s->open_phase = 11;
        } else if (s->journal_present[other] &&
                   s->jgeneration[other] > s->jgeneration[s->current]) {
            s->identity_recovered = true;
            s->replay_mode = 1;
            s->replay_slot = s->current;
            s->open_phase = 8;
        } else if (s->journal_present[other]) {
            s->identity_recovered = true;
            s->replay_mode = 2;
            s->replay_slot = other;
            s->open_phase = 8;
        } else {
            s->replay_mode = 0;
            s->replay_slot = s->current;
            s->open_phase = 8;
        }
        return true;
    case 8:
        if (!replay_step(s, s->replay_slot,
                         s->replay_mode == 2 ? NULL : s->live, &done))
            return false;
        if (done)
            ++s->open_phase;
        return true;
    case 9:
        if (s->replay_mode == 1) {
            if (!parent_binding(s, other, s->current, s->selected) ||
                get(s->headers[other] + 112, 8) != s->replay_last[s->current])
                return open_failure(s, ARC_RPC_HD_CORRUPT);
            unsigned role = (unsigned)get(s->headers[other] + 192, 4);
            if (role == 2) {
                if (s->lengths[other] != 256)
                    return open_failure(s, ARC_RPC_HD_CORRUPT);
                s->abandon = true;
                s->sequence = s->replay_last[s->current];
                s->open_phase = 11;
                return true;
            }
            if (role != 1)
                return open_failure(s, ARC_RPC_HD_CORRUPT);
            s->snapshot = s->replay_last[s->current];
            copy(s, s->frozen, s->live, sizeof(s->live));
            copy(s, s->manifests[2], s->manifests[s->selected], MANIFEST_BYTES);
            s->parent = s->current;
            s->current = other;
            for (unsigned i = 0; i < ARC_RPC_HD_SECTORS; ++i)
                if (s->frozen[i])
                    s->dirty[i / 512] = true;
            s->state = ARC_RPC_HD_MATERIALIZE;
        } else if (s->replay_mode == 2) {
            uint8_t digest[32];
            arc_sha256_final(&s->digest[other], digest);
            const uint8_t *h = s->headers[s->current];
            if (get(h + 192, 4) == 0 ||
                get(h + 96, 8) != s->jgeneration[other] ||
                get(h + 152, 8) != s->lengths[other] ||
                get(h + 112, 8) != s->replay_last[other] ||
                memcmp(digest, h + 160, 32))
                return open_failure(s, ARC_RPC_HD_CORRUPT);
            if (s->present[s->selected ^ 1] &&
                !parent_binding(s, s->current, other, s->selected ^ 1))
                return open_failure(s, ARC_RPC_HD_CORRUPT);
            s->parent = other;
            s->state = ARC_RPC_HD_RETIRE;
        } else {
            s->sequence = s->replay_last[s->current];
            s->open_phase = 11;
            return true;
        }
        s->replay_mode = 0;
        s->replay_slot = s->current;
        s->open_phase = 8;
        return true;
    case 11: {
        uint64_t candidate = s->published ? s->generation + 1 : 0;
        if (s->open_at < s->advisory_count) {
            unsigned idx;
            uint64_t gen;
            sscanf(s->chunks[s->open_at], "c%3x.g%16" SCNx64, &idx, &gen);
            if (((s->published && s->generation == UINT64_MAX) ||
                 gen != candidate) &&
                !referenced(s, s->chunks[s->open_at]))
                return open_failure(s, ARC_RPC_HD_CORRUPT);
            ++s->open_at;
            return true;
        }
        s->advisory_count = 0;
        s->open_at = 0;
        ++s->open_phase;
        return true;
    }
    case 12:
        while (s->open_at < 1008) {
            unsigned m = s->open_at / 504, i = s->open_at % 504;
            const uint8_t *e = entry(s->manifests[m], i);
            ++s->open_at;
            if (s->present[m] && get(e, 4) == 1) {
                chunk_leaf(i, get(e + 8, 8), leaf);
                if (size(s, leaf, &n) && !track_chunk(s, leaf))
                    return open_failure(s, ARC_RPC_HD_CORRUPT);
                if (s->stat_failed)
                    return open_failure(s, ARC_RPC_HD_UNAVAILABLE);
                return true;
            }
        }
        s->open_at = 0;
        s->open_validated = true;
        ++s->open_phase;
        return true;
    case 13:
        if (s->open_at == 2) {
            s->open_at = 0;
            s->open_phase = 15;
            return true;
        }
        if (!s->torn[s->open_at]) {
            ++s->open_at;
            return true;
        }
        journal_leaf(s->open_at, leaf);
        s->rotation_writer = open_leaf(s, leaf, true, false);
        if (!s->rotation_writer)
            return recovered_read_only(s);
        s->open_phase = 14;
        return true;
    case 14: {
        ArcIoFile *f = s->rotation_writer;
        s->rotation_writer = NULL;
        op(s);
        bool ok =
            arc_io_truncate_file(f, s->lengths[s->open_at]) && flush(s, f);
        bool closed = close_file(s, f, ok);
        if (!ok || !closed) {
            /* The root and committed prefix already validated. An absent
             * or failing truncate prevents writes, not use of that view. */
            return recovered_read_only(s);
        }
        s->open_phase = 18;
        return true;
    }
    case 18:
        journal_leaf(s->open_at, leaf);
        if (!size(s, leaf, &n) || n != s->lengths[s->open_at])
            return recovered_read_only(s);
        s->notice = "An interrupted hard-disc write was recovered.";
        s->notice_terminal = false;
        ++s->open_at;
        s->open_phase = 13;
        return true;
    case 15:
        if (s->open_at < 2) {
            unsigned i = s->open_at++;
            if (s->partial[i] || (!s->published && s->journal_present[i]) ||
                (s->abandon && i == (s->current ^ 1))) {
                journal_leaf(i, leaf);
                if (!remove_leaf(s, leaf))
                    return open_failure(s, ARC_RPC_HD_UNAVAILABLE);
                s->journal_present[i] = false;
                s->lengths[i] = 0;
            }
            return true;
        }
        s->open_at = 0;
        s->open_phase = 16;
        return true;
    case 16:
        if (s->open_at < 506) {
            if (s->open_at < 504) {
                if (s->published && s->generation == UINT64_MAX) {
                    s->open_at = 504;
                    return true;
                }
                chunk_leaf(s->open_at, s->published ? s->generation + 1 : 0,
                           leaf);
            } else
                strcpy(leaf, s->open_at == 504 ? "chunk.tmp" : "manifest.tmp");
            if (size(s, leaf, &n))
            {
                s->identity_recovered = true;
                s->open_phase = 17;
            }
            else
                ++s->open_at;
            if (s->stat_failed)
                return open_failure(s, ARC_RPC_HD_UNAVAILABLE);
            return true;
        }
        if (s->state == ARC_RPC_HD_IDLE && s->published &&
            s->lengths[s->current] >= ARC_RPC_HD_TRIGGER)
            s->requested = true;
        s->outcome = s->published ? (get(s->manifests[s->selected] + 8, 4) == 2
                                         ? ARC_RPC_HD_RECEIPTED
                                         : ARC_RPC_HD_LEGACY)
                                  : ARC_RPC_HD_UNPUBLISHED;
        s->stats.open_bytes = s->stats.bytes_read + s->stats.bytes_written;
        s->stats.open_operations = s->stats.operations;
        if (!arc_io_flush_available())
            failure(s, false);
        return true;
    case 17:
        if (s->open_at < 504)
            chunk_leaf(s->open_at, s->published ? s->generation + 1 : 0, leaf);
        else
            strcpy(leaf, s->open_at == 504 ? "chunk.tmp" : "manifest.tmp");
        if (!remove_leaf(s, leaf))
            return open_failure(s, ARC_RPC_HD_UNAVAILABLE);
        untrack_chunk(s, leaf);
        ++s->open_at;
        s->open_phase = 16;
        return true;
    default:
        return open_failure(s, ARC_RPC_HD_CORRUPT);
    }
}
ArcRpcHdOutcome arc_riscpc_hd_open_step(ArcRpcHdStore *s) {
    if (!s)
        return ARC_RPC_HD_UNAVAILABLE;
    ArcRpcHdStats before;
    step_start(s, &before);
    if (s->outcome == ARC_RPC_HD_OPEN_PENDING)
        (void)recover_step(s);
    step_finish(s, &before);
    return s->outcome;
}
ArcRpcHdStore *arc_riscpc_hd_open_begin(const char *save, const char *content,
                                        char *error, size_t n) {
    if (error && n)
        *error = 0;
    if (!arc_install_store_native_path(save) || !key_valid(content)) {
        if (error && n)
            snprintf(error, n,
                     "Invalid RiscPC hard-disc path or content identity");
        return NULL;
    }
    ArcRpcHdStore *s = store_allocate(1, sizeof(*s));
    if (!s) {
        if (error && n)
            snprintf(error, n, "RiscPC hard-disc allocation failed");
        return NULL;
    }
    int len = snprintf(s->base, sizeof(s->base), "%s/arcdroid/hd-riscpc/%s",
                       save, content);
    if (len < 0 || (size_t)len >= sizeof(s->base)) {
        free(s);
        if (error && n)
            snprintf(error, n, "RiscPC hard-disc path is too long");
        return NULL;
    }
    copy(s, s->content, content, 41);
    s->stats.allocation_high = sizeof(*s) + 12288;
    return s;
}
ArcRpcHdStore *arc_riscpc_hd_open(const char *save,const char *content,bool create_blank,char *error,size_t error_size)
{
    ArcRpcHdStore *s=arc_riscpc_hd_open_begin(save,content,error,error_size);if(!s)return NULL;
    ArcRpcHdOutcome outcome;do{outcome=arc_riscpc_hd_open_step(s);}while(outcome==ARC_RPC_HD_OPEN_PENDING);
    if(outcome==ARC_RPC_HD_CORRUPT||outcome==ARC_RPC_HD_UNSUPPORTED||(outcome==ARC_RPC_HD_UNAVAILABLE&&!s->open_validated)){
        if(error&&error_size)snprintf(error,error_size,"%s",arc_riscpc_hd_outcome_notice(outcome));arc_riscpc_hd_close(s);return NULL;}
    if(!s->published&&!s->refused){
        if(create_blank&&arc_riscpc_hd_population_begin(s)){s->authored=true;
            bool ok=true;for(unsigned i=0;i<504&&ok;++i){ok=arc_hd_format_range(256,13,(uint64_t)i*ARC_RPC_HD_CHUNK,s->scratch,ARC_RPC_HD_CHUNK);
                if(ok&&!zero(s->scratch,ARC_RPC_HD_CHUNK)){/* write uses the bounded command latch, not a whole-image allocation */
                    copy(s,s->record,s->scratch,RECORD_MAX-96);
                    ok=arc_riscpc_hd_population_write(s,i*512,256,s->record)&&arc_hd_format_range(256,13,(uint64_t)i*ARC_RPC_HD_CHUNK+131072,s->record,131072)&&
                       arc_riscpc_hd_population_write(s,i*512+256,256,s->record);}}
            if(ok)ok=arc_riscpc_hd_population_seal(s)&&arc_riscpc_hd_population_publish(s);
            if(!ok){arc_riscpc_hd_population_abort(s);failure(s,false);}
        }}
    if(s->refused&&error&&error_size)snprintf(error,error_size,"%s",s->notice?s->notice:"RiscPC hard disc is read-only");return s;
}
void arc_riscpc_hd_close(ArcRpcHdStore *s) { if(s){if(s->listing)(void)arc_io_close_directory(s->listing);if(s->replay_file)(void)close_file(s,s->replay_file,true);if(s->population)arc_riscpc_hd_population_abort(s);if(s->rotation_writer){ArcIoFile *f=s->rotation_writer;s->rotation_writer=NULL;(void)close_file(s,f,true);}free(s);} }
bool arc_riscpc_hd_population_begin(ArcRpcHdStore *s)
{
    if(!s||s->outcome==ARC_RPC_HD_OPEN_PENDING||s->refused||s->population||s->state!=ARC_RPC_HD_IDLE||s->requested||s->generation==UINT64_MAX||
       (s->published&&s->jgeneration[s->current]==UINT64_MAX))return false;
    s->population=true;s->sealed=false;s->seal_at=0;s->seal_step=s->publish_step=0;s->reconstruction_active=false;s->snapshot=s->sequence;s->parent=s->current;
    copy(s,s->frozen,s->live,sizeof(s->live));memset(s->population_changed,0,sizeof(s->population_changed));
    if(s->published)copy(s,s->manifests[2],s->manifests[s->selected],MANIFEST_BYTES);else memset(s->manifests[2],0,MANIFEST_BYTES);
    return true;
}
bool arc_riscpc_hd_population_blank_begin(ArcRpcHdStore *s)
{
    if(!arc_riscpc_hd_population_begin(s))return false;
    s->authored=true;return true;
}
bool arc_riscpc_hd_population_io_begin(ArcRpcHdStore *s, uint32_t lba,
                                       uint32_t count, uint8_t *data,
                                       bool write) {
    if (!s || !s->population || s->refused || s->population_io || !data ||
        !range(lba, count) || (write && (s->sealed || s->seal_at)))
        return false;
    s->population_io = true;
    s->population_write = write;
    s->population_lba = lba;
    s->population_count = count;
    s->population_output = data;
    s->population_phase = 0;
    if (write) {
        s->tags[0].valid = false;
        copy(s, s->cache[0], data, count * 512);
    }
    return true;
}
/* One private chunk publication substep; buffers are retained until final
 * readback. The encoded bytes match the original publish_chunk exactly. */
static bool population_publish_chunk_step(ArcRpcHdStore *s, unsigned i,
                                          bool *done) {
    *done = false;
    uint8_t *e = entry(s->manifests[2], i);
    uint64_t gen = s->published ? s->generation + 1 : 0;
    char leaf[23];
    chunk_leaf(i, gen, leaf);
    size_t n;
    switch (s->population_phase) {
    case 1: {
        if (zero(s->scratch, ARC_RPC_HD_CHUNK)) {
            if (size(s, leaf, &n)) {
                if (!remove_leaf(s, leaf))
                    return false;
                untrack_chunk(s, leaf);
            }
            if (s->refused)
                return false;
            memset(e, 0, 48);
            *done = true;
            return true;
        }
        uint8_t h[128] = {0}, tail[4], digest[32];
        sha(s, s->scratch, ARC_RPC_HD_CHUNK, digest);
        memcpy(h, "ARCRHDC1", 8);
        put(h + 8, 1, 4);
        put(h + 12, CHUNK_BYTES, 4);
        memcpy(h + 16, s->content, 40);
        put(h + 56, i, 4);
        put(h + 60, 1, 4);
        put(h + 64, 1, 4);
        put(h + 68, ARC_RPC_HD_CHUNK, 4);
        put(h + 72, gen, 8);
        memcpy(h + 80, digest, 32);
        put(h + 124, crc(s, h, 124), 4);
        put(tail, crc(s, s->scratch, ARC_RPC_HD_CHUNK), 4);
        ArcIoFile *f = open_leaf(s, "chunk.tmp", true, true);
        if (!f)
            return false;
        bool ok = write_file(s, f, h, 128) &&
                  write_file(s, f, s->scratch, ARC_RPC_HD_CHUNK) &&
                  write_file(s, f, tail, 4) && flush(s, f);
        bool closed = close_file(s, f, ok);
        if (!ok || !closed)
            return false;
        memset(e, 0, 48);
        put(e, 1, 4);
        put(e + 8, gen, 8);
        copy(s, e + 16, digest, 32);
        ++s->population_phase;
        return true;
    }
    case 2:
        if (!chunk_read(s, s->manifests[2], i, s->scratch, "chunk.tmp"))
            return false;
        ++s->population_phase;
        return true;
    case 3:
        if (size(s, leaf, &n)) {
            for (unsigned m = 0; m < 2; ++m)
                if (s->present[m] && get(entry(s->manifests[m], i), 4) == 1 &&
                    get(entry(s->manifests[m], i) + 8, 8) == gen)
                    return false;
            if (!remove_leaf(s, leaf))
                return false;
            untrack_chunk(s, leaf);
        }
        if (s->refused)
            return false;
        ++s->population_phase;
        return true;
    case 4:
        if (!rename_empty(s, "chunk.tmp", leaf) || !track_chunk(s, leaf))
            return false;
        ++s->population_phase;
        return true;
    case 5:
        if (!base_chunk(s, s->manifests[2], i))
            return false;
        *done = true;
        return true;
    default:
        return false;
    }
}
bool arc_riscpc_hd_population_io_step(ArcRpcHdStore *s, bool *done) {
    *done = false;
    if (!s || !s->population_io || s->refused)
        return false;
    ArcRpcHdStats before;
    step_start(s, &before);
    unsigned i = s->population_lba / 512, at = s->population_lba % 512;
    unsigned take = 512 - at;
    if (take > s->population_count)
        take = s->population_count;
    bool ok = true, ready = false;
    if (s->population_phase == 0) {
        if (s->population_changed[i]) {
            ok = base_chunk(s, s->manifests[2], i);
            ready = ok;
        } else
            ok = reconstruction_step(
                s, s->published ? s->manifests[s->selected] : s->manifests[2],
                i, s->frozen, s->scratch, &ready);
        if (ok && ready) {
            if (s->population_write) {
                copy(s, s->scratch + at * 512, s->cache[0], take * 512);
                s->population_phase = 1;
                ready = false;
            } else
                copy(s, s->population_output, s->scratch + at * 512,
                     take * 512);
        }
    } else
        ok = population_publish_chunk_step(s, i, &ready);
    if (ok && ready) {
        if (s->population_write)
            s->population_changed[i] = true;
        s->population_lba += take;
        s->population_count -= take;
        s->population_output += take * 512;
        s->population_phase = 0;
        if (s->population_count && s->population_write) {
            s->stats.copied_bytes += s->population_count * 512;
            memmove(s->cache[0], s->cache[0] + take * 512,
                    s->population_count * 512);
        }
        if (!s->population_count) {
            s->population_io = false;
            *done = true;
        }
    }
    if (!ok && !s->refused)
        failure(s, false);
    step_finish(s, &before);
    return ok;
}
bool arc_riscpc_hd_population_read(ArcRpcHdStore *s, uint32_t lba,
                                   uint32_t count, uint8_t *out) {
    if (!arc_riscpc_hd_population_io_begin(s, lba, count, out, false))
        return false;
    bool done = false;
    while (!done)
        if (!arc_riscpc_hd_population_io_step(s, &done))
            return false;
    return true;
}
bool arc_riscpc_hd_population_write(ArcRpcHdStore *s, uint32_t lba,
                                    uint32_t count, const uint8_t *in) {
    if (!arc_riscpc_hd_population_io_begin(s, lba, count, (uint8_t *)in, true))
        return false;
    bool done = false;
    while (!done)
        if (!arc_riscpc_hd_population_io_step(s, &done))
            return false;
    return true;
}

static bool seal_unit(ArcRpcHdStore *s, bool *done) {
    *done = false;
    if (!s || !s->population || s->refused || s->population_io ||
        s->snapshot != s->sequence)
        return false;
    if (s->sealed) {
        *done = true;
        return true;
    }
    if (s->seal_at < 504) {
        unsigned i = s->seal_at;
        bool ready;
        if (!s->population_changed[i]) {
            bool dirty = false;
            for (unsigned j = 0; j < 512; ++j)
                dirty |= s->frozen[i * 512 + j] != 0;
            if (dirty) {
                if (!s->seal_step) {
                    if (!reconstruction_step(s, s->manifests[s->selected], i,
                                             s->frozen, s->scratch, &ready))
                        return false;
                    if (ready) {
                        s->population_phase = 1;
                        s->seal_step = 1;
                    }
                    return true;
                }
                if (s->seal_step == 1) {
                    if (!population_publish_chunk_step(s, i, &ready))
                        return false;
                    if (ready) {
                        s->population_changed[i] = true;
                        s->seal_step = 2;
                    }
                    return true;
                }
            }
        }
        if (!base_chunk(s, s->manifests[2], i))
            return false;
        ++s->seal_at;
        s->seal_step = 0;
        return true;
    }
    unsigned next = s->published ? s->parent ^ 1 : 0;
    char leaf[16];
    journal_leaf(next, leaf);
    if (s->seal_at == 504) {
        if (!s->seal_step) {
            if ((uint64_t)s->lengths[0] + s->lengths[1] + 256 >
                ARC_RPC_HD_JOURNAL_CAP)
                return false;
            header_encode(s, s->headers[next],
                          s->published ? s->jgeneration[s->parent] + 1 : 0,
                          s->published ? 2 : (s->authored ? 0 : 2));
            if (!write_leaf(s, leaf, s->headers[next], 256))
                return false;
            s->seal_step = 1;
            return true;
        }
        if (!read_leaf(s, leaf, s->record, 256) ||
            memcmp(s->record, s->headers[next], 256))
            return false;
        s->lengths[next] = 256;
        s->jgeneration[next] = get(s->headers[next] + 88, 8);
        s->journal_present[next] = true;
        arc_sha256_init(&s->digest[next]);
        stream_hash(s, next, s->headers[next], 256);
        s->current = next;
        ++s->seal_at;
        s->seal_step = 0;
        return true;
    }
    if (!s->seal_step) {
        manifest_encode(s, s->manifests[2],
                        s->published ? s->generation + 1 : 0,
                        s->jgeneration[s->current], s->snapshot);
        if (!write_leaf(s, "manifest.tmp", s->manifests[2],
                        manifest_length(s->manifests[2])))
            return false;
        s->seal_step = 1;
        return true;
    }
    if (!read_leaf(s, "manifest.tmp", s->record,
                   manifest_length(s->manifests[2])) ||
        memcmp(s->record, s->manifests[2], manifest_length(s->manifests[2])))
        return false;
    s->sealed = true;
    s->publish_step = 0;
    *done = true;
    return true;
}
bool arc_riscpc_hd_population_seal_step(ArcRpcHdStore *s, bool *done) {
    if (!s || !done)
        return false;
    ArcRpcHdStats before;
    step_start(s, &before);
    bool ok = seal_unit(s, done);
    if (!ok && !s->refused)
        failure(s, false);
    step_finish(s, &before);
    return ok;
}
bool arc_riscpc_hd_population_seal(ArcRpcHdStore *s)
{
    if(!s||s->sealed)return false;
    bool done=false;while(!done)if(!arc_riscpc_hd_population_seal_step(s,&done))return false;
    return true;
}

static bool population_publish_unit(ArcRpcHdStore *s, bool *done) {
    *done = false;
    if (!s || !s->population || !s->sealed || s->refused ||
        s->snapshot != s->sequence)
        return false;
    char leaf[16];
    size_t n;
    uint8_t digest[32];
    switch (s->publish_step) {
    case 0:
        if (s->published) {
            manifest_leaf(s->selected, leaf);
            if (!read_leaf(s, leaf, s->record,
                           manifest_length(s->manifests[s->selected])))
                return failure(s, false);
            if (memcmp(s->record, s->manifests[s->selected],
                       manifest_length(s->manifests[s->selected])))
                return failure(s, true);
        }
        ++s->publish_step;
        return true;
    case 1:
        if (s->published) {
            journal_leaf(s->parent, leaf);
            if (!read_header(s, s->parent, s->record))
                return failure(s, false);
            if (memcmp(s->record, s->headers[s->parent], 256))
                return failure(s, true);
        }
        ++s->publish_step;
        return true;
    case 2:
        if (s->published) {
            journal_leaf(s->parent, leaf);
            arc_sha256_final(&s->digest[s->parent], digest);
            if (!size(s, leaf, &n))
                return failure(s, false);
            if (n != s->lengths[s->parent] ||
                get(s->headers[s->current] + 96, 8) !=
                    s->jgeneration[s->parent] ||
                get(s->headers[s->current] + 152, 8) != n ||
                memcmp(s->headers[s->current] + 160, digest, 32))
                return failure(s, true);
        }
        ++s->publish_step;
        return true;
    case 3:
        manifest_leaf((unsigned)(get(s->manifests[2] + 88, 8) & 1), leaf);
        if (size(s, leaf, &n) || s->refused)
            return false;
        ++s->publish_step;
        return true;
    case 4:
        manifest_leaf((unsigned)(get(s->manifests[2] + 88, 8) & 1), leaf);
        if (!rename_empty(s, "manifest.tmp", leaf))
            return false;
        ++s->publish_step;
        return true;
    case 5: {
        unsigned slot = (unsigned)(get(s->manifests[2] + 88, 8) & 1);
        manifest_leaf(slot, leaf);
        if (!read_leaf(s, leaf, s->record, manifest_length(s->manifests[2])) ||
            memcmp(s->record, s->manifests[2],
                   manifest_length(s->manifests[2])))
            return false;
        copy(s, s->manifests[slot], s->record, MANIFEST_BYTES);
        s->present[slot] = true;
        s->selected = slot;
        s->generation = get(s->record + 88, 8);
        s->published = true;
        s->gc_done = false;
        memset(s->live, 0, sizeof(s->live));
        memset(s->tags, 0, sizeof(s->tags));
        s->state = ARC_RPC_HD_RETIRE;
        s->phase_step = 0;
        ++s->stats.checkpoints;
        s->population = s->sealed = false;
        s->outcome = get(s->manifests[slot] + 8, 4) == 2 ? ARC_RPC_HD_RECEIPTED
                                                         : ARC_RPC_HD_LEGACY;
        *done = true;
        return true;
    }
    default:
        return false;
    }
}
bool arc_riscpc_hd_population_publish_step(ArcRpcHdStore *s, bool *done) {
    if (!s || !done)
        return false;
    ArcRpcHdStats before;
    step_start(s, &before);
    bool ok = population_publish_unit(s, done);
    if (!ok && !s->refused)
        failure(s, false);
    step_finish(s, &before);
    return ok;
}
bool arc_riscpc_hd_population_publish(ArcRpcHdStore *s) {
    bool done = false;
    while (!done)
        if (!arc_riscpc_hd_population_publish_step(s, &done))
            return false;
    return true;
}
void arc_riscpc_hd_population_abort(ArcRpcHdStore *s) {
    if (!s || !s->population)
        return;
    s->population = false;
    s->population_io = s->reconstruction_active = false;
    /* Rename already published a complete root. Preserve it and its journal
     * even when cancellation precedes final live-handle verification. */
    if (s->publish_step >= 5) {
        s->sealed = false;
        failure(s, false);
        return;
    }
    if ((s->sealed || s->seal_at > 504 ||
         (s->seal_at == 504 && s->seal_step)) &&
        !s->refused) {
        char leaf[16];
        unsigned candidate = s->published ? s->parent ^ 1 : 0;
        journal_leaf(candidate, leaf);
        if (!remove_leaf(s, leaf))
            failure(s, false);
        else {
            s->journal_present[candidate] = false;
            s->lengths[candidate] = 0;
            s->current = s->parent;
        }
    }
    s->sealed = false;
    if (!s->refused) {
        s->state = ARC_RPC_HD_GC;
        s->phase_step = 0;
        s->gc_at = 0;
    }
}
bool arc_riscpc_hd_import(ArcRpcHdStore *s,const char *source)
{
    if(!s||s->published||s->refused||!arc_install_store_native_path(source))return false;
    op(s);ArcIoFile *f=arc_io_open_stream_counted(source,false,&s->stats.operations);if(!f)return false;
    bool ok=length(s,f)==ARC_RPC_HD_BYTES&&arc_riscpc_hd_population_begin(s);
    uint8_t *block=s->cache[1];s->tags[1].valid=false;for(uint32_t lba=0;ok&&lba<ARC_RPC_HD_SECTORS;lba+=256){
        ok=read_file(s,f,block,131072)&&arc_riscpc_hd_population_write(s,lba,256,block);}
    bool closed=close_file(s,f,ok);ok&=closed;if(ok)ok=arc_riscpc_hd_population_seal(s)&&arc_riscpc_hd_population_publish(s);
    if(!ok)arc_riscpc_hd_population_abort(s);return ok;
}

/* Conservative declaration includes failed namespace attempts and abort after
 * a sealed candidate. No clock or speculative filesystem probe is used. */
bool arc_riscpc_hd_next_namespace(const ArcRpcHdStore *s, unsigned operation)
{
    if (!s) return false;
    if (operation == 0) return s->open_phase == 0 || s->open_phase == 15 || s->open_phase == 17;
    if (operation == 1) return s->population_io && (s->population_phase == 1 || s->population_phase == 3 || s->population_phase == 4);
    if (operation == 2) return (s->seal_at >= 504 && !s->seal_step) ||
        (s->seal_step == 1 && (s->population_phase == 1 || s->population_phase == 3 || s->population_phase == 4));
    if (operation == 4) return s->seal_at > 504 || (s->seal_at == 504 && s->seal_step);
    return s->publish_step == 4;
}
