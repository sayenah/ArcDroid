/* ArcDroid save-state graph codec. GPL-2.0-only.
 * Wire values have no padding or native pointer bytes. A load builds a write
 * set and replacement ownership graph; only commit touches the live session. */
#include "arc_state.h"
#undef arc_state_value
#include "arcdroid_core.h"
#include <stdlib.h>
#include <string.h>

#define ARC_STATE_LIMIT (1024u * 1024u * 1024u)
#define ARC_STATE_ITEMS 262144u
#define ARC_STATE_MAGIC "ARCSS1\0\0"
#define ARC_STATE_HEADER 40u
#define ARC_STATE_RECORD 24u

typedef struct {
   uint64_t key;
   uint32_t kind;
   size_t bytes,encoded_bytes;
   bool compressed;
   const uint8_t *data;
   uint8_t *owned;
   bool seen;
} StateItem;
typedef struct {
   uint64_t key;
   void *base, *previous;
   size_t bytes, previous_bytes;
   void *slot;
   bool heap;
} StateRegion;
typedef struct {
   void *destination;const void *source;size_t bytes,encoded_bytes;
   bool small,compressed;uint8_t inline_value[64];
} StateWrite;
typedef struct StateScratch { struct StateScratch *next; uint8_t data[]; } StateScratch;
typedef struct StateText { struct StateText *next; char text[]; } StateText;
static uint8_t *state_decode_arena;
static size_t state_decode_capacity;
static StateText *state_text_pool; /* Host ownership; values are explicit strings. */
typedef struct StateAction { struct StateAction *next; void (*commit)(void *),(*rollback)(void *); void *opaque; } StateAction;
typedef struct {uint64_t key;const void *data;size_t bytes;} StateView;
typedef struct {void *address;size_t bytes;ArcStateReferenceKind kind;} StateTarget;
struct StateRaster;
struct ArcState {
   struct StateRaster *rasters;
   StateView *views;size_t view_count,view_capacity;
   StateTarget *targets;
   size_t target_count,target_capacity;
   StateAction *actions;
   StateScratch *scratch;
   StateText *texts;
   bool loading, committed, registering, measuring;
#if defined(ARCDROID_TEST_FAULTS)
   uint64_t drop_owner;bool drop_done;
#endif
   unsigned pass;
   const char *error;
   char error_text[128];
   StateItem *items;
   uint32_t *item_table;size_t item_table_size;
   size_t count, capacity;
   StateRegion *regions;
   size_t region_count, region_capacity;
   StateWrite *writes;
   uint32_t *write_table;size_t write_table_size;
   size_t write_count, write_capacity;
   uint32_t *region_table;size_t region_table_size;
   StateRegion **addresses;size_t address_count;
   StateRegion *object;
   size_t output_size,heap_bytes,decoded_limit;
};
static uint64_t get64(const uint8_t *p)
{ uint64_t v=0; for(unsigned i=0;i<8;++i)v|=(uint64_t)p[i]<<(8*i); return v; }
static void put64(uint8_t *p,uint64_t v)
{ for(unsigned i=0;i<8;++i)p[i]=(uint8_t)(v>>(8*i)); }
static uint64_t hash(const void *data,size_t n)
{ const uint8_t *p=data; uint64_t h=UINT64_C(14695981039346656037); while(n--)h=(h^*p++)*UINT64_C(1099511628211); return h; }
/* Bounded word runs retain every pixel/sample/RAM byte. Literal blocks keep
 * incompressible data bounded; compressed records are used only when smaller. */
