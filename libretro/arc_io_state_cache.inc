/* ArcDroid raw-media capture index. GPL-2.0-only.
 * Prime once during media admission. Guest reads continue through the original
 * file; only snapshots use this mirror. Successful writes update every open
 * alias, including speculative writes and restored file mirrors. */
struct ArcIoStateCache {
   uint8_t *data;
   const uint8_t *base;
   size_t bytes,sectors,count,pending_count;
   bool owns_data;
   uint16_t indices[]; /* Dirty IDs/positions, then changed-since-capture IDs/flags. */
};
static ArcIoFile *arc_io_state_files;
static void arc_io_state_cache_dirty(ArcIoStateCache *c,size_t sector)
{
   uint16_t *positions=c->indices+c->sectors;
   size_t bytes=c->bytes-sector*512u;if(bytes>512u)bytes=512u;
   bool dirty=memcmp(c->data+sector*512u,c->base+sector*512u,bytes)!=0;
   unsigned position=positions[sector];
   if(dirty&&!position){c->indices[c->count]=(uint16_t)sector;positions[sector]=(uint16_t)(++c->count);}
   else if(!dirty&&position){
      uint16_t last=c->indices[--c->count];c->indices[position-1u]=last;
      positions[last]=(uint16_t)position;positions[sector]=0;
   }
}
static void arc_io_state_cache_close(ArcIoFile *file)
{
   if(!file->state_cache)return;
   ArcIoFile **at=&arc_io_state_files;
   while(*at&&*at!=file)at=&(*at)->state_next;
   if(*at)*at=file->state_next;
   if(file->state_cache->owns_data)free(file->state_cache->data);
   free(file->state_cache);file->state_cache=NULL;file->state_next=NULL;
}
static bool arc_io_state_cache_open(ArcIoFile *file)
{
   unsigned binding=ARC_STATE_FILE_BINDINGS;
   for(unsigned i=0;i<ARC_STATE_FILE_BINDINGS;++i)
      if(arc_io_state_paths[i].path[0]&&arc_io_state_bases[i].path[0]&&
         !strcmp(file->path,arc_io_state_paths[i].path)){binding=i;break;}
   if(binding==ARC_STATE_FILE_BINDINGS)return true;
   size_t bytes=0;const uint8_t *base=arc_io_state_base(binding,&bytes,NULL);
   if(!base||bytes>16u*1024u*1024u)return false;
   size_t sectors=(bytes+511u)/512u;
   ArcIoStateCache *c=calloc(1,sizeof(*c)+sectors*8u);if(!c)return false;
   c->bytes=bytes;c->sectors=sectors;c->base=base;
   if(file->memory)c->data=(uint8_t *)file->memory;
   else {
      c->data=malloc(bytes?bytes:1u);c->owns_data=true;
      if(!c->data||arc_io_file_length(file)!=(int64_t)bytes||
         arc_io_seek_file(file,0,RETRO_VFS_SEEK_POSITION_START)<0||
         arc_io_read_file(file,c->data,bytes)!=bytes||
         arc_io_seek_file(file,0,RETRO_VFS_SEEK_POSITION_START)<0){
         free(c->data);free(c);return false;
      }
   }
   for(size_t i=0;i<sectors;++i)arc_io_state_cache_dirty(c,i);
   file->state_cache=c;file->state_next=arc_io_state_files;arc_io_state_files=file;return true;
}
static void arc_io_state_cache_write(ArcIoFile *file,int64_t at,const void *data,size_t bytes)
{
   if(at<0||!bytes)return;
   for(ArcIoFile *f=arc_io_state_files;f;f=f->state_next)if(!strcmp(f->path,file->path)) {
      ArcIoStateCache *c=f->state_cache;
      if((uint64_t)at>c->bytes||bytes>c->bytes-(size_t)at)continue;
      /* A restored file reads its owned logical view, rather than the native
       * file. Writes through another live alias must update that view too. */
      if(f!=file&&f->owns_memory)memcpy((uint8_t *)f->memory+(size_t)at,data,bytes);
      uint16_t *pending=c->indices+c->sectors*2u,*flags=c->indices+c->sectors*3u;
      size_t last=((size_t)at+bytes-1u)/512u;
      for(size_t i=(size_t)at/512u;i<=last;++i)if(!flags[i]){flags[i]=1;pending[c->pending_count++]=(uint16_t)i;}
   }
}
static bool arc_io_state_cache_refresh(ArcIoFile *file)
{
   ArcIoStateCache *c=file->state_cache;if(!c||!c->pending_count)return true;
   uint16_t *pending=c->indices+c->sectors*2u,*flags=c->indices+c->sectors*3u;
   int64_t position=file->memory?(int64_t)file->memory_at:file->state_at;bool okay=true;
   for(size_t i=0;i<c->pending_count;++i){
      size_t sector=pending[i],at=sector*512u,n=c->bytes-at;if(n>512u)n=512u;
      if(!file->memory&&(arc_io_seek_file(file,(int64_t)at,RETRO_VFS_SEEK_POSITION_START)<0||
         arc_io_read_file(file,c->data+at,n)!=n)){okay=false;break;}
      arc_io_state_cache_dirty(c,sector);
   }
   if(!file->memory&&arc_io_seek_file(file,position,RETRO_VFS_SEEK_POSITION_START)<0)okay=false;
   if(okay){for(size_t i=0;i<c->pending_count;++i)flags[pending[i]]=0;c->pending_count=0;}return okay;
}
static int arc_io_state_sector_order(const void *left,const void *right)
{
   uint16_t a=*(const uint16_t *)left,b=*(const uint16_t *)right;return (a>b)-(a<b);
}
static void arc_io_state_cache_u64(uint8_t *p,uint64_t n)
{for(unsigned i=0;i<8u;++i)p[i]=(uint8_t)(n>>(8u*i));}
static void arc_io_state_cache_capture(ArcState *s,const char *key,const ArcIoStateCache *c)
{
   size_t capacity=16u+c->count*520u,used=16u;
   uint8_t *wire=arc_state_scratch(s,capacity);
   uint16_t *ids=arc_state_scratch(s,c->count*2u);
   if(!wire||!ids){arc_state_fail(s,"media capture allocation");return;}
   memcpy(ids,c->indices,c->count*2u);qsort(ids,c->count,sizeof(*ids),arc_io_state_sector_order);
   arc_io_state_cache_u64(wire,c->bytes);arc_io_state_cache_u64(wire+8,c->count);
   for(size_t i=0;i<c->count;++i){
      size_t sector=ids[i],bytes=c->bytes-sector*512u;if(bytes>512u)bytes=512u;
      arc_io_state_cache_u64(wire+used,sector);used+=8u;
      memcpy(wire+used,c->data+sector*512u,bytes);used+=bytes;
   }
   size_t saved;(void)arc_state_blob(s,key,wire,used,16u+c->bytes+c->sectors*8u,&saved);
}
