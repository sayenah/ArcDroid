/* Authored fingerprints through the real diagnostic scanner. GPL-2.0-only. */
#include "rpc_inventory.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct { const char *name; const uint8_t *data; size_t size; bool directory; } Entry;
static Entry entries[1100];static unsigned count;
static int fail_open=-1,fail_size=-1,fail_read=-1,fail_close=-1,fail_name=-1;
static bool fail_dir_open,fail_dir_close,silent_end;
static bool finite_tree;
static int fail_allocation=-1;
static unsigned opens,closes,dir_opens,dir_closes,reads,allocations;
static uint8_t data[4][40];static ArcRpcFirmwareFingerprint table[4];static ArcDigest digests[4];
struct retro_vfs_file_handle { unsigned at; };
struct retro_vfs_dir_handle { unsigned next,current,depth; };
void *arc_test_rpc_inventory_malloc(size_t size)
{unsigned at=allocations++;return fail_allocation==-2||(int)at==fail_allocation?NULL:malloc(size);}
static struct retro_vfs_file_handle *open_file(const char *p,unsigned mode,unsigned hints)
{
   (void)hints;assert(mode==RETRO_VFS_FILE_ACCESS_READ);const char *leaf=strrchr(p,'/');assert(leaf);
   for(unsigned i=0;i<count;++i)if(!strcmp(leaf+1,entries[i].name)){
      if((int)i==fail_open)return NULL;
      struct retro_vfs_file_handle *h=malloc(sizeof(*h));assert(h);h->at=i;++opens;return h;
   }
   return NULL;
}
static int close_file(struct retro_vfs_file_handle *h)
{int ret=(int)h->at==fail_close?-1:0;free(h);++closes;return ret;}
static int64_t size_file(struct retro_vfs_file_handle *h)
{return (int)h->at==fail_size?-1:(int64_t)entries[h->at].size;}
static int64_t read_file(struct retro_vfs_file_handle *h,void *p,uint64_t size)
{++reads;if((int)h->at==fail_read)return -1;assert(size==entries[h->at].size);memcpy(p,entries[h->at].data,(size_t)size);return (int64_t)size;}
static struct retro_vfs_dir_handle *open_dir(const char *p,bool hidden)
{
   (void)hidden;assert(!strstr(p,"/.")&&!strstr(p,"/.."));
   if(fail_dir_open)return NULL;
   struct retro_vfs_dir_handle *d=calloc(1,sizeof(*d));assert(d);
   if(finite_tree)for(const char *at=p;(at=strstr(at,"/nested"))!=NULL;at+=7)++d->depth;
   ++dir_opens;return d;
}
static bool read_dir(struct retro_vfs_dir_handle *d)
{
   if(silent_end)return false;
   while(d->next<count){unsigned i=d->next++;
      if(finite_tree&&!((i<=8&&entries[i].directory&&i==d->depth)||(i==9&&d->depth==8)||(i==10&&d->depth==9)))continue;
      d->current=i;return true;
   }
   return false;
}
static const char *dir_name(struct retro_vfs_dir_handle *d)
{return (int)d->current==fail_name?NULL:entries[d->current].name;}
static bool dir_is_dir(struct retro_vfs_dir_handle *d){return entries[d->current].directory;}
static int close_dir(struct retro_vfs_dir_handle *d){free(d);++dir_closes;return fail_dir_close?-1:0;}
static struct retro_vfs_interface vfs={.open=open_file,.close=close_file,.size=size_file,.read=read_file,
   .opendir=open_dir,.readdir=read_dir,.dirent_get_name=dir_name,.dirent_is_dir=dir_is_dir,.closedir=close_dir};
static void reset(void)
{count=0;fail_open=fail_size=fail_read=fail_close=fail_name=fail_allocation=-1;
   fail_dir_open=fail_dir_close=silent_end=finite_tree=false;opens=closes=dir_opens=dir_closes=reads=allocations=0;}
