/* ArcDroid speculative VFS view and ordered publication. GPL-2.0-only.
 * Every speculative mutation updates an in-memory namespace and an ordered
 * operation log. Readback sees that namespace. Publication replays the original
 * operations (including journal commit trailers and snapshot renames), so the
 * existing HD/JFD crash protocols remain the publication authority. */
#include "arc_state_io.h"
#include "arcdroid_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define STATE_IO_FILES 256u
#define STATE_IO_BYTES (32u*1024u*1024u)
#define STATE_IO_OPERATIONS 65536u

typedef struct {
   char path[4096];
   uint8_t *data;
   size_t size;
   bool removed,directory;
} StateFile;
typedef struct StateOperation {
   struct StateOperation *next;
   unsigned kind,mode;
   int64_t offset;
   size_t bytes;
   char path[4096],target[4096];
   uint8_t data[];
} StateOperation;
typedef struct {
   struct retro_vfs_file_handle *native;
   char path[4096];
   unsigned mode;
   int64_t position,native_position;
   bool needs_truncate;
   unsigned frontier_slot;uint64_t frontier_revision;
} StateHandle;
typedef struct {
   struct retro_vfs_dir_handle *native;
   char path[4096],name[4096];
   bool directory,native_finished,name_missing;
   unsigned next;
} StateDirectory;
static struct retro_vfs_interface *state_io_native,state_io_proxy;
static StateFile state_io_files[STATE_IO_FILES];
static unsigned state_io_file_count,state_io_operation_count;
static size_t state_io_operation_bytes;
static size_t state_io_wire_limit=STATE_IO_BYTES;
static StateOperation *state_io_head,*state_io_tail;
static bool state_io_speculating,state_io_failed;
static void state_frontier_bind(StateHandle *handle);
static bool state_frontier_known(StateHandle *handle);
static int64_t state_frontier_write(StateHandle *handle,const void *data,size_t bytes);
static void state_frontier_observe(StateHandle *handle,int64_t at,const void *data,size_t bytes,bool truncate);
static StateFile *state_file(const char *path)
{ for(unsigned i=0;i<state_io_file_count;++i)if(!strcmp(path,state_io_files[i].path))return &state_io_files[i];return NULL; }
static bool state_path(char destination[4096],const char *path)
{ if(!path||strlen(path)>=4096u)return false;strcpy(destination,path);return true; }
static bool state_record(unsigned kind,const char *path,const char *target,unsigned mode,int64_t offset,const void *data,size_t bytes)
{
   if(state_io_failed||state_io_operation_count>=STATE_IO_OPERATIONS||bytes>STATE_IO_BYTES||sizeof(StateOperation)+bytes+32u>STATE_IO_BYTES-16u-state_io_operation_bytes) {state_io_failed=true;return false;}
   StateOperation *op=calloc(1,sizeof(*op)+bytes);
   if(!op){state_io_failed=true;return false;}
   if(!state_path(op->path,path)||(target&&!state_path(op->target,target))) {free(op);state_io_failed=true;return false;}
   op->kind=kind;op->mode=mode;op->offset=offset;op->bytes=bytes;
   if(bytes)memcpy(op->data,data,bytes);
   if(state_io_tail)state_io_tail->next=op;else state_io_head=op;
   state_io_tail=op;++state_io_operation_count;state_io_operation_bytes+=sizeof(*op)+bytes+32u;return true;
}
static void (*state_io_commit_observer)(const char *,const void *,size_t);
void arc_state_io_set_commit_observer(void (*observer)(const char *,const void *,size_t))
{ state_io_commit_observer=observer; }
static bool state_read_native_limit(const char *path,uint8_t **data,size_t *size,size_t limit)
{
   *data=NULL;*size=0;
   if(!state_io_native||!state_io_native->open||!state_io_native->size||!state_io_native->read||!state_io_native->close)return false;
   struct retro_vfs_file_handle *f=state_io_native->open(path,RETRO_VFS_FILE_ACCESS_READ,0);
   if(!f)return false;
   int64_t n=state_io_native->size(f);bool okay=n>=0&&(uint64_t)n<=STATE_IO_BYTES&&(uint64_t)n<=limit;
   uint8_t *p=okay?malloc(n?(size_t)n:1u):NULL;
   if(!p)okay=false;
   if(okay&&n&&state_io_native->read(f,p,(uint64_t)n)!=n)okay=false;
   if(state_io_native->close(f))okay=false;
   if(!okay){free(p);return false;}
   *data=p;*size=(size_t)n;return true;
}
static bool state_read_native(const char *path,uint8_t **data,size_t *size)
{ return state_read_native_limit(path,data,size,STATE_IO_BYTES); }
static StateFile *state_materialize(const char *path,bool create,bool truncate)
{
   StateFile *f=state_file(path);
   if(!f) {
      if(state_io_file_count>=STATE_IO_FILES){state_io_failed=true;return NULL;}
      f=&state_io_files[state_io_file_count];memset(f,0,sizeof(*f));
      if(!state_path(f->path,path)){state_io_failed=true;return NULL;}
      if(!truncate&&!state_read_native(path,&f->data,&f->size)&&!create)return NULL;
      ++state_io_file_count;
   } else if(f->removed&&!create)return NULL;
   if(truncate||f->removed){free(f->data);f->data=NULL;f->size=0;f->removed=false;}
   return f;
}
static const char *state_get_path(struct retro_vfs_file_handle *file)
{ return ((StateHandle*)file)->path; }
static struct retro_vfs_file_handle *state_open(const char *path,unsigned mode,unsigned hints)
{
   /* A failed ordered publication is fail-closed until session recovery.
    * No later native mutation may overtake the retained earlier queue. */
   if(state_io_failed&&(mode&RETRO_VFS_FILE_ACCESS_WRITE))return NULL;
   StateHandle *h=calloc(1,sizeof(*h));if(!h)return NULL;
   if(!state_path(h->path,path)){free(h);return NULL;}
   h->mode=mode;state_frontier_bind(h);
   StateFile *view=state_io_speculating?state_file(path):NULL;
   bool write=(mode&RETRO_VFS_FILE_ACCESS_WRITE)!=0;
   bool truncate=write&&!(mode&RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING);
   if(state_io_speculating&&write) {
      view=state_materialize(path,truncate,truncate);
      if(!view||!state_record(1,path,NULL,mode,0,NULL,0)){free(h);return NULL;}
   } else if(view) {
      if(view->removed||view->directory){free(h);return NULL;}
   } else if(write&&truncate&&state_frontier_known(h)) {
      h->needs_truncate=true;
      /* Defer the single truncating target open until its intent is durable. */
   } else {
      h->native=state_io_native->open(path,mode,hints);
      if(!h->native){free(h);return NULL;}
      if(truncate)state_frontier_observe(h,0,NULL,0,true);
   }
   return (struct retro_vfs_file_handle*)h;
}
static int state_close(struct retro_vfs_file_handle *file)
{ StateHandle *h=(StateHandle*)file;int result=h->native?state_io_native->close(h->native):0;free(h);return result; }
static int64_t state_size(struct retro_vfs_file_handle *file)
{
   StateHandle *h=(StateHandle*)file;StateFile *f=state_io_speculating?state_file(h->path):NULL;
   if(f)return f->removed?-1:(int64_t)f->size;
   if(h->needs_truncate)return 0;
   return h->native&&state_io_native->size?state_io_native->size(h->native):-1;
}
static int64_t state_tell(struct retro_vfs_file_handle *file)
{
   StateHandle *h=(StateHandle*)file;
   if(!state_io_speculating&&h->native&&state_io_native->tell) {
      int64_t actual=state_io_native->tell(h->native);
      if(actual>=0)h->position=h->native_position=actual;
      return actual;
   }
   return h->position;
}
static int64_t state_seek(struct retro_vfs_file_handle *file,int64_t offset,int origin)
{
   StateHandle *h=(StateHandle*)file;
   int64_t base=origin==RETRO_VFS_SEEK_POSITION_START?0:origin==RETRO_VFS_SEEK_POSITION_CURRENT?h->position:origin==RETRO_VFS_SEEK_POSITION_END?state_size(file):-1;
   if(base<0||offset < -base||offset>INT64_MAX-base)return -1;
   if(!state_io_speculating&&h->native) {
      int64_t actual=state_io_native->seek(h->native,base+offset,RETRO_VFS_SEEK_POSITION_START);
      if(actual<0)return actual;h->position=h->native_position=base+offset;
   } else h->position=base+offset;
   return h->position;
}
static int64_t state_read(struct retro_vfs_file_handle *file,void *output,uint64_t bytes)
{
   StateHandle *h=(StateHandle*)file;StateFile *f=state_io_speculating?state_file(h->path):NULL;
   int64_t got;
   if(f) {
      if(f->removed||f->directory||h->position<0||(uint64_t)h->position>f->size)return -1;
      size_t n=f->size-(size_t)h->position;if(bytes<n)n=(size_t)bytes;
      if(n)memcpy(output,f->data+(size_t)h->position,n);got=(int64_t)n;
   } else {
      if(!h->native||!state_io_native->read||!state_io_native->seek)return -1;
      if(h->native_position!=h->position&&state_io_native->seek(h->native,h->position,RETRO_VFS_SEEK_POSITION_START)<0)return -1;
      got=state_io_native->read(h->native,output,bytes);
      h->native_position=h->position+(got>0?got:0);
   }
   if(got>0)h->position+=got;return got;
}
static int64_t state_write(struct retro_vfs_file_handle *file,const void *input,uint64_t bytes)
{
   StateHandle *h=(StateHandle*)file;
   if(state_io_failed||!(h->mode&RETRO_VFS_FILE_ACCESS_WRITE))return -1;
   if(state_io_speculating) {
      if(bytes>STATE_IO_BYTES||h->position<0||(uint64_t)h->position>STATE_IO_BYTES-bytes)return -1;
      StateFile *f=state_materialize(h->path,true,false);
      if(!f||f->directory)return -1;
      size_t end=(size_t)h->position+(size_t)bytes;
      if(end>f->size) {
         uint8_t *p=realloc(f->data,end?end:1u);if(!p){state_io_failed=true;return -1;}
         memset(p+f->size,0,end-f->size);f->data=p;f->size=end;
      }
      if(!state_record(2,h->path,NULL,h->mode,h->position,input,(size_t)bytes))return -1;
      if(bytes)memcpy(f->data+(size_t)h->position,input,(size_t)bytes);
      h->position+=(int64_t)bytes;return (int64_t)bytes;
   }
   if(state_frontier_known(h))return state_frontier_write(h,input,(size_t)bytes);
   if(!h->native||!state_io_native->write||!state_io_native->seek)return -1;
   if(h->native_position!=h->position&&state_io_native->seek(h->native,h->position,RETRO_VFS_SEEK_POSITION_START)<0)return -1;
   int64_t n=state_io_native->write(h->native,input,bytes);
   if(n>0){state_frontier_observe(h,h->position,input,(size_t)n,false);h->position+=n;}
   h->native_position=h->position;return n;
}
static int state_flush(struct retro_vfs_file_handle *file)
{
   StateHandle *h=(StateHandle*)file;
   if(state_io_failed)return -1;
   if(state_io_speculating)return state_record(3,h->path,NULL,h->mode,0,NULL,0)?0:-1;
   return h->native&&state_io_native->flush?state_io_native->flush(h->native):0;
}
static int state_remove(const char *path)
{
   if(state_io_failed)return -1;
   if(!state_io_speculating)return state_io_native->remove(path);
   StateFile *f=state_materialize(path,false,false);
   if(!f)return -1;
   if(!state_record(4,path,NULL,0,0,NULL,0))return -1;
   f->removed=true;return 0;
}
static int state_rename(const char *path,const char *target)
{
   if(state_io_failed)return -1;
   if(!state_io_speculating)return state_io_native->rename(path,target);
   StateFile *source=state_materialize(path,false,false);if(!source||source->directory)return -1;
   StateFile *dest=state_materialize(target,true,true);if(!dest)return -1;
   uint8_t *copy=malloc(source->size?source->size:1u);if(!copy){state_io_failed=true;return -1;}
   memcpy(copy,source->data,source->size);
   if(!state_record(5,path,target,0,0,NULL,0)){free(copy);return -1;}
   dest->data=copy;dest->size=source->size;source->removed=true;return 0;
}
static int64_t state_truncate(struct retro_vfs_file_handle *file,int64_t length)
{
   if(state_io_failed)return -1;
   StateHandle *h=(StateHandle*)file;
   if(!state_io_speculating)return h->native&&state_io_native->truncate?state_io_native->truncate(h->native,length):-1;
   if(length<0||(uint64_t)length>STATE_IO_BYTES)return -1;
   StateFile *f=state_materialize(h->path,true,false);if(!f)return -1;
   uint8_t *p=realloc(f->data,length?(size_t)length:1u);if(!p){state_io_failed=true;return -1;}
   if((size_t)length>f->size)memset(p+f->size,0,(size_t)length-f->size);
   f->data=p;f->size=(size_t)length;
   return state_record(6,h->path,NULL,h->mode,length,NULL,0)?length:-1;
}
static int32_t state_stat(const char *path,int32_t *size)
{
   StateFile *f=state_io_speculating?state_file(path):NULL;
   if(f) {
      if(f->removed)return 0;
      if(size)*size=(int32_t)f->size;
      return RETRO_VFS_STAT_IS_VALID|(f->directory?RETRO_VFS_STAT_IS_DIRECTORY:0);
   }
   return state_io_native->stat(path,size);
}
static int state_mkdir(const char *path)
{
   if(state_io_failed)return -1;
   if(!state_io_speculating)return state_io_native->mkdir(path);
   int32_t n;int32_t flags=state_stat(path,&n);
   if(flags&RETRO_VFS_STAT_IS_DIRECTORY)return -2;
   StateFile *f=state_materialize(path,true,true);if(!f)return -1;f->directory=true;
   return state_record(7,path,NULL,0,0,NULL,0)?0:-1;
}
static struct retro_vfs_dir_handle *state_opendir(const char *path,bool hidden)
{
   StateDirectory *d=calloc(1,sizeof(*d));if(!d)return NULL;
   if(!state_path(d->path,path)){free(d);return NULL;}
   d->native=state_io_native->opendir(path,hidden);
   if(!d->native) {
      StateFile *f=state_io_speculating?state_file(path):NULL;
      if(!f||!f->directory||f->removed){free(d);return NULL;}
   }
   return (struct retro_vfs_dir_handle*)d;
}
static bool state_child(StateDirectory *d,const char *path,const char **leaf)
{
   size_t n=strlen(d->path);
   if(strncmp(path,d->path,n)||path[n]!='/'||!path[n+1]||strchr(path+n+1,'/'))return false;
   *leaf=path+n+1;return true;
}
static bool state_readdir(struct retro_vfs_dir_handle *directory)
{
   StateDirectory *d=(StateDirectory*)directory;
   while(!d->native_finished&&d->native) {
      if(!state_io_native->readdir(d->native)){d->native_finished=true;break;}
      const char *leaf=state_io_native->dirent_get_name(d->native);char path[4096];
      /* A native entry with an invalid name is not EOF. Inventory callers
       * distinguish those outcomes and must retain their failure evidence. */
      d->name_missing=!leaf;
      if(!leaf)return true;
      int n=snprintf(path,sizeof(path),"%s/%s",d->path,leaf?leaf:"");
      if(n<0||(size_t)n>=sizeof(path))return false;
      if(state_io_speculating&&state_file(path))continue;
      if(!state_path(d->name,leaf))return false;
      d->directory=state_io_native->dirent_is_dir(d->native);return true;
   }
   while(state_io_speculating&&d->next<state_io_file_count) {
      StateFile *f=&state_io_files[d->next++];const char *leaf;
      if(f->removed||!state_child(d,f->path,&leaf))continue;
      if(!state_path(d->name,leaf))return false;d->directory=f->directory;return true;
   }
   return false;
}
static const char *state_dir_name(struct retro_vfs_dir_handle *directory)
{ StateDirectory *d=(StateDirectory*)directory;return d->name_missing?NULL:d->name; }
static bool state_dir_is_dir(struct retro_vfs_dir_handle *directory)
{ return ((StateDirectory*)directory)->directory; }
static int state_closedir(struct retro_vfs_dir_handle *directory)
{ StateDirectory *d=(StateDirectory*)directory;int r=d->native?state_io_native->closedir(d->native):0;free(d);return r; }
void arc_state_io_clear(void)
{
   for(unsigned i=0;i<state_io_file_count;++i)free(state_io_files[i].data);
   memset(state_io_files,0,state_io_file_count*sizeof(state_io_files[0]));state_io_file_count=0;state_io_operation_bytes=0;
   while(state_io_head){StateOperation *op=state_io_head;state_io_head=op->next;free(op);}
   state_io_tail=NULL;state_io_operation_count=0;state_io_failed=false;state_io_speculating=false;
}
struct retro_vfs_interface *arc_state_io_attach(struct retro_vfs_interface *vfs)
{
   arc_state_io_clear();state_io_wire_limit=STATE_IO_BYTES;state_io_native=vfs;if(!vfs)return NULL;state_io_proxy=*vfs;
#define ARC_STATE_WRAP(member,fn) if(vfs->member)state_io_proxy.member=fn
   ARC_STATE_WRAP(get_path,state_get_path);ARC_STATE_WRAP(open,state_open);ARC_STATE_WRAP(close,state_close);
   ARC_STATE_WRAP(size,state_size);ARC_STATE_WRAP(tell,state_tell);ARC_STATE_WRAP(seek,state_seek);
   ARC_STATE_WRAP(read,state_read);ARC_STATE_WRAP(write,state_write);ARC_STATE_WRAP(flush,state_flush);
   ARC_STATE_WRAP(remove,state_remove);ARC_STATE_WRAP(rename,state_rename);ARC_STATE_WRAP(truncate,state_truncate);
   ARC_STATE_WRAP(stat,state_stat);ARC_STATE_WRAP(mkdir,state_mkdir);ARC_STATE_WRAP(opendir,state_opendir);
   ARC_STATE_WRAP(readdir,state_readdir);ARC_STATE_WRAP(dirent_get_name,state_dir_name);
   ARC_STATE_WRAP(dirent_is_dir,state_dir_is_dir);ARC_STATE_WRAP(closedir,state_closedir);
#undef ARC_STATE_WRAP
   return &state_io_proxy;
}
void arc_state_io_speculate(bool speculative) { state_io_speculating=speculative; }
bool arc_state_io_speculative(void) { return state_io_speculating; }
bool arc_state_io_pending(void) { return state_io_head!=NULL; }
bool arc_state_io_publish(void)
{
   if(state_io_failed)return false;
   bool speculative=state_io_speculating;state_io_speculating=false;
   for(StateOperation *op=state_io_head;op;op=op->next) {
      bool okay=true;
      if(op->kind==1) {
         struct retro_vfs_file_handle *f=state_open(op->path,op->mode,0);
         okay=f!=NULL;if(f&&state_close(f))okay=false;
      } else if(op->kind==2||op->kind==3||op->kind==6) {
         struct retro_vfs_file_handle *f=state_open(op->path,RETRO_VFS_FILE_ACCESS_READ_WRITE|RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING,0);
         okay=f!=NULL;
         if(f&&op->kind==2)okay=state_seek(f,op->offset,RETRO_VFS_SEEK_POSITION_START)==op->offset;
         if(f&&op->kind==2) { ((StateHandle*)f)->needs_truncate=!(op->mode&RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING);okay=okay&&state_write(f,op->data,op->bytes)==(int64_t)op->bytes; }
         if(f&&op->kind==3)okay=state_io_native->flush&&state_flush(f)==0;
         if(f&&op->kind==6)okay=state_io_native->truncate&&state_truncate(f,op->offset)==op->offset;
         if(f&&state_close(f))okay=false;
      } else if(op->kind==4)okay=state_io_native->remove(op->path)==0;
      else if(op->kind==5)okay=state_io_native->rename(op->path,op->target)==0;
      else if(op->kind==7)okay=(state_io_native->mkdir(op->path)==0||(state_io_native->stat(op->path,NULL)&RETRO_VFS_STAT_IS_DIRECTORY));
      else okay=false;
      if(!okay){state_io_failed=true;state_io_speculating=speculative;return false;}
   }
   arc_state_io_clear();state_io_speculating=speculative;return true;
}

