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

#define MANIFEST_BYTES 24324u
#define CHUNK_BYTES (ARC_RPC_HD_CHUNK + 132u)
#define RECORD_MAX 131168u
#define RECORDS_MAX ((ARC_RPC_HD_JOURNAL_CAP - 512u) / 608u)
#define SLOT_BIT UINT32_C(0x80000000)
#define OFFSET_MASK UINT32_C(0x7fffffff)
#define FILES_MAX 1008u
#define CACHE_COUNT 6u

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
    unsigned selected, current, parent, next_chunk, phase_step;
    ArcIoFile *rotation_writer;
    bool present[2], journal_present[2], torn[2], dirty[ARC_RPC_HD_CHUNKS];
    bool population, sealed, published, refused, damaged, requested, authored, gc_done, foreign_entry, stat_failed,
        saving_notified, damage_notified;
    bool population_changed[ARC_RPC_HD_CHUNKS];
    ArcRpcHdState state;
    char base[1024], content[41], io_leaf[23], diagnostic[192];
    const char *notice, *io_operation; bool diagnostic_pending;
    ArcRpcHdStats stats;
};
/* Store buffers, index and metadata are one allocation; VFS handles and the
 * formatter's bounded stack buffers fit the contract's remaining allowance. */
_Static_assert(sizeof(ArcRpcHdStore) + 12288u <= 4889964u, "RPC store memory bound");
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
{ char p[1100];snprintf(s->io_leaf,sizeof(s->io_leaf),"%s",leaf);s->io_operation="remove";path(s,leaf,p);metadata(s);return arc_io_remove(p); }
/* The split destination step already proved absence under exclusive ownership. */
static bool rename_empty(ArcRpcHdStore *s,const char *from,const char *to)
{ char a[1100],b[1100];snprintf(s->io_leaf,sizeof(s->io_leaf),"%s",to);s->io_operation="rename";
  path(s,from,a);path(s,to,b);metadata(s);return arc_io_rename(a,b); }