static size_t repeat_words(const uint8_t *p,size_t words,size_t at)
{
   uint32_t value;memcpy(&value,p+at*4u,4);uint64_t pattern=(uint64_t)value|((uint64_t)value<<32);
   size_t end=at+1u;
   while(words-end>=8u) {
      uint64_t a,b,c,d;memcpy(&a,p+end*4u,8);memcpy(&b,p+end*4u+8,8);
      memcpy(&c,p+end*4u+16,8);memcpy(&d,p+end*4u+24,8);
      if((a^pattern)|(b^pattern)|(c^pattern)|(d^pattern))break;end+=8u;
   }
   while(end<words){uint32_t next;memcpy(&next,p+end*4u,4);if(next!=value)break;++end;}
   return end-at;
}
static uint8_t *compress_words(const uint8_t *p,size_t bytes,size_t *encoded)
{
   if(bytes<4096u||bytes>ARC_STATE_LIMIT)return NULL;
   size_t capacity=bytes+bytes/128u+64u;
   uint8_t *out=malloc(capacity);if(!out)return NULL;
   put64(out,bytes);size_t used=8,at=0,words=bytes/4u;
   while(at<words&&used<bytes) {
      size_t run=repeat_words(p,words,at);
      if(run>=4u){put64(out+used,((uint64_t)run<<1u)|1u);memcpy(out+used+8,p+at*4u,4);used+=12;at+=run;}
      else {
         size_t end=at+run;
         while(end<words&&end-at<1048576u) {
            uint32_t v[4];if(words-end>=4u){memcpy(v,p+end*4u,sizeof(v));if(v[0]==v[1]&&v[0]==v[2]&&v[0]==v[3])break;}
            ++end;
         }
         size_t n=(end-at)*4u;
         if(used>capacity-8u||n>capacity-used-8u){free(out);return NULL;}
         put64(out+used,(uint64_t)(end-at)<<1u);memcpy(out+used+8,p+at*4u,n);used+=8u+n;at=end;
      }
   }
   if(at!=words||used+bytes%4u>=bytes){free(out);return NULL;}
   memcpy(out+used,p+words*4u,bytes%4u);used+=bytes%4u;*encoded=used;return out;
}
static bool decompress_words(uint8_t *out,size_t bytes,const uint8_t *p,size_t encoded)
{
   if(encoded<8u||get64(p)!=bytes)return false;
   size_t used=8,at=0,words=bytes/4u;
   while(at<words) {
      if(encoded-used<8u)return false;
      uint64_t packet=get64(p+used),count=packet>>1u;used+=8u;
      if(!count||count>words-at)return false;
      if(packet&1u) {
         if(encoded-used<4u)return false;uint32_t value;memcpy(&value,p+used,4);used+=4;
         /* Compiler vectorizes the fill without aliasing/alignment assumptions. */
         if(out)for(size_t i=0;i<(size_t)count;++i)memcpy(out+(at+i)*4u,&value,4);
      } else {
         if(count>(encoded-used)/4u)return false;if(out)memcpy(out+at*4u,p+used,(size_t)count*4u);used+=(size_t)count*4u;
      }
      at+=(size_t)count;
   }
   if(encoded-used!=bytes%4u)return false;
   if(out)memcpy(out+words*4u,p+used,bytes%4u);return true;
}
static uint64_t name_key(const char *name) { return hash(name,strlen(name)); }
static uint64_t field_key(uint64_t owner,size_t offset,size_t bytes)
{ uint8_t b[24];put64(b,owner);put64(b+8,offset);put64(b+16,bytes);return hash(b,sizeof(b)); }
void arc_state_fail(ArcState *s,const char *reason)
{
   if(s&&!s->error) {
      size_t n=reason?strlen(reason):0;if(n>=sizeof(s->error_text))n=sizeof(s->error_text)-1u;
      if(n)memcpy(s->error_text,reason,n);s->error_text[n]=0;s->error=s->error_text;
   }
}
void arc_state_free(void *pointer) { free(pointer); }
const char *arc_state_error(const ArcState *s) { return s?s->error:"allocation"; }
bool arc_state_loading(const ArcState *s) { return s&&s->loading; }
unsigned arc_state_pass(const ArcState *s) { return s?s->pass:0; }
size_t arc_state_decoded_limit(const ArcState *s) { return s?s->decoded_limit:0; }
bool arc_state_registering(const ArcState *s) { return s&&s->registering; }
static bool grow(ArcState *s,void **p,size_t *capacity,size_t count,size_t width)
{
   if(s->error)return false;
   if(count>*capacity) {
      size_t cap=*capacity?*capacity*2u:64u;
      if(cap<count)cap=count;
      if(cap>ARC_STATE_ITEMS||width>SIZE_MAX/cap) {arc_state_fail(s,"item limit");return false;}
      void *next=realloc(*p,cap*width);
      if(!next){arc_state_fail(s,"allocation");return false;}
      *p=next;*capacity=cap;
   }
   return true;
}
static int item_order(const void *a,const void *b)
{ uint64_t x=((const StateItem*)a)->key,y=((const StateItem*)b)->key;return x<y?-1:x>y; }
static StateItem *find_item(ArcState *s,uint64_t key)
{
   if(!s->loading) {for(size_t i=0;i<s->count;++i)if(s->items[i].key==key)return &s->items[i];return NULL;}
   if(s->item_table_size) {
      size_t at=(size_t)(key^(key>>32u))&(s->item_table_size-1u);
      while(s->item_table[at]) {
         StateItem *r=&s->items[s->item_table[at]-1u];if(r->key==key)return r;
         at=(at+1u)&(s->item_table_size-1u);
      }
      return NULL;
   }
   size_t low=0,high=s->count;
   while(low<high) {
      size_t middle=low+(high-low)/2u;StateItem *r=&s->items[middle];
      if(r->key==key)return r;
      if(r->key<key)low=middle+1u;else high=middle;
   }
   return NULL;
}
static StateItem *item(ArcState *s,uint64_t key,unsigned kind,const void *data,size_t bytes)
{
   if(s->error)return NULL;
   if(s->loading) {
      StateItem *r=find_item(s,key);
      if(!r||r->seen||r->kind!=kind||r->bytes!=bytes) {arc_state_fail(s,"schema");return NULL;}
      r->seen=true;return r;
   }
   if(!grow(s,(void**)&s->items,&s->capacity,s->count+1,sizeof(*s->items)))return NULL;
   StateItem *r=&s->items[s->count++];memset(r,0,sizeof(*r));
   r->key=key;r->kind=kind;r->bytes=bytes;r->encoded_bytes=bytes;
   if(bytes) {
      if(kind==1&&bytes<4096u)r->data=data;
      else if((kind==1||kind==6)&&bytes>=4096u) {
         r->owned=compress_words(data,bytes,&r->encoded_bytes);
         r->compressed=r->owned!=NULL;
         if(!r->owned&&kind==6u) {r->owned=malloc(bytes);if(!r->owned){arc_state_fail(s,"allocation");return NULL;}memcpy(r->owned,data,bytes);r->encoded_bytes=bytes;}
         r->data=r->owned?r->owned:data;
      }
      else {
         r->owned=malloc(bytes);
         if(!r->owned){arc_state_fail(s,"allocation");return NULL;}
         memcpy(r->owned,data,bytes);r->data=r->owned;
      }
   }
   if(r->encoded_bytes>ARC_STATE_LIMIT-ARC_STATE_RECORD||s->output_size>ARC_STATE_LIMIT-r->encoded_bytes-ARC_STATE_RECORD)arc_state_fail(s,"size limit");
   else s->output_size+=ARC_STATE_RECORD+r->encoded_bytes;
   return r;
}
static StateRegion *address_region(ArcState *s,uintptr_t address,size_t extent);
static size_t write_hash(const void *address,size_t bytes,size_t mask)
{ uintptr_t p=(uintptr_t)address;return (size_t)((p>>3u)^(p>>19u)^((uint64_t)bytes*UINT64_C(0x9e3779b97f4a7c15)))&mask; }
static void write_index(ArcState *s,size_t index)
{
   if(s->write_count*2u>=s->write_table_size) {
      size_t capacity=s->write_table_size?s->write_table_size*2u:128u;
      uint32_t *table=calloc(capacity,sizeof(*table));
      if(!table){arc_state_fail(s,"allocation");return;}
      for(size_t i=0;i<s->write_count;++i) {
         const StateWrite *w=&s->writes[i];size_t at=write_hash(w->destination,w->bytes,capacity-1u);
         while(table[at]) {
            const StateWrite *previous=&s->writes[table[at]-1u];
            if(previous->destination==w->destination&&previous->bytes==w->bytes)break;
            at=(at+1u)&(capacity-1u);
         }
         table[at]=(uint32_t)(i+1u);
      }
      free(s->write_table);s->write_table=table;s->write_table_size=capacity;return;
   }
   const StateWrite *w=&s->writes[index];size_t at=write_hash(w->destination,w->bytes,s->write_table_size-1u);
   while(s->write_table[at]) {
      const StateWrite *previous=&s->writes[s->write_table[at]-1u];
      if(previous->destination==w->destination&&previous->bytes==w->bytes)break;
      at=(at+1u)&(s->write_table_size-1u);
   }
   s->write_table[at]=(uint32_t)(index+1u);
}
static StateWrite *write_find(ArcState *s,const void *address,size_t bytes)
{
   if(!s->write_table_size)return NULL;
   size_t at=write_hash(address,bytes,s->write_table_size-1u);
   while(s->write_table[at]) {
      StateWrite *w=&s->writes[s->write_table[at]-1u];
      if(w->destination==address&&w->bytes==bytes)return w;
      at=(at+1u)&(s->write_table_size-1u);
   }
   return NULL;
}
static void write_later(ArcState *s,void *destination,const void *source,size_t bytes)
{
   if(!s->loading||s->error||!bytes)return;
   if(s->addresses) {
      StateRegion *r=address_region(s,(uintptr_t)destination,bytes);
      if(r&&r->heap){memcpy(destination,source,bytes);return;}
   } else for(size_t i=0;i<s->region_count;++i) {
      StateRegion *r=&s->regions[i];uintptr_t a=(uintptr_t)destination,b=(uintptr_t)r->base;
      if(r->heap&&a>=b&&a-b<=r->bytes&&bytes<=r->bytes-(a-b)) {memcpy(destination,source,bytes);return;}
   }
   if(!grow(s,(void**)&s->writes,&s->write_capacity,s->write_count+1,sizeof(*s->writes)))return;
   StateWrite *w=&s->writes[s->write_count++];memset(w,0,sizeof(*w));
   w->destination=destination;w->bytes=bytes;
   if(bytes>=sizeof(w->inline_value))w->source=source;
   else {w->small=true;memcpy(w->inline_value,source,bytes);}
   write_index(s,s->write_count-1u);
}
static const void *write_source(const StateWrite *w)
{ return w->small?w->inline_value:w->source; }
static void compressed_later(ArcState *s,void *destination,const StateItem *item)
{
   StateRegion *r=address_region(s,(uintptr_t)destination,item->bytes);
   if(r&&r->heap) {
      if(!decompress_words(destination,item->bytes,item->data,item->encoded_bytes))arc_state_fail(s,"compressed record");
      return;
   }
   if(!grow(s,(void**)&s->writes,&s->write_capacity,s->write_count+1u,sizeof(*s->writes)))return;
   StateWrite *w=&s->writes[s->write_count++];memset(w,0,sizeof(*w));
   w->destination=destination;w->source=item->data;w->bytes=item->bytes;
   w->encoded_bytes=item->encoded_bytes;w->compressed=true;write_index(s,s->write_count-1u);
}