static char state_io_root_path[4096],state_io_bindings[68][4096];
void arc_state_io_root(const char *path)
{ snprintf(state_io_root_path,sizeof(state_io_root_path),"%s",path?path:""); }
void arc_state_io_binding(unsigned index,const char *path)
{ if(index<68u)snprintf(state_io_bindings[index],sizeof(state_io_bindings[index]),"%s",path?path:""); }
static bool state_logical_path(const char *path,char wire[4096])
{
   for(unsigned i=0;i<68u;++i)if(state_io_bindings[i][0]&&!strcmp(path,state_io_bindings[i])) {
      snprintf(wire,4096u,"M%u",i);return true;
   }
   size_t n=strlen(state_io_root_path);
   if(n&&!strncmp(path,state_io_root_path,n)&&path[n]=='/'&&strlen(path+n)<4095u) {
      wire[0]='S';strcpy(wire+1,path+n+1);return true;
   }
   return false;
}
static bool state_physical_path(const char *wire,char path[4096])
{
   if(wire[0]=='M') {
      char *end=NULL;unsigned long index=strtoul(wire+1,&end,10);
      return end&&!*end&&index<68u&&state_io_bindings[index][0]&&state_path(path,state_io_bindings[index]);
   }
   if(wire[0]!='S'||!wire[1]||wire[1]=='/'||strchr(wire,'\\')||strchr(wire,':'))return false;
   const char *p=wire+1;
   while(*p) {
      const char *end=strchr(p,'/');size_t n=end?(size_t)(end-p):strlen(p);
      if(!n||(n==1u&&p[0]=='.')||(n==2u&&p[0]=='.'&&p[1]=='.'))return false;
      if(!end)break;p=end+1;
   }
   int n=snprintf(path,4096u,"%s/%s",state_io_root_path,wire+1);
   return n>0&&n<4096;
}
typedef struct { uint8_t *data; size_t size,capacity,at; bool failed; } StateIoWire;
static void state_wire_write(StateIoWire *w,const void *p,size_t n)
{
   if(w->failed||n>STATE_IO_BYTES||w->size>STATE_IO_BYTES-n){w->failed=true;return;}
   if(w->size+n>w->capacity) {
      size_t cap=(w->size+n)*2u+64u;uint8_t *next=realloc(w->data,cap);
      if(!next){w->failed=true;return;}w->data=next;w->capacity=cap;
   }
   if(n)memcpy(w->data+w->size,p,n);w->size+=n;
}
static void state_wire_u64(StateIoWire *w,uint64_t n)
{ uint8_t b[8];for(unsigned i=0;i<8u;++i)b[i]=(uint8_t)(n>>(8u*i));state_wire_write(w,b,8); }
static void state_wire_path(StateIoWire *w,const char *path)
{
   char wire[4096];if(!state_logical_path(path,wire)){w->failed=true;return;}
   size_t n=strlen(wire)+1u;state_wire_u64(w,n);state_wire_write(w,wire,n);
}
static const uint8_t *state_wire_take(StateIoWire *w,size_t n)
{
   if(w->failed||w->at>w->size||n>w->size-w->at){w->failed=true;return NULL;}
   const uint8_t *p=w->data+w->at;w->at+=n;return p;
}
static uint64_t state_wire_read_u64(StateIoWire *w)
{ const uint8_t *p=state_wire_take(w,8);uint64_t n=0;if(p)for(unsigned i=0;i<8u;++i)n|=(uint64_t)p[i]<<(8u*i);return n; }
static bool state_wire_read_path(StateIoWire *w,char path[4096])
{
   uint64_t n=state_wire_read_u64(w);if(!n||n>4096u){w->failed=true;return false;}
   const uint8_t *p=state_wire_take(w,(size_t)n);
   if(!p||p[n-1]||memchr(p,0,(size_t)n-1u)||!state_physical_path((const char*)p,path)){w->failed=true;return false;}
   return true;
}
typedef struct {
   unsigned count,operations,capacity;
   StateOperation *head,*tail;size_t operation_bytes;
   size_t view_bytes,view_limit;
   ArcState *state;
   StateFile files[];
} StateIoReplacement;
static void state_io_replacement_free(StateIoReplacement *r)
{
   for(unsigned i=0;i<r->count;++i)free(r->files[i].data);
   while(r->head){StateOperation *op=r->head;r->head=op->next;free(op);}
   free(r);
}
static void state_io_restore_rollback(void *opaque) { state_io_replacement_free(opaque); }
static void state_io_restore_empty(void *unused)
{
   (void)unused;bool speculating=state_io_speculating;
   arc_state_io_clear();state_io_speculating=speculating;
}
static void state_io_restore_commit(void *opaque)
{
   StateIoReplacement *r=opaque;bool speculating=state_io_speculating;
   arc_state_io_clear();state_io_speculating=speculating;
   memcpy(state_io_files,r->files,r->count*sizeof(*r->files));state_io_file_count=r->count;
   state_io_head=r->head;state_io_tail=r->tail;state_io_operation_count=r->operations;state_io_operation_bytes=r->operation_bytes;free(r);
}
/* The operation log determines the staged namespace. Rebuild its file views
 * against the unchanged committed frontier rather than serializing duplicates. */
