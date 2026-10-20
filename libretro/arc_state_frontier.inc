#include "arc_io.h"
/* ArcDroid durable CMOS/raw frontiers. GPL-2.0-only.
 * Two validated slots hold a range redo. An intent is durable before the
 * changed range is written, then an applied record acknowledges its flush.
 * ACFR2 full-image records remain readable. ACFR3 carries offset, resulting
 * sector fingerprint and changed bytes; checkpoint records carry only a hash.
 * A reopened lazy disc invalidates its checkpoint once before ordinary native
 * writes, and seals a new one on capture. No state can name that unsealed epoch. */
#define STATE_FRONTIERS 68u
#define STATE_FRONTIER_HEADER 160u
typedef struct {
   char path[4096],stem[4096],identity[41];
   uint64_t generation;
   ArcDigest origin;
   unsigned active;
   bool failed,lazy,tracking_failed,captured,unsealed;
   uint8_t *untracked;uint16_t *untracked_index;
   size_t untracked_bytes,untracked_count;uint64_t fingerprint;
} StateFrontier;
static StateFrontier state_frontiers[STATE_FRONTIERS];
static unsigned state_frontier_count;
static uint64_t state_frontier_revision;
static uint64_t frontier_get64(const uint8_t *p)
{ uint64_t n=0;for(unsigned i=0;i<8u;++i)n|=(uint64_t)p[i]<<(8u*i);return n; }
static void frontier_put64(uint8_t *p,uint64_t n)
{ for(unsigned i=0;i<8u;++i)p[i]=(uint8_t)(n>>(8u*i)); }
static StateFrontier *state_frontier_find(const char *path)
{ for(unsigned i=0;i<state_frontier_count;++i)if(!strcmp(state_frontiers[i].path,path))return &state_frontiers[i];return NULL; }
static bool state_frontier_cached(const char *path,const uint8_t **data,size_t *bytes,bool clean)
{
   StateFrontier *f=state_frontier_find(path);
   if(!f||!f->untracked||(clean&&(f->failed||f->tracking_failed||f->untracked_count)))return false;
   /* After a partial publication the cache can already contain a prefix of
    * the queued writes. Overlaying the complete retained log is idempotent. */
   if(data)*data=f->untracked;*bytes=f->untracked_bytes;return true;
}
static void state_frontier_bind(StateHandle *handle)
{
   StateFrontier *f=state_frontier_find(handle->path);
   handle->frontier_slot=f?(unsigned)(f-state_frontiers):STATE_FRONTIERS;
   handle->frontier_revision=state_frontier_revision;
}
static bool state_frontier_known(StateHandle *handle)
{
   if(handle->frontier_revision!=state_frontier_revision)state_frontier_bind(handle);
   StateFrontier *f=handle->frontier_slot<state_frontier_count?&state_frontiers[handle->frontier_slot]:NULL;
   return f&&(!f->lazy||f->captured||f->failed);
}
static bool frontier_bound(const StateFrontier *f)
{
   for(unsigned i=0;i<68u;++i)if(state_io_bindings[i][0]&&!strcmp(f->path,state_io_bindings[i]))return true;
   return false;
}
static bool frontier_pending(const StateFrontier *f)
{
   for(StateOperation *op=state_io_head;op;op=op->next)
      if(!strcmp(op->path,f->path)||(op->kind==5&&!strcmp(op->target,f->path)))return true;
   return false;
}
/* A labelled sector fingerprint is an accidental-edit check for the initial
 * frontier, not an authentication mechanism. Updating it visits only sectors
 * touched by a write. It also checks external edits on a cold reopen/hot load. */