ArcState *arc_state_begin(const void *input,size_t length)
{ return arc_state_begin_limited(input,length,ARC_STATE_LIMIT); }
ArcState *arc_state_begin_limited(const void *input,size_t length,size_t decoded_limit)
{
   ArcState *s=calloc(1,sizeof(*s));if(!s)return NULL;
   s->loading=input!=NULL;s->output_size=ARC_STATE_HEADER;
   s->decoded_limit=decoded_limit<ARC_STATE_LIMIT?decoded_limit:ARC_STATE_LIMIT;
#if defined(ARCDROID_TEST_FAULTS)
   const char *drop=getenv("ARCDROID_SS1_DROP_OBJECT");
   if(drop)s->drop_owner=name_key(drop);
#endif
   if(!s->loading)return s;
   const uint8_t *p=input;
   if(length<ARC_STATE_HEADER||length>ARC_STATE_LIMIT||memcmp(p,ARC_STATE_MAGIC,8)||get64(p+8)!=3||
      get64(p+16)>length||get64(p+16)<ARC_STATE_HEADER||get64(p+24)>ARC_STATE_ITEMS) {
      arc_state_fail(s,"container");return s;
   }
   size_t used=(size_t)get64(p+16),count=(size_t)get64(p+24);
   if(arc_crc32(p+ARC_STATE_HEADER,used-ARC_STATE_HEADER)!=get64(p+32)) {arc_state_fail(s,"checksum");return s;}
   /* Reserved frontend capacity is not machine data. The payload CRC covers
    * all interpreted bytes; a terminal capacity guard authenticates its
    * framing without scanning hundreds of MiB of unused frontend storage. */
   if(length!=used&&(length-used<16u||memcmp(p+length-16u,"ARCCAP3",8u)||get64(p+length-8u)!=length)) {
      arc_state_fail(s,"capacity guard");return s;
   }
   size_t at=ARC_STATE_HEADER;
   for(size_t i=0;i<count;++i) {
      if(used-at<ARC_STATE_RECORD){arc_state_fail(s,"record bounds");break;}
      uint64_t bytes=get64(p+at+16);
      if(bytes>used-at-ARC_STATE_RECORD||!grow(s,(void**)&s->items,&s->capacity,s->count+1,sizeof(*s->items))) {arc_state_fail(s,"record bounds");break;}
      StateItem *r=&s->items[s->count++];memset(r,0,sizeof(*r));r->key=get64(p+at);uint64_t kind=get64(p+at+8);r->kind=(uint32_t)kind;
      r->compressed=(kind&UINT64_C(0x100000000))!=0;r->encoded_bytes=(size_t)bytes;r->bytes=(size_t)bytes;r->data=p+at+ARC_STATE_RECORD;
      if(r->kind<1u||r->kind>6u||(kind>>32u)>1u||(r->compressed&&((r->kind!=1u&&r->kind!=6u)||bytes<8u))) {arc_state_fail(s,"record kind");break;}
      if(r->compressed) {
         uint64_t logical=get64(r->data);if(logical<4096u||logical>s->decoded_limit||bytes>=logical){arc_state_fail(s,"decoded bounds");break;}
         r->bytes=(size_t)logical;
      }
      at+=ARC_STATE_RECORD+(size_t)bytes;
   }
   if(at!=used||s->count!=count)arc_state_fail(s,"record bounds");
   size_t decoded=0;
   for(size_t i=0;!s->error&&i<s->count;++i)if(s->items[i].compressed) {
      size_t n=s->items[i].bytes;
      if(n>s->decoded_limit||decoded>s->decoded_limit-n)arc_state_fail(s,"decoded bounds");
      else decoded+=n;
   }
   /* Validate every packet before preparing the transaction. Large live
    * arrays are decoded only at commit, eliminating a full scratch copy. */
   for(size_t i=0;!s->error&&i<s->count;++i)if(s->items[i].compressed) {
      StateItem *r=&s->items[i];
      if(!decompress_words(NULL,r->bytes,r->data,r->encoded_bytes))arc_state_fail(s,"compressed record");
   }
   if(!s->error) {
      size_t capacity=128u;while(capacity<s->count*2u)capacity*=2u;
      s->item_table=calloc(capacity,sizeof(*s->item_table));
      if(!s->item_table)arc_state_fail(s,"allocation");
      else {
         s->item_table_size=capacity;
         for(size_t i=0;i<s->count;++i) {
            uint64_t key=s->items[i].key;size_t at=(size_t)(key^(key>>32u))&(capacity-1u);
            while(s->item_table[at]) {
               if(s->items[s->item_table[at]-1u].key==key){arc_state_fail(s,"duplicate field");break;}
               at=(at+1u)&(capacity-1u);
            }
            s->item_table[at]=(uint32_t)(i+1u);
         }
      }
   }
   return s;
}
static StateRegion *region(ArcState *s,uint64_t key)
{
   if(!s->region_table_size)return NULL;
   size_t at=(size_t)key&(s->region_table_size-1u);
   while(s->region_table[at]) {
      StateRegion *r=&s->regions[s->region_table[at]-1u];if(r->key==key)return r;
      at=(at+1u)&(s->region_table_size-1u);
   }
   return NULL;
}
static bool region_table_prepare(ArcState *s)
{
   if((s->region_count+1u)*2u<s->region_table_size)return true;
   size_t size=s->region_table_size?s->region_table_size*2u:128u;
   uint32_t *table=calloc(size,sizeof(*table));if(!table){arc_state_fail(s,"allocation");return false;}
   for(size_t i=0;i<s->region_count;++i) {
      size_t at=(size_t)s->regions[i].key&(size-1u);while(table[at])at=(at+1u)&(size-1u);table[at]=(uint32_t)(i+1u);
   }
   free(s->region_table);s->region_table=table;s->region_table_size=size;return true;
}
static int address_order(const void *a,const void *b)
{
   uintptr_t x=(uintptr_t)(*(StateRegion*const*)a)->base,y=(uintptr_t)(*(StateRegion*const*)b)->base;
   return x<y?-1:x>y;
}
static StateRegion *address_region(ArcState *s,uintptr_t address,size_t extent)
{
   size_t lo=0,hi=s->address_count;
   while(lo<hi){size_t mid=lo+(hi-lo)/2u;if((uintptr_t)s->addresses[mid]->base<=address)lo=mid+1u;else hi=mid;}
   if(!lo)return NULL;
   StateRegion *r=s->addresses[lo-1u];uintptr_t offset=address-(uintptr_t)r->base;
   return offset<r->bytes&&extent<=r->bytes-offset?r:NULL;
}
void arc_state_region(ArcState *s,const char *name,void *base,size_t bytes)
{
   if(!s||s->error||s->pass)return;
   uint64_t key=name_key(name);
   if(region(s,key)||(!base&&bytes)) {arc_state_fail(s,"region");return;}
   if(!grow(s,(void**)&s->regions,&s->region_capacity,s->region_count+1,sizeof(*s->regions)))return;
   if(!region_table_prepare(s))return;
   StateRegion *r=&s->regions[s->region_count++];memset(r,0,sizeof(*r));
   size_t at=(size_t)key&(s->region_table_size-1u);
   while(s->region_table[at])at=(at+1u)&(s->region_table_size-1u);
   s->region_table[at]=(uint32_t)s->region_count;
   r->key=key;r->base=base;r->bytes=bytes;r->previous=base;r->previous_bytes=bytes;
}
void arc_state_object(ArcState *s,const char *name)
{ if(s&&!s->error){s->object=region(s,name_key(name));if(!s->object)arc_state_fail(s,"object");} }
static uint64_t address_key(ArcState *s,const void *p,size_t bytes)
{
   if(!s->object){arc_state_fail(s,"missing object");return 0;}
   uintptr_t a=(uintptr_t)p,b=(uintptr_t)s->object->base;
   if(a<b||a-b>s->object->bytes||bytes>s->object->bytes-(a-b)){arc_state_fail(s,"field bounds");return 0;}
   return field_key(s->object->key,(size_t)(a-b),bytes);
}
bool arc_state_measuring(const ArcState *s){return s&&s->measuring;}
bool arc_state_measure(ArcState *s,size_t bytes)
{
   if(!s||!s->measuring)return false;
   if(bytes>ARC_STATE_LIMIT-ARC_STATE_RECORD||s->output_size>ARC_STATE_LIMIT-ARC_STATE_RECORD-bytes)
      arc_state_fail(s,"static bound");
   else s->output_size+=ARC_STATE_RECORD+bytes;
   return true;
}
void arc_state_value(ArcState *s,void *p,size_t bytes)
{
   if(!s||s->error||!s->pass)return;
   if(arc_state_measure(s,bytes))return;
   StateItem *r=item(s,address_key(s,p,bytes),1,p,bytes);
   if(r&&s->loading) {
#if defined(ARCDROID_TEST_FAULTS)
      if(!s->drop_done&&s->drop_owner==s->object->key&&bytes) {
         void *decoded=NULL;const void *comparison=r->data;
         if(r->compressed) {
            decoded=malloc(bytes);if(!decoded){arc_state_fail(s,"allocation");return;}
            if(!decompress_words(decoded,bytes,r->data,r->encoded_bytes)){free(decoded);arc_state_fail(s,"compressed record");return;}
            comparison=decoded;
         }
         bool different=memcmp(p,comparison,bytes)!=0;free(decoded);
         if(different){s->drop_done=true;return;}
      }
#endif
      if(r->compressed)compressed_later(s,p,r);
      else write_later(s,p,r->data,bytes);
   }
}
void arc_state_target_range(ArcState *s,void *address,size_t bytes,ArcStateReferenceKind kind)
{
   if(!s||s->error||s->pass)return;
   if(!grow(s,(void**)&s->targets,&s->target_capacity,s->target_count+1u,sizeof(*s->targets)))return;
   s->targets[s->target_count++]=(StateTarget){address,bytes,kind};
}
void arc_state_target(ArcState *s,void *address,ArcStateReferenceKind kind)
{ arc_state_target_range(s,address,1,kind); }
static bool reference_target(ArcState *s,const StateRegion *r,size_t offset,ArcStateReferenceKind kind)
{
   if(kind==ARC_STATE_REFERENCE_ANY)return true;
   void *address=(void*)((uintptr_t)r->base+offset);
   /* Ranges are admitted only for byte payloads and register banks. */
   if(kind==ARC_STATE_REFERENCE_REGISTER) {
      const char *names[]={"engine/arculator/src/arm.c:armregs","engine/arculator/src/arm.c:userregs","engine/arculator/src/arm.c:superregs","engine/arculator/src/arm.c:fiqregs","engine/arculator/src/arm.c:irqregs"};
      for(unsigned i=0;i<5;++i)if(r->key==name_key(names[i]))return offset%4u==0;
   }
   if(kind==ARC_STATE_REFERENCE_MEMORY) {
      const char *names[]={"memory.ram","memory.rom","memory.arcrom","memory.fifth"};
      for(unsigned i=0;i<4;++i)if(r->key==name_key(names[i]))return true;
   }
   for(unsigned i=0;i<s->target_count;++i)if((uintptr_t)address>=(uintptr_t)s->targets[i].address&&(uintptr_t)address-(uintptr_t)s->targets[i].address<s->targets[i].bytes&&s->targets[i].kind==kind)return true;
   return false;
}
void arc_state_typed_reference(ArcState *s,void *slot,intptr_t bias,size_t extent,size_t alignment,ArcStateReferenceKind kind)
{
   if(!s||s->error||!s->pass)return;
   if(arc_state_measure(s,16u))return;
   uint8_t wire[16]={0};
   if(!s->loading) {
      void *value;memcpy(&value,slot,sizeof(value));
      if(value) {
         uintptr_t address=(uintptr_t)value+(uintptr_t)bias;
         StateRegion *target=address_region(s,address,extent);
         size_t offset=target?(size_t)(address-(uintptr_t)target->base):0;
         if(!target||!reference_target(s,target,offset,kind)){arc_state_fail(s,"unbound reference");return;}
         put64(wire,target->key);put64(wire+8,offset);
      }
   }
   StateItem *r=item(s,address_key(s,slot,sizeof(void*)),2,wire,sizeof(wire));
   if(r&&s->loading) {
      uint64_t key=get64(r->data),off=get64(r->data+8);void *value=NULL;
      if(key) {
         StateRegion *target=region(s,key);
         if(!target||off>=target->bytes||extent>target->bytes-off||!alignment||off%alignment||!reference_target(s,target,(size_t)off,kind)) {arc_state_fail(s,"reference bounds");return;}
         value=(void*)((uintptr_t)target->base+(uintptr_t)off-(uintptr_t)bias);
      } else if(off) {arc_state_fail(s,"null reference");return;}
      write_later(s,slot,&value,sizeof(value));
   }
}
void arc_state_reference(ArcState *s,void *slot,intptr_t bias,size_t extent,size_t alignment)
{ arc_state_typed_reference(s,slot,bias,extent,alignment,ARC_STATE_REFERENCE_ANY); }
/* String ownership is deliberately not implicit. Registered immutable strings
 * use references; session diagnostic strings use an explicit fixed buffer. */
