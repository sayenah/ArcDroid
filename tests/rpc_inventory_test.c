/* Authored fingerprints and a finite mock VFS, never private ROMs. GPL-2.0-only. */
#include "rpc_inventory_limits.h"
#include "arc_io.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { char path[4096]; const uint8_t *data; size_t size; bool directory; } Entry;
static Entry entries[1100]; static unsigned count;
static int fail_open,fail_size,fail_read,fail_close,fail_stat,fail_name,fail_allocation;
static bool fail_dir_open,fail_dir_close,silent_end,root_invalid,root_file,negative_stat;
static bool reverse_order,short_read,grow_after_read,long_dirent;
static unsigned opens,closes,attempts,dir_opens,dir_closes,reads,allocations,live_buffers,stats;
static uint8_t data[4][40]; static ArcRpcFirmwareFingerprint table[4]; static ArcDigest digests[4];
struct retro_vfs_file_handle { unsigned at,read_count; };
struct retro_vfs_dir_handle { unsigned next,current; char path[4096]; };
void *arc_test_rpc_inventory_malloc(size_t size)
{
   unsigned at=allocations++;
   if(fail_allocation==-2||(int)at==fail_allocation)return NULL;
   void *p=malloc(size); if(p)++live_buffers; return p;
}
void arc_test_rpc_inventory_free(void *p)
{if(p){assert(live_buffers);--live_buffers;free(p);}}
static int index_of(const char *p)
{for(unsigned i=0;i<count;++i)if(!strcmp(p,entries[i].path))return (int)i;return -1;}
static int32_t stat_path(const char *p,int32_t *size)
{
   ++stats;
   if(!strcmp(p,"/firmware")){if(size)*size=0;return root_invalid?0:RETRO_VFS_STAT_IS_VALID|(root_file?0:RETRO_VFS_STAT_IS_DIRECTORY);}
   int i=index_of(p);if(i<0||i==fail_stat)return 0;
   if(size)*size=negative_stat?-1:(int32_t)entries[i].size;
   return RETRO_VFS_STAT_IS_VALID|(entries[i].directory?RETRO_VFS_STAT_IS_DIRECTORY:0);
}
static struct retro_vfs_file_handle *open_file(const char *p,unsigned mode,unsigned hints)
{
   (void)hints;assert(mode==RETRO_VFS_FILE_ACCESS_READ);++attempts;
   int i=index_of(p);if(i<0||i==fail_open)return NULL;
   assert(!entries[i].directory);
   struct retro_vfs_file_handle *h=calloc(1,sizeof(*h));assert(h);h->at=(unsigned)i;++opens;return h;
}
static int close_file(struct retro_vfs_file_handle *h)
{int rc=(int)h->at==fail_close?-1:0;free(h);++closes;return rc;}
static int64_t size_file(struct retro_vfs_file_handle *h)
{return (int)h->at==fail_size?-1:(int64_t)entries[h->at].size+(grow_after_read&&h->read_count?1:0);}
static int64_t read_file(struct retro_vfs_file_handle *h,void *p,uint64_t size)
{
   ++reads;++h->read_count;if((int)h->at==fail_read)return -1;
   assert(size==entries[h->at].size);memcpy(p,entries[h->at].data,(size_t)size);return (int64_t)size-(short_read?1:0);
}
static struct retro_vfs_dir_handle *open_dir(const char *p,bool hidden)
{
   (void)hidden;if(fail_dir_open)return NULL;
   struct retro_vfs_dir_handle *d=calloc(1,sizeof(*d));assert(d);strcpy(d->path,p);++dir_opens;return d;
}
static bool read_dir(struct retro_vfs_dir_handle *d)
{
   if(silent_end)return false;
   while(d->next<count){unsigned i=reverse_order?count-1-d->next:d->next;++d->next;
      const char *p=entries[i].path;size_t n=strlen(d->path);
      if(strncmp(p,d->path,n)||p[n]!='/'||strchr(p+n+1,'/'))continue;
      d->current=i;return true;
   }return false;
}
static const char *dir_name(struct retro_vfs_dir_handle *d)
{
   static char oversized[4087];
   if(long_dirent){memset(oversized,'x',sizeof(oversized)-1);oversized[sizeof(oversized)-1]=0;return oversized;}
   return (int)d->current==fail_name?NULL:strrchr(entries[d->current].path,'/')+1;
}
static bool dir_is_dir(struct retro_vfs_dir_handle *d){return entries[d->current].directory;}
static int close_dir(struct retro_vfs_dir_handle *d)
{free(d);++dir_closes;return fail_dir_close?-1:0;}
static struct retro_vfs_interface vfs={.open=open_file,.close=close_file,.size=size_file,.read=read_file,.stat=stat_path,
   .opendir=open_dir,.readdir=read_dir,.dirent_get_name=dir_name,.dirent_is_dir=dir_is_dir,.closedir=close_dir};