static uint64_t frontier_sector_fingerprint(const uint8_t *data,size_t bytes,size_t sector)
{
   uint64_t h=UINT64_C(0x9e3779b97f4a7c15)^(uint64_t)sector;
   for(size_t i=0;i<bytes;){
      uint64_t word=0;size_t n=bytes-i;if(n>8u)n=8u;memcpy(&word,data+i,n);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
      word=__builtin_bswap64(word);
#endif
      h=(h^word)*UINT64_C(0x100000001b3);i+=n;
   }
   h^=h>>33u;h*=UINT64_C(0xff51afd7ed558ccd);h^=h>>33u;return h;
}
static uint64_t frontier_fingerprint(const uint8_t *data,size_t bytes)
{
   uint64_t h=0;for(size_t at=0;at<bytes;at+=512u){size_t n=bytes-at;if(n>512u)n=512u;h^=frontier_sector_fingerprint(data+at,n,at/512u);}return h;
}
static void state_frontier_observe(StateHandle *handle,int64_t at,const void *data,size_t bytes,bool truncate)
{
   StateFrontier *f=handle->frontier_slot<state_frontier_count?&state_frontiers[handle->frontier_slot]:NULL;
   if(!f||!f->lazy||f->captured||f->tracking_failed||f->failed)return;
   if(truncate||at<0||(uint64_t)at>f->untracked_bytes||bytes>f->untracked_bytes-(size_t)at){f->tracking_failed=true;return;}
   if(!bytes)return;
   size_t sectors=(f->untracked_bytes+511u)/512u;
   uint8_t *flags=(uint8_t *)(f->untracked_index+sectors);
   size_t last=((size_t)at+bytes-1u)/512u;
   for(size_t sector=(size_t)at/512u;sector<=last;++sector)if(!flags[sector]){
      flags[sector]=1;f->untracked_index[f->untracked_count++]=(uint16_t)sector;
   }
   (void)data; /* Read only these written sectors when the first state is used. */
}
static bool frontier_write_all(const char *path,const void *data,size_t size)
{
   if(!state_io_native||!state_io_native->open||!state_io_native->write||!state_io_native->flush||!state_io_native->close)return false;
   struct retro_vfs_file_handle *f=state_io_native->open(path,RETRO_VFS_FILE_ACCESS_WRITE,0);
   if(!f)return false;
   bool okay=state_io_native->write(f,data,size)==(int64_t)size&&state_io_native->flush(f)==0;
   if(state_io_native->close(f))okay=false;return okay;
}
static uint32_t frontier_header_crc(const uint8_t *record)
{
   uint8_t header[144];memcpy(header,record,80);memcpy(header+80,record+96,64);
   return arc_crc32(header,sizeof(header));
}
static void frontier_origin_write(uint8_t *record,const ArcDigest *origin)
{
   memset(record+96,0,64);memcpy(record+96,origin->sha1,40);
   frontier_put64(record+136,origin->crc32);frontier_put64(record+144,origin->size);
}
static bool frontier_publish(StateFrontier *f,uint8_t *record,size_t bytes)
{
   char candidate[4096],slot[4096];
   unsigned next=f->active^1u;
   int n=snprintf(candidate,sizeof(candidate),"%s.tmp",f->stem);
   if(n<0||(size_t)n>=sizeof(candidate))return false;
   n=snprintf(slot,sizeof(slot),"%s.%c",f->stem,next?'b':'a');
   if(n<0||(size_t)n>=sizeof(slot))return false;
   frontier_put64(record+88,arc_crc32(record+STATE_FRONTIER_HEADER,bytes-STATE_FRONTIER_HEADER));
   frontier_put64(record+80,frontier_header_crc(record));
   if(!frontier_write_all(candidate,record,bytes))return false;
   uint8_t *readback=NULL;size_t length=0;
   bool okay=state_read_native(candidate,&readback,&length)&&length==bytes&&!memcmp(record,readback,bytes);
   free(readback);if(!okay)return false;
   int32_t size=0;int32_t flags=state_io_native->stat?state_io_native->stat(slot,&size):0;
   if((flags&RETRO_VFS_STAT_IS_VALID)&&state_io_native->remove(slot))return false;
   if(!state_io_native->rename||state_io_native->rename(candidate,slot))return false;
   f->active=next;f->generation=frontier_get64(record+8);return true;
}
static bool frontier_parse(const StateFrontier *f,const uint8_t *p,size_t bytes)
{
   if(bytes<STATE_FRONTIER_HEADER)return false;
   bool origin=p[96]!=0,range=!memcmp(p,"ACFR3\0\0\0",8);
   for(unsigned i=96;i<136u;++i) {
      if(origin?(!((p[i]>='0'&&p[i]<='9')||(p[i]>='a'&&p[i]<='f'))):p[i]!=0)return false;
   }
   uint64_t kind=frontier_get64(p+16),extent=frontier_get64(p+32);
   if(frontier_get64(p+136)>UINT32_MAX||frontier_get64(p+144)>STATE_IO_BYTES||
      (!range&&memcmp(p,"ACFR2\0\0\0",8))||
      memcmp(p+40,f->identity,40)||frontier_get64(p+24)!=bytes-STATE_FRONTIER_HEADER||
      !frontier_get64(p+8)||kind<1u||kind>(range?4u:3u)||
      (kind>=3u&&bytes!=STATE_FRONTIER_HEADER+8u)||
      extent>STATE_IO_BYTES||frontier_get64(p+24)>STATE_IO_BYTES)return false;
   if(range&&kind<=2u) {
      if(bytes<STATE_FRONTIER_HEADER+16u||frontier_get64(p+152)>1u)return false;
      uint64_t at=frontier_get64(p+STATE_FRONTIER_HEADER),n=bytes-STATE_FRONTIER_HEADER-16u;
      if(at>extent||n>extent-at||(frontier_get64(p+152)&&at+n!=extent))return false;
   }
   return frontier_get64(p+80)==frontier_header_crc(p)&&
      frontier_get64(p+88)==arc_crc32(p+STATE_FRONTIER_HEADER,bytes-STATE_FRONTIER_HEADER);
}
static bool frontier_checkpoint(StateFrontier *f,uint64_t generation,unsigned kind)
{
   uint8_t record[STATE_FRONTIER_HEADER+8u]={0};
   memcpy(record,"ACFR3\0\0\0",8);frontier_put64(record+8,generation);frontier_put64(record+16,kind);
   frontier_put64(record+24,8);frontier_put64(record+32,f->untracked_bytes);memcpy(record+40,f->identity,40);
   frontier_origin_write(record,&f->origin);frontier_put64(record+STATE_FRONTIER_HEADER,f->fingerprint);
   char directory[4096];int n=snprintf(directory,sizeof(directory),"%s/arcdroid/state-stores",state_io_root_path);
   return n>0&&(size_t)n<sizeof(directory)&&arc_io_make_path(directory)&&frontier_publish(f,record,sizeof(record));
}
/* No state exists at an unsealed generation. Invalidate once BEFORE the first
 * native mutation, including after a cold reopen. Subsequent writes retain
 * main's seek/write/flush path; dirty sectors are read only at capture. */
