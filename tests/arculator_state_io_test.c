/* ArcDroid authored speculative I/O and durable-frontier tests. GPL-2.0-only. */
#define main legacy_install_store_main
#include "install_store_test.c"
#undef main
#include "arc_state.h"
#include "arc_state_io.h"
static void state_visit(ArcState *s)
{ arc_state_io_snapshot(s);arc_state_io_frontiers(s); }
static uint8_t *capture_visit(ArcStateVisit visit,size_t *bytes)
{
   ArcState *s=arc_state_begin(NULL,0);bool okay=arc_state_run(s,visit);
   if(!okay)fprintf(stderr,"capture refused: %s\n",arc_state_error(s));assert(okay);
   *bytes=arc_state_size(s);uint8_t *p=malloc(*bytes);assert(p&&arc_state_output(s,p,*bytes));arc_state_end(s);return p;
}
static bool restore_visit(ArcStateVisit visit,const uint8_t *p,size_t bytes)
{
   ArcState *s=arc_state_begin(p,bytes);bool okay=arc_state_run(s,visit)&&arc_state_commit(s);arc_state_end(s);return okay;
}
static uint8_t *capture(size_t *bytes) {return capture_visit(state_visit,bytes);}
static bool restore(const uint8_t *p,size_t bytes) {return restore_visit(state_visit,p,bytes);}
static uint8_t codec_base[4099],codec_current[4099];
static uint32_t *codec_pixels;
static uint16_t codec_rows[2048][2];
static unsigned codec_forge;
static bool codec_sparse,codec_indexed;
static uint8_t codec_dirty[2];
static uint32_t *copy_pixels;
static uint16_t copy_rows[2048][2];
static void codec_u64(uint8_t *p,uint64_t n)
{for(unsigned i=0;i<8u;++i)p[i]=(uint8_t)(n>>(i*8u));}
static void codec_visit(ArcState *s)
{
   char digest[41];arc_sha1_hex(codec_base,sizeof(codec_base),digest);
   arc_state_expect(s,"fixture.base",digest,sizeof(digest));
   if(arc_state_pass(s))return;
   if(codec_forge==1u||codec_forge==2u) {
      uint8_t bad[1056]={0};size_t bytes=codec_forge==1u?sizeof(bad):536u,n;
      codec_u64(bad,sizeof(codec_base));codec_u64(bad+8,codec_forge==1u?2u:1u);
      codec_u64(bad+16,codec_forge==1u?2u:99u);codec_u64(bad+536,2u);
      (void)arc_state_blob(s,"fixture.delta",bad,bytes,bytes,&n);
   } else {
      if(codec_indexed)arc_state_delta_stage_indexed(s,"fixture.delta",codec_current,codec_base,sizeof(codec_base),codec_dirty);
      else if(codec_sparse)arc_state_delta_stage(s,"fixture.delta",codec_current,codec_base,sizeof(codec_base));
      else {
         const void *next=arc_state_delta(s,"fixture.delta",codec_current,codec_base,sizeof(codec_base));
         if(arc_state_loading(s)&&next)arc_state_stage(s,codec_current,next,sizeof(codec_current));
      }
   }
   if(codec_forge>=3u) {
      uint8_t bad[20]={0};size_t n,bytes=sizeof(bad);
      if(codec_forge==3u)codec_u64(bad,UINT64_C(2048)|(UINT64_C(1)<<32u)|(UINT64_C(1)<<48u));
      else if(codec_forge<6u) {
         codec_u64(bad,(UINT64_C(1)<<32u)|(UINT64_C(5)<<48u));
         codec_u64(bad+8,codec_forge==4u?0u:UINT64_C(1)<<32u);bytes=16u;
      } else {
         codec_u64(bad,(UINT64_C(1)<<32u)|(UINT64_C(6)<<48u));bad[16]=14u;bytes=18u;
      }
      (void)arc_state_blob(s,"fixture.raster",bad,bytes,bytes,&n);
   } else arc_state_raster(s,"fixture.raster",codec_pixels,4096u,codec_rows);
   if(copy_pixels)arc_state_raster(s,"fixture.copy",copy_pixels,4096u,copy_rows);
}
static void bounded_blob_visit(ArcState *s)
{
   size_t bytes; (void)arc_state_blob(s,"fixture.bounded",NULL,0,4096u,&bytes);
}
static void test_declared_decode_limit(void)
{
   /* A CRC-valid 84-byte record must never reserve its forged 1 GB extent. */
   uint8_t wire[84]={0};memcpy(wire,"ARCSS1",6);codec_u64(wire+8,3);
   codec_u64(wire+16,sizeof(wire));codec_u64(wire+24,1);
   uint64_t key=UINT64_C(14695981039346656037);
   const char *name="fixture.bounded";while(*name)key=(key^(uint8_t)*name++)*UINT64_C(1099511628211);
   codec_u64(wire+40,key);codec_u64(wire+48,UINT64_C(0x100000006));codec_u64(wire+56,20);
   codec_u64(wire+64,1000000000u);codec_u64(wire+72,UINT64_C(500000001));
   codec_u64(wire+32,arc_crc32(wire+40,44));
   ArcState *s=arc_state_begin_limited(wire,sizeof(wire),16u*1024u*1024u);
   assert(arc_state_error(s)&&!arc_state_run(s,bounded_blob_visit));arc_state_end(s);
   /* Even without a session limit, the owner's maximum is checked before decode. */
   s=arc_state_begin(wire,sizeof(wire));assert(!arc_state_error(s));
   assert(!arc_state_run(s,bounded_blob_visit)&&!strcmp(arc_state_error(s),"blob bounds"));arc_state_end(s);
   puts("declared decode bounds: tiny amplified record refused before blob allocation");
}
static uint8_t forged_pending[1024];
static size_t forged_pending_bytes;
static void forged_pending_u64(uint64_t value)
{codec_u64(forged_pending+forged_pending_bytes,value);forged_pending_bytes+=8u;}
static void forged_pending_visit(ArcState *s)
{if(!arc_state_pass(s)){size_t n;(void)arc_state_blob(s,"stores.pending",forged_pending,forged_pending_bytes,sizeof(forged_pending),&n);}}
static void test_pending_view_limit(void)
{
   /* Each resize is individually legal, but their derived views exceed the
    * admitted session budget. Refuse before allocating the third 8 MiB view. */
   forged_pending_bytes=0;forged_pending_u64(3);forged_pending_u64(6);
   for(unsigned file=0;file<3u;++file)for(unsigned op=0;op<2u;++op){
      forged_pending_u64(op?6u:1u);forged_pending_u64(RETRO_VFS_FILE_ACCESS_WRITE);
      forged_pending_u64(op?8u*1024u*1024u:0u);
      char path[]="Sbudget0";path[7]=(char)('0'+file);size_t n=sizeof(path);
      forged_pending_u64(n);memcpy(forged_pending+forged_pending_bytes,path,n);forged_pending_bytes+=n;
      forged_pending_u64(0);
   }
   size_t bytes;uint8_t *wire=capture_visit(forged_pending_visit,&bytes);
   unsigned before=mutations;ArcState *s=arc_state_begin_limited(wire,bytes,16u*1024u*1024u);
   assert(!arc_state_run(s,arc_state_io_snapshot)&&before==mutations&&!arc_state_io_pending());
   arc_state_end(s);free(wire);
   const unsigned declared[]={0,2,4,257,3};
   for(unsigned test=0;test<5u;++test) {
      forged_pending_bytes=0;forged_pending_u64(declared[test]);forged_pending_u64(3);
      for(unsigned file=0;file<3u;++file) {
         forged_pending_u64(1);forged_pending_u64(RETRO_VFS_FILE_ACCESS_WRITE);forged_pending_u64(0);
         char path[]="Sbudget0";path[7]=(char)('0'+file);size_t n=sizeof(path);
         forged_pending_u64(n);memcpy(forged_pending+forged_pending_bytes,path,n);forged_pending_bytes+=n;
         forged_pending_u64(0);
      }
      wire=capture_visit(forged_pending_visit,&bytes);s=arc_state_begin_limited(wire,bytes,16u*1024u*1024u);
      bool valid=arc_state_run(s,arc_state_io_snapshot);
      assert(valid==(declared[test]==3u)&&mutations==before&&!arc_state_io_pending());
      if(valid)assert(arc_state_commit(s)&&arc_state_io_pending());
      arc_state_end(s);free(wire);arc_state_io_clear();
   }
   puts("pending derived views: aggregate resize bounds, exact file-count capacity and four malformed counts refuse without native effects");
}
static uint8_t *derived_owner;
static void derived_visit(ArcState *s)
{ (void)arc_state_heap_derived(s,"fixture.derived",&derived_owner,2u*1024u*1024u,2u*1024u*1024u); }
static void test_independent_limits(void)
{
   derived_owner=calloc(2u*1024u*1024u,1u);assert(derived_owner);
   size_t bytes;uint8_t *wire=capture_visit(derived_visit,&bytes);assert(bytes<4096u);
   ArcState *s=arc_state_begin_bounded(wire,bytes,4096u,4096u,3u*1024u*1024u,65536u);
   assert(arc_state_run(s,derived_visit)&&arc_state_commit(s));arc_state_end(s);
   uint8_t *previous=derived_owner;
   s=arc_state_begin_bounded(wire,bytes,4096u,4096u,1024u*1024u,65536u);
   assert(!arc_state_run(s,derived_visit)&&derived_owner==previous);arc_state_end(s);
   s=arc_state_begin_bounded(wire,bytes,4096u,4096u,3u*1024u*1024u,1u);
   assert(!arc_state_run(s,derived_visit)&&derived_owner==previous);arc_state_end(s);
   free(wire);free(derived_owner);derived_owner=NULL;
   puts("independent budgets: small wire, large derived ownership, ownership/scratch refusals preserve live heap");
}
static void test_compact_codec(void)
{
   memset(codec_dirty,0xff,sizeof(codec_dirty));
   codec_pixels=calloc(4096u*2048u,sizeof(*codec_pixels));assert(codec_pixels);
   for(unsigned i=0;i<sizeof(codec_base);++i)codec_base[i]=(uint8_t)(i*13u+5u);
   memcpy(codec_current,codec_base,sizeof(codec_base));codec_current[700]^=0x3au;codec_current[sizeof(codec_current)-1u]^=0x42u;
   codec_pixels[2u*4096u+7u]=0x12345678u;arc_state_raster_mark(codec_rows,2u,0,1024u);
   codec_pixels[4096u*2048u-1u]=0x23456789u;arc_state_raster_mark(codec_rows,2047u,4095u,1u);
   size_t bytes,n;uint8_t *saved=capture_visit(codec_visit,&bytes);assert(bytes<8192u);
   codec_current[1900]^=0x6bu;
   if(codec_indexed)codec_dirty[(1900u/512u)/8u]|=(uint8_t)(1u<<((1900u/512u)%8u));codec_pixels[2u*4096u+7u]=0;
   codec_pixels[4u*4096u+8u]=0xabcdefu;arc_state_raster_mark(codec_rows,4u,8u,1u);
   uint8_t mask_before[2];memcpy(mask_before,codec_dirty,sizeof(mask_before));
   uint8_t pending[sizeof(codec_current)];memcpy(pending,codec_current,sizeof(pending));
   ArcState *transaction=arc_state_begin(saved,bytes);assert(arc_state_run(transaction,codec_visit));
   assert(!memcmp(pending,codec_current,sizeof(pending))&&!memcmp(mask_before,codec_dirty,sizeof(mask_before)));arc_state_end(transaction);
   assert(restore_visit(codec_visit,saved,bytes));
   if(codec_indexed)assert(codec_dirty[0]==2u&&codec_dirty[1]==1u);
   assert(codec_current[sizeof(codec_current)-1u]==(uint8_t)(codec_base[sizeof(codec_base)-1u]^0x42u));
   assert(codec_current[700]==(uint8_t)(codec_base[700]^0x3au)&&codec_current[1900]==codec_base[1900]);
   assert(codec_pixels[2u*4096u+7u]==0x12345678u&&!codec_pixels[4u*4096u+8u]&&codec_pixels[4096u*2048u-1u]==0x23456789u);
   uint8_t *again=capture_visit(codec_visit,&n);assert(n==bytes&&!memcmp(saved,again,n));free(again);
   uint8_t before[sizeof(codec_current)];memcpy(before,codec_current,sizeof(before));
   for(codec_forge=1u;codec_forge<=6u;++codec_forge) {
      uint8_t *bad=capture_visit(codec_visit,&n);unsigned mode=codec_forge;codec_forge=0;
      assert(!restore_visit(codec_visit,bad,n)&&!memcmp(before,codec_current,sizeof(before)));
      assert(codec_pixels[2u*4096u+7u]==0x12345678u&&codec_pixels[4096u*2048u-1u]==0x23456789u);
      codec_forge=mode;free(bad);
   }
   /* Spare frontend capacity is framed, never interpreted as machine data. */
   size_t capacity=bytes+4096u;uint8_t *framed=malloc(capacity);assert(framed);
   ArcState *wire=arc_state_begin(NULL,0);assert(arc_state_run(wire,codec_visit));
   memset(framed,0xa7,capacity);assert(arc_state_output(wire,framed,capacity));arc_state_end(wire);
   for(size_t i=bytes;i<capacity-16u;++i)assert(!framed[i]);
   framed[bytes+128u]=0xadu;assert(restore_visit(codec_visit,framed,capacity));
   framed[capacity-1u]^=1u;assert(!restore_visit(codec_visit,framed,capacity));
   assert(!memcmp(before,codec_current,sizeof(before)));free(framed);
   codec_forge=0;codec_base[0]^=1u;
   assert(!restore_visit(codec_visit,saved,bytes)&&!memcmp(before,codec_current,sizeof(before)));
   codec_base[0]^=1u;free(saved);free(codec_pixels);codec_pixels=NULL;
   puts("compact codec: sector deltas, retained spans, canonical bytes, copy ordering and seven malformed/base refusals passed");
}
static void test_raster_copies(void)
{
   codec_pixels=calloc(4096u*2048u,4u);copy_pixels=calloc(4096u*2048u,4u);assert(codec_pixels&&copy_pixels);
   memset(codec_rows,0,sizeof(codec_rows));memset(copy_rows,0,sizeof(copy_rows));
   /* An odd RGB444 row, an RGB888 UI row and a full-width alternating row.
    * Later rows and the second surface must derive their copies from the
    * saved rows, even when every live source has been overwritten. */
   for(unsigned x=0;x<4096u;++x)codec_pixels[4096u+x]=x&1u?0x00ff11eeu:0x0011ee22u;
   codec_pixels[2u*4096u+3u]=0x00aa33eeu;codec_pixels[2u*4096u+4u]=0x00cc44ffu;
   codec_pixels[2u*4096u+5u]=0x00ee5500u;
   codec_pixels[3u*4096u+3u]=0x00123456u;codec_pixels[3u*4096u+4u]=0x00789abcu;
   for(unsigned x=0;x<13u;++x)codec_pixels[5u*4096u+x]=((x*255u/12u)<<16u)|(21u<<8u)|42u;
   /* Large zero interiors compress the blob itself. A later surface's row
    * index must still address the uncompressed recipes, not packed bytes. */
   codec_pixels[6u*4096u]=0x003377bbu;codec_pixels[7u*4096u]=0x00ff11eeu;
   codec_pixels[7u*4096u-1u]=codec_pixels[8u*4096u-1u]=0x004488aau;
   memcpy(codec_pixels+4u*4096u,codec_pixels+4096u,4096u*4u);
   arc_state_raster_mark(codec_rows,1,0,4096);arc_state_raster_mark(codec_rows,2,3,3);
   arc_state_raster_mark(codec_rows,3,3,2);arc_state_raster_mark(codec_rows,4,0,4096);
   arc_state_raster_mark(codec_rows,5,0,13);
   arc_state_raster_mark(codec_rows,6,0,4096);arc_state_raster_mark(codec_rows,7,0,4096);
   memcpy(copy_pixels,codec_pixels,4096u*2048u*4u);memcpy(copy_rows,codec_rows,sizeof(copy_rows));
   size_t bytes,n;uint8_t *saved=capture_visit(codec_visit,&bytes);assert(bytes<8000u);
   memset(codec_pixels,0,8u*4096u*4u);memset(copy_pixels,0,8u*4096u*4u);
   assert(restore_visit(codec_visit,saved,bytes));
   assert(codec_pixels[4097]==0x00ff11eeu&&codec_pixels[2u*4096u+5u]==0x00ee5500u);
   assert(codec_pixels[3u*4096u+4u]==0x00789abcu&&codec_pixels[4u*4096u+1u]==0x00ff11eeu);
   for(unsigned x=0;x<13u;++x)assert(codec_pixels[5u*4096u+x]==(((x*255u/12u)<<16u)|(21u<<8u)|42u));
   assert(!memcmp(codec_pixels,copy_pixels,4096u*2048u*4u));
   uint8_t *again=capture_visit(codec_visit,&n);assert(n==bytes&&!memcmp(saved,again,n));
   free(again);free(saved);free(codec_pixels);free(copy_pixels);codec_pixels=copy_pixels=NULL;
   puts("raster recipes: odd RGB444, UI RGB888, full-width row copies and cross-surface reconstruction passed");
}
static struct retro_vfs_interface *io_proxy;
static void setup(void)
{
   strict_directories=true;
   const char *parents[]={"save","save/arcdroid","save/arcdroid/state-stores","media"};
   for(unsigned i=0;i<4u;++i)if(!directory_exists(parents[i]))add_directory(parents[i]);
   io_proxy=arc_state_io_attach(&vfs);arc_io_set_vfs(io_proxy);arc_state_io_forget_stores();
   arc_state_io_root("save");arc_state_io_binding(0,"media/raw.adf");
}
static ArcInstallStore *indexed_hd;
static void indexed_hd_visit(ArcState *s)
{ state_visit(s);arc_install_store_snapshot(s,indexed_hd); }
static void test_indexed_hd_restore(void)
{
   clear_files();setup();
   indexed_hd=open_store("1234567890abcdef1234567890abcdef12345678",true,true,ARC_INSTALL_STORE_READY);
   size_t n,m;uint8_t *clean=capture_visit(indexed_hd_visit,&n);
   uint8_t before[512],extra[512],changed[512];
   assert(arc_install_store_read(indexed_hd,100u,1u,before));
   assert(arc_install_store_read(indexed_hd,2000u,1u,extra));
   memcpy(changed,before,sizeof(changed));changed[17]^=0x5a;
   unsigned native=mutations;arc_state_io_speculate(true);
   assert(arc_install_store_write(indexed_hd,100u,1u,changed));
   uint8_t *dirty=capture_visit(indexed_hd_visit,&m);
   assert(arc_install_store_write(indexed_hd,100u,1u,before));
   changed[23]^=0x63;assert(arc_install_store_write(indexed_hd,2000u,1u,changed));
   assert(restore_visit(indexed_hd_visit,dirty,m));
   const uint8_t *image=arc_install_store_image(indexed_hd);assert(image);
   assert(image[100u*512u+17u]==(uint8_t)(before[17]^0x5a));
   assert(!memcmp(image+2000u*512u,extra,sizeof(extra)));
   size_t again_bytes;uint8_t *again=capture_visit(indexed_hd_visit,&again_bytes);
   assert(again_bytes==m&&!memcmp(again,dirty,m));free(again);
   /* A second post-load mutation must be indexed too. */
   assert(arc_install_store_write(indexed_hd,2000u,1u,changed));
   assert(restore_visit(indexed_hd_visit,clean,n));
   image=arc_install_store_image(indexed_hd);
   assert(!memcmp(image+100u*512u,before,sizeof(before))&&!memcmp(image+2000u*512u,extra,sizeof(extra)));
   again=capture_visit(indexed_hd_visit,&again_bytes);
   assert(again_bytes==n&&!memcmp(again,clean,n)&&mutations==native);free(again);
   free(clean);free(dirty);arc_state_io_clear();arc_install_store_close(indexed_hd);indexed_hd=NULL;clear_files();
   puts("HD capture index: speculative writes, return-to-base, absent live deltas, repeated loads and canonical bytes; no native mutations");
}
static void raw_write(uint8_t value)
{
   ArcIoFile *f=arc_io_open_file("media/raw.adf",true);assert(f);
   uint8_t sector[512];memset(sector,value,sizeof(sector));
   assert(arc_io_seek_file(f,512,RETRO_VFS_SEEK_POSITION_START)==512);
   assert(arc_io_write_file(f,sector,sizeof(sector))==sizeof(sector));
   assert(arc_io_flush_file(f)&&arc_io_close_file(f));
}
static ArcIoFile *indexed_file;
static uint64_t indexed_reads,indexed_writes;
static int64_t indexed_read(struct retro_vfs_file_handle *file,void *data,uint64_t bytes)
{indexed_reads+=bytes;return vread(file,data,bytes);}
static int64_t indexed_write(struct retro_vfs_file_handle *file,const void *data,uint64_t bytes)
{int64_t n=vwrite(file,data,bytes);if(n>0)indexed_writes+=(uint64_t)n;return n;}
static int64_t indexed_tell(struct retro_vfs_file_handle *file){return (int64_t)file->at;}
static void indexed_visit(ArcState *s)
{
   arc_state_region(s,"fixture.indexed.slot",&indexed_file,sizeof(indexed_file));
   arc_io_state_file(s,"fixture.indexed",indexed_file);
   if(arc_state_pass(s)){
      arc_state_object(s,"fixture.indexed.slot");
      arc_state_typed_reference(s,&indexed_file,0,1,1,ARC_STATE_REFERENCE_FILE);
   }
}
static void test_dirty_media_index(void)
{
   clear_files();setup();arc_io_state_clear();
   uint8_t base[4096];for(unsigned i=0;i<sizeof(base);++i)base[i]=(uint8_t)(i*13u+7u);
   add_file("media","base.adf",base,sizeof(base));add_file("media","raw.adf",base,sizeof(base));
   struct retro_vfs_interface measured=vfs;measured.read=indexed_read;measured.tell=indexed_tell;
   arc_io_set_vfs(arc_state_io_attach(&measured));arc_io_state_binding(0,"media/raw.adf",true);
   arc_io_state_base_path(0,"media/base.adf");indexed_file=arc_io_open_file("media/raw.adf",true);assert(indexed_file);
   size_t clean_bytes,dirty_bytes,again_bytes;indexed_reads=0;
   uint8_t *clean=capture_visit(indexed_visit,&clean_bytes);assert(!indexed_reads);
   ArcIoFile *alias=arc_io_open_file("media/raw.adf",true);assert(alias);
   uint8_t sector[512];memset(sector,0x64,sizeof(sector));
   assert(arc_io_seek_file(alias,512,RETRO_VFS_SEEK_POSITION_START)>=0&&arc_io_write_file(alias,sector,sizeof(sector))==sizeof(sector));
   assert(arc_io_seek_file(alias,1536,RETRO_VFS_SEEK_POSITION_START)>=0&&arc_io_write_file(alias,sector,sizeof(sector))==sizeof(sector));
   assert(arc_io_close_file(alias));indexed_reads=0;
   uint8_t *dirty=capture_visit(indexed_visit,&dirty_bytes);assert(dirty_bytes==clean_bytes+2u*520u&&indexed_reads==2u*512u);
   assert(restore_visit(indexed_visit,dirty,dirty_bytes));indexed_reads=0;
   uint8_t *again=capture_visit(indexed_visit,&again_bytes);
   assert(again_bytes==dirty_bytes&&!memcmp(dirty,again,dirty_bytes)&&!indexed_reads);free(again);
   /* After restore the original file reads an owned view. A separately opened
    * native alias must update both guest readback and the next snapshot. */
   alias=arc_io_open_file("media/raw.adf",true);assert(alias);
   memset(sector,0x75,sizeof(sector));assert(arc_io_seek_file(alias,1536,RETRO_VFS_SEEK_POSITION_START)>=0);
   assert(arc_io_write_file(alias,sector,sizeof(sector))==sizeof(sector)&&arc_io_close_file(alias));
   uint8_t observed[512];assert(arc_io_seek_file(indexed_file,1536,RETRO_VFS_SEEK_POSITION_START)>=0);
   assert(arc_io_read_file(indexed_file,observed,sizeof(observed))==sizeof(observed)&&!memcmp(observed,sector,sizeof(observed)));
   assert(arc_io_seek_file(indexed_file,512,RETRO_VFS_SEEK_POSITION_START)>=0&&arc_io_write_file(indexed_file,base+512,512)==512);
   indexed_reads=0;again=capture_visit(indexed_visit,&again_bytes);
   assert(again_bytes==clean_bytes+520u&&!indexed_reads);
   free(clean);free(dirty);free(again);assert(arc_io_close_file(indexed_file));indexed_file=NULL;
   arc_io_state_clear();arc_io_set_vfs(arc_state_io_attach(&vfs));clear_files();
   puts("raw capture: 0/1/2 dirty sectors, alias updates, restoration; native reads limited to changed sectors");
}
static void test_lazy_raw_frontier(void)
{
   clear_files();setup();arc_io_state_clear();uint8_t *original=malloc(819200u);assert(original);memset(original,0x31,819200u);
   add_file("media","raw.adf",original,819200u);struct retro_vfs_interface measured=vfs;
   measured.read=indexed_read;measured.write=indexed_write;measured.tell=indexed_tell;
   arc_io_set_vfs(arc_state_io_attach(&measured));arc_state_io_forget_stores();
   assert(arc_state_io_store_lazy("media/raw.adf","RAW:authored"));
   indexed_reads=indexed_writes=0;
   for(unsigned i=0;i<16u;++i)raw_write(i&1u?0x31u:0x42u);
   assert(indexed_reads==0&&indexed_writes==16u*512u);
   /* Publication before the first checkpoint must update its native authority,
    * independent of the emulated, speculative mirror. */
   arc_state_io_speculate(true);raw_write(0x54u);assert(arc_state_io_publish());arc_state_io_speculate(false);
   indexed_reads=indexed_writes=0;size_t bytes;uint8_t *saved=capture(&bytes);
   assert(indexed_writes==168u&&indexed_reads==168u+512u); /* Small checkpoint plus one written sector, no image scan. */
   /* Once captured, each write uses two 688-byte range records and the
    * 512-byte sector, independent of the 800 KiB image extent. */
   indexed_reads=indexed_writes=0;
   for(unsigned i=0;i<16u;++i)raw_write(i&1u?0x54u:0x63u);
   assert(indexed_reads==16u*1376u&&indexed_writes==16u*1888u);
   indexed_reads=indexed_writes=0;size_t latest_bytes;uint8_t *latest=capture(&latest_bytes);
   assert(!indexed_reads&&!indexed_writes); /* Already cached; no image scan. */
   unsigned before=mutations;assert(!restore(saved,bytes)&&before==mutations);
   setup();arc_io_set_vfs(arc_state_io_attach(&measured));assert(arc_state_io_store_lazy("media/raw.adf","RAW:authored"));assert(restore(latest,latest_bytes));
   indexed_reads=indexed_writes=0;raw_write(0x54u);
   assert(indexed_reads==1376u&&indexed_writes==1888u); /* Loading also re-arms range protection. */
   free(latest);latest=capture(&latest_bytes);
   setup();arc_io_set_vfs(arc_state_io_attach(&measured));assert(arc_state_io_store_lazy("media/raw.adf","RAW:authored"));
   /* A new session with old state history invalidates that history once,
    * before writing. The unchanged reuse is still loadable until that write. */
   indexed_reads=indexed_writes=0;raw_write(0x63u);
   assert(indexed_reads==168u&&indexed_writes==168u+512u);
   indexed_reads=indexed_writes=0;
   for(unsigned i=0;i<16u;++i)raw_write(i&1u?0x54u:0x63u);
   assert(!indexed_reads&&indexed_writes==16u*512u);
   before=mutations;assert(!restore(latest,latest_bytes)&&before==mutations);
   /* ABA and cold reopen cannot reseal an invalidated state. */
   setup();arc_io_set_vfs(arc_state_io_attach(&measured));assert(arc_state_io_store_lazy("media/raw.adf","RAW:authored"));
   before=mutations;assert(!restore(latest,latest_bytes)&&before==mutations);
   free(latest);raw_write(0x63u);raw_write(0x54u);before=mutations;
   assert(!restore(saved,bytes)&&before==mutations); /* ABA is still an older frontier. */
   free(saved);free(original);arc_state_io_clear();arc_state_io_forget_stores();arc_io_state_clear();clear_files();
   puts("lazy raw frontier: main's exact sector IO before states, 168-byte first checkpoint, post-capture sector redo, reused-folder native writes, cold reopen and ABA refusal");
}
static void test_sparse_raw_view(void)
{
   clear_files();setup();arc_io_state_clear();uint8_t original[4096],expected[4096],observed[4096],patch[700];
   for(unsigned i=0;i<sizeof(original);++i)original[i]=(uint8_t)(i*13u+7u);
   memcpy(expected,original,sizeof(expected));memset(patch,0x64,sizeof(patch));
   add_file("media","raw.adf",original,sizeof(original));struct retro_vfs_interface measured=vfs;
   measured.read=indexed_read;measured.write=indexed_write;
   arc_io_set_vfs(arc_state_io_attach(&measured));assert(arc_state_io_store_lazy("media/raw.adf","RAW:authored"));
   size_t clean_bytes;uint8_t *clean=capture(&clean_bytes);unsigned before=mutations;
   indexed_reads=indexed_writes=0;arc_state_io_speculate(true);
   ArcIoFile *f=arc_io_open_file("media/raw.adf",true);assert(f);
   assert(arc_io_seek_file(f,500,RETRO_VFS_SEEK_POSITION_START)==500);
   assert(arc_io_write_file(f,patch,sizeof(patch))==sizeof(patch));memcpy(expected+500,patch,sizeof(patch));
   memset(patch,0x75,20);assert(arc_io_seek_file(f,800,RETRO_VFS_SEEK_POSITION_START)==800);
   assert(arc_io_write_file(f,patch,20)==20);memcpy(expected+800,patch,20);
   assert(arc_io_seek_file(f,0,RETRO_VFS_SEEK_POSITION_START)==0&&arc_io_read_file(f,observed,sizeof(observed))==sizeof(observed));
   assert(!memcmp(expected,observed,sizeof(expected))&&!indexed_reads&&!indexed_writes&&before==mutations);
   size_t bytes,again_bytes;uint8_t *saved=capture(&bytes);assert(!indexed_reads&&!indexed_writes&&before==mutations);
   assert(restore(saved,bytes));assert(arc_io_seek_file(f,0,RETRO_VFS_SEEK_POSITION_START)==0);
   assert(arc_io_read_file(f,observed,sizeof(observed))==sizeof(observed)&&!memcmp(expected,observed,sizeof(expected)));
   uint8_t *again=capture(&again_bytes);assert(bytes==again_bytes&&!memcmp(saved,again,bytes));free(again);
   assert(arc_state_io_publish());arc_state_io_speculate(false);int raw=locate("media/raw.adf");
   assert(raw>=0&&!memcmp(files[raw].bytes,expected,sizeof(expected)));
   assert(arc_io_close_file(f));free(saved);free(clean);arc_state_io_forget_stores();arc_io_state_clear();clear_files();
   puts("sparse raw speculation: unaligned/overlapping writes, readback and capture use zero native IO; canonical restored view and publication exact");
}
static void test_sparse_namespace(void)
{
   for(unsigned mode=0;mode<3u;++mode) {
      clear_files();setup();uint8_t original[2048],expected[2048];memset(original,0x31,sizeof(original));memcpy(expected,original,sizeof(original));
      add_file("media","raw.adf",original,sizeof(original));assert(arc_state_io_store_lazy("media/raw.adf","RAW:authored"));
      arc_state_io_binding(1,"media/moved.adf");size_t clean_bytes;uint8_t *clean=capture(&clean_bytes);
      unsigned before=mutations;arc_state_io_speculate(true);raw_write(0x63);memset(expected+512,0x63,512);
      const char *read_path="media/raw.adf";size_t expected_bytes=sizeof(expected);
      if(mode==0u){assert(io_proxy->rename("media/raw.adf","media/moved.adf")==0);read_path="media/moved.adf";}
      else if(mode==1u){
         ArcIoFile *f=arc_io_open_file(read_path,true);assert(f&&arc_io_truncate_file(f,600)&&arc_io_truncate_file(f,1200));
         assert(arc_io_close_file(f));memset(expected+600,0,600);expected_bytes=1200;
      } else {assert(arc_io_write_all(read_path,expected,256));expected_bytes=256;}
      void *view;size_t n;assert(arc_io_read_all(read_path,&view,&n)&&n==expected_bytes&&!memcmp(view,expected,n));free(view);
      assert(mutations==before);size_t bytes;uint8_t *saved=capture(&bytes);assert(restore(saved,bytes));
      assert(arc_io_read_all(read_path,&view,&n)&&n==expected_bytes&&!memcmp(view,expected,n));free(view);
      uint8_t *again=capture(&n);assert(n==bytes&&!memcmp(saved,again,n));free(again);free(saved);
      assert(restore(clean,clean_bytes)&&mutations==before);free(clean);
      int raw=locate("media/raw.adf");assert(raw>=0&&files[raw].size==sizeof(original)&&!memcmp(files[raw].bytes,original,sizeof(original)));
      arc_state_io_clear();arc_state_io_forget_stores();clear_files();
   }
   /* VFS truncate returns zero, both while staging and during publication. */
   setup();uint8_t data[2048]={0};add_file("media","plain.adf",data,sizeof(data));
   arc_state_io_speculate(true);ArcIoFile *f=arc_io_open_file("media/plain.adf",true);
   assert(f&&arc_io_truncate_file(f,600)&&arc_io_close_file(f)&&arc_state_io_publish());arc_state_io_speculate(false);
   int plain=locate("media/plain.adf");assert(plain>=0&&files[plain].size==600u);
   arc_state_io_forget_stores();clear_files();
   puts("sparse namespace: rename, shrink/extend, truncating open and canonical reload; discard changes no native bytes; VFS truncate publication succeeds");
}
static void test_sparse_failed_publication(void)
{
   unsigned cases=0;
   for(unsigned step=1;step<40u;++step) {
      clear_files();setup();uint8_t original[2048],expected[2048],sector[512];memset(original,0x31,sizeof(original));memcpy(expected,original,sizeof(original));
      add_file("media","raw.adf",original,sizeof(original));assert(arc_state_io_store_lazy("media/raw.adf","RAW:authored"));
      size_t bytes;uint8_t *saved=capture(&bytes);free(saved);arc_state_io_speculate(true);
      ArcIoFile *f=arc_io_open_file("media/raw.adf",true);assert(f);
      for(unsigned i=1;i<=2u;++i) {
         memset(sector,(int)(0x42u+i),sizeof(sector));memcpy(expected+i*512u,sector,512);
         assert(arc_io_seek_file(f,(int64_t)(i*512u),RETRO_VFS_SEEK_POSITION_START)>=0&&arc_io_write_file(f,sector,512)==512&&arc_io_flush_file(f));
      }
      assert(arc_io_close_file(f));fail_mutation=mutations+step;bool published=arc_state_io_publish(),interrupted=!fail_mutation;fail_mutation=0;
      unsigned before=mutations;void *view;size_t n;
      assert(arc_io_read_all("media/raw.adf",&view,&n)&&n==sizeof(expected)&&!memcmp(view,expected,n)&&mutations==before);free(view);
      if(!published)assert(arc_state_io_pending());
      arc_state_io_clear();arc_state_io_forget_stores();++cases;
      if(!interrupted){assert(published);break;}
   }
   clear_files();printf("sparse failed publication: %u interruption points retain exact readback over committed prefixes, without further native mutations\n",cases);
}
static void test_legacy_frontier(void)
{
   /* Existing ACFR2 full-image applied and pending records must reopen, then
    * use the bounded ACFR3 range representation without resetting generation. */
   for(unsigned kind=1;kind<=3u;++kind) {
      clear_files();setup();uint8_t original[2048],record[2208]={0};memset(original,0x31,sizeof(original));
      add_file("media","raw.adf",original,sizeof(original));
      char identity[41],name[96];arc_sha1_hex("RAW:authored",12,identity);
      memcpy(record,"ACFR2",5);codec_u64(record+8,7);codec_u64(record+16,kind);
      codec_u64(record+24,kind==3u?8u:sizeof(original));codec_u64(record+32,sizeof(original));memcpy(record+40,identity,40);
      size_t length=kind==3u?168u:sizeof(record);
      if(kind==3u) {
         /* Produce a legacy checkpoint with the same sector-labelled hash via
          * a real capture, then change only its magic/generation and checksums. */
         assert(arc_state_io_store_lazy("media/raw.adf","RAW:authored"));size_t n;uint8_t *state=capture(&n);free(state);
         snprintf(name,sizeof(name),"save/arcdroid/state-stores/%s.a",identity);int slot=locate(name);assert(slot>=0);
         memcpy(record+160,files[slot].bytes+160,8);assert(vremove(name)==0);setup();
      } else {memcpy(record+160,original,sizeof(original));memset(record+160+512,0x42,512);}
      codec_u64(record+88,arc_crc32(record+160,length-160));
      uint8_t header[144];memcpy(header,record,80);memcpy(header+80,record+96,64);codec_u64(record+80,arc_crc32(header,sizeof(header)));
      snprintf(name,sizeof(name),"%s.a",identity);add_file("save/arcdroid/state-stores",name,record,length);
      if(kind==2u){int raw=locate("media/raw.adf");memcpy(files[raw].bytes,record+160,sizeof(original));}
      assert(arc_state_io_store_lazy("media/raw.adf","RAW:authored"));
      size_t bytes;uint8_t *state=capture(&bytes);assert(restore(state,bytes));free(state);
      raw_write(0x63);
      snprintf(name,sizeof(name),"save/arcdroid/state-stores/%s.%c",identity,kind==1u?'b':'a');
      int applied=locate(name);assert(applied>=0&&files[applied].size==688u&&files[applied].bytes[8]==8u&&!memcmp(files[applied].bytes,"ACFR3",5));
      setup();assert(arc_state_io_store_lazy("media/raw.adf","RAW:authored"));
      int raw=locate("media/raw.adf");assert(raw>=0&&files[raw].bytes[512]==0x63);
   }
   arc_state_io_forget_stores();arc_io_state_clear();clear_files();
   puts("frontier upgrade: ACFR2 intent/applied/checkpoint recover, preserve generations and adopt sector-sized ACFR3 records");
}
static void test_reused_invalidation_failures(void)
{
   unsigned cases=0;
   for(unsigned step=1;step<16u;++step) {
      clear_files();setup();uint8_t original[2048]={0},sector[512];memset(sector,0x63,sizeof(sector));
      add_file("media","raw.adf",original,sizeof(original));assert(arc_state_io_store_lazy("media/raw.adf","RAW:authored"));
      size_t bytes;uint8_t *saved=capture(&bytes);
      setup();assert(arc_state_io_store_lazy("media/raw.adf","RAW:authored"));
      ArcIoFile *f=arc_io_open_file("media/raw.adf",true);assert(f&&arc_io_seek_file(f,512,RETRO_VFS_SEEK_POSITION_START)==512);
      fail_mutation=mutations+step;size_t wrote=arc_io_write_file(f,sector,sizeof(sector));bool interrupted=!fail_mutation;
      fail_mutation=0;assert(arc_io_close_file(f));setup();assert(arc_state_io_store_lazy("media/raw.adf","RAW:authored"));
      unsigned before=mutations;bool accepted=restore(saved,bytes);assert(before==mutations);
      int raw=locate("media/raw.adf");assert(raw>=0);
      if(wrote)assert(wrote==512u&&!accepted&&!memcmp(files[raw].bytes+512,sector,512));
      if(accepted)assert(!memcmp(files[raw].bytes,original,sizeof(original)));
      free(saved);++cases;if(!interrupted){assert(wrote==512u);break;}
   }
   arc_state_io_forget_stores();clear_files();
   printf("reused-folder invalidation: %u interruption points; no old state accepted after a native write\n",cases);
}
static void test_unsealed_original_identity(void)
{
   clear_files();setup();uint8_t original[2048];memset(original,0x31,sizeof(original));
   add_file("media","raw.adf",original,sizeof(original));ArcDigest first=arc_digest(original,sizeof(original)),id=first;
   assert(arc_state_io_original("media/raw.adf",&id));
   assert(arc_state_io_store_lazy("media/raw.adf","RAW-ORIGINAL:media/raw.adf"));
   size_t n;uint8_t *saved=capture(&n);free(saved);
   setup();id=first;assert(arc_state_io_original("media/raw.adf",&id));
   assert(arc_state_io_store_lazy("media/raw.adf","RAW-ORIGINAL:media/raw.adf"));raw_write(0x63u);
   /* Leave the no-state epoch unsealed; then replace bytes outside the core. */
   int raw=locate("media/raw.adf");assert(raw>=0);files[raw].bytes[0]^=0x42u;
   ArcDigest replacement=arc_digest(files[raw].bytes,files[raw].size);setup();id=replacement;
   assert(arc_state_io_original("media/raw.adf",&id)&&!strcmp(id.sha1,replacement.sha1));
   arc_state_io_forget_stores();clear_files();puts("unsealed Original media: changed bytes establish identity; no previous content CMOS/base binding survives replacement");
}
static void test_range_truncation(void)
{
   clear_files();setup();uint8_t original[2048]={0},changed[512];memset(changed,0x74,sizeof(changed));
   add_file("media","raw.adf",original,sizeof(original));assert(arc_state_io_store("media/raw.adf","RAW:authored"));
   arc_state_io_speculate(true);assert(arc_io_write_all("media/raw.adf",changed,sizeof(changed)));
   int raw=locate("media/raw.adf");assert(raw>=0&&files[raw].size==sizeof(original));
   assert(arc_state_io_publish());arc_state_io_speculate(false);
   assert(files[raw].size==512u&&!memcmp(files[raw].bytes,changed,512));
   setup();assert(arc_state_io_store("media/raw.adf","RAW:authored"));
   assert(files[raw].size==512u&&!memcmp(files[raw].bytes,changed,512));
   arc_state_io_forget_stores();clear_files();puts("range redo: staged truncating writes remove old tails and recover idempotently");
}
static void test_failed_publication_order(void)
{
   clear_files();setup();uint8_t original[2048]={0},sector[512];memset(sector,0x42,sizeof(sector));
   add_file("media","raw.adf",original,sizeof(original));
   ArcIoFile *existing=arc_io_open_file("media/raw.adf",true);assert(existing);
   arc_state_io_speculate(true);raw_write(0x63);
   assert(arc_state_io_pending());fail_mutation=mutations+1u;
   assert(!arc_state_io_publish()&&!fail_mutation);
   arc_state_io_speculate(false);unsigned before=mutations;
   assert(!arc_io_open_file("media/raw.adf",true));
   assert(arc_io_write_file(existing,sector,sizeof(sector))==0u);
   assert(!arc_io_flush_file(existing));
   struct retro_vfs_interface *proxy=io_proxy;
   assert(proxy->remove("media/raw.adf")<0);
   assert(proxy->rename("media/raw.adf","media/new.adf")<0);
   assert(proxy->mkdir("media/new")<0);
   assert(!arc_io_truncate_file(existing,0));
   assert(mutations==before&&arc_state_io_pending()&&!arc_state_io_publish());
   arc_state_io_speculate(true);void *view=NULL;size_t n=0;
   assert(arc_io_read_all("media/raw.adf",&view,&n)&&n==sizeof(original)&&((uint8_t *)view)[512]==0x63u);
   free(view);arc_state_io_speculate(false);assert(mutations==before);
   int raw=locate("media/raw.adf");assert(raw>=0&&!memcmp(files[raw].bytes,original,sizeof(original)));
   assert(arc_io_close_file(existing));arc_state_io_clear();clear_files();
   puts("failed publication: later opens, writes, flushes and namespace changes fail closed in order");
}
static void test_pending_budget(void)
{
   clear_files();setup();uint8_t original[2048]={0},sector[512]={0};
   add_file("media","raw.adf",original,sizeof(original));unsigned before=mutations;
   arc_state_io_speculate(true);ArcIoFile *f=arc_io_open_file("media/raw.adf",true);assert(f);
   unsigned writes=0;size_t checkpoint_bytes=0;uint8_t *checkpoint=NULL;
   for(;writes<65536u;++writes) {
      if(writes==100u)checkpoint=capture(&checkpoint_bytes);
      assert(arc_io_seek_file(f,0,RETRO_VFS_SEEK_POSITION_START)==0);
      sector[0]=(uint8_t)writes;if(arc_io_write_file(f,sector,sizeof(sector))!=sizeof(sector))break;
   }
   assert(writes>100u&&writes<65536u&&mutations==before);
   unsigned exhausted_at=writes;assert(restore(checkpoint,checkpoint_bytes));free(checkpoint);
   for(writes=100u;writes<65536u;++writes) {
      assert(arc_io_seek_file(f,0,RETRO_VFS_SEEK_POSITION_START)==0);
      sector[0]=(uint8_t)writes;if(arc_io_write_file(f,sector,sizeof(sector))!=sizeof(sector))break;
   }
   assert(writes==exhausted_at&&mutations==before);
   ArcState *s=arc_state_begin(NULL,0);assert(!arc_state_run(s,state_visit));arc_state_end(s);
   assert(arc_io_close_file(f));arc_state_io_clear();arc_state_io_forget_stores();
   assert(!arc_state_io_pending()&&mutations==before);clear_files();
   puts("pending budget: bounded ownership, failed snapshot, zero native mutations and cleanup passed");
}
static void test_pending_wire_bound(void)
{
   clear_files();setup();uint8_t sector[1024]={0};
   add_file("media","raw.adf",sector,sizeof(sector));unsigned before=mutations;
   size_t old=arc_state_io_pending_bound(),small=arc_state_io_session_bound(1024u,256u);
   assert(small&&small<old/4u&&arc_state_io_pending_bound()==small);
   arc_state_io_root("save");assert(arc_state_io_pending_bound()==small);
   assert(!arc_state_io_session_bound(SIZE_MAX,256u)&&arc_state_io_pending_bound()==small);
   arc_state_io_speculate(true);ArcIoFile *f=arc_io_open_file("media/raw.adf",true);assert(f);
   for(unsigned i=0;i<300u;++i) {
      sector[0]=(uint8_t)i;
      assert(!arc_io_seek_file(f,0,RETRO_VFS_SEEK_POSITION_START));
      assert(arc_io_write_file(f,sector,sizeof(sector))==sizeof(sector)&&arc_io_flush_file(f));
   }
   size_t n;uint8_t *saved=capture(&n);assert(n<=small&&mutations==before);
   size_t low=16u,high=small;
   while(low<high){size_t middle=low+(high-low)/2u;if(arc_state_io_preflight(middle))high=middle;else low=middle+1u;}
   assert(arc_state_io_preflight(low)&&!arc_state_io_preflight(low-1u)&&!arc_state_io_preflight(65536u));
   assert(mutations==before);
   assert(restore(saved,n)&&arc_state_io_pending_bound()==small&&mutations==before);
   arc_state_io_clear();assert(arc_state_io_pending_bound()==small);free(saved);
   assert(arc_io_close_file(f));arc_state_io_forget_stores();clear_files();
   puts("pending wire bound: session producer limits, 300 ordered writes, restore and clear preserve capacity without native effects");
}
int main(void)
{
   test_declared_decode_limit();
   test_raster_copies();
   test_compact_codec();codec_sparse=true;test_compact_codec();codec_sparse=false;
   codec_indexed=true;test_compact_codec();codec_indexed=false;
   test_independent_limits();test_indexed_hd_restore();
   test_pending_budget();
   test_pending_wire_bound();
   test_failed_publication_order();
   test_dirty_media_index();
   test_lazy_raw_frontier();test_sparse_raw_view();test_sparse_namespace();test_sparse_failed_publication();test_legacy_frontier();test_reused_invalidation_failures();test_unsealed_original_identity();test_range_truncation();
   clear_files();setup();test_pending_view_limit();
   clear_files();setup();uint8_t original[2048];memset(original,0x31,sizeof(original));
   add_file("media","raw.adf",original,sizeof(original));
   assert(arc_state_io_store("media/raw.adf","RAW:authored"));
   raw_write(0x42);int raw=locate("media/raw.adf");assert(raw>=0&&files[raw].bytes[512]==0x42);
   size_t base_bytes=0;uint8_t *base=capture(&base_bytes);unsigned committed=mutations;
   arc_state_io_speculate(true);raw_write(0x63);
   assert(mutations==committed&&files[raw].bytes[512]==0x42&&arc_state_io_pending());
   void *view=NULL;size_t view_size=0;assert(arc_io_read_all("media/raw.adf",&view,&view_size));
   assert(view_size==sizeof(original)&&((uint8_t*)view)[512]==0x63);free(view);
   size_t pending_bytes=0;uint8_t *pending=capture(&pending_bytes);
   raw_write(0x74);assert(restore(pending,pending_bytes));
   assert(arc_io_read_all("media/raw.adf",&view,&view_size)&&((uint8_t*)view)[512]==0x63);free(view);
   assert(restore(base,base_bytes)&&!arc_state_io_pending()&&mutations==committed);
   raw_write(0x85);assert(arc_state_io_publish());arc_state_io_speculate(false);
   assert(files[raw].bytes[512]==0x85&&!arc_state_io_pending());
   assert(!restore(base,base_bytes)&&files[raw].bytes[512]==0x85);
   free(base);free(pending);
   /* All mutation failure points, followed by a cold reopen. An intent which
    * was published before interruption is recovered as the complete new image. */
   unsigned cases=0;
   for(unsigned step=1;step<40u;++step) {
      clear_files();setup();add_file("media","raw.adf",original,sizeof(original));
      assert(arc_state_io_store("media/raw.adf","RAW:authored"));
      ArcIoFile *f=arc_io_open_file("media/raw.adf",true);assert(f);
      uint8_t sector[512];memset(sector,0x96,sizeof(sector));
      assert(arc_io_seek_file(f,512,RETRO_VFS_SEEK_POSITION_START)==512);
      fail_mutation=mutations+step;
      size_t wrote=arc_io_write_file(f,sector,sizeof(sector));bool interrupted=fail_mutation==0;
      (void)arc_io_close_file(f);fail_mutation=0;setup();
      assert(arc_state_io_store("media/raw.adf","RAW:authored"));
      raw=locate("media/raw.adf");assert(raw>=0&&files[raw].size==sizeof(original));
      uint8_t value=files[raw].bytes[512];assert(value==0x31||value==0x96);
      for(unsigned i=0;i<512;++i)assert(files[raw].bytes[i]==0x31&&files[raw].bytes[i+1536u]==0x31);
      for(unsigned i=512;i<1024;++i)assert(files[raw].bytes[i]==value);
      ++cases;if(!interrupted){assert(wrote==512u&&value==0x96);break;}
   }
   /* A hot load must never accept an incomplete native journal or reset a
    * failed store's append cursor. Cold-open recovery alone may truncate it. */
   clear_files();arc_io_set_vfs(&vfs);strict_directories=false;
   ArcInstallStore *hd=open_store("abcdefabcdefabcdefabcdefabcdefabcdefabcd",true,true,ARC_INSTALL_STORE_READY);
   uint64_t frontier[2];assert(arc_install_store_frontier(hd,frontier));
   File *journal=file_named("journal.g0");assert(journal);
   uint8_t *tail=realloc(journal->bytes,journal->size+1u);assert(tail);
   journal->bytes=tail;tail[journal->size++]=0x41;
   unsigned before=mutations;assert(!arc_install_store_frontier(hd,frontier)&&before==mutations);
   arc_install_store_close(hd);
   arc_state_io_clear();arc_state_io_forget_stores();arc_io_set_vfs(NULL);clear_files();
   printf("save-state I/O: staging, restore, eager publication, stale frontier, %u interrupted-publication cases passed\n",cases);return 0;
}