void arc_state_string(ArcState *s,const char **slot)
{
   if(!s||s->error||!s->pass)return;
   if(arc_state_measure(s,4096u))return;
   size_t bytes=*slot?strlen(*slot)+1u:0u;
   if(bytes>4096u){arc_state_fail(s,"string bounds");return;}
   uint64_t key=address_key(s,slot,sizeof(*slot));
   StateItem *r;
   if(s->loading) {
      r=find_item(s,key);
      if(!r||r->seen||r->kind!=4||r->bytes>4096u||(r->bytes&&
         (r->data[r->bytes-1]||memchr(r->data,0,r->bytes-1)))) {arc_state_fail(s,"string bounds");return;}
      r->seen=true;
      const char *value=NULL;
      if(r->bytes) {
         StateText *t=malloc(sizeof(*t)+r->bytes);
         if(!t){arc_state_fail(s,"allocation");return;}
         memcpy(t->text,r->data,r->bytes);t->next=s->texts;s->texts=t;value=t->text;
      }
      write_later(s,slot,&value,sizeof(value));
   } else item(s,key,4,*slot,bytes);
}
void arc_state_expect(ArcState *s,const char *name,const void *value,size_t bytes)
{
   if(!s||s->error||!s->pass)return;
   StateItem *r=item(s,name_key(name),5,value,bytes);
   if(r&&s->loading&&memcmp(r->data,value,bytes))arc_state_fail(s,name);
}
void arc_state_buffer(ArcState *s,const char *name,void *value,size_t bytes)
{
   arc_state_region(s,name,value,bytes);
   if(s&&s->pass){arc_state_object(s,name);arc_state_value(s,value,bytes);}
}
void *arc_state_heap(ArcState *s,const char *name,void *slot,size_t bytes,size_t maximum)
{
   void *old=NULL;memcpy(&old,slot,sizeof(old));
   if(!s||s->error)return old;
   uint64_t key=name_key(name);
   if(s->pass){StateRegion *r=region(s,key);return r?r->base:NULL;}
   uint8_t wire[8];put64(wire,old?bytes:0);
   StateItem *saved=item(s,field_key(key,SIZE_MAX,0),3,wire,sizeof(wire));
   if(!saved)return old;
   size_t count=old?bytes:0;
   if(s->loading) {
      uint64_t n=get64(saved->data);
      if(n>maximum||n>s->decoded_limit||n>s->decoded_limit-s->heap_bytes){arc_state_fail(s,"heap bounds");return old;}
      s->heap_bytes+=(size_t)n;
      count=(size_t)n;
   }
   void *next=old;
   if(s->loading) {
      next=count?calloc(count,1):NULL;
      if(count&&!next){arc_state_fail(s,"allocation");return old;}
      if(next&&old)memcpy(next,old,count<bytes?count:bytes);
   }
   arc_state_region(s,name,next,count);
   StateRegion *r=region(s,key);
   if(!r){if(s->loading)free(next);return old;}
   r->heap=true;r->previous=old;r->previous_bytes=bytes;r->slot=slot;
   if(s->loading)write_later(s,slot,&next,sizeof(next));
   return next;
}
size_t arc_state_heap_size(ArcState *s,const char *name)
{ StateRegion *r=s?region(s,name_key(name)):NULL;return r?r->bytes:0; }
void arc_state_project(ArcState *s,void *output,const void *value,size_t bytes)
{
   memcpy(output,value,bytes);
   if(s&&s->loading) {
      StateWrite *exact=write_find(s,value,bytes);
      if(exact) {
         if(exact->compressed)(void)decompress_words(output,bytes,exact->source,exact->encoded_bytes);
         else memcpy(output,write_source(exact),bytes);
         return;
      }
      if(s->addresses) {
         StateRegion *r=address_region(s,(uintptr_t)value,bytes);
         if(r&&r->heap)return;
      }
   }
   if(s&&s->loading)for(size_t i=0;i<s->write_count;++i) {
      uintptr_t start=(uintptr_t)value,end=start+bytes;
      uintptr_t from=(uintptr_t)s->writes[i].destination,to=from+s->writes[i].bytes;
      uintptr_t first=start>from?start:from,last=end<to?end:to;
      if(first<last) {
         StateWrite *w=&s->writes[i];const uint8_t *source=write_source(w);
         if(w->compressed) {
            if(w->bytes>state_decode_capacity) {
               uint8_t *next=realloc(state_decode_arena,w->bytes);
               if(!next){arc_state_fail(s,"allocation");return;}
               state_decode_arena=next;state_decode_capacity=w->bytes;
            }
            if(!decompress_words(state_decode_arena,w->bytes,source,w->encoded_bytes)){arc_state_fail(s,"compressed record");return;}
            source=state_decode_arena;
         }
         memcpy((uint8_t*)output+(first-start),source+(first-from),last-first);
      }
   }
}
const void *arc_state_blob(ArcState *s,const char *name,const void *data,size_t bytes,size_t maximum,size_t *loaded_bytes)
{
   if(loaded_bytes)*loaded_bytes=0;
   if(!s||s->error)return NULL;
   StateItem *r;
   if(s->pass) {
      r=find_item(s,name_key(name));
      if(!r&&!s->loading)r=item(s,name_key(name),6,data,bytes);
      if(!r||r->kind!=6u||r->bytes>maximum){arc_state_fail(s,"blob bounds");return NULL;}
      if(s->loading)r->seen=true;
   }
   else if(s->loading) {
      r=find_item(s,name_key(name));
      if(!r||r->seen||r->kind!=6||r->bytes>maximum){arc_state_fail(s,"blob bounds");return NULL;}
      r->seen=true;
   } else r=item(s,name_key(name),6,data,bytes);
   /* A blob is decoded only after its owner has validated the declared size. */
   if(r&&s->loading&&r->compressed) {
      if(r->bytes>s->decoded_limit){arc_state_fail(s,"decoded bounds");return NULL;}
      r->owned=malloc(r->bytes);
      if(!r->owned||!decompress_words(r->owned,r->bytes,r->data,r->encoded_bytes)) {
         arc_state_fail(s,"blob decode");return NULL;
      }
      r->data=r->owned;r->compressed=false;r->encoded_bytes=r->bytes;
   }
   if(r&&loaded_bytes)*loaded_bytes=r->bytes;
   return r?r->data:NULL;
}
void arc_state_action(ArcState *s,void (*commit)(void *),void (*rollback)(void *),void *opaque)
{
   if(!s||!s->loading||s->error){if(rollback)rollback(opaque);return;}
   StateAction *a=malloc(sizeof(*a));
   if(!a){arc_state_fail(s,"allocation");if(rollback)rollback(opaque);return;}
   a->commit=commit;a->rollback=rollback;a->opaque=opaque;a->next=s->actions;s->actions=a;
}
bool arc_state_run(ArcState *s,ArcStateVisit visit)
{
   if(!s||!visit||s->error)return false;
   s->pass=0;visit(s);
   if(!s->error) {
      s->addresses=malloc((s->region_count?s->region_count:1u)*sizeof(*s->addresses));
      if(!s->addresses)arc_state_fail(s,"allocation");
      else {
         for(size_t i=0;i<s->region_count;++i)if(s->regions[i].base&&s->regions[i].bytes)s->addresses[s->address_count++]=&s->regions[i];
         qsort(s->addresses,s->address_count,sizeof(*s->addresses),address_order);
      }
   }
   s->pass=1;visit(s);
   if(s->loading) { for(size_t i=0;i<s->count;++i)if(!s->items[i].seen)arc_state_fail(s,"unknown field"); }
   else if(!s->error) {
      qsort(s->items,s->count,sizeof(*s->items),item_order);
      for(size_t i=1;i<s->count;++i)if(s->items[i-1].key==s->items[i].key)arc_state_fail(s,"duplicate field");
   }
   return !s->error;
}
size_t arc_state_size(const ArcState *s) { return s&&!s->error?s->output_size:0; }
/* Measure the maximum encoded fields, with schema-declared dynamic maxima;
 * never reserve inactive structs, native pointer padding or the parser item cap. */