static bool state_frontier_prepare(StateHandle *handle)
{
   StateFrontier *f=handle->frontier_slot<state_frontier_count?&state_frontiers[handle->frontier_slot]:NULL;
   if(!f)return true;
   if(f->failed)return false;
   if(!f->lazy||f->captured||!f->generation||f->unsealed)return true;
   if(f->generation==UINT64_MAX||!frontier_checkpoint(f,f->generation+1u,4u)){f->failed=true;return false;}
   f->unsealed=true;return true;
}
static bool frontier_same(const uint8_t *record,size_t length,const uint8_t *data,size_t bytes)
{
   unsigned kind=(unsigned)frontier_get64(record+16);
   if(kind==4u)return true; /* Deliberately unsealed; cannot be loaded. */
   bool range=!memcmp(record,"ACFR3\0\0\0",8);
   if(kind==3u||range)return bytes==frontier_get64(record+32)&&frontier_fingerprint(data,bytes)==
      frontier_get64(record+STATE_FRONTIER_HEADER+(kind==3u?0u:8u));
   return bytes==length-STATE_FRONTIER_HEADER&&!memcmp(data,record+STATE_FRONTIER_HEADER,bytes);
}
void arc_state_io_forget_stores(void)
{ for(unsigned i=0;i<state_frontier_count;++i){free(state_frontiers[i].untracked);free(state_frontiers[i].untracked_index);}
  memset(state_frontiers,0,sizeof(state_frontiers));state_frontier_count=0;++state_frontier_revision; }