static ArcRpcFirmwareInventory scan(void)
{ArcRpcFirmwareInventory r=arc_riscpc_inventory_scan(&vfs,"/system/arcdroid/firmware",table,4);assert(opens==closes&&dir_opens==dir_closes);return r;}
static void add(const char *name,unsigned os)
{entries[count++]=(Entry){name,data[os],digests[os].size,false};}
int main(void)
{
   for(unsigned i=0;i<4;++i){for(unsigned j=0;j<40;++j)data[i][j]=(uint8_t)(i*40+j);
      digests[i]=arc_digest(data[i],32+i);table[i]=(ArcRpcFirmwareFingerprint){(ArcRpcSelectionOs)(i+1),digests[i].size,digests[i].crc32,digests[i].sha1};}
   const char *names[]={"renamed.bin","not-a-ROM-name","odd.extension","ROM350"};
   /* Every permutation, duplicate fingerprints, misnamed OS and rename. */
   unsigned permutations=0;
   for(unsigned a=0;a<4;++a)for(unsigned b=0;b<4;++b)for(unsigned c=0;c<4;++c)for(unsigned d=0;d<4;++d){
      if(a==b||a==c||a==d||b==c||b==d||c==d)continue;
      reset();add(names[a],a);add(names[b],b);add(names[c],c);add(names[d],d);add("copy",a);
      ArcRpcFirmwareInventory r=scan();assert(r.installed==30&&!r.vfs_failure&&!r.bounded);
      ArcRpcCompatibility hints={20,12};ArcRpcSelection s=arc_riscpc_select(&hints,1,&r,0,0);
      assert(s.model==ARC_RPC_SELECT_ARM710&&s.os==ARC_RPC_OS_371);++permutations;
   }
   reset();add("renamed",0);assert(scan().installed==2);
   data[0][0]^=1;assert(scan().installed==0&&!scan().vfs_failure);data[0][0]^=1;
   entries[0].size--;assert(scan().installed==0&&!scan().vfs_failure);
   reset();add("bad",0);add("good",2);
   for(unsigned failure=0;failure<5;++failure){
      fail_open=fail_size=fail_read=fail_close=fail_name=-1;
      int *sites[]={&fail_open,&fail_size,&fail_read,&fail_close,&fail_name};*sites[failure]=0;
      ArcRpcFirmwareInventory r=scan();assert(r.vfs_failure&&r.installed==8);
      ArcRpcCompatibility hints={16,8};ArcRpcSelection s=arc_riscpc_select(&hints,1,&r,0,0);
      assert(s.reason==ARC_RPC_SELECTION_SELECTED&&s.incomplete&&s.os==ARC_RPC_OS_370);
      hints=(ArcRpcCompatibility){4,4};assert(arc_riscpc_select(&hints,1,&r,0,0).reason==ARC_RPC_SELECTION_UNREADABLE_INVENTORY);
   }
   reset();fail_dir_open=true;assert(scan().vfs_failure);
   reset();add("good",3);fail_dir_close=true;assert(scan().vfs_failure&&scan().installed==16);
   reset();silent_end=true;assert(!scan().vfs_failure&&!scan().installed); /* Boolean EOF ambiguity. */
   reset();count=ARC_RPC_SCAN_ENTRIES;for(unsigned i=0;i<count;++i)entries[i]=(Entry){"ignored",data[0],1,false};
   assert(!scan().bounded);entries[count++]=entries[0];assert(scan().bounded&&!scan().vfs_failure);
   reset();entries[count++]=(Entry){".",NULL,0,false};count=1100;
   for(unsigned i=1;i<count;++i)entries[i]=entries[0];
   assert(scan().bounded);
   /* Dot entries are directory typed, but must never recurse or open files. */
   reset();entries[count++]=(Entry){".",NULL,0,true};entries[count++]=(Entry){"..",NULL,0,true};add("good",3);
   ArcRpcFirmwareInventory dots=scan();assert(dots.installed==16&&!dots.bounded&&!dots.vfs_failure&&dir_opens==1&&opens==1);
   /* A finite tree proves both sides of the exact bound, independently of
    * entry/path budgets: depth-eight files count; depth-nine files do not. */
   reset();finite_tree=true;
   for(unsigned i=0;i<=8;++i)entries[count++]=(Entry){"nested",NULL,0,true};
   add("at-bound",0);add("too-deep",3);
   ArcRpcFirmwareInventory depth=scan();
   assert(depth.bounded&&!depth.vfs_failure&&depth.installed==2&&dir_opens==9&&opens==1);
   reset();finite_tree=true;count=8;
   for(unsigned i=0;i<8;++i)entries[i]=(Entry){"nested",NULL,0,true};
   entries[count++]=(Entry){"unused",NULL,0,false};add("at-bound",0);
   depth=scan();assert(!depth.bounded&&depth.installed==2&&dir_opens==9&&opens==1);
   /* Only scanner buffers use the failing allocator; VFS handles remain real.
    * Failure closes its file and scanning can still recognize another one. */
   reset();add("failed",0);add("good",3);fail_allocation=0;
   ArcRpcFirmwareInventory resource=scan();
   assert(resource.resource_failure&&!resource.vfs_failure&&!resource.bounded&&resource.installed==16&&reads==1);
   ArcRpcCompatibility hints={4,4};ArcRpcSelection selection=arc_riscpc_select(&hints,1,&resource,0,0);
   assert(selection.reason==ARC_RPC_SELECTION_MISSING_FIRMWARE&&selection.incomplete);
   hints=(ArcRpcCompatibility){4,8};selection=arc_riscpc_select(&hints,1,&resource,0,0);
   assert(selection.reason==ARC_RPC_SELECTION_SELECTED&&selection.incomplete&&selection.os==ARC_RPC_OS_371);
   reset();add("failed",0);fail_allocation=-2;resource=scan();
   assert(resource.resource_failure&&!resource.vfs_failure&&!resource.installed&&!reads&&opens==1&&closes==1);
   reset();char long_name[4096];memset(long_name,'x',sizeof(long_name)-1);long_name[4095]=0;
   entries[count++]=(Entry){long_name,NULL,0,false};assert(scan().bounded&&!scan().vfs_failure);
   /* Byte budget is real even for repeated recognised files. */
   reset();uint8_t *large=calloc(1,4194304);assert(large);ArcDigest big=arc_digest(large,4194304);
   ArcRpcFirmwareFingerprint large_table={ARC_RPC_OS_371,big.size,big.crc32,big.sha1};
   count=17;for(unsigned i=0;i<count;++i)entries[i]=(Entry){"large",large,4194304,false};
   ArcRpcFirmwareInventory r=arc_riscpc_inventory_scan(&vfs,"/firmware",&large_table,1);
   assert(r.installed==16&&r.bounded&&!r.vfs_failure&&opens==closes);free(large);
   assert(arc_riscpc_inventory_scan(NULL,"/firmware",table,4).vfs_failure);
   puts("R3a inventory: public authored bytes, 24 permutations, rename/duplicates/corruption/size, reported VFS failures, bounds and ambiguous EOF passed");
   assert(permutations==24);return 0;
}