size_t arc_state_static_bound(ArcStateVisit visit)
{
   ArcState *s=arc_state_begin(NULL,0);if(!s)return 0;
   s->registering=true;visit(s);s->registering=false;s->measuring=true;s->pass=1;visit(s);
   size_t bytes=s->error?0:s->output_size;arc_state_end(s);return bytes;
}
bool arc_state_output(ArcState *s,void *output,size_t size)
{
   if(!s||s->loading||s->error||!output||size<s->output_size||size>ARC_STATE_LIMIT||(size!=s->output_size&&size-s->output_size<16u))return false;
   /* AC-2 covers every byte returned to the frontend, including its reserved
    * capacity. Keep this canonical until a smaller complete bound is proven;
    * caller allocation contents must not become state-file contents. */
   uint8_t *p=output;memset(p,0,size);
   memcpy(p,ARC_STATE_MAGIC,8);put64(p+8,3);put64(p+16,s->output_size);put64(p+24,s->count);
   size_t at=ARC_STATE_HEADER;
   for(size_t i=0;i<s->count;++i) {
      StateItem *r=&s->items[i];put64(p+at,r->key);put64(p+at+8,(uint64_t)r->kind|(r->compressed?UINT64_C(0x100000000):0));put64(p+at+16,r->encoded_bytes);
      if(r->encoded_bytes)memcpy(p+at+ARC_STATE_RECORD,r->data,r->encoded_bytes);at+=ARC_STATE_RECORD+r->encoded_bytes;
   }
   put64(p+32,arc_crc32(p+ARC_STATE_HEADER,s->output_size-ARC_STATE_HEADER));
   if(size!=s->output_size){memcpy(p+size-16u,"ARCCAP3",8u);put64(p+size-8u,size);}
   return true;
}
bool arc_state_commit(ArcState *s)
{
   if(!s||!s->loading||s->error||s->committed)return false;
   for(size_t i=0;i<s->write_count;++i) {
      StateWrite *w=&s->writes[i];
      if(w->compressed) {
         /* Packet validation completed before any live write. */
         (void)decompress_words(w->destination,w->bytes,w->source,w->encoded_bytes);
      } else memcpy(w->destination,write_source(w),w->bytes);
   }
   for(StateAction *a=s->actions;a;a=a->next)if(a->commit)a->commit(a->opaque);
   for(size_t i=0;i<s->region_count;++i)if(s->regions[i].heap)free(s->regions[i].previous);
   while(state_text_pool){StateText *next=state_text_pool->next;free(state_text_pool);state_text_pool=next;}
   state_text_pool=s->texts;s->texts=NULL;
   s->committed=true;return true;
}
void arc_state_end(ArcState *s)
{
   if(!s)return;
   while(s->scratch){StateScratch *p=s->scratch;s->scratch=p->next;free(p);}
   while(s->actions){StateAction *a=s->actions;s->actions=a->next;if(!s->committed&&a->rollback)a->rollback(a->opaque);free(a);}
   while(s->texts){StateText *next=s->texts->next;free(s->texts);s->texts=next;}
   for(size_t i=0;i<s->count;++i)free(s->items[i].owned);
   if(s->loading&&!s->committed)for(size_t i=0;i<s->region_count;++i)if(s->regions[i].heap)free(s->regions[i].base);
   free(s->views);free(s->write_table);free(s->item_table);free(s->items);free(s->writes);free(s->regions);free(s->targets);free(s->region_table);free(s->addresses);free(s);
}