bool arc_state_io_store(const char *path,const char *identity)
{
   if(!path||!path[0]||!identity||!state_io_root_path[0])return false;
   StateFrontier *f=state_frontier_find(path);if(f)return !f->failed;
   if(state_frontier_count>=STATE_FRONTIERS){
      for(unsigned i=0;i<state_frontier_count;++i)if(!frontier_bound(&state_frontiers[i])&&!frontier_pending(&state_frontiers[i])){
         f=&state_frontiers[i];free(f->untracked);free(f->untracked_index);break;
      }
      if(!f)return false;
   }else f=&state_frontiers[state_frontier_count++];
   ++state_frontier_revision;memset(f,0,sizeof(*f));f->active=1;
   if(!state_path(f->path,path)){f->failed=true;return false;}
   arc_sha1_hex(identity,strlen(identity),f->identity);
   int n=snprintf(f->stem,sizeof(f->stem),"%s/arcdroid/state-stores/%s",state_io_root_path,f->identity);
   if(n<0||(size_t)n>=sizeof(f->stem)){f->failed=true;return false;}
   uint8_t *records[2]={NULL,NULL};size_t lengths[2]={0,0};bool valid[2]={false,false},present[2]={false,false};
   for(unsigned slot=0;slot<2;++slot) {
      char name[4096];n=snprintf(name,sizeof(name),"%s.%c",f->stem,slot?'b':'a');
      if(n<0||(size_t)n>=sizeof(name)){f->failed=true;break;}
      int32_t size=0;int32_t flags=state_io_native->stat?state_io_native->stat(name,&size):0;
      present[slot]=(flags&RETRO_VFS_STAT_IS_VALID)!=0;
      if(present[slot])valid[slot]=state_read_native(name,&records[slot],&lengths[slot])&&frontier_parse(f,records[slot],lengths[slot]);
   }
   unsigned newest=0;
   if(valid[1]&&(!valid[0]||frontier_get64(records[1]+8)>frontier_get64(records[0]+8)||
      (frontier_get64(records[1]+8)==frontier_get64(records[0]+8)&&frontier_get64(records[1]+16)>frontier_get64(records[0]+16))))newest=1;
   if(valid[newest]) {
      uint8_t *record=records[newest];f->generation=frontier_get64(record+8);f->active=newest;
      memcpy(f->origin.sha1,record+96,40);f->origin.sha1[40]=0;
      f->origin.crc32=(uint32_t)frontier_get64(record+136);f->origin.size=(size_t)frontier_get64(record+144);
      uint64_t kind=frontier_get64(record+16);
      bool range=!memcmp(record,"ACFR3\0\0\0",8);
      uint8_t *current=NULL;size_t bytes=0;
      bool read=state_read_native(path,&current,&bytes);
      if(kind==1u) {
         bool okay;
         if(range) {
            size_t at=(size_t)frontier_get64(record+STATE_FRONTIER_HEADER),n=lengths[newest]-STATE_FRONTIER_HEADER-16u;
            unsigned mode=RETRO_VFS_FILE_ACCESS_WRITE;
            if(!frontier_get64(record+152))mode|=RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING;
            struct retro_vfs_file_handle *file=state_io_native->open(path,mode,0);
            okay=file&&state_io_native->seek(file,(int64_t)at,RETRO_VFS_SEEK_POSITION_START)>=0&&
               state_io_native->write(file,record+STATE_FRONTIER_HEADER+16u,n)==(int64_t)n&&state_io_native->flush(file)==0;
            if(file&&state_io_native->close(file))okay=false;
         } else okay=frontier_write_all(path,record+STATE_FRONTIER_HEADER,lengths[newest]-STATE_FRONTIER_HEADER);
         free(current);current=NULL;bytes=0;
         read=okay&&state_read_native(path,&current,&bytes);
         if(!read||!frontier_same(record,lengths[newest],current,bytes))f->failed=true;
         if(!f->failed){frontier_put64(record+16,2);if(!frontier_publish(f,record,lengths[newest]))f->failed=true;}
      } else if(kind!=4u&&(!read||!frontier_same(record,lengths[newest],current,bytes))) {
         /* Adopt an external edit on normal reopen at a NEW frontier. Never
          * replay an acknowledged redo over the user's edited image. */
         int32_t size=0;int32_t flags=state_io_native->stat?state_io_native->stat(path,&size):0;
         if((!read&&(flags&RETRO_VFS_STAT_IS_VALID))||f->generation==UINT64_MAX)f->failed=true;
         else {
            memset(&f->origin,0,sizeof(f->origin));f->untracked_bytes=bytes;f->fingerprint=frontier_fingerprint(current,bytes);
            if(!frontier_checkpoint(f,f->generation+1u,3u))f->failed=true;
         }
      }
      f->unsealed=kind==4u;
      f->untracked=current;f->untracked_bytes=bytes;f->fingerprint=frontier_fingerprint(current,bytes);
      /* No checkpoint acknowledged the subsequent native writes. Their bytes
       * (or an external replacement) now establish Original-media identity,
       * just as on the ordinary no-state path. Never bind a replacement to
       * the previous content's CMOS or immutable base. */
      if(f->unsealed&&(bytes!=frontier_get64(record+32)||
         f->fingerprint!=frontier_get64(record+STATE_FRONTIER_HEADER)))memset(&f->origin,0,sizeof(f->origin));
   } else if(present[0]||present[1])f->failed=true;
   free(records[0]);free(records[1]);
   if(!f->untracked&&!f->failed) {
      if(!state_read_native(path,&f->untracked,&f->untracked_bytes)) {
         int32_t size=0;if(state_io_native->stat&&(state_io_native->stat(path,&size)&RETRO_VFS_STAT_IS_VALID))f->failed=true;
      }
      f->fingerprint=frontier_fingerprint(f->untracked,f->untracked_bytes);
   }
   return !f->failed;
}
bool arc_state_io_store_lazy(const char *path,const char *identity)
{
   if(!arc_state_io_store(path,identity))return false;
   StateFrontier *f=state_frontier_find(path);if(f->lazy)return !f->failed;
   f->lazy=true;
   size_t sectors=(f->untracked_bytes+511u)/512u;
   f->untracked_index=calloc(sectors?sectors:1u,3u);
   if(!f->untracked_index)f->tracking_failed=true;
   return !f->failed;
}
static bool frontier_activate(StateFrontier *f)
{
   if(!f->lazy||f->captured)return !f->failed;
   if(f->tracking_failed)return false;
   if(f->generation&&!f->unsealed&&!f->untracked_count){f->captured=true;return !f->failed;}
   if(f->untracked_count){
      struct retro_vfs_file_handle *file=state_io_native->open(f->path,RETRO_VFS_FILE_ACCESS_READ,0);
      bool okay=file!=NULL;
      for(size_t i=0;okay&&i<f->untracked_count;++i){
         size_t sector=f->untracked_index[i],at=sector*512u,n=f->untracked_bytes-at;if(n>512u)n=512u;
         f->fingerprint^=frontier_sector_fingerprint(f->untracked+at,n,sector);
         okay=state_io_native->seek&&state_io_native->seek(file,(int64_t)at,RETRO_VFS_SEEK_POSITION_START)>=0&&
            state_io_native->read(file,f->untracked+at,n)==(int64_t)n;
         f->fingerprint^=frontier_sector_fingerprint(f->untracked+at,n,sector);
      }
      if(file&&state_io_native->close(file))okay=false;
      if(!okay){f->tracking_failed=true;return false;}
   }
   if(f->generation==UINT64_MAX)return false;
   bool speculative=state_io_speculating;state_io_speculating=false;
   bool okay=frontier_checkpoint(f,f->generation+1u,3u);
   state_io_speculating=speculative;
   if(!okay)f->failed=true;
   else {f->captured=true;f->unsealed=false;}
   free(f->untracked_index);f->untracked_index=NULL;f->untracked_count=0;return okay;
}
static int64_t state_frontier_write(StateHandle *handle,const void *data,size_t bytes)
{
   StateFrontier *f=handle->frontier_slot<state_frontier_count?&state_frontiers[handle->frontier_slot]:NULL;
   if(!f||f->failed||f->generation==UINT64_MAX||handle->position<0||bytes>STATE_IO_BYTES||
      (uint64_t)handle->position>STATE_IO_BYTES-bytes)return -1;
   size_t at=(size_t)handle->position,after=at+bytes;
   if(!handle->needs_truncate&&after<f->untracked_bytes)after=f->untracked_bytes;
   /* Only a resize/truncate changes allocation. Fixed-size guest sectors patch
    * the native cache and fingerprint in place, touching the changed sectors. */
   size_t first=at/512u,last=(at+bytes+511u)/512u;
   uint64_t fingerprint=f->fingerprint;
   if(handle->needs_truncate) {
      uint8_t *next=calloc(after?after:1u,1);if(!next)return -1;
      free(f->untracked);f->untracked=next;f->untracked_bytes=after;
      fingerprint=0;first=0;last=(after+511u)/512u;
   } else {
      if(after>f->untracked_bytes) {
         first=f->untracked_bytes/512u;if(at/512u<first)first=at/512u;
      }
      for(size_t i=first;i<last&&i*512u<f->untracked_bytes;++i){
         size_t n=f->untracked_bytes-i*512u;if(n>512u)n=512u;
         fingerprint^=frontier_sector_fingerprint(f->untracked+i*512u,n,i);
      }
      if(after>f->untracked_bytes) {
         uint8_t *next=realloc(f->untracked,after);if(!next)return -1;
         memset(next+f->untracked_bytes,0,after-f->untracked_bytes);f->untracked=next;f->untracked_bytes=after;
      }
   }
   if(bytes)memcpy(f->untracked+at,data,bytes);
   for(size_t i=first;i<last;++i){size_t n=after-i*512u;if(n>512u)n=512u;
      fingerprint^=frontier_sector_fingerprint(f->untracked+i*512u,n,i);}
   size_t length=STATE_FRONTIER_HEADER+16u+bytes;
   uint8_t *record=calloc(length,1);if(!record){f->failed=true;return -1;}
   memcpy(record,"ACFR3\0\0\0",8);frontier_put64(record+8,f->generation+1u);frontier_put64(record+16,1);
   frontier_put64(record+24,16u+bytes);frontier_put64(record+32,after);memcpy(record+40,f->identity,40);
   frontier_origin_write(record,&f->origin);frontier_put64(record+152,handle->needs_truncate?1u:0u);
   frontier_put64(record+STATE_FRONTIER_HEADER,at);frontier_put64(record+STATE_FRONTIER_HEADER+8u,fingerprint);
   if(bytes)memcpy(record+STATE_FRONTIER_HEADER+16u,data,bytes);
   char directory[4096];int n=snprintf(directory,sizeof(directory),"%s/arcdroid/state-stores",state_io_root_path);
   bool okay=n>0&&(size_t)n<sizeof(directory)&&!state_io_speculating&&arc_io_make_path(directory);
   if(okay)okay=frontier_publish(f,record,length);
   if(okay) {
      if(handle->needs_truncate) {
         if(handle->native)state_io_native->close(handle->native);
         handle->native=state_io_native->open(handle->path,handle->mode&~RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING,0);
      }
      okay=handle->native&&state_io_native->seek(handle->native,(int64_t)at,RETRO_VFS_SEEK_POSITION_START)>=0&&
         state_io_native->write(handle->native,data,bytes)==(int64_t)bytes&&state_io_native->flush(handle->native)==0;
      if(okay)handle->native_position=handle->position+(int64_t)bytes;
   }
   if(okay){frontier_put64(record+16,2);okay=frontier_publish(f,record,length);}
   if(okay&&state_io_commit_observer)state_io_commit_observer(handle->path,f->untracked,after);
   free(record);
   if(!okay){f->failed=true;return -1;}
   f->fingerprint=fingerprint;handle->position+=(int64_t)bytes;handle->needs_truncate=false;
   return handle->native?(int64_t)bytes:-1;
}
static bool frontier_current(const StateFrontier *f,uint64_t *generation)
{
   *generation=0;bool present_any=false;uint8_t *newest=NULL;size_t newest_size=0;
   for(unsigned slot=0;slot<2u;++slot) {
      char path[4096];int n=snprintf(path,sizeof(path),"%s.%c",f->stem,slot?'b':'a');
      if(n<0||(size_t)n>=sizeof(path)){free(newest);return false;}
      int32_t size=0;int32_t flags=state_io_native->stat?state_io_native->stat(path,&size):0;
      if(!(flags&RETRO_VFS_STAT_IS_VALID))continue;
      present_any=true;uint8_t *bytes=NULL;size_t length=0;
      bool valid=state_read_native(path,&bytes,&length)&&frontier_parse(f,bytes,length);
      if(valid) {
         uint64_t candidate=frontier_get64(bytes+8);
         if(!newest||candidate>*generation||(candidate==*generation&&frontier_get64(bytes+16)>frontier_get64(newest+16))) {
            free(newest);newest=bytes;bytes=NULL;newest_size=length;*generation=candidate;
         }
      }
      free(bytes);
   }
   bool okay=!present_any;
   if(newest&&(frontier_get64(newest+16)==2u||frontier_get64(newest+16)==3u)) {
      uint8_t *target=NULL;size_t bytes=0;
      bool read=state_read_native(f->path,&target,&bytes);
      okay=read&&frontier_same(newest,newest_size,target,bytes);
      free(target);
   }
   free(newest);return okay&&*generation>=f->generation;
}
static void frontier_restore_commit(void *opaque)
{
   StateFrontier *f=opaque;f->captured=true;
   free(f->untracked_index);f->untracked_index=NULL;f->untracked_count=0;
}
void arc_state_io_frontiers(ArcState *s)
{
   if(arc_state_error(s))return;
   unsigned count=0;for(unsigned i=0;i<state_frontier_count;++i)if(frontier_bound(&state_frontiers[i]))++count;
   arc_state_expect(s,"frontier.count",&count,sizeof(count));
   for(unsigned i=0;i<state_frontier_count;++i) {
      char name[96];StateFrontier *f=&state_frontiers[i];if(!frontier_bound(f))continue;
      if(!arc_state_loading(s)&&!arc_state_pass(s)&&!frontier_activate(f)){arc_state_fail(s,"store frontier activation");return;}
      snprintf(name,sizeof(name),"frontier.%s",f->identity);
      uint64_t generation=f->generation;
      if(arc_state_loading(s)&&arc_state_pass(s)&&!frontier_current(f,&generation))
         arc_state_fail(s,"store frontier unreadable");
      arc_state_expect(s,name,&generation,sizeof(generation));
      if(arc_state_loading(s)&&arc_state_pass(s)&&f->lazy)
         arc_state_action(s,frontier_restore_commit,NULL,f);
      if(f->failed)arc_state_fail(s,"store frontier unavailable");
   }
}

