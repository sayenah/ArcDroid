#include "arc_io.h"
/* ArcDroid durable CMOS/raw frontiers. GPL-2.0-only.
 * Two validated slots hold a full redo image. An intent is durable before a
 * target can be truncated or written. An applied record of the same generation
 * acknowledges the flush. Recovery replays a pending intent; an acknowledged
 * image changed outside this protocol fails closed. */
#define STATE_FRONTIERS 68u
#define STATE_FRONTIER_HEADER 160u
typedef struct {
   char path[4096],stem[4096],identity[41];
   uint64_t generation;
   ArcDigest origin;
   unsigned active;
   bool failed;
} StateFrontier;
static StateFrontier state_frontiers[STATE_FRONTIERS];
static unsigned state_frontier_count;
static uint64_t frontier_get64(const uint8_t *p)
{ uint64_t n=0;for(unsigned i=0;i<8u;++i)n|=(uint64_t)p[i]<<(8u*i);return n; }
static void frontier_put64(uint8_t *p,uint64_t n)
{ for(unsigned i=0;i<8u;++i)p[i]=(uint8_t)(n>>(8u*i)); }
static StateFrontier *state_frontier_find(const char *path)
{ for(unsigned i=0;i<state_frontier_count;++i)if(!strcmp(state_frontiers[i].path,path))return &state_frontiers[i];return NULL; }
static bool state_frontier_known(const char *path) { return state_frontier_find(path)!=NULL; }
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
   bool origin=p[96]!=0;
   for(unsigned i=96;i<136u;++i) {
      if(origin?(!((p[i]>='0'&&p[i]<='9')||(p[i]>='a'&&p[i]<='f'))):p[i]!=0)return false;
   }
   if(frontier_get64(p+136)>UINT32_MAX||frontier_get64(p+144)>STATE_IO_BYTES)return false;
   if(memcmp(p,"ACFR2\0\0\0",8)||
      memcmp(p+40,f->identity,40)||frontier_get64(p+24)!=bytes-STATE_FRONTIER_HEADER||
      !frontier_get64(p+8)||frontier_get64(p+16)<1||frontier_get64(p+16)>2||
      frontier_get64(p+32)>STATE_IO_BYTES||frontier_get64(p+24)>STATE_IO_BYTES)return false;
   return frontier_get64(p+80)==frontier_header_crc(p)&&
      frontier_get64(p+88)==arc_crc32(p+STATE_FRONTIER_HEADER,bytes-STATE_FRONTIER_HEADER);
}
void arc_state_io_forget_stores(void)
{ memset(state_frontiers,0,sizeof(state_frontiers));state_frontier_count=0; }
bool arc_state_io_store(const char *path,const char *identity)
{
   if(!path||!path[0]||!identity||!state_io_root_path[0])return false;
   StateFrontier *f=state_frontier_find(path);if(f)return !f->failed;
   if(state_frontier_count>=STATE_FRONTIERS)return false;
   f=&state_frontiers[state_frontier_count++];memset(f,0,sizeof(*f));f->active=1;
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
      size_t expected=lengths[newest]-STATE_FRONTIER_HEADER;uint8_t *current=NULL;size_t bytes=0;
      bool read=state_read_native(path,&current,&bytes);
      bool same=read&&bytes==expected&&!memcmp(current,record+STATE_FRONTIER_HEADER,expected);
      if(!same) {
         if(frontier_get64(record+16)==2) {
            /* A normal content reopen may observe an external edit (including
             * a CMOS record which the existing seed/healing path will repair).
             * Adopt those bytes at a new frontier; never overwrite the edit
             * from the previous acknowledged redo image. */
            int32_t size=0;int32_t flags=state_io_native->stat?state_io_native->stat(path,&size):0;
            if((!read&&(flags&RETRO_VFS_STAT_IS_VALID))||f->generation==UINT64_MAX)f->failed=true;
            else {
               uint8_t *observed=calloc(STATE_FRONTIER_HEADER+bytes,1);
               if(!observed)f->failed=true;
               else {
                  memcpy(observed,record,STATE_FRONTIER_HEADER);frontier_put64(observed+8,f->generation+1u);
                  memset(&f->origin,0,sizeof(f->origin));frontier_origin_write(observed,&f->origin);
                  frontier_put64(observed+24,bytes);frontier_put64(observed+32,bytes);
                  if(bytes)memcpy(observed+STATE_FRONTIER_HEADER,current,bytes);
                  if(!frontier_publish(f,observed,STATE_FRONTIER_HEADER+bytes))f->failed=true;
                  free(observed);
               }
            }
         } else if(!frontier_write_all(path,record+STATE_FRONTIER_HEADER,expected))f->failed=true;
      }
      free(current);
      if(!f->failed&&frontier_get64(record+16)==1) {
         frontier_put64(record+16,2);if(!frontier_publish(f,record,lengths[newest]))f->failed=true;
      }
   } else if(present[0]||present[1])f->failed=true;
   free(records[0]);free(records[1]);return !f->failed;
}
static int64_t state_frontier_write(StateHandle *handle,const void *data,size_t bytes)
{
   StateFrontier *f=state_frontier_find(handle->path);
   if(!f||f->failed||f->generation==UINT64_MAX||handle->position<0||bytes>STATE_IO_BYTES||
      (uint64_t)handle->position>STATE_IO_BYTES-bytes)return -1;
   uint8_t *old=NULL;size_t old_bytes=0;
   if(!handle->needs_truncate&&!state_read_native(handle->path,&old,&old_bytes))return -1;
   size_t after=(size_t)handle->position+bytes;
   if(!handle->needs_truncate&&after<old_bytes)after=old_bytes;
   uint8_t *record=calloc(STATE_FRONTIER_HEADER+after,1);if(!record){free(old);return -1;}
   memcpy(record,"ACFR2\0\0\0",8);frontier_put64(record+8,f->generation+1u);frontier_put64(record+16,1);
   frontier_put64(record+24,after);frontier_put64(record+32,old_bytes);memcpy(record+40,f->identity,40);
   frontier_origin_write(record,&f->origin);
   if(old_bytes)memcpy(record+STATE_FRONTIER_HEADER,old,old_bytes);
   if(bytes)memcpy(record+STATE_FRONTIER_HEADER+(size_t)handle->position,data,bytes);
   free(old);
   char directory[4096];int n=snprintf(directory,sizeof(directory),"%s/arcdroid/state-stores",state_io_root_path);
   bool okay=n>0&&(size_t)n<sizeof(directory)&&!state_io_speculating;
   /* Directory preparation cannot modify the target bytes. */
   if(okay)okay=arc_io_make_path(directory);
   if(okay)okay=frontier_publish(f,record,STATE_FRONTIER_HEADER+after);
   if(okay) {
      if(handle->needs_truncate) {
         if(handle->native)state_io_native->close(handle->native);
         handle->native=state_io_native->open(handle->path,handle->mode,0);
      }
      okay=handle->native&&state_io_native->seek(handle->native,0,RETRO_VFS_SEEK_POSITION_START)>=0&&
         state_io_native->write(handle->native,record+STATE_FRONTIER_HEADER,after)==(int64_t)after&&
         state_io_native->flush(handle->native)==0;
      if(okay)handle->native_position=(int64_t)after;
   }
   if(okay){frontier_put64(record+16,2);okay=frontier_publish(f,record,STATE_FRONTIER_HEADER+after);}
   if(okay&&state_io_commit_observer)
      state_io_commit_observer(handle->path,record+STATE_FRONTIER_HEADER,after);
   free(record);
   if(!okay){f->failed=true;return -1;}
   handle->position+=(int64_t)bytes;handle->needs_truncate=false;
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
   if(newest&&frontier_get64(newest+16)==2u) {
      uint8_t *target=NULL;size_t bytes=0;
      bool read=state_read_native(f->path,&target,&bytes);
      okay=read&&bytes==newest_size-STATE_FRONTIER_HEADER&&
         !memcmp(target,newest+STATE_FRONTIER_HEADER,bytes);
      free(target);
   }
   free(newest);return okay&&*generation>=f->generation;
}
void arc_state_io_frontiers(ArcState *s)
{
   arc_state_expect(s,"frontier.count",&state_frontier_count,sizeof(state_frontier_count));
   for(unsigned i=0;i<state_frontier_count;++i) {
      char name[96];StateFrontier *f=&state_frontiers[i];
      snprintf(name,sizeof(name),"frontier.%s",f->identity);
      uint64_t generation=f->generation;
      if(arc_state_loading(s)&&arc_state_pass(s)&&!frontier_current(f,&generation))
         arc_state_fail(s,"store frontier unreadable");
      arc_state_expect(s,name,&generation,sizeof(generation));
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