void arc_state_clear_cache(void)
{
   free(state_decode_arena);state_decode_arena=NULL;state_decode_capacity=0;
   while(state_text_pool){StateText *next=state_text_pool->next;free(state_text_pool);state_text_pool=next;}
}

void arc_state_boolean(ArcState *s,void *value,size_t bytes)
{
   if(s&&s->loading&&s->pass&&!s->error) {
      StateItem *r=find_item(s,address_key(s,value,bytes));
      if(bytes!=1u||!r||r->bytes!=1u||r->data[0]>1u)arc_state_fail(s,"boolean value");
   }
   arc_state_value(s,value,bytes);
}

void arc_state_boolean_array(ArcState *s,void *value,size_t bytes)
{
   if(s&&s->loading&&s->pass&&!s->error) {
      StateItem *r=find_item(s,address_key(s,value,bytes));
      if(!r||r->bytes!=bytes){arc_state_fail(s,"boolean extent");return;}
      const uint8_t *data=r->data;
      if(r->compressed) {
         if(bytes>state_decode_capacity) {
            uint8_t *next=realloc(state_decode_arena,bytes);
            if(!next){arc_state_fail(s,"allocation");return;}
            state_decode_arena=next;state_decode_capacity=bytes;
         }
         if(!decompress_words(state_decode_arena,bytes,data,r->encoded_bytes)){arc_state_fail(s,"compressed record");return;}
         data=state_decode_arena;
      }
      for(size_t i=0;i<bytes;++i)if(data[i]>1u){arc_state_fail(s,"boolean array value");return;}
   }
   arc_state_value(s,value,bytes);
}