static StateFile *replacement_file(StateIoReplacement *r,const char *path,bool create,bool truncate)
{
   StateFile *f=NULL;
   for(unsigned i=0;i<r->count;++i)if(!strcmp(path,r->files[i].path)){f=&r->files[i];break;}
   if(!f) {
      if(r->count>=r->capacity)return NULL;
      f=&r->files[r->count];if(!state_path(f->path,path))return NULL;
      if(!truncate) {
         size_t remaining=r->view_limit-r->view_bytes;
         if(remaining>arc_state_ownership_remaining(r->state))remaining=arc_state_ownership_remaining(r->state);
         if(!state_read_native_limit(path,&f->data,&f->size,remaining)&&
            (!create||!state_io_native->stat||
             (state_io_native->stat(path,NULL)&RETRO_VFS_STAT_IS_VALID)))return NULL;
      }
      if(f->size>r->view_limit-r->view_bytes){free(f->data);memset(f,0,sizeof(*f));return NULL;}
      if(!arc_state_ownership_resize(r->state,0,f->size)){free(f->data);memset(f,0,sizeof(*f));return NULL;}
      r->view_bytes+=f->size;
      ++r->count;
   } else if(f->removed&&!create)return NULL;
   if(truncate||f->removed){(void)arc_state_ownership_resize(r->state,f->size,0);r->view_bytes-=f->size;free(f->data);f->data=NULL;f->size=0;f->removed=false;}
   return f;
}
static bool replacement_views(StateIoReplacement *r)
{
   for(StateOperation *op=r->head;op;op=op->next) {
      StateFile *f;
      switch(op->kind) {
         case 1:{bool truncate=(op->mode&RETRO_VFS_FILE_ACCESS_WRITE)&&!(op->mode&RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING);
            if(!replacement_file(r,op->path,truncate,truncate))return false;break;}
         case 2:case 6:{
            f=replacement_file(r,op->path,true,false);if(!f||f->directory)return false;
            size_t size=op->kind==6?(size_t)op->offset:(size_t)op->offset+op->bytes;
            if(op->kind==2&&size<f->size)size=f->size;
            if(size>f->size&&size-f->size>r->view_limit-r->view_bytes)return false;
            if(!arc_state_ownership_resize(r->state,f->size,size))return false;
            uint8_t *data=realloc(f->data,size?size:1u);if(!data)return false;
            if(size>f->size)memset(data+f->size,0,size-f->size);
            r->view_bytes=r->view_bytes-f->size+size;f->data=data;f->size=size;
            if(op->kind==2&&op->bytes)memcpy(data+(size_t)op->offset,op->data,op->bytes);break;
         }
         case 3:break;
         case 4:f=replacement_file(r,op->path,false,false);if(!f)return false;f->removed=true;break;
         case 5:{
            f=replacement_file(r,op->path,false,false);if(!f||f->directory)return false;
            StateFile *target=replacement_file(r,op->target,true,true);if(!target)return false;
            if(f->size>r->view_limit-r->view_bytes)return false;
            if(!arc_state_ownership_resize(r->state,0,f->size))return false;
            target->data=malloc(f->size?f->size:1u);if(!target->data)return false;
            r->view_bytes+=f->size;memcpy(target->data,f->data,f->size);target->size=f->size;f->removed=true;break;
         }
         case 7:f=replacement_file(r,op->path,true,true);if(!f)return false;f->directory=true;break;
         default:return false;
      }
   }
   return true;
}
bool arc_state_io_preflight(size_t maximum)
{
   if(state_io_failed||maximum<16u)return false;
   size_t views=0;
   for(unsigned i=0;i<state_io_file_count;++i) {
      if(state_io_files[i].size>STATE_IO_BYTES-views)return false;
      views+=state_io_files[i].size;
   }
   size_t bytes=16u;
   for(StateOperation *op=state_io_head;op;op=op->next) {
      char path[4096],target[4096];
      if(!state_logical_path(op->path,path)||
         (op->kind==5u&&!state_logical_path(op->target,target)))return false;
      size_t overhead=41u+strlen(path);
      if(op->kind==5u)overhead+=9u+strlen(target);
      if(overhead>maximum-bytes||op->bytes>maximum-bytes-overhead)return false;
      bytes+=overhead+op->bytes;
   }
   return true;
}
void arc_state_io_snapshot(ArcState *s)
{ arc_state_io_snapshot_bounded(s,state_io_wire_limit); }
void arc_state_io_snapshot_bounded(ArcState *s,size_t maximum)
{
   if(maximum>state_io_wire_limit)maximum=state_io_wire_limit;
   if(arc_state_pass(s)||arc_state_error(s))return;
   StateIoWire w={0};
   if(!arc_state_loading(s)) {
      if(!arc_state_io_preflight(maximum)){arc_state_fail(s,"pending store bounds");return;}
      state_wire_u64(&w,state_io_file_count);state_wire_u64(&w,state_io_operation_count);
      for(StateOperation *op=state_io_head;op;op=op->next) {
         state_wire_u64(&w,op->kind);state_wire_u64(&w,op->mode);state_wire_u64(&w,(uint64_t)op->offset);
         state_wire_path(&w,op->path);
         if(op->kind==5)state_wire_path(&w,op->target);
         state_wire_u64(&w,op->bytes);state_wire_write(&w,op->data,op->bytes);
      }
      if(w.failed||w.size>maximum){free(w.data);arc_state_fail(s,"pending store bounds");return;}
   }
   size_t bytes=0;
   const void *payload=arc_state_blob(s,"stores.pending",w.data,w.size,maximum,&bytes);
   free(w.data);
   if(!arc_state_loading(s)||arc_state_error(s))return;
   w=(StateIoWire){.data=(uint8_t*)payload,.size=bytes};
   uint64_t count=state_wire_read_u64(&w),operations=state_wire_read_u64(&w);
   if(!count&&!operations) {
      if(w.failed||w.at!=w.size){arc_state_fail(s,"pending store format");return;}
#if defined(ARCDROID_TEST_FAULTS)
      const char *drop=getenv("ARCDROID_SS1_DROP_OBJECT");
      if(drop&&!strcmp(drop,"stores.pending"))return;
#endif
      arc_state_action(s,state_io_restore_empty,NULL,NULL);return;
   }
   if(w.failed||count>STATE_IO_FILES||operations>STATE_IO_OPERATIONS){arc_state_fail(s,"pending store format");return;}
   size_t owned=sizeof(StateIoReplacement)+(size_t)count*sizeof(StateFile);
   if(!arc_state_ownership_resize(s,0,owned))return;
   StateIoReplacement *r=calloc(1,owned);if(!r){arc_state_fail(s,"allocation");return;}
   r->capacity=(unsigned)count;r->state=s;r->view_limit=arc_state_ownership_limit(s);
   if(r->view_limit>STATE_IO_BYTES)r->view_limit=STATE_IO_BYTES;
   for(uint64_t i=0;!w.failed&&i<operations;++i) {
      uint64_t kind=state_wire_read_u64(&w),mode=state_wire_read_u64(&w),offset=state_wire_read_u64(&w);
      char path[4096],target[4096]={0};state_wire_read_path(&w,path);
      if(kind==5)state_wire_read_path(&w,target);
      uint64_t n=state_wire_read_u64(&w);
      if(kind<1||kind>7||mode>7u||offset>STATE_IO_BYTES||n>STATE_IO_BYTES||
         (kind!=2&&n)||(kind==2&&offset>STATE_IO_BYTES-n)){w.failed=true;break;}
      const uint8_t *p=state_wire_take(&w,(size_t)n);if(!p)break;
      if(sizeof(StateOperation)+(size_t)n+32u>STATE_IO_BYTES-16u-r->operation_bytes){w.failed=true;break;}
      if(!arc_state_ownership_resize(s,0,sizeof(StateOperation)+(size_t)n+32u)){w.failed=true;break;}
      StateOperation *op=calloc(1,sizeof(*op)+(size_t)n);if(!op){w.failed=true;break;}
      r->operation_bytes+=sizeof(*op)+(size_t)n+32u;
      op->kind=(unsigned)kind;op->mode=(unsigned)mode;op->offset=(int64_t)offset;op->bytes=(size_t)n;
      strcpy(op->path,path);strcpy(op->target,target);memcpy(op->data,p,(size_t)n);
      if(r->tail)r->tail->next=op;else r->head=op;r->tail=op;++r->operations;
   }
   if(w.failed||w.at!=w.size||!replacement_views(r)||r->count!=count){state_io_replacement_free(r);arc_state_fail(s,"pending store format");return;}
#if defined(ARCDROID_TEST_FAULTS)
   const char *drop=getenv("ARCDROID_SS1_DROP_OBJECT");
   if(drop&&!strcmp(drop,"stores.pending")){state_io_replacement_free(r);return;}
#endif
   arc_state_action(s,state_io_restore_commit,state_io_restore_rollback,r);
}