static bool rename_leaf(ArcRpcHdStore *s,const char *from,const char *to)
{ size_t n;if(size(s,to,&n)||s->refused)return false;return rename_empty(s,from,to); }
static ArcIoFile *open_leaf(ArcRpcHdStore *s,const char *leaf,bool write,bool create)
{ char p[1100];snprintf(s->io_leaf,sizeof(s->io_leaf),"%s",leaf);s->io_operation=create?"create":"open";path(s,leaf,p);metadata(s);
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
    size_t actual;return ok&&closed&&size(s,leaf,&actual)&&actual==n;
}
static uint8_t *entry(uint8_t *m,unsigned index) { return m+128u+index*48u; }
static void geometry(uint8_t *p)
{ const uint32_t values[]={512,63,16,256,262144,504};for(unsigned i=0;i<6;++i)put(p+4*i,values[i],4); }
static bool geometry_valid(const uint8_t *p)
{ uint8_t expected[24];geometry(expected);return !memcmp(p,expected,24); }
static void manifest_encode(ArcRpcHdStore *s,uint8_t *m,uint64_t generation,uint64_t jgen,uint64_t boundary)
{
    memset(m,0,128);copy(s,m,"ARCRHDM1",8);put(m+8,1,4);put(m+12,MANIFEST_BYTES,4);
    copy(s,m+16,s->content,40);put(m+56,1,4);put(m+60,1,4);geometry(m+64);
    put(m+88,generation,8);put(m+96,jgen,8);put(m+104,boundary,8);
    put(m+MANIFEST_BYTES-4,crc(s,m,MANIFEST_BYTES-4),4);
}
static bool manifest_valid(ArcRpcHdStore *s,const uint8_t *m,unsigned slot)
{
    if(memcmp(m,"ARCRHDM1",8)||get(m+8,4)!=1||get(m+12,4)!=MANIFEST_BYTES||
       memcmp(m+16,s->content,40)||get(m+56,4)!=1||get(m+60,4)!=1||!geometry_valid(m+64)||
       !zero(m+112,16)||get(m+88,8)%2!=slot||get(m+MANIFEST_BYTES-4,4)!=crc(s,m,MANIFEST_BYTES-4))return false;
    for(unsigned i=0;i<ARC_RPC_HD_CHUNKS;++i){const uint8_t *e=m+128+i*48;
        if(get(e,4)>1||!zero(e+4,4)||(get(e,4)==0&&!zero(e+8,40))||
           (get(e,4)==1&&get(e+8,8)>get(m+88,8)))return false;}
    return true;
}
static void header_encode(ArcRpcHdStore *s,uint8_t h[256],uint64_t gen,unsigned role)
{
    memset(h,0,256);copy(s,h,"ARCRHDJ1",8);put(h+8,1,4);put(h+12,256,4);copy(s,h+16,s->content,40);
    put(h+56,1,4);put(h+60,1,4);geometry(h+64);put(h+88,gen,8);put(h+112,s->snapshot,8);put(h+192,role,4);
    if(s->published){put(h+96,s->jgeneration[s->parent],8);put(h+104,s->generation,8);
       sha(s,s->manifests[s->selected],MANIFEST_BYTES,h+120);put(h+152,s->lengths[s->parent],8);
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
static bool create_header(ArcRpcHdStore *s,unsigned slot,uint64_t gen,unsigned role)
{
    if((uint64_t)s->lengths[0]+s->lengths[1]+256>ARC_RPC_HD_JOURNAL_CAP)return false;
    char leaf[16];journal_leaf(slot,leaf);uint8_t h[256],check[256];header_encode(s,h,gen,role);
    if(!write_leaf(s,leaf,h,256)||!read_leaf(s,leaf,check,256)||memcmp(h,check,256))return false;
    copy(s,s->headers[slot],h,256);s->lengths[slot]=256;s->jgeneration[slot]=gen;s->journal_present[slot]=true;
    arc_sha256_init(&s->digest[slot]);stream_hash(s,slot,h,256);return true;
}
static void index_record(ArcRpcHdStore *s,const Record *r,uint32_t *index)
{ (void)s;for(unsigned i=0;i<r->count;++i)index[r->lba+i]=(r->slot?SLOT_BIT:0)|(r->offset+80+i*512); }
static bool replay(ArcRpcHdStore *s,unsigned slot,uint32_t *index,uint64_t *last)
{
    char leaf[16];journal_leaf(slot,leaf);ArcIoFile *f=open_leaf(s,leaf,false,false);if(!f)return false;
    bool torn=false;uint32_t at=256,physical=s->lengths[slot];*last=get(s->headers[slot]+112,8);
    /* Replay is sequential after one confirmed position. Exact read counts
     * advance the cursor; redundant per-record seek/tell would exceed the
     * accepted near-cap open-operation budget for one-sector commands. */
    bool ok=physical==at||seek(s,f,at);
    arc_sha256_init(&s->digest[slot]);stream_hash(s,slot,s->headers[slot],256);
    while(ok&&at<physical){uint32_t left=physical-at;if(left<80){torn=true;break;}
        ok=read_file(s,f,s->record,80);if(!ok)break;
        uint8_t *p=s->record;uint32_t count=(uint32_t)get(p+68,4),lba=(uint32_t)get(p+64,4);
        uint64_t sequence=get(p+56,8);
        ok=!memcmp(p,"RCJR",4)&&get(p+4,4)==1&&!memcmp(p+8,s->content,40)&&
            get(p+48,8)==s->jgeneration[slot]&&*last!=UINT64_MAX&&sequence==*last+1&&
            count>=1&&count<=256&&lba<ARC_RPC_HD_SECTORS&&count<=ARC_RPC_HD_SECTORS-lba&&
            get(p+72,4)==count*512&&get(p+76,4)==crc(s,p,76);
        if(!ok)break;uint32_t total=96+count*512;if(left<total){torn=true;break;}
        ok=read_file(s,f,p+80,total-80)&&get(p+80+count*512,8)==sequence&&
            get(p+88+count*512,4)==crc(s,p+80,count*512)&&!memcmp(p+92+count*512,"RCCM",4);
        if(!ok||s->records_count==RECORDS_MAX){ok=false;break;}
        Record *r=&s->records[s->records_count++];*r=(Record){at,lba,count,slot};
        if(index)index_record(s,r,index);stream_hash(s,slot,p,total);*last=sequence;at+=total;
    }
    bool closed=close_file(s,f,ok);if(!ok||!closed)return false;
    if(torn){s->lengths[slot]=at;s->torn[slot]=true;}
    return true;
}
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
static bool publish_chunk(ArcRpcHdStore *s,unsigned index)
{
    uint8_t *e=entry(s->manifests[2],index);
    uint64_t generation=s->published?s->generation+1:0;char leaf[23];chunk_leaf(index,generation,leaf);
    if(zero(s->scratch,ARC_RPC_HD_CHUNK)){
        /* Repeated population writes can turn a private CHUNK into HOLE.
         * Remove that private leaf before publication loses its inventory. */
        size_t n;if(size(s,leaf,&n)){if(!remove_leaf(s,leaf))return false;untrack_chunk(s,leaf);}
        if(s->refused)return false;memset(e,0,48);return true;
    }
    uint8_t h[128]={0},tail[4],digest[32];sha(s,s->scratch,ARC_RPC_HD_CHUNK,digest);
    copy(s,h,"ARCRHDC1",8);put(h+8,1,4);put(h+12,CHUNK_BYTES,4);copy(s,h+16,s->content,40);
    put(h+56,index,4);put(h+60,1,4);put(h+64,1,4);put(h+68,ARC_RPC_HD_CHUNK,4);put(h+72,generation,8);
    copy(s,h+80,digest,32);put(h+124,crc(s,h,124),4);put(tail,crc(s,s->scratch,ARC_RPC_HD_CHUNK),4);
    ArcIoFile *f=open_leaf(s,"chunk.tmp",true,true);if(!f)return false;
    bool ok=write_file(s,f,h,128)&&write_file(s,f,s->scratch,ARC_RPC_HD_CHUNK)&&write_file(s,f,tail,4)&&flush(s,f);
    bool closed=close_file(s,f,ok);if(!ok||!closed)return false;
    memset(e,0,48);put(e,1,4);put(e+8,generation,8);copy(s,e+16,digest,32);
    /* Verify temporary using an exact derived entry, with the same envelope
     * checks as every cache miss. It cannot be a recovery root. */
    f=open_leaf(s,"chunk.tmp",false,false);if(!f)return false;
    uint8_t check[128],crc[4];ok=length(s,f)==CHUNK_BYTES&&read_file(s,f,check,128)&&!memcmp(check,h,128)&&
        read_file(s,f,s->scratch,ARC_RPC_HD_CHUNK)&&read_file(s,f,crc,4);closed=close_file(s,f,ok);
    uint8_t actual[32];sha(s,s->scratch,ARC_RPC_HD_CHUNK,actual);
    if(!ok||!closed||memcmp(crc,tail,4)||memcmp(actual,digest,32))return false;
    size_t n;if(size(s,leaf,&n)){
        /* Only a private generation can already exist (interrupted checkpoint
         * or repeated population write); no retained root may reference it. */
        for(unsigned slot=0;slot<2;++slot)if(s->present[slot]&&get(entry(s->manifests[slot],index),4)==1&&
            get(entry(s->manifests[slot],index)+8,8)==generation)return false;
        if(!remove_leaf(s,leaf))return false;
    }
    if(!rename_leaf(s,"chunk.tmp",leaf)||!track_chunk(s,leaf))return false;
    return base_chunk(s,s->manifests[2],index);
}
static bool parent_binding(ArcRpcHdStore *s,unsigned child,unsigned parent,unsigned manifest)
{
    uint8_t digest[32];const uint8_t *h=s->headers[child];
    if(get(h+96,8)!=s->jgeneration[parent]||get(h+104,8)!=get(s->manifests[manifest]+88,8)||
       get(h+152,8)!=s->lengths[parent])return false;
    arc_sha256_final(&s->digest[parent],digest);if(memcmp(digest,h+160,32))return false;
    sha(s,s->manifests[manifest],MANIFEST_BYTES,digest);return !memcmp(digest,h+120,32);
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
static bool publish_manifest(ArcRpcHdStore *s)
{
    uint8_t *check=s->record;unsigned slot=(unsigned)(get(s->manifests[2]+88,8)&1);char leaf[16];manifest_leaf(slot,leaf);
    if(!rename_leaf(s,"manifest.tmp",leaf)||!read_leaf(s,leaf,check,MANIFEST_BYTES)||memcmp(check,s->manifests[2],MANIFEST_BYTES))return failure(s,false);
    copy(s,s->manifests[slot],check,MANIFEST_BYTES);s->present[slot]=true;s->selected=slot;
    s->generation=get(check+88,8);s->published=true;s->gc_done=false;
    for(unsigned i=0;i<ARC_RPC_HD_SECTORS;++i)if(s->live[i]&&((s->live[i]&SLOT_BIT)!=0)==(s->parent!=0))s->live[i]=0;
    memset(s->tags,0,sizeof(s->tags));s->state=ARC_RPC_HD_RETIRE;s->phase_step=0;++s->stats.checkpoints;return true;
}
static bool prepare_manifest(ArcRpcHdStore *s)
{
    uint8_t *check=s->record;manifest_encode(s,s->manifests[2],s->published?s->generation+1:0,s->jgeneration[s->current],s->snapshot);
    return write_leaf(s,"manifest.tmp",s->manifests[2],MANIFEST_BYTES)&&read_leaf(s,"manifest.tmp",check,MANIFEST_BYTES)&&!memcmp(check,s->manifests[2],MANIFEST_BYTES);
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
        if(!read_leaf(s,leaf,check,MANIFEST_BYTES)||memcmp(check,s->manifests[s->selected],MANIFEST_BYTES))return false;
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
        journal_leaf(s->parent,leaf);if(!remove_leaf(s,leaf))return false;
        s->journal_present[s->parent]=false;s->lengths[s->parent]=0;
        unsigned keep=0;for(unsigned r=0;r<s->records_count;++r)if(s->records[r].slot==s->current)s->records[keep++]=s->records[r];s->records_count=keep;
        break;}
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
        if(!reconstruct_into(s,s->manifests[s->selected],i,s->frozen,s->checkpoint))return false;
        ++s->phase_step;return true;
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
    s->stats.tick_bytes=s->stats.tick_operations=s->stats.tick_hash_bytes=s->stats.tick_copied_bytes=s->stats.tick_metadata_operations=0;
    if(s->population||s->refused)return;
    uint64_t bytes=s->stats.requested_bytes,ops=s->stats.operations,
        hashes=s->stats.hash_bytes,copies=s->stats.copied_bytes,meta=s->stats.metadata_operations;
    bool ok=true;switch(s->state){
    case ARC_RPC_HD_IDLE:if(s->requested){s->requested=false;s->state=ARC_RPC_HD_ROTATE;s->phase_step=0;}break;
    case ARC_RPC_HD_ROTATE:ok=rotate_step(s);break;
    case ARC_RPC_HD_MATERIALIZE:ok=materialize_step(s);break;
    case ARC_RPC_HD_MANIFEST:
        if(s->phase_step==0){manifest_encode(s,s->manifests[2],s->generation+1,s->jgeneration[s->current],s->snapshot);
            /* Fresh readback in the next step establishes independent length. */
            ArcIoFile *f=open_leaf(s,"manifest.tmp",true,true);if(!f){ok=false;break;}
            ok=write_file(s,f,s->manifests[2],MANIFEST_BYTES)&&flush(s,f);bool closed=close_file(s,f,ok);ok&=closed;
            if(ok)s->phase_step=1;
        }else{ok=read_leaf(s,"manifest.tmp",s->record,MANIFEST_BYTES)&&!memcmp(s->record,s->manifests[2],MANIFEST_BYTES);
            if(ok){s->state=ARC_RPC_HD_PUBLISH;s->phase_step=0;}}
        break;
    case ARC_RPC_HD_PUBLISH:
        if(s->phase_step==0){char leaf[16];manifest_leaf((unsigned)(get(s->manifests[2]+88,8)&1),leaf);
            ok=rename_leaf(s,"manifest.tmp",leaf);if(ok)s->phase_step=1;
        }else{
            unsigned slot=(unsigned)(get(s->manifests[2]+88,8)&1);char leaf[16];manifest_leaf(slot,leaf);
            ok=read_leaf(s,leaf,s->record,MANIFEST_BYTES)&&!memcmp(s->record,s->manifests[2],MANIFEST_BYTES);
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
    if(s->stats.tick_bytes>s->stats.max_tick_bytes)s->stats.max_tick_bytes=s->stats.tick_bytes;
    if(s->stats.tick_operations>s->stats.max_tick_operations)s->stats.max_tick_operations=s->stats.tick_operations;
    if(s->stats.tick_hash_bytes>s->stats.max_tick_hash_bytes)s->stats.max_tick_hash_bytes=s->stats.tick_hash_bytes;
    if(s->stats.tick_copied_bytes>s->stats.max_tick_copied_bytes)s->stats.max_tick_copied_bytes=s->stats.tick_copied_bytes;
    if(s->stats.tick_metadata_operations>s->stats.max_tick_metadata_operations)s->stats.max_tick_metadata_operations=s->stats.tick_metadata_operations;
}
static bool range(uint32_t lba,uint32_t count)
{ return count&&count<=256&&lba<ARC_RPC_HD_SECTORS&&count<=ARC_RPC_HD_SECTORS-lba; }
bool arc_riscpc_hd_read(ArcRpcHdStore *s,uint32_t lba,uint32_t count,uint8_t *out)
{
    if(!s||!out||!s->published||s->population||s->damaged||!range(lba,count))return false;
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
    if(!s||!in||!s->published||s->population||s->refused||!range(lba,count))return false;
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
{ memset(out,0,sizeof(*out));if(s&&s->published&&!s->damaged){out->opaque=s;out->read=backend_read;out->write=backend_write;out->failed=backend_failed;out->sectors=ARC_RPC_HD_SECTORS;out->writable=!s->refused;} }
void arc_riscpc_hd_stats(const ArcRpcHdStore *s,ArcRpcHdStats *out)
{ *out=s->stats;out->phase_step=s->phase_step;out->chunk_files=s->chunk_count;out->sequence=s->sequence;out->generation=s->generation;out->state=s->state;out->published=s->published;out->population=s->population;out->read_only=s->refused; }
const char *arc_riscpc_hd_notice(ArcRpcHdStore *s) { if(!s)return NULL;const char *n=s->notice;s->notice=NULL;return n; }
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
static bool recover(ArcRpcHdStore *s)
{
    char leaf[16];size_t n;
    for(unsigned i=0;i<2;++i){manifest_leaf(i,leaf);if(size(s,leaf,&n)){
        if(n!=MANIFEST_BYTES||!read_leaf(s,leaf,s->manifests[i],MANIFEST_BYTES)||!manifest_valid(s,s->manifests[i],i))return failure(s,true);
        s->present[i]=true;}}
    if(s->stat_failed)return false;
    if(s->present[0]&&s->present[1]&&get(s->manifests[0]+88,8)==get(s->manifests[1]+88,8))return failure(s,true);
    s->published=s->present[0]||s->present[1];s->selected=s->present[1]&&(!s->present[0]||get(s->manifests[1]+88,8)>get(s->manifests[0]+88,8))?1:0;
    if(s->published){s->generation=get(s->manifests[s->selected]+88,8);s->current=(unsigned)(get(s->manifests[s->selected]+96,8)&1);}
    bool partial[2]={false,false};
    for(unsigned i=0;i<2;++i){journal_leaf(i,leaf);if(!size(s,leaf,&n))continue;
        if(n<256){for(unsigned m=0;m<2;++m)if(s->present[m]&&(get(s->manifests[m]+96,8)&1)==i)return failure(s,true);partial[i]=true;continue;}
        if(n>ARC_RPC_HD_JOURNAL_CAP)return failure(s,true);s->lengths[i]=(uint32_t)n;
        if(!read_header(s,i,s->headers[i])||!header_valid(s,s->headers[i],i))return failure(s,true);
        s->jgeneration[i]=get(s->headers[i]+88,8);s->journal_present[i]=true;
    }
    if(s->stat_failed)return false;
    if((uint64_t)s->lengths[0]+s->lengths[1]>ARC_RPC_HD_JOURNAL_CAP)return failure(s,true);
    for(unsigned m=0;m<2;++m)if(s->present[m]){unsigned j=(unsigned)(get(s->manifests[m]+96,8)&1);
        if(!s->journal_present[j]||get(s->manifests[m]+96,8)!=s->jgeneration[j]||get(s->manifests[m]+104,8)!=get(s->headers[j]+112,8))return failure(s,true);
        uint64_t generation=get(s->manifests[m]+88,8),parent=get(s->headers[j]+104,8);
        if(s->jgeneration[j]==0?generation!=0:parent==UINT64_MAX||generation!=parent+1)return failure(s,true);
    }
    bool abandon=false;unsigned other=s->current^1;uint64_t last[2]={0,0};
    if(s->present[0]&&s->present[1]&&
       (!s->journal_present[other]||s->jgeneration[other]>=s->jgeneration[s->current]||
        get(s->manifests[s->selected^1]+96,8)!=s->jgeneration[other]))return failure(s,true);
    if(!s->published){for(unsigned i=0;i<2;++i)if(s->journal_present[i]){
            if(s->lengths[i]!=256||get(s->headers[i]+88,8)!=0||get(s->headers[i]+112,8)!=0)return failure(s,true);}
    }else if(s->journal_present[other]&&s->jgeneration[other]>s->jgeneration[s->current]){
        if(!replay(s,s->current,s->live,&last[s->current]))return failure(s,true);
        if(!parent_binding(s,other,s->current,s->selected)||get(s->headers[other]+112,8)!=last[s->current])return failure(s,true);
        unsigned role=(unsigned)get(s->headers[other]+192,4);
        if(role==2){if(s->lengths[other]!=256)return failure(s,true);abandon=true;s->sequence=last[s->current];}
        else if(role==1){s->snapshot=last[s->current];copy(s,s->frozen,s->live,sizeof(s->live));
            copy(s,s->manifests[2],s->manifests[s->selected],MANIFEST_BYTES);s->parent=s->current;s->current=other;
            if(!replay(s,s->current,s->live,&last[s->current]))return failure(s,true);s->sequence=last[s->current];
            for(unsigned i=0;i<ARC_RPC_HD_SECTORS;++i)if(s->frozen[i])s->dirty[i/512]=true;
            s->state=ARC_RPC_HD_MATERIALIZE;
        }else return failure(s,true);
    }else{
        if(s->journal_present[other]){
            if(!replay(s,other,NULL,&last[other]))return failure(s,true);
            uint8_t digest[32];arc_sha256_final(&s->digest[other],digest);const uint8_t *h=s->headers[s->current];
            if(get(h+192,4)==0||get(h+96,8)!=s->jgeneration[other]||get(h+152,8)!=s->lengths[other]||
               get(h+112,8)!=last[other]||memcmp(digest,h+160,32))return failure(s,true);
            if(s->present[s->selected^1]&&!parent_binding(s,s->current,other,s->selected^1))return failure(s,true);
            s->parent=other;s->state=ARC_RPC_HD_RETIRE;
        }
        if(s->published){if(!replay(s,s->current,s->live,&last[s->current]))return failure(s,true);s->sequence=last[s->current];}
    }
    /* A truthful listing may expose foreign canonical-looking generations.
     * Empty/failed listings prove nothing; only fixed roots build inventory. */
    uint64_t candidate=s->published?s->generation+1:0;
    for(unsigned i=0;i<s->advisory_count;++i){unsigned idx;uint64_t gen;
        sscanf(s->chunks[i],"c%3x.g%16" SCNx64,&idx,&gen);
        if(((s->published&&s->generation==UINT64_MAX)||gen!=candidate)&&
           !referenced(s,s->chunks[i]))return failure(s,true);
    }
    s->advisory_count=0;
    /* D-147: root entries provide the retained inventory; listings are never
     * authoritative. A next cycle cannot exist before RETIRE finishes. */
    for(unsigned m=0;m<2;++m)if(s->present[m])for(unsigned i=0;i<ARC_RPC_HD_CHUNKS;++i){
        const uint8_t *e=entry(s->manifests[m],i);if(get(e,4)!=1)continue;
        char name[23];chunk_leaf(i,get(e+8,8),name);
        if(size(s,name,&n)&&!track_chunk(s,name))return failure(s,true);
    }
        if(s->stat_failed)return false;
    /* Validation of both roots/journals is complete before any cleanup. */
    for(unsigned i=0;i<2;++i)if(s->torn[i]){journal_leaf(i,leaf);ArcIoFile *f=open_leaf(s,leaf,true,false);bool ok=false;
        if(f){s->io_operation="truncate";op(s);ok=arc_io_truncate_file(f,s->lengths[i])&&flush(s,f);bool closed=close_file(s,f,ok);ok&=closed;}
        if(!ok){failure(s,false);s->notice="An interrupted hard-disc write was recovered; the hard disc is read-only.";}
        else{s->notice="An interrupted hard-disc write was recovered.";size_t actual;if(!size(s,leaf,&actual)||actual!=s->lengths[i])failure(s,false);}}
    if(s->refused)return true;
    for(unsigned i=0;i<2;++i)if(partial[i]||(!s->published&&s->journal_present[i])||(abandon&&i==other)){journal_leaf(i,leaf);
        if(!remove_leaf(s,leaf))return failure(s,false);s->journal_present[i]=false;s->lengths[i]=0;}
    /* Interrupted checkpoint/population leaves only selected+1 (or zero for
     * unpublished creation). Remove fixed candidate paths, restart materialize
     * from the frozen index, and leave retiring old-only cleanup to ticks. */
    if(!s->published||s->generation!=UINT64_MAX)for(unsigned i=0;i<ARC_RPC_HD_CHUNKS;++i){
        char name[23];chunk_leaf(i,candidate,name);
        if(size(s,name,&n)){if(!remove_leaf(s,name))return failure(s,false);untrack_chunk(s,name);}
        if(s->refused)return false;
    }
    const char *temps[]={"chunk.tmp","manifest.tmp"};
    for(unsigned i=0;i<2;++i){if(size(s,temps[i],&n)&&!remove_leaf(s,temps[i]))return failure(s,false);
        if(s->refused)return false;}
    if(s->stat_failed)return failure(s,false);
    if(s->state==ARC_RPC_HD_IDLE&&s->published&&s->lengths[s->current]>=ARC_RPC_HD_TRIGGER)s->requested=true;
    return true;
}
ArcRpcHdStore *arc_riscpc_hd_open(const char *save,const char *content,bool create_blank,char *error,size_t error_size)
{
    if(error&&error_size)error[0]=0;
    if(!arc_install_store_native_path(save)||!key_valid(content)){if(error&&error_size)snprintf(error,error_size,"Invalid RiscPC hard-disc path or content identity");return NULL;}
    ArcRpcHdStore *s=store_allocate(1,sizeof(*s));if(!s){if(error&&error_size)snprintf(error,error_size,"RiscPC hard-disc allocation failed");return NULL;}
    int n=snprintf(s->base,sizeof(s->base),"%s/arcdroid/hd-riscpc/%s",save,content);
    if(n<0||(size_t)n>=sizeof(s->base)){if(error&&error_size)snprintf(error,error_size,"RiscPC hard-disc path is too long");free(s);return NULL;}copy(s,s->content,content,41);
    s->stats.allocation_high=sizeof(*s)+12288;
    op(s);bool exists=arc_io_existing_directory(s->base);
    if(!exists){if(!arc_io_make_path_counted(s->base,&s->stats.operations)){if(error&&error_size)snprintf(error,error_size,"RiscPC hard-disc directory creation failed");free(s);return NULL;}}
    (void)arc_io_list_directory_counted(s->base,directory_visit,s,&s->stats.advisory_operations);
    s->stats.operations+=s->stats.advisory_operations;
    if(s->foreign_entry||!recover(s)){
        if(error&&error_size)snprintf(error,error_size,"The saved RiscPC hard disc is damaged; files have been preserved.");free(s);return NULL;}
    s->stats.open_bytes=s->stats.bytes_read+s->stats.bytes_written;s->stats.open_operations=s->stats.operations;
    if(!arc_io_flush_available())failure(s,false);
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
void arc_riscpc_hd_close(ArcRpcHdStore *s) { if(s){if(s->population)arc_riscpc_hd_population_abort(s);if(s->rotation_writer){ArcIoFile *f=s->rotation_writer;s->rotation_writer=NULL;(void)close_file(s,f,true);}free(s);} }
bool arc_riscpc_hd_population_begin(ArcRpcHdStore *s)
{
    if(!s||s->refused||s->population||s->state!=ARC_RPC_HD_IDLE||s->requested||s->generation==UINT64_MAX||
       (s->published&&s->jgeneration[s->current]==UINT64_MAX))return false;
    s->population=true;s->sealed=false;s->snapshot=s->sequence;s->parent=s->current;
    copy(s,s->frozen,s->live,sizeof(s->live));memset(s->population_changed,0,sizeof(s->population_changed));
    if(s->published)copy(s,s->manifests[2],s->manifests[s->selected],MANIFEST_BYTES);else memset(s->manifests[2],0,MANIFEST_BYTES);
    return true;
}
static bool population_chunk(ArcRpcHdStore *s,unsigned i)
{
    return s->population_changed[i]?base_chunk(s,s->manifests[2],i):
        reconstruct(s,s->published?s->manifests[s->selected]:s->manifests[2],i,s->frozen);
}
bool arc_riscpc_hd_population_read(ArcRpcHdStore *s,uint32_t lba,uint32_t count,uint8_t *out)
{
    if(!s||!s->population||s->refused||!out||!range(lba,count))return false;
    while(count){unsigned i=lba/512,at=lba%512,take=512-at;if(take>count)take=count;
        if(!population_chunk(s,i))return false;copy(s,out,s->scratch+at*512,take*512);out+=take*512;lba+=take;count-=take;}return true;
}
bool arc_riscpc_hd_population_write(ArcRpcHdStore *s,uint32_t lba,uint32_t count,const uint8_t *in)
{
    if(!s||!s->population||s->sealed||s->refused||!in||!range(lba,count))return false;
    /* The source may alias the reconstruction scratch or journal buffer; copy
     * the complete bounded input before base/record reconstruction. */
    uint8_t *input=s->cache[0];s->tags[0].valid=false;copy(s,input,in,count*512);
    while(count){unsigned i=lba/512,at=lba%512,take=512-at;if(take>count)take=count;
        if(!population_chunk(s,i))return false;copy(s,s->scratch+at*512,input,take*512);
        if(!publish_chunk(s,i))return failure(s,false);s->population_changed[i]=true;lba+=take;count-=take;
        if(count){s->stats.copied_bytes+=count*512;memmove(input,input+take*512,count*512);}
    }return true;
}
bool arc_riscpc_hd_population_seal(ArcRpcHdStore *s)
{
    if(!s||!s->population||s->sealed||s->refused||s->snapshot!=s->sequence)return false;
    for(unsigned i=0;i<504;++i){if(!s->population_changed[i]){
        bool dirty=false;for(unsigned j=0;j<512;++j)dirty|=s->frozen[i*512+j]!=0;
        if(dirty){if(!population_chunk(s,i)||!publish_chunk(s,i))return failure(s,false);s->population_changed[i]=true;}}
        if(!base_chunk(s,s->manifests[2],i))return false;}
    unsigned next=s->published?s->current^1:0;
    if(!create_header(s,next,s->published?s->jgeneration[s->current]+1:0,s->published?2:(s->authored?0:2)))return failure(s,false);
    s->current=next;if(!prepare_manifest(s))return failure(s,false);s->sealed=true;return true;
}
bool arc_riscpc_hd_population_publish(ArcRpcHdStore *s)
{
    if(!s||!s->population||!s->sealed||s->refused||s->snapshot!=s->sequence)return false;
    if(s->published){
        /* D-145: revalidate the frozen source before publication. Exclusive
         * ownership precludes append/maintenance, and metadata checks remain
         * bounded; source payloads were verified during candidate sealing. */
        char leaf[16];uint8_t h[256],digest[32];size_t n;
        manifest_leaf(s->selected,leaf);
        if(!read_leaf(s,leaf,s->record,MANIFEST_BYTES)||memcmp(s->record,s->manifests[s->selected],MANIFEST_BYTES))return failure(s,true);
        journal_leaf(s->parent,leaf);arc_sha256_final(&s->digest[s->parent],digest);
        if(!read_header(s,s->parent,h)||memcmp(h,s->headers[s->parent],256)||!size(s,leaf,&n)||n!=s->lengths[s->parent]||
            get(s->headers[s->current]+96,8)!=s->jgeneration[s->parent]||
            get(s->headers[s->current]+152,8)!=n||memcmp(s->headers[s->current]+160,digest,32))return failure(s,true);
    }
    if(!publish_manifest(s))return false;s->population=false;s->sealed=false;
    /* Retirement and GC remain bounded frame maintenance; exclusive ownership
     * ends only after a complete new recovery root has been published. */
    return true;
}
void arc_riscpc_hd_population_abort(ArcRpcHdStore *s)
{
    if(!s||!s->population)return;s->population=false;
    if(s->sealed&&!s->refused){char leaf[16];journal_leaf(s->current,leaf);if(!remove_leaf(s,leaf))failure(s,false);
        else{s->journal_present[s->current]=false;s->lengths[s->current]=0;s->current=s->parent;}}
    s->sealed=false;if(!s->refused){s->state=ARC_RPC_HD_GC;s->phase_step=0;s->gc_at=0;}
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