static void reset(void)
{
   assert(!live_buffers);count=0;fail_open=fail_size=fail_read=fail_close=fail_stat=fail_name=fail_allocation=-1;
   fail_dir_open=fail_dir_close=silent_end=root_invalid=root_file=negative_stat=false;
   reverse_order=short_read=grow_after_read=long_dirent=false;
   opens=closes=attempts=dir_opens=dir_closes=reads=allocations=stats=0;arc_io_set_vfs(&vfs);
}
static unsigned add(const char *name,unsigned os)
{
   unsigned i=count++;assert(count<=1100);snprintf(entries[i].path,sizeof(entries[i].path),"/firmware/%s",name);
   entries[i].data=data[os];entries[i].size=digests[os].size;entries[i].directory=false;return i;
}
static void directory(const char *name)
{unsigned i=add(name,0);entries[i].directory=true;}
static ArcRpcFirmwareInventory scan(void)
{
   ArcRpcFirmwareInventory r=arc_riscpc_inventory_scan("/firmware",table,4);
   assert(opens==closes&&dir_opens==dir_closes&&!live_buffers&&r.state==ARC_RPC_INVENTORY_SCANNED);return r;
}
static ArcRpcSelection selection(const ArcRpcFirmwareInventory *r,unsigned cpu,unsigned os)
{ArcRpcCompatibility hints={(uint8_t)cpu,(uint8_t)os};return arc_riscpc_select(&hints,1,r,0,0);}
static void refused_read(const ArcRpcFirmwareInventory *r,unsigned os)
{
   void *bytes=(void *)1;size_t size=99;
   assert(!arc_riscpc_firmware_read(r,(ArcRpcSelectionOs)os,&bytes,&size));
   assert(!bytes&&!size&&!live_buffers&&opens==closes);
}
static void exact_read(const ArcRpcFirmwareInventory *r,unsigned os)
{
   void *bytes=NULL;size_t size=0;
   assert(arc_riscpc_firmware_read(r,(ArcRpcSelectionOs)(os+1),&bytes,&size));
   assert(size==digests[os].size&&!memcmp(bytes,data[os],size));
   assert(live_buffers==1);arc_test_rpc_inventory_free(bytes);assert(opens==closes);
}
static void permutations(void)
{
   unsigned permutations=0;
   const char *names[]={"renamed.bin","wrong-ROM-name","odd.extension","ROM350"};
   for(unsigned a=0;a<4;++a)for(unsigned b=0;b<4;++b)for(unsigned c=0;c<4;++c)for(unsigned d=0;d<4;++d){
      if(a==b||a==c||a==d||b==c||b==d||c==d)continue;
      reset();add(names[a],a);add(names[b],b);add(names[c],c);add(names[d],d);
      directory("nested");add("nested/copy",a);add("AAA-canonical",a);
      for(unsigned order=0;order<2;++order){reverse_order=order!=0;
         ArcRpcFirmwareInventory r=scan();assert(r.installed==30&&!r.vfs_failure&&!r.bounded);
         assert(!strcmp(r.locator[a+1].path,"/firmware/AAA-canonical"));
         assert(selection(&r,20,12).os==ARC_RPC_OS_371);
         for(unsigned os=0;os<4;++os)exact_read(&r,os);
      }++permutations;
   }assert(permutations==24);
   /* POSIX permits backslashes in a leaf name. The VFS owns path spelling;
    * recognition must not impose Windows filename rules on every frontend. */
   reset();add("renamed\\firmware",0);ArcRpcFirmwareInventory r=scan();
   assert(r.installed==2&&!r.vfs_failure&&!r.bounded);exact_read(&r,0);
}
static void metadata(void)
{
   reset();unsigned i=add("unrelated",0);entries[i].size=1;fail_open=0;
   ArcRpcFirmwareInventory r=scan();assert(!attempts&&!r.vfs_failure);
   assert(selection(&r,4,4).reason==ARC_RPC_SELECTION_MISSING_FIRMWARE);
   reset();root_invalid=true;r=scan();assert(r.vfs_failure&&!dir_opens&&!attempts);
   assert(selection(&r,4,4).reason==ARC_RPC_SELECTION_UNREADABLE_INVENTORY);
   reset();root_file=true;r=scan();assert(!r.vfs_failure&&!r.installed&&!dir_opens);
   assert(selection(&r,4,4).reason==ARC_RPC_SELECTION_MISSING_FIRMWARE);
   /* Every required callback is independently unavailable. */
   for(unsigned missing=0;missing<10;++missing){
      reset();add("candidate",0);struct retro_vfs_interface broken=vfs;
      switch(missing){case 0:broken.stat=NULL;break;case 1:broken.open=NULL;break;
         case 2:broken.size=NULL;break;case 3:broken.read=NULL;break;case 4:broken.close=NULL;break;
         case 5:broken.opendir=NULL;break;case 6:broken.readdir=NULL;break;
         case 7:broken.dirent_get_name=NULL;break;case 8:broken.dirent_is_dir=NULL;break;case 9:broken.closedir=NULL;break;}
      arc_io_set_vfs(&broken);r=scan();assert(r.vfs_failure&&!r.installed);
   }
   reset();arc_io_set_vfs(NULL);assert(scan().vfs_failure);
   for(unsigned failure=0;failure<7;++failure){
      reset();add("bad",0);add("good",2);
      int *sites[]={&fail_open,&fail_size,&fail_read,&fail_close,&fail_name,&fail_stat};
      if(failure<6)*sites[failure]=0;else negative_stat=true;
      r=scan();assert(r.vfs_failure);
      if(failure<6){assert(r.installed==8);ArcRpcSelection s=selection(&r,16,8);
         assert(s.reason==ARC_RPC_SELECTION_SELECTED&&s.incomplete&&s.os==ARC_RPC_OS_370);}
      assert(selection(&r,4,4).reason==ARC_RPC_SELECTION_UNREADABLE_INVENTORY);
   }
   reset();fail_dir_open=true;assert(scan().vfs_failure);
   reset();add("good",3);fail_dir_close=true;r=scan();assert(r.vfs_failure&&r.installed==16);
   reset();silent_end=true;r=scan();assert(!r.vfs_failure&&!r.installed); /* Boolean EOF ambiguity. */
   reset();directory(".");directory("..");add("good",3);r=scan();
   assert(r.installed==16&&!r.bounded&&!r.vfs_failure&&dir_opens==1&&opens==1);
   reset();add("",0);assert(scan().vfs_failure);
}
static void locator_failures(void)
{
   reset();add("selected",3);add("fallback",1);ArcRpcFirmwareInventory r=scan();
   assert(!live_buffers);exact_read(&r,3);
   char saved_sha1[41];memcpy(saved_sha1,digests[3].sha1,sizeof(saved_sha1));
   memset(digests[3].sha1,'0',40);exact_read(&r,3);
   memcpy(digests[3].sha1,saved_sha1,sizeof(saved_sha1));
   r.locator[4].fingerprint.crc32^=1;refused_read(&r,4);r.locator[4].fingerprint.crc32^=1;
   r.locator[4].fingerprint.sha1[0]^=1;refused_read(&r,4);r.locator[4].fingerprint.sha1[0]^=1;
   data[3][0]^=1;refused_read(&r,4);data[3][0]^=1;
   --entries[0].size;refused_read(&r,4);++entries[0].size;
   entries[0].data=data[0];refused_read(&r,4);entries[0].data=data[3];
   for(unsigned failure=0;failure<5;++failure){
      int *sites[]={&fail_open,&fail_size,&fail_read,&fail_close,&fail_stat};
      *sites[failure]=0;refused_read(&r,4);*sites[failure]=-1;
   }
   short_read=true;refused_read(&r,4);short_read=false;
   grow_after_read=true;refused_read(&r,4);grow_after_read=false;
   fail_allocation=-2;refused_read(&r,4);fail_allocation=-1;
   count=1;strcpy(entries[0].path,"/firmware/moved");refused_read(&r,4);
   refused_read(&r,0);refused_read(&r,99);refused_read(NULL,4);
   reset();add("corrupt",0);data[0][0]^=1;assert(!scan().installed);data[0][0]^=1;
   --entries[0].size;attempts=0;assert(!scan().installed&&!attempts);
}
static void exhausted(const ArcRpcFirmwareInventory *r)
{
   assert(r->bounded&&!r->installed&&!live_buffers);
   for(unsigned i=0;i<5;++i)assert(!r->locator[i].path[0]);
   ArcRpcSelection s=selection(r,20,12);assert(s.reason==ARC_RPC_SELECTION_INCOMPLETE_SCAN&&s.incomplete&&!s.model&&!s.os);
   assert(selection(r,1,2).reason==ARC_RPC_SELECTION_LEGACY);
   assert(selection(r,32,8).reason==ARC_RPC_SELECTION_UNSUPPORTED);
   assert(selection(r,16,4).reason==ARC_RPC_SELECTION_UNQUALIFIED);
   ArcRpcCompatibility conflict[]={{4,4},{16,8}};
   assert(arc_riscpc_select(conflict,2,r,0,0).reason==ARC_RPC_SELECTION_CONFLICTING_DISCS);
   refused_read(r,4);
}
static void bounds(void)
{
   /* All orders of candidate/failure relative to the limit; same tree, both
    * candidate and failures can land before or after exhaustion. */
   for(unsigned candidate=0;candidate<3;++candidate)for(unsigned failure=0;failure<3;++failure){
      if(candidate==failure)continue;
      reset();for(unsigned i=0;i<ARC_RPC_SCAN_ENTRIES+1;++i){char name[32];snprintf(name,sizeof(name),"entry%04u",i);
         unsigned at=add(name,3);entries[at].size=1;}
      unsigned pos[]={0,512,1024};entries[pos[candidate]].size=digests[3].size;fail_stat=(int)pos[failure];
      for(unsigned order=0;order<2;++order){reverse_order=order!=0;ArcRpcFirmwareInventory r=scan();exhausted(&r);}
   }
   reset();for(unsigned i=0;i<ARC_RPC_SCAN_ENTRIES;++i){char n[32];snprintf(n,sizeof(n),"n%u",i);unsigned at=add(n,0);entries[at].size=1;}
   assert(!scan().bounded);
   reset();for(unsigned i=0;i<ARC_RPC_SCAN_ENTRIES+1;++i)directory(".");
   ArcRpcFirmwareInventory dots=scan();exhausted(&dots);assert(dir_opens==1&&!opens);
   /* A finite tree at depth eight is readable; descending to nine invalidates
    * the complete inventory, even if a candidate was already recognised. */
   reset();char name[256]="";
   for(unsigned i=0;i<8;++i){strcat(name,i?"/nested":"nested");directory(name);}
   char leaf[300];snprintf(leaf,sizeof(leaf),"%s/at-bound",name);add(leaf,0);
   ArcRpcFirmwareInventory r=scan();assert(!r.bounded&&r.installed==2&&dir_opens==9);
   strcat(name,"/nested");directory(name);snprintf(leaf,sizeof(leaf),"%s/too-deep",name);add(leaf,3);
   for(unsigned order=0;order<2;++order){reverse_order=order!=0;r=scan();exhausted(&r);}
   reset();char long_name[4096];memset(long_name,'x',4085);long_name[4085]=0;add(long_name,0);
   assert(strlen(entries[0].path)==4095);r=scan();assert(!r.bounded&&r.installed==2);
   long_dirent=true;r=scan();exhausted(&r);long_dirent=false;
   char root[4097];memset(root,'x',4096);root[4096]=0;
   r=arc_riscpc_inventory_scan(root,table,4);exhausted(&r);
   /* Large candidate duplicates charge bytes even when opening fails. */
   reset();uint8_t *large=calloc(1,ARC_RPC_SCAN_BYTES/16);assert(large);
   ArcDigest big=arc_digest(large,ARC_RPC_SCAN_BYTES/16);
   ArcRpcFirmwareFingerprint large_table={ARC_RPC_OS_371,big.size,big.crc32,big.sha1};
   for(unsigned i=0;i<17;++i){char n[32];snprintf(n,sizeof(n),"large%u",i);unsigned at=add(n,3);entries[at].data=large;entries[at].size=big.size;}
   count=16;r=arc_riscpc_inventory_scan("/firmware",&large_table,1);
   assert(!r.bounded&&r.installed==16&&!r.vfs_failure&&reads==16&&!live_buffers&&opens==closes);
   count=17;
   for(unsigned order=0;order<2;++order)for(unsigned failure=0;failure<2;++failure){
      reverse_order=order!=0;fail_open=failure?0:-1;
      r=arc_riscpc_inventory_scan("/firmware",&large_table,1);exhausted(&r);
   }
   /* With a stat failure there are still 17 known-size attempts: exhaustion
    * wins whether uncertainty occurs before or after the first recognition. */
   unsigned at=add("large17",3);entries[at].data=large;entries[at].size=big.size;
   fail_open=-1;fail_stat=0;
   for(unsigned order=0;order<2;++order){reverse_order=order!=0;
      r=arc_riscpc_inventory_scan("/firmware",&large_table,1);exhausted(&r);}
   free(large);
   reset();add("oversized",3);entries[0].size=ARC_RPC_IMAGE_BYTES+1;
   ArcRpcFirmwareFingerprint oversized={ARC_RPC_OS_371,ARC_RPC_IMAGE_BYTES+1,0,"unused"};
   r=arc_riscpc_inventory_scan("/firmware",&oversized,1);exhausted(&r);assert(!attempts);
}
static void resources(void)
{
   reset();add("failed",0);add("good",3);fail_allocation=0;
   ArcRpcFirmwareInventory r=scan();assert(r.resource_failure&&!r.vfs_failure&&!r.bounded&&r.installed==16&&reads==1);
   ArcRpcSelection s=selection(&r,4,4);assert(s.reason==ARC_RPC_SELECTION_MISSING_FIRMWARE&&s.incomplete);
   s=selection(&r,4,8);assert(s.reason==ARC_RPC_SELECTION_SELECTED&&s.incomplete);
   reset();add("failed",0);fail_allocation=-2;r=scan();assert(r.resource_failure&&!r.vfs_failure&&!r.installed&&!reads&&!opens);
}
int main(void)
{
   for(unsigned i=0;i<4;++i){for(unsigned j=0;j<40;++j)data[i][j]=(uint8_t)(i*40+j);
      digests[i]=arc_digest(data[i],32+i);table[i]=(ArcRpcFirmwareFingerprint){(ArcRpcSelectionOs)(i+1),digests[i].size,digests[i].crc32,digests[i].sha1};}
   permutations();metadata();locator_failures();bounds();resources();
   puts("R3d-2 inventory: exact locator bytes, canonical permutations, failure/metadata matrix, bounded refusal precedence and ownership passed");return 0;
}
