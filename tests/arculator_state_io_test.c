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
   ArcState *s=arc_state_begin(NULL,0);assert(arc_state_run(s,visit));
   *bytes=arc_state_size(s);uint8_t *p=malloc(*bytes);assert(p&&arc_state_output(s,p,*bytes));arc_state_end(s);return p;
}
static bool restore_visit(ArcStateVisit visit,const uint8_t *p,size_t bytes)
{
   ArcState *s=arc_state_begin(p,bytes);bool okay=arc_state_run(s,visit)&&arc_state_commit(s);arc_state_end(s);return okay;
}
static uint8_t *capture(size_t *bytes) {return capture_visit(state_visit,bytes);}
static bool restore(const uint8_t *p,size_t bytes) {return restore_visit(state_visit,p,bytes);}
static uint8_t codec_base[4096],codec_current[4096];
static uint32_t *codec_pixels;
static uint16_t codec_rows[2048][2];
static unsigned codec_forge;
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
      codec_u64(bad+16,codec_forge==1u?2u:8u);codec_u64(bad+536,2u);
      (void)arc_state_blob(s,"fixture.delta",bad,bytes,bytes,&n);
   } else {
      const void *next=arc_state_delta(s,"fixture.delta",codec_current,codec_base,sizeof(codec_base));
      if(arc_state_loading(s)&&next)arc_state_stage(s,codec_current,next,sizeof(codec_current));
   }
   if(codec_forge==3u) {
      uint8_t bad[12]={0};size_t n;codec_u64(bad,UINT64_C(2048)|(UINT64_C(1)<<32u));
      (void)arc_state_blob(s,"fixture.raster",bad,sizeof(bad),sizeof(bad),&n);
   } else arc_state_raster(s,"fixture.raster",codec_pixels,4096u,codec_rows);
}
static void test_compact_codec(void)
{
   codec_pixels=calloc(4096u*2048u,sizeof(*codec_pixels));assert(codec_pixels);
   for(unsigned i=0;i<sizeof(codec_base);++i)codec_base[i]=(uint8_t)(i*13u+5u);
   memcpy(codec_current,codec_base,sizeof(codec_base));codec_current[700]^=0x3au;
   codec_pixels[2u*4096u+7u]=0x12345678u;arc_state_raster_mark(codec_rows,2u,0,1024u);
   codec_pixels[4096u*2048u-1u]=0x23456789u;arc_state_raster_mark(codec_rows,2047u,4095u,1u);
   size_t bytes,n;uint8_t *saved=capture_visit(codec_visit,&bytes);assert(bytes<8192u);
   codec_current[1900]^=0x6bu;codec_pixels[2u*4096u+7u]=0;
   codec_pixels[4u*4096u+8u]=0xabcdefu;arc_state_raster_mark(codec_rows,4u,8u,1u);
   assert(restore_visit(codec_visit,saved,bytes));
   assert(codec_current[700]==(uint8_t)(codec_base[700]^0x3au)&&codec_current[1900]==codec_base[1900]);
   assert(codec_pixels[2u*4096u+7u]==0x12345678u&&!codec_pixels[4u*4096u+8u]&&codec_pixels[4096u*2048u-1u]==0x23456789u);
   uint8_t *again=capture_visit(codec_visit,&n);assert(n==bytes&&!memcmp(saved,again,n));free(again);
   uint8_t before[4096];memcpy(before,codec_current,sizeof(before));
   for(codec_forge=1u;codec_forge<=3u;++codec_forge) {
      uint8_t *bad=capture_visit(codec_visit,&n);unsigned mode=codec_forge;codec_forge=0;
      assert(!restore_visit(codec_visit,bad,n)&&!memcmp(before,codec_current,sizeof(before)));
      assert(codec_pixels[2u*4096u+7u]==0x12345678u&&codec_pixels[4096u*2048u-1u]==0x23456789u);
      codec_forge=mode;free(bad);
   }
   /* Spare frontend capacity is framed, never interpreted as machine data. */
   size_t capacity=bytes+4096u;uint8_t *framed=malloc(capacity);assert(framed);
   ArcState *wire=arc_state_begin(NULL,0);assert(arc_state_run(wire,codec_visit));
   assert(arc_state_output(wire,framed,capacity));arc_state_end(wire);
   framed[bytes+128u]=0xadu;assert(restore_visit(codec_visit,framed,capacity));
   framed[capacity-1u]^=1u;assert(!restore_visit(codec_visit,framed,capacity));
   assert(!memcmp(before,codec_current,sizeof(before)));free(framed);
   codec_forge=0;codec_base[0]^=1u;
   assert(!restore_visit(codec_visit,saved,bytes)&&!memcmp(before,codec_current,sizeof(before)));
   codec_base[0]^=1u;free(saved);free(codec_pixels);codec_pixels=NULL;
   puts("compact codec: sector deltas, retained spans, canonical bytes and four malformed/base refusals passed");
}
static void setup(void)
{
   strict_directories=true;
   const char *parents[]={"save","save/arcdroid","save/arcdroid/state-stores","media"};
   for(unsigned i=0;i<4u;++i)if(!directory_exists(parents[i]))add_directory(parents[i]);
   arc_io_set_vfs(arc_state_io_attach(&vfs));arc_state_io_forget_stores();
   arc_state_io_root("save");arc_state_io_binding(0,"media/raw.adf");
}
static void raw_write(uint8_t value)
{
   ArcIoFile *f=arc_io_open_file("media/raw.adf",true);assert(f);
   uint8_t sector[512];memset(sector,value,sizeof(sector));
   assert(arc_io_seek_file(f,512,RETRO_VFS_SEEK_POSITION_START)==512);
   assert(arc_io_write_file(f,sector,sizeof(sector))==sizeof(sector));
   assert(arc_io_flush_file(f)&&arc_io_close_file(f));
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
int main(void)
{
   test_compact_codec();
   test_pending_budget();
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