void *arc_state_scratch(ArcState *s,size_t bytes)
{
   if(!s||s->error||bytes>s->decoded_limit)return NULL;
   StateScratch *p=malloc(sizeof(*p)+bytes);
   if(!p){arc_state_fail(s,"allocation");return NULL;}
   p->next=s->scratch;s->scratch=p;return p->data;
}
void arc_state_stage(ArcState *s,void *destination,const void *source,size_t bytes)
{
   if(!s||!s->loading||s->error)return;
   void *copy=arc_state_scratch(s,bytes);if(!copy)return;
   memcpy(copy,source,bytes);write_later(s,destination,copy,bytes);
}
void arc_state_reference_array(ArcState *s,void *slots,size_t count,intptr_t stride,size_t extent,size_t alignment,ArcStateReferenceKind kind)
{
   if(!s||!s->pass||s->error)return;
   if(arc_state_measure(s,count*16u))return;
   uint8_t *wire=arc_state_scratch(s,count*16u);if(!wire)return;memset(wire,0,count*16u);
   StateRegion *cached=NULL;
   if(!s->loading)for(size_t i=0;i<count;++i) {
      void *p;memcpy(&p,(uint8_t*)slots+i*sizeof(p),sizeof(p));
      if(p) {
         uintptr_t address=(uintptr_t)p+i*(uintptr_t)stride;
         StateRegion *r=cached;
         if(!r||address<(uintptr_t)r->base||address-(uintptr_t)r->base>=r->bytes||extent>r->bytes-(address-(uintptr_t)r->base))r=address_region(s,address,extent);
         size_t offset=r?address-(uintptr_t)r->base:0;
         if(!r||((r!=cached||kind!=ARC_STATE_REFERENCE_MEMORY)&&!reference_target(s,r,offset,kind))){arc_state_fail(s,"unbound reference");return;}
         cached=r;put64(wire+i*16u,r->key);put64(wire+i*16u+8u,offset);
      }
   }
   StateItem *r=item(s,address_key(s,slots,count*sizeof(void*)),2,wire,count*16u);
   if(!r||!s->loading)return;
   void **next=arc_state_scratch(s,count*sizeof(void*));if(!next)return;
   for(size_t i=0;i<count;++i) {
      uint64_t key=get64(r->data+i*16u),off=get64(r->data+i*16u+8u);next[i]=NULL;
      if(key) {
         StateRegion *target=cached&&cached->key==key?cached:region(s,key);
         if(!target||off>=target->bytes||extent>target->bytes-off||!alignment||off%alignment||((target!=cached||kind!=ARC_STATE_REFERENCE_MEMORY)&&!reference_target(s,target,(size_t)off,kind))) {arc_state_fail(s,"reference bounds");return;}
         cached=target;next[i]=(void*)((uintptr_t)target->base+(uintptr_t)off-i*(uintptr_t)stride);
      } else if(off){arc_state_fail(s,"null reference");return;}
   }
   write_later(s,slots,next,count*sizeof(void*));
}
#include "arc_state_raster.inc"

