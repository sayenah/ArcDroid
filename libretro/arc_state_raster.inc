/* ArcDroid retained raster spans; GPL-2.0-only. */
typedef struct {
   uint32_t *pixels;size_t pitch;
   uint16_t (*rows)[2];const uint8_t *wire;size_t bytes;
} StateRasterRestore;
static void raster_restore_commit(void *opaque)
{
   StateRasterRestore *r=opaque;
   for(unsigned y=0;y<2048u;++y)if(r->rows[y][1])
      memset(r->pixels+(size_t)y*r->pitch+r->rows[y][0],0,(r->rows[y][1]-r->rows[y][0])*4u);
   memset(r->rows,0,2048u*sizeof(*r->rows));
   for(size_t at=0;at<r->bytes;) {
      uint64_t span=get64(r->wire+at);at+=8u;
      unsigned y=(unsigned)(span&0xffffu),x=(unsigned)((span>>16u)&0xffffu),end=(unsigned)(span>>32u);
      memcpy(r->pixels+(size_t)y*r->pitch+x,r->wire+at,(end-x)*4u);at+=(end-x)*4u;
      r->rows[y][0]=(uint16_t)x;r->rows[y][1]=(uint16_t)end;
   }
   free(r);
}
void arc_state_raster(ArcState *s,const char *name,uint32_t *pixels,size_t pitch,uint16_t rows[2048][2])
{
   if(!s||s->pass||s->error)return;
   uint16_t spans[2048][2];size_t bytes=0;
   if(!s->loading)for(unsigned y=0;y<2048u;++y) {
      unsigned first=rows[y][0],end=rows[y][1];const uint32_t *line=pixels+(size_t)y*pitch;
      while(first<end&&!line[first])++first;
      while(end>first&&!line[end-1u])--end;
      spans[y][0]=(uint16_t)first;spans[y][1]=(uint16_t)end;
      if(first<end)bytes+=8u+(end-first)*4u;
   }
   uint8_t *wire=s->loading?NULL:arc_state_scratch(s,bytes);size_t at=0;
   if(!s->loading&&wire)for(unsigned y=0;y<2048u;++y)if(spans[y][0]<spans[y][1]) {
      unsigned x=spans[y][0],end=spans[y][1];put64(wire+at,(uint64_t)y|((uint64_t)x<<16u)|((uint64_t)end<<32u));at+=8u;
      memcpy(wire+at,pixels+(size_t)y*pitch+x,(end-x)*4u);at+=(end-x)*4u;
   }
   const uint8_t *saved=arc_state_blob(s,name,wire,bytes,4096u*2048u*4u+2048u*8u,&bytes);
   if(!s->loading||s->error)return;
   unsigned previous=0;bool first=true;
   for(at=0;at<bytes;) {
      if(bytes-at<8u){arc_state_fail(s,"raster span");return;}
      uint64_t span=get64(saved+at);at+=8u;
      unsigned y=(unsigned)(span&0xffffu),x=(unsigned)((span>>16u)&0xffffu),end=(unsigned)(span>>32u);
      if(y>=2048u||(!first&&y<=previous)||x>=end||end>4096u||(end-x)>(bytes-at)/4u) {arc_state_fail(s,"raster extent");return;}
      first=false;previous=y;at+=(end-x)*4u;
   }
#if defined(ARCDROID_TEST_FAULTS)
   if(s->drop_owner==name_key(name)){s->drop_done=true;return;}
#endif
   StateRasterRestore *r=malloc(sizeof(*r));if(!r){arc_state_fail(s,"allocation");return;}
   *r=(StateRasterRestore){pixels,pitch,rows,saved,bytes};arc_state_action(s,raster_restore_commit,free,r);
}