bool arc_state_io_original(const char *path,ArcDigest *identity)
{
   char name[4352];int n=snprintf(name,sizeof(name),"RAW-ORIGINAL:%s",path);
   if(n<0||(size_t)n>=sizeof(name)||!arc_state_io_store(path,name))return false;
   StateFrontier *f=state_frontier_find(path);
   if(f->origin.sha1[0])*identity=f->origin;
   else f->origin=*identity;
   return true;
}

bool arc_state_io_recover_original(const char *path)
{
   if(!path||!path[0])return true;
   char identity[4352],digest[41],slot[4096];
   int n=snprintf(identity,sizeof(identity),"RAW-ORIGINAL:%s",path);
   if(n<0||(size_t)n>=sizeof(identity))return false;
   arc_sha1_hex(identity,strlen(identity),digest);
   for(unsigned i=0;i<2u;++i) {
      n=snprintf(slot,sizeof(slot),"%s/arcdroid/state-stores/%s.%c",state_io_root_path,digest,i?'b':'a');
      if(n<0||(size_t)n>=sizeof(slot))return false;
      int32_t size=0;int32_t flags=state_io_native&&state_io_native->stat?state_io_native->stat(slot,&size):0;
      if(flags&RETRO_VFS_STAT_IS_VALID)return arc_state_io_store(path,identity);
   }
   return true;
}

#if defined(ARCDROID_TEST_FAULTS)
bool arc_state_io_test_generation(const char *path,uint64_t *generation)
{
   StateFrontier *f=state_frontier_find(path);
   return f&&!f->failed&&generation&&frontier_current(f,generation);
}
#endif
