/* ArcDroid retained raster rows; GPL-2.0-only.
 * VIDC colours have twelve significant bits (vidc_make_colour). Store their
 * exact channel codes, including the Acorn black-level transform, rather
 * than XRGB8888 host pixels. UI colours use RGB888.
 * Identical rows, including copies in other retained surfaces, are recipes
 * referring to already validated rows. No palette or current video mode is
 * used to reconstruct historical colours. */
typedef struct StateRaster {
   struct StateRaster *previous;
   uint32_t *pixels;size_t pitch;
   uint16_t (*rows)[2];
   const uint8_t *wire;size_t bytes;
   bool restored,omitted;
   size_t offsets[2048];
   uint32_t hashes[2048];
} StateRaster;
static const uint8_t *raster_literal(StateRaster *r,unsigned y,unsigned *kind)
{
   /* Every reference goes to an earlier surface or earlier row in this one.
    * Validation enforces that order, so this loop cannot cycle. */
   const uint8_t *p=r->wire+r->offsets[y];
   for(;;) {
      uint64_t h=get64(p);*kind=(unsigned)(h>>48u);
      if(*kind!=5u)return p;
      uint64_t ref=get64(p+8);unsigned back=(unsigned)(ref>>32u);
      while(back--)r=r->previous;
      y=(unsigned)ref;p=r->wire+r->offsets[y];
   }
}
static unsigned raster_acorn_channel(unsigned value)
{ return (value*12u+254u)/255u; }
static bool raster_acorn_colour(uint32_t colour)
{
   if(colour>>24u)return false;
   for(unsigned shift=0;shift<24u;shift+=8u) {
      unsigned value=(colour>>shift)&255u;
      if(raster_acorn_channel(value)*255u/12u!=value)return false;
   }
   return true;
}
static void raster_restore_commit(void *opaque)
{
   StateRaster *r=opaque;
   if(r->restored)return;
   /* Restore referenced surfaces once before their dependants. Earlier rows
    * in this surface are already installed when a row-copy is encountered. */
   if(r->previous)raster_restore_commit(r->previous);
   if(r->omitted)return;
   for(unsigned y=0;y<2048u;++y)if(r->rows[y][1])
      memset(r->pixels+(size_t)y*r->pitch+r->rows[y][0],0,(r->rows[y][1]-r->rows[y][0])*4u);
   memset(r->rows,0,2048u*sizeof(*r->rows));
   for(unsigned y=0;y<2048u;++y)if(r->offsets[y]!=SIZE_MAX) {
      uint64_t h=get64(r->wire+r->offsets[y]);
      unsigned x=(unsigned)((h>>16u)&0xffffu),end=(unsigned)((h>>32u)&0xffffu),kind;
      uint32_t *out=r->pixels+(size_t)y*r->pitch+x;
      const uint8_t *p=r->wire+r->offsets[y];kind=(unsigned)(h>>48u);
      if(kind==5u) {
         uint64_t ref=get64(p+8u);unsigned back=(unsigned)(ref>>32u),row=(unsigned)ref;
         StateRaster *base=r;while(back--)base=base->previous;
         if(!base->omitted) {
            memcpy(out,base->pixels+(size_t)row*base->pitch+x,(end-x)*4u);
            r->rows[y][0]=(uint16_t)x;r->rows[y][1]=(uint16_t)end;continue;
         }
         /* Field-drop probes omit the source's installation, while every
          * other owner still reconstructs the correct saved row. */
         p=raster_literal(r,y,&kind);
      }
      p+=16u;
      if(kind==1u){uint32_t colour;memcpy(&colour,p,4u);for(unsigned i=x;i<end;++i)*out++=colour;}
      else if(kind==2u||kind==6u)for(unsigned i=0;i<end-x;i+=2u) {
         unsigned pair=(unsigned)p[0]|((unsigned)p[1]<<8u);p+=2u;
         unsigned a=pair&4095u;
         *out++=kind==2u?((a&15u)*17u)|(((a>>4u)&15u)*17u<<8u)|(((a>>8u)&15u)*17u<<16u):
            ((a&15u)*255u/12u)|(((a>>4u)&15u)*255u/12u<<8u)|(((a>>8u)&15u)*255u/12u<<16u);
         if(i+1u<end-x) {
            a=(pair>>12u)|((unsigned)*p++<<4u);
            *out++=kind==2u?((a&15u)*17u)|(((a>>4u)&15u)*17u<<8u)|(((a>>8u)&15u)*17u<<16u):
               ((a&15u)*255u/12u)|(((a>>4u)&15u)*255u/12u<<8u)|(((a>>8u)&15u)*255u/12u<<16u);
         }
      }
      else if(kind==3u)for(unsigned i=x;i<end;++i){*out++=(uint32_t)p[0]|((uint32_t)p[1]<<8u)|((uint32_t)p[2]<<16u);p+=3u;}
      else memcpy(out,p,(end-x)*4u);
      r->rows[y][0]=(uint16_t)x;r->rows[y][1]=(uint16_t)end;
   }
   r->restored=true;
}
void arc_state_raster(ArcState *s,const char *name,uint32_t *pixels,size_t pitch,uint16_t rows[2048][2])
{
   if(!s||s->pass||s->error||s->measuring)return;
   StateRaster *r=arc_state_scratch(s,sizeof(*r));if(!r)return;
   memset(r,0,sizeof(*r));r->previous=s->rasters;r->pixels=pixels;r->pitch=pitch;r->rows=rows;
   for(unsigned y=0;y<2048u;++y)r->offsets[y]=SIZE_MAX;
   uint16_t spans[2048][2];unsigned kinds[2048];uint64_t refs[2048];size_t bytes=0;
   if(!s->loading)for(unsigned y=0;y<2048u;++y) {
      unsigned first=rows[y][0],end=rows[y][1];const uint32_t *line=pixels+(size_t)y*pitch;
      while(first<end&&!line[first])++first;
      while(end>first&&!line[end-1u])--end;
      spans[y][0]=(uint16_t)first;spans[y][1]=(uint16_t)end;
      if(first==end)continue;
      unsigned kind=1u;
      uint32_t hash=arc_crc32(line+first,(end-first)*4u);r->hashes[y]=hash;
      unsigned back=0;
      for(StateRaster *base=r;base&&kind!=5u;base=base->previous,++back) {
         unsigned limit=base==r?y:2048u;
         for(unsigned row=0;row<limit;++row) {
            unsigned a,b;
            if(base==r){a=spans[row][0];b=spans[row][1];}
            else if(base->offsets[row]!=SIZE_MAX){uint64_t h=get64(base->wire+base->offsets[row]);a=(unsigned)((h>>16u)&0xffffu);b=(unsigned)((h>>32u)&0xffffu);}
            else continue;
            if(a==first&&b==end&&base->hashes[row]==hash&&
               !memcmp(line+first,base->pixels+(size_t)row*base->pitch+first,(end-first)*4u)) {
               kind=5u;refs[y]=((uint64_t)back<<32u)|row;break;
            }
         }
      }
      if(kind!=5u) {
         uint32_t bits=0,colour=line[first];bool same=true;
         for(unsigned x=first;x<end;++x){bits|=line[x];same=same&&line[x]==colour;}
         if(!same) {
            kind=bits>>24u?4u:2u;
            if(kind==2u)for(unsigned x=first;x<end;++x)if(((line[x]^(line[x]>>4u))&0x000f0f0fu)!=0u){kind=6u;break;}
            if(kind==6u)for(unsigned x=first;x<end;++x)if(!raster_acorn_colour(line[x])){kind=3u;break;}
         }
      }
      kinds[y]=kind;bytes+=16u+(kind==1u?4u:kind==2u||kind==6u?((end-first)*3u+1u)/2u:kind==5u?0u:(end-first)*(kind==3u?3u:4u));
   }
   uint8_t *wire=s->loading?NULL:arc_state_scratch(s,bytes);size_t at=0;
   if(!s->loading&&wire)for(unsigned y=0;y<2048u;++y)if(spans[y][0]<spans[y][1]) {
      unsigned x=spans[y][0],end=spans[y][1],kind=kinds[y];const uint32_t *line=pixels+(size_t)y*pitch;
      r->offsets[y]=at;
      put64(wire+at,(uint64_t)y|((uint64_t)x<<16u)|((uint64_t)end<<32u)|((uint64_t)kind<<48u));
      put64(wire+at+8u,kind==5u?refs[y]:0u);at+=16u;
      if(kind==1u){memcpy(wire+at,line+x,4u);at+=4u;}
      else if(kind==2u||kind==6u)for(unsigned i=x;i<end;i+=2u) {
         unsigned a,b=0;
         if(kind==2u) {
            a=(line[i]&15u)|((line[i]>>4u)&240u)|((line[i]>>8u)&3840u);
            if(i+1u<end)b=(line[i+1u]&15u)|((line[i+1u]>>4u)&240u)|((line[i+1u]>>8u)&3840u);
         } else {
            a=raster_acorn_channel(line[i]&255u)|(raster_acorn_channel((line[i]>>8u)&255u)<<4u)|(raster_acorn_channel((line[i]>>16u)&255u)<<8u);
            if(i+1u<end)b=raster_acorn_channel(line[i+1u]&255u)|(raster_acorn_channel((line[i+1u]>>8u)&255u)<<4u)|(raster_acorn_channel((line[i+1u]>>16u)&255u)<<8u);
         }
         wire[at++]=(uint8_t)a;wire[at++]=(uint8_t)((a>>8u)|(b<<4u));if(i+1u<end)wire[at++]=(uint8_t)(b>>4u);
      }
      else if(kind==3u)for(unsigned i=x;i<end;++i){wire[at++]=(uint8_t)line[i];wire[at++]=(uint8_t)(line[i]>>8u);wire[at++]=(uint8_t)(line[i]>>16u);}
      else if(kind==4u){memcpy(wire+at,line+x,(end-x)*4u);at+=(end-x)*4u;}
   }
   const uint8_t *saved=arc_state_blob(s,name,wire,bytes,4096u*2048u*4u+2048u*16u,&r->bytes);
   /* Saving may compress the item. Row indexes always address the raw recipe
    * stream; loading receives the blob's validated, decoded stream. */
   r->wire=s->loading?saved:wire;
   if(s->error)return;
   if(s->loading) {
      unsigned previous=0;bool first=true;
      for(at=0;at<r->bytes;) {
         if(r->bytes-at<16u){arc_state_fail(s,"raster row");return;}
         size_t offset=at;uint64_t h=get64(r->wire+at),ref=get64(r->wire+at+8u);at+=16u;
         unsigned y=(unsigned)(h&0xffffu),x=(unsigned)((h>>16u)&0xffffu),end=(unsigned)((h>>32u)&0xffffu),kind=(unsigned)(h>>48u);
         if(y>=2048u||(!first&&y<=previous)||x>=end||end>4096u||kind<1u||kind>6u){arc_state_fail(s,"raster extent");return;}
         first=false;previous=y;
         size_t n=kind==1u?4u:kind==2u||kind==6u?((end-x)*3u+1u)/2u:kind==5u?0u:(end-x)*(kind==3u?3u:4u);
         if(n>r->bytes-at){arc_state_fail(s,"raster pixels");return;}
         if(kind==5u) {
            StateRaster *base=r;uint64_t back=ref>>32u;unsigned row=(unsigned)ref;
            while(back&&base){base=base->previous;--back;}
            if(!base||row>=2048u||(base==r&&row>=y)||base->offsets[row]==SIZE_MAX){arc_state_fail(s,"raster copy");return;}
            uint64_t src=get64(base->wire+base->offsets[row]);
            if((src&UINT64_C(0xffffffff0000))!=(h&UINT64_C(0xffffffff0000))){arc_state_fail(s,"raster copy extent");return;}
         } else if(ref||((kind==2u||kind==6u)&&(end-x)%2u&&(r->wire[at+n-1u]&0xf0u))){arc_state_fail(s,"raster encoding");return;}
         if(kind==6u)for(size_t i=0;i<n;++i) {
            uint8_t byte=r->wire[at+i];
            if((byte&15u)>12u||(byte>>4u)>12u){arc_state_fail(s,"raster channel");return;}
         }
         r->offsets[y]=offset;at+=n;
      }
   }
   s->rasters=r;
#if defined(ARCDROID_TEST_FAULTS)
   if(s->drop_owner==name_key(name)){s->drop_done=true;r->omitted=true;return;}
#endif
   if(s->loading)arc_state_action(s,raster_restore_commit,NULL,r);
}