#include "arc_state_frontier.inc"

/* Account for the actual producers' maximum write payload and logical paths.
 * A write uses 40+path+payload wire bytes, a rename 48+2*path. Native queue
 * ownership charges sizeof(StateOperation)+payload+32 for either. The largest
 * wire/ownership ratio bounds the sum, independent of how many speculative
 * frames a frontend requests. There is no assumed one-frame publication
 * window or menu-only run-ahead limit. Eager IO and queue exhaustion do not
 * change. The schema refuses an out-of-contract pending blob at capture/load. */
size_t arc_state_io_session_bound(size_t maximum_write,size_t maximum_path)
{
   if(maximum_write>STATE_IO_BYTES||!maximum_path||maximum_path>4096u)return 0;
   uint64_t cost=sizeof(StateOperation)+32u;
   uint64_t numerator=40u+maximum_path+maximum_write,denominator=cost+maximum_write;
   uint64_t rename=48u+2u*maximum_path;
   if(rename*denominator>numerator*cost){numerator=rename;denominator=cost;}
   uint64_t bound=16u+(STATE_IO_BYTES-16u)*numerator/denominator;
   state_io_wire_limit=bound<STATE_IO_BYTES?(size_t)bound:STATE_IO_BYTES;
   return state_io_wire_limit;
}
size_t arc_state_io_pending_bound(void) { return state_io_wire_limit; }