/* Only changed 512-byte sectors travel in the state. The immutable baseline is
 * rebound and fingerprinted by the owning media/store visitor. */
const void *arc_state_delta(ArcState *s,const char *name,const void *current,const void *base,size_t bytes)
{
   if(!s||s->error)return NULL;
   size_t sectors=(bytes+511u)/512u,count=0;
   if(!s->loading)for(size_t i=0;i<sectors;++i) {
      size_t n=bytes-i*512u;if(n>512u)n=512u;
      if(memcmp((const uint8_t*)current+i*512u,(const uint8_t*)base+i*512u,n))++count;
   }
   size_t capacity=16u+count*520u;uint8_t *wire=s->loading?NULL:arc_state_scratch(s,capacity);
   size_t used=16u;
   if(!s->loading&&wire) {
      put64(wire,bytes);put64(wire+8,count);
      for(size_t i=0;i<sectors;++i) {
         size_t n=bytes-i*512u;if(n>512u)n=512u;
         if(memcmp((const uint8_t*)current+i*512u,(const uint8_t*)base+i*512u,n)) {
            put64(wire+used,i);used+=8u;memcpy(wire+used,(const uint8_t*)current+i*512u,n);used+=n;
         }
      }
   }
   const uint8_t *saved=arc_state_blob(s,name,wire,used,16u+bytes+sectors*8u,&used);
   if(!s->loading||s->error)return current;
   if(used<16u||get64(saved)!=bytes||get64(saved+8)>sectors){arc_state_fail(s,"media delta header");return NULL;}
   uint8_t *next=arc_state_scratch(s,bytes);if(!next)return NULL;memcpy(next,base,bytes);
   uint64_t previous=0;size_t at=16u;count=(size_t)get64(saved+8);
#if defined(ARCDROID_TEST_FAULTS)
   bool drop=count&&s->drop_owner==name_key(name);if(drop)s->drop_done=true;
#else
   bool drop=false;
#endif
   for(size_t j=0;j<count;++j) {
      if(used-at<8u){arc_state_fail(s,"media delta bounds");return NULL;}
      uint64_t sector=get64(saved+at);at+=8u;
      if(sector>=sectors||(j&&sector<=previous)){arc_state_fail(s,"media delta order");return NULL;}
      size_t n=bytes-(size_t)sector*512u;if(n>512u)n=512u;
      if(n>used-at){arc_state_fail(s,"media delta bounds");return NULL;}
      if(!drop)memcpy(next+(size_t)sector*512u,saved+at,n);at+=n;previous=sector;
   }
   if(at!=used){arc_state_fail(s,"media delta trailing bytes");return NULL;}
   return next;
}

const void *arc_state_view(ArcState *s,const char *name,const void *data,size_t bytes,size_t *view_bytes)
{
   if(view_bytes)*view_bytes=0;if(!s||s->error)return NULL;
   uint64_t key=name_key(name);
   for(size_t i=0;i<s->view_count;++i)if(s->views[i].key==key) {
      if(view_bytes)*view_bytes=s->views[i].bytes;return s->views[i].data;
   }
   if(s->pass){arc_state_fail(s,"missing media view");return NULL;}
   if(!grow(s,(void**)&s->views,&s->view_capacity,s->view_count+1u,sizeof(*s->views)))return NULL;
   s->views[s->view_count++]=(StateView){key,data,bytes};if(view_bytes)*view_bytes=bytes;return data;
}
