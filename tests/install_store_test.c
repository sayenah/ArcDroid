/* In-memory VFS and adversarial persisted install-store tests.
 * Project-authored GPL-2.0-only. */
#include "install_store.h"
#include "arc_io.h"
#include "arcdroid_core.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FILES 64u
#define DIRECTORIES 16u
struct retro_vfs_file_handle { unsigned index, mode; size_t at; };
struct retro_vfs_dir_handle { char path[256]; unsigned at, current; };
typedef struct { char path[256]; uint8_t *bytes; size_t size; bool exists; } File;
static File files[FILES];
static char directories[DIRECTORIES][256];
static unsigned directory_count;
static bool strict_directories;
static unsigned mutations, fail_mutation;
static bool fail_probe_close;
static bool fail_base_open;
static char probe_failure[256], creation_failure[256];
static void put32(uint8_t *p, uint32_t n)
{ for (unsigned i=0u;i<4u;++i) p[i]=(uint8_t)(n>>(i*8u)); }
static void store_log(void *opaque, const char *message)
{
   (void)opaque;
   if (!strncmp(message,"store-write-probe=failed ",25u))
      snprintf(probe_failure,sizeof(probe_failure),"%s",message);
   if (!strncmp(message,"store-create=failed ",20u))
      snprintf(creation_failure,sizeof(creation_failure),"%s",message);
}
static bool canonical(const char *path, char output[256])
{
   size_t n = strlen(path);
   if (n >= 256u) return false;
   for (size_t i=0u;i<=n;++i)
      output[i] = path[i] == '\\' ? '/' : path[i];
   return true;
}
static bool directory_exists(const char *path)
{
   char normalized[256];
   if (!canonical(path, normalized)) return false;
   for (unsigned i=0u;i<directory_count;++i)
      if (!strcmp(directories[i],normalized)) return true;
   return false;
}
static void add_directory(const char *path)
{
   assert(directory_count < DIRECTORIES);
   assert(canonical(path,directories[directory_count]));
   ++directory_count;
}
static bool fail_now(void)
{
   ++mutations;
   if (fail_mutation && mutations == fail_mutation) {
      fail_mutation = 0u; return true;
   }
   return false;
}
static int locate(const char *path)
{
   char normalized[256];
   if (!canonical(path,normalized)) return -1;
   for (unsigned i = 0u; i < FILES; ++i)
      if (files[i].exists && !strcmp(files[i].path, normalized)) return (int)i;
   return -1;
}
static int vacant(void)
{ for (unsigned i = 0u; i < FILES; ++i) if (!files[i].exists) return (int)i; return -1; }
static struct retro_vfs_file_handle *vopen(const char *path, unsigned mode,
                                            unsigned hint)
{
   (void)hint;
   if (fail_base_open && (mode & RETRO_VFS_FILE_ACCESS_WRITE) &&
       strstr(path,"disc.g0.hdf.tmp")) return NULL;
   int i = locate(path);
   if (mode & RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING) {
      if (i < 0) return NULL;
   } else if (mode & RETRO_VFS_FILE_ACCESS_WRITE) {
      if (strict_directories) {
         char normalized[256];
         if (!canonical(path,normalized)) return NULL;
         char *slash=strrchr(normalized,'/');
         if (!slash) return NULL;
         *slash='\0';
         if (!directory_exists(normalized)) return NULL;
      }
      if (fail_now()) return NULL;
      if (i < 0) i = vacant();
      if (i < 0) return NULL;
      free(files[i].bytes); files[i].bytes = NULL; files[i].size = 0u;
      files[i].exists = true;
      assert(canonical(path,files[i].path));
   } else if (i < 0) return NULL;
   struct retro_vfs_file_handle *h = calloc(1u, sizeof(*h)); assert(h);
   h->index = (unsigned)i; h->mode = mode; return h;
}
static int vclose(struct retro_vfs_file_handle *h)
{
   bool fail=fail_probe_close &&
      strstr(files[h->index].path,"/rwprobe.tmp") != NULL;
   if (fail) fail_probe_close=false;
   free(h); return fail ? -1 : 0;
}
static int64_t vsize(struct retro_vfs_file_handle *h)
{ return (int64_t)files[h->index].size; }
static int64_t vseek(struct retro_vfs_file_handle *h, int64_t offset, int origin)
{
   int64_t base = origin == RETRO_VFS_SEEK_POSITION_START ? 0 :
      origin == RETRO_VFS_SEEK_POSITION_CURRENT ? (int64_t)h->at :
      origin == RETRO_VFS_SEEK_POSITION_END ? (int64_t)files[h->index].size : -1;
   if (base < 0 || offset < -base || base + offset < 0) return -1;
   h->at = (size_t)(base + offset); return (int64_t)h->at;
}
static int64_t vread(struct retro_vfs_file_handle *h, void *data, uint64_t n)
{
   File *f = &files[h->index];
   if (h->at > f->size) return 0;
   if (n > f->size - h->at) n = f->size - h->at;
   if (n) memcpy(data, f->bytes + h->at, (size_t)n);
   h->at += (size_t)n; return (int64_t)n;
}
static int64_t vwrite(struct retro_vfs_file_handle *h, const void *data,
                       uint64_t n)
{
   if (!(h->mode & RETRO_VFS_FILE_ACCESS_WRITE) || fail_now()) return -1;
   File *f = &files[h->index];
   if (n > SIZE_MAX - h->at) return -1;
   size_t end = h->at + (size_t)n;
   if (end > f->size) {
      uint8_t *grown = realloc(f->bytes, end); assert(grown);
      memset(grown + f->size, 0, end - f->size);
      f->bytes = grown; f->size = end;
   }
   if (n) memcpy(f->bytes + h->at, data, (size_t)n);
   h->at = end; return (int64_t)n;
}
static int vflush(struct retro_vfs_file_handle *h)
{ (void)h; return fail_now() ? -1 : 0; }
static int vremove(const char *path)
{
   int i = locate(path);
   if (i < 0 || fail_now()) return -1;
   free(files[i].bytes); memset(&files[i], 0, sizeof(files[i])); return 0;
}
static int vrename(const char *from, const char *to)
{
   int i = locate(from);
   if (i < 0 || locate(to) >= 0 || fail_now()) return -1;
   assert(canonical(to,files[i].path)); return 0;
}
static int64_t vtruncate(struct retro_vfs_file_handle *h, int64_t length)
{
   if (length < 0 || (uint64_t)length > SIZE_MAX || fail_now()) return -1;
   File *f = &files[h->index];
   uint8_t *grown = realloc(f->bytes, (size_t)length ? (size_t)length : 1u);
   assert(grown);
   if ((size_t)length > f->size)
      memset(grown + f->size, 0, (size_t)length - f->size);
   f->bytes = grown; f->size = (size_t)length;
   return 0;
}
static int32_t vstat(const char *path, int32_t *size)
{
   int i = locate(path);
   if (i >= 0) {
      if (size) *size = (int32_t)files[i].size;
      return RETRO_VFS_STAT_IS_VALID;
   }
   if (size) *size = 0;
   if (strict_directories && !directory_exists(path)) return 0;
   return RETRO_VFS_STAT_IS_VALID | RETRO_VFS_STAT_IS_DIRECTORY;
}
static int vmkdir(const char *path)
{
   if (!strict_directories) return 0;
   char normalized[256];
   if (!canonical(path,normalized) || directory_exists(normalized) ||
       fail_now()) return -1;
   char *slash=strrchr(normalized,'/');
   if (!slash) return -1;
   *slash='\0';
   if (!directory_exists(normalized)) return -1;
   *slash='/';
   add_directory(normalized);
   return 0;
}
static struct retro_vfs_dir_handle *vopendir(const char *path, bool hidden)
{
   (void)hidden;
   if (strict_directories && !directory_exists(path)) return NULL;
   struct retro_vfs_dir_handle *h = calloc(1u, sizeof(*h)); assert(h);
   assert(canonical(path,h->path)); return h;
}
static bool vreaddir(struct retro_vfs_dir_handle *h)
{
   size_t n = strlen(h->path);
   while (h->at < FILES) {
      unsigned i = h->at++;
      if (files[i].exists && !strncmp(files[i].path, h->path, n) &&
          files[i].path[n] == '/' && !strchr(files[i].path+n+1u, '/')) {
         h->current = i; return true;
      }
   }
   return false;
}
static const char *vname(struct retro_vfs_dir_handle *h)
{ return strrchr(files[h->current].path, '/') + 1u; }
static bool visdir(struct retro_vfs_dir_handle *h) { (void)h; return false; }
static int vclosedir(struct retro_vfs_dir_handle *h) { free(h); return 0; }
static struct retro_vfs_interface vfs = {
   .open=vopen,.close=vclose,.size=vsize,.seek=vseek,.read=vread,
   .write=vwrite,.flush=vflush,.remove=vremove,.rename=vrename,
   .truncate=vtruncate,.stat=vstat,.mkdir=vmkdir,.opendir=vopendir,
   .readdir=vreaddir,.dirent_get_name=vname,.dirent_is_dir=visdir,
   .closedir=vclosedir
};
static void clear_files(void)
{
   for (unsigned i = 0u; i < FILES; ++i) {
      free(files[i].bytes); memset(&files[i], 0, sizeof(files[i]));
   }
   mutations = fail_mutation = 0u;
   fail_probe_close=false; fail_base_open=false;
   probe_failure[0]='\0'; creation_failure[0]='\0';
   directory_count=0u; strict_directories=false;
}
static File *file_named(const char *leaf)
{
   for (unsigned i = 0u; i < FILES; ++i) {
      if (!files[i].exists) continue;
      const char *last = strrchr(files[i].path, '/');
      if (last && !strcmp(last+1u, leaf)) return &files[i];
   }
   return NULL;
}
static void add_file(const char *directory, const char *leaf,
                     const uint8_t *data, size_t size)
{
   int i = vacant(); assert(i >= 0);
   File *f = &files[i];
   snprintf(f->path, sizeof(f->path), "%s/%s", directory, leaf);
   f->bytes = malloc(size ? size : 1u); assert(f->bytes);
   if (size) memcpy(f->bytes, data, size);
   f->size = size; f->exists = true;
}
static ArcInstallPlan plan(void)
{
   const ArcInstallProfile *profile = arc_install_profile(10140u, 2u);
   assert(profile);
   ArcInstallPlan result = {0};
   result.profile = profile; result.variant = &profile->variants[0];
   strcpy(result.storage_id, "1234567890abcdef1234567890abcdef12345678");
   return result;
}
static ArcInstallStore *open_store(const ArcInstallPlan *p, bool create,
                                   bool writable, ArcInstallStoreState expected)
{
   char error[256] = {0};
   ArcInstallStore *s = arc_install_store_open("save", p, create, writable,
                                              NULL, NULL, error, sizeof(error));
   if (!s || arc_install_store_state(s) != expected)
      fprintf(stderr, "store state=%d expected=%d error=%s\n",
              s ? arc_install_store_state(s) : -1, expected, error);
   assert(s && arc_install_store_state(s) == expected);
   return s;
}
static void ready_store(const ArcInstallPlan *p)
{
   ArcInstallStore *s = open_store(p, true, true,
                                    ARC_INSTALL_STORE_UNPUBLISHED);
   assert(arc_install_store_publish(s, NULL, 0u));
   assert(arc_install_store_state(s) == ARC_INSTALL_STORE_READY);
   arc_install_store_close(s);
}
static void test_roundtrip(void)
{
   clear_files(); ArcInstallPlan p = plan();
   ArcInstallStore *s = open_store(&p, true, true,
                                    ARC_INSTALL_STORE_UNPUBLISHED);
   uint8_t data[1024], readback[1024];
   memset(data, 0x5au, sizeof(data));
   assert(arc_install_store_write(s, 100u, 2u, data));
   assert(arc_install_store_read(s, 100u, 2u, readback) &&
          !memcmp(data, readback, sizeof(data)));
   assert(arc_install_store_publish(s, NULL, 0u));
   assert(file_named("ready.a") && file_named("journal.g0"));
   memset(data, 0xa5u, sizeof(data));
   assert(arc_install_store_write(s, 100u, 2u, data));
   arc_install_store_close(s);
   unsigned before = mutations;
   s = open_store(&p, false, false, ARC_INSTALL_STORE_READY);
   assert(!arc_install_store_writable(s));
   assert(arc_install_store_read(s, 100u, 2u, readback) &&
          !memcmp(data, readback, sizeof(data)));
   assert(!arc_install_store_write(s, 100u, 2u, data));
   arc_install_store_close(s);
   assert(mutations == before);
   /* A committed save makes the missing sole manifest unambiguously damage. */
   File *ready = file_named("ready.a"); assert(ready);
   assert(vremove(ready->path) == 0);
   before = mutations;
   s = open_store(&p, true, true, ARC_INSTALL_STORE_DAMAGED);
   arc_install_store_close(s);
   assert(mutations == before);
   clear_files();
}
static void test_damage(void)
{
   ArcInstallPlan p = plan();
   ready_store(&p);
   File *m = file_named("ready.a"); assert(m);
   uint8_t copy[276]; memcpy(copy, m->bytes, sizeof(copy));
   copy[68u] = 2u;
   uint32_t crc = arc_crc32(copy, 272u);
   for (unsigned i=0;i<4u;++i) copy[272u+i]=(uint8_t)(crc>>(8u*i));
   memcpy(m->bytes, copy, sizeof(copy));
   unsigned before=mutations;
   ArcInstallStore *s=open_store(&p,false,false,ARC_INSTALL_STORE_DAMAGED);
   arc_install_store_close(s); assert(mutations==before);
   clear_files(); ready_store(&p);
   m=file_named("ready.a"); assert(m);
   /* Version 1 named the pre-fix, non-mountable FileCore formatter. */
   put32(m->bytes+64u,1u);
   crc=arc_crc32(m->bytes,272u);
   put32(m->bytes+272u,crc);
   before=mutations;
   s=open_store(&p,true,true,ARC_INSTALL_STORE_DAMAGED);
   arc_install_store_close(s); assert(mutations==before);
   clear_files(); ready_store(&p);
   m=file_named("ready.a"); assert(m);
   add_file("save/arcdroid/install/1234567890abcdef1234567890abcdef12345678",
            "ready.b",m->bytes,m->size);
   s=open_store(&p,false,false,ARC_INSTALL_STORE_DAMAGED);
   arc_install_store_close(s);
   clear_files(); ready_store(&p);
   m=file_named("journal.g0"); assert(m);
   m->bytes[0]^=1u;
   s=open_store(&p,false,false,ARC_INSTALL_STORE_DAMAGED);
   arc_install_store_close(s);
   clear_files(); ready_store(&p);
   m=file_named("disc.g0.hdf"); assert(m);
   m->bytes[1234u]^=1u;
   s=open_store(&p,false,false,ARC_INSTALL_STORE_DAMAGED);
   arc_install_store_close(s);
   clear_files();
}
static void test_tail(void)
{
   ArcInstallPlan p=plan();
   ready_store(&p);
   File *journal=file_named("journal.g0"); assert(journal);
   uint8_t fragment[3]={1u,2u,3u};
   uint8_t *grown=realloc(journal->bytes,journal->size+3u); assert(grown);
   journal->bytes=grown; memcpy(grown+journal->size,fragment,3u);
   journal->size+=3u;
   unsigned before=mutations;
   ArcInstallStore *s=open_store(&p,false,false,ARC_INSTALL_STORE_READY);
   arc_install_store_close(s);
   assert(mutations==before && journal->size==127u);
   s=open_store(&p,false,true,ARC_INSTALL_STORE_READY);
   arc_install_store_close(s);
   assert(journal->size==124u);
   clear_files();
}
static void test_directory_states(void)
{
   ArcInstallPlan p=plan();
   const char *dir="save/arcdroid/install/1234567890abcdef1234567890abcdef12345678";
   clear_files();
   uint8_t foreign=0xceu;
   add_file(dir,"foreign",&foreign,1u);
   ArcInstallStore *s=open_store(&p,true,true,ARC_INSTALL_STORE_DAMAGED);
   arc_install_store_close(s);
   clear_files();
   add_file(dir,"disc.g1.hdf",&foreign,1u);
   s=open_store(&p,true,true,ARC_INSTALL_STORE_DAMAGED);
   arc_install_store_close(s);
   clear_files();
   add_file(dir,"journal.g0",&foreign,1u);
   s=open_store(&p,true,true,ARC_INSTALL_STORE_DAMAGED);
   arc_install_store_close(s);
   clear_files();
   add_file(dir,"journal.g0.tmp",&foreign,1u);
   s=open_store(&p,false,false,ARC_INSTALL_STORE_UNPUBLISHED);
   arc_install_store_close(s);
   s=open_store(&p,true,true,ARC_INSTALL_STORE_UNPUBLISHED);
   assert(!file_named("journal.g0.tmp") && file_named("disc.g0.hdf"));
   arc_install_store_close(s);
   clear_files();
}
static void test_record_damage(void)
{
   ArcInstallPlan p=plan(); uint8_t data[512]={0};
   for (unsigned mode=0u;mode<5u;++mode) {
      clear_files(); ready_store(&p);
      ArcInstallStore *s=open_store(&p,false,true,ARC_INSTALL_STORE_READY);
      assert(arc_install_store_write(s,20u,1u,data));
      arc_install_store_close(s);
      File *j=file_named("journal.g0"); assert(j);
      size_t record=124u;
      if (mode==0u) {
         j->bytes[record+56u]=2u; /* sequence gap, valid header CRC */
      } else if (mode==1u) {
         j->bytes[record+72u]=0xffu; /* length reaches beyond EOF */
      } else if (mode==2u) {
         j->bytes[j->size-1u]^=1u; /* complete bad trailer */
      } else if (mode==3u) {
         j->bytes[record+8u]^=1u; /* foreign record binding */
      } else {
         j->bytes[record+48u]=1u; /* wrong generation */
      }
      if (mode!=2u) {
         uint32_t crc=arc_crc32(j->bytes+record,76u);
         for (unsigned i=0u;i<4u;++i)
            j->bytes[record+76u+i]=(uint8_t)(crc>>(i*8u));
      }
      unsigned before=mutations;
      s=open_store(&p,false,true,ARC_INSTALL_STORE_DAMAGED);
      arc_install_store_close(s);
      assert(mutations==before);
   }
   clear_files();
}
static void test_interrupted_operations(void)
{
   ArcInstallPlan p=plan();
   bool saw_creation_failure=false, saw_publication_failure=false;
   for (unsigned step=1u;step<18u;++step) {
      clear_files(); fail_mutation=step;
      ArcInstallStore *s=arc_install_store_open("save",&p,true,true,NULL,NULL,NULL,0u);
      assert(s);
      bool succeeded=arc_install_store_state(s)==ARC_INSTALL_STORE_UNPUBLISHED;
      arc_install_store_close(s);
      if (fail_mutation) { assert(succeeded); break; }
      assert(!succeeded); saw_creation_failure=true;
      s=arc_install_store_open("save",&p,false,false,NULL,NULL,NULL,0u);
      assert(s && arc_install_store_state(s)!=ARC_INSTALL_STORE_READY);
      arc_install_store_close(s);
   }
   assert(saw_creation_failure);
   for (unsigned step=1u;step<18u;++step) {
      clear_files();
      ArcInstallStore *s=open_store(&p,true,true,ARC_INSTALL_STORE_UNPUBLISHED);
      fail_mutation=mutations+step;
      bool published=arc_install_store_publish(s,NULL,0u);
      arc_install_store_close(s);
      if (fail_mutation) { assert(published); break; }
      assert(!published); saw_publication_failure=true;
      s=arc_install_store_open("save",&p,false,false,NULL,NULL,NULL,0u);
      assert(s && arc_install_store_state(s)==ARC_INSTALL_STORE_UNPUBLISHED);
      arc_install_store_close(s);
   }
   assert(saw_publication_failure);
   clear_files();
}
static void test_identity(void)
{
   ArcInstallPlan p=plan(); ready_store(&p);
   ArcInstallPlan other=p;
   strcpy(other.storage_id,"abcdef1234567890abcdef1234567890abcdef12");
   ArcInstallStore *s=open_store(&other,false,false,ARC_INSTALL_STORE_NEW);
   arc_install_store_close(s);
   clear_files();
}
static void test_cap_and_compaction(void)
{
   ArcInstallPlan p=plan(); ready_store(&p);
   ArcInstallStore *s=open_store(&p,false,true,ARC_INSTALL_STORE_READY);
   uint8_t *command=malloc(256u*512u); assert(command);
   memset(command,0x7bu,256u*512u);
   for (unsigned i=0u;i<9u;++i)
      assert(arc_install_store_write(s,100u+i*256u,256u,command));
   arc_install_store_close(s);
   assert(file_named("journal.g0") && file_named("journal.g0")->size>1024u*1024u);
   unsigned before=mutations;
   s=open_store(&p,false,false,ARC_INSTALL_STORE_READY);
   arc_install_store_close(s);
   assert(mutations==before && !file_named("ready.b"));
   s=open_store(&p,false,true,ARC_INSTALL_STORE_READY);
   assert(file_named("ready.b") && file_named("disc.g1.hdf") &&
          file_named("journal.g1"));
   uint8_t out[512];
   assert(arc_install_store_read(s,100u,1u,out) && out[0]==0x7bu);
   arc_install_store_close(s);
   File *newer=file_named("ready.b"); assert(newer);
   newer->bytes[0]^=1u;
   s=open_store(&p,false,false,ARC_INSTALL_STORE_DAMAGED);
   arc_install_store_close(s);
   free(command); clear_files();
}
static void test_journal_cap(void)
{
   ArcInstallPlan p=plan(); ready_store(&p);
   ArcInstallStore *s=open_store(&p,false,true,ARC_INSTALL_STORE_READY);
   uint8_t *payload=calloc(256u,512u); assert(payload);
   unsigned commands=0u;
   while (arc_install_store_write(s,100u,256u,payload)) {
      ++commands; assert(commands<100u);
   }
   assert(commands>50u && !arc_install_store_writable(s));
   unsigned before=mutations;
   assert(!arc_install_store_write(s,100u,1u,payload));
   assert(mutations==before);
   arc_install_store_close(s); free(payload); clear_files();
}
static void test_compaction_interruptions(void)
{
   ArcInstallPlan p=plan();
   uint8_t *payload=calloc(256u,512u); assert(payload);
   unsigned interrupted=0u;
   for (unsigned step=1u;step<24u;++step) {
      clear_files(); ready_store(&p);
      ArcInstallStore *s=open_store(&p,false,true,ARC_INSTALL_STORE_READY);
      for (unsigned i=0u;i<9u;++i)
         assert(arc_install_store_write(s,2000u+i*256u,256u,payload));
      arc_install_store_close(s);
      fail_mutation=mutations+step;
      s=arc_install_store_open("save",&p,false,true,NULL,NULL,NULL,0u);
      assert(s);
      bool consumed=fail_mutation==0u;
      arc_install_store_close(s);
      if (!consumed) break;
      ++interrupted;
      /* The previously selected manifest remains sufficient after every
       * interrupted operation; temporaries and other generations are ignored. */
      s=open_store(&p,false,false,ARC_INSTALL_STORE_READY);
      arc_install_store_close(s);
   }
   assert(interrupted>=5u);
   free(payload); clear_files();
}
static void test_compaction_retry_with_stale_generation(void)
{
   ArcInstallPlan p=plan();
   clear_files(); ready_store(&p);
   ArcInstallStore *s=open_store(&p,false,true,ARC_INSTALL_STORE_READY);
   uint8_t *payload=calloc(256u,512u); assert(payload);
   for (unsigned i=0u;i<9u;++i)
      assert(arc_install_store_write(s,2000u+i*256u,256u,payload));
   arc_install_store_close(s);
   const char *dir="save/arcdroid/install/1234567890abcdef1234567890abcdef12345678";
   uint8_t stale=0x5au;
   add_file(dir,"disc.g1.hdf",&stale,1u);
   add_file(dir,"journal.g1",&stale,1u);
   add_file(dir,"ready.tmp",&stale,1u);
   unsigned before=mutations;
   s=open_store(&p,false,false,ARC_INSTALL_STORE_READY);
   assert(mutations==before && file_named("disc.g1.hdf") &&
          file_named("journal.g1") && file_named("ready.tmp"));
   arc_install_store_close(s);
   s=open_store(&p,false,true,ARC_INSTALL_STORE_READY);
   assert(file_named("ready.b") && !file_named("ready.tmp"));
   assert(!file_named("disc.g0.hdf") && !file_named("journal.g0"));
   arc_install_store_close(s);
   free(payload); clear_files();
}
static void test_writable_orphan_cleanup(void)
{
   ArcInstallPlan p=plan();
   clear_files(); ready_store(&p);
   const char *dir="save/arcdroid/install/1234567890abcdef1234567890abcdef12345678";
   uint8_t stale=0x5au;
   add_file(dir,"disc.g7.hdf",&stale,1u);
   add_file(dir,"journal.g7.tmp",&stale,1u);
   add_file(dir,"ready.tmp",&stale,1u);
   unsigned before=mutations;
   ArcInstallStore *s=open_store(&p,false,false,ARC_INSTALL_STORE_READY);
   assert(mutations==before);
   arc_install_store_close(s);
   s=open_store(&p,false,true,ARC_INSTALL_STORE_READY);
   assert(!file_named("disc.g7.hdf") && !file_named("journal.g7.tmp") &&
          !file_named("ready.tmp"));
   assert(file_named("ready.a") && file_named("disc.g0.hdf") &&
          file_named("journal.g0"));
   arc_install_store_close(s);
   clear_files();
}
static void test_probe_temporaries(void)
{
   ArcInstallPlan p=plan();
   const char *dir="save/arcdroid/install/1234567890abcdef1234567890abcdef12345678";
   const uint8_t stale=0x5au;
   for (unsigned state=0u; state<4u; ++state) {
      clear_files();
      if (state==1u) {
         ArcInstallStore *candidate=open_store(&p,true,true,ARC_INSTALL_STORE_UNPUBLISHED);
         arc_install_store_close(candidate);
      } else if (state==2u) ready_store(&p);
      else if (state==3u) add_file(dir,"foreign",&stale,1u);
      add_file(dir,"rwprobe.tmp",&stale,1u);
      add_file(dir,"rwprobe2.tmp",&stale,1u);
      unsigned before=mutations;
      ArcInstallStoreState expected=(ArcInstallStoreState)state;
      ArcInstallStore *s=open_store(&p,false,false,expected);
      arc_install_store_close(s);
      assert(mutations==before && file_named("rwprobe.tmp") &&
             file_named("rwprobe2.tmp"));
      if (state!=3u) {
         s=open_store(&p,state!=2u,true,
                      state==2u ? ARC_INSTALL_STORE_READY :
                                  ARC_INSTALL_STORE_UNPUBLISHED);
         assert(arc_install_store_writable(s));
         arc_install_store_close(s);
         assert(!file_named("rwprobe.tmp") && !file_named("rwprobe2.tmp"));
      }
   }
   clear_files();
}
static void test_probe_failures(void)
{
   ArcInstallPlan p=plan();
   const char *dir="save/arcdroid/install/1234567890abcdef1234567890abcdef12345678";
   for (unsigned step=1u; step<=5u; ++step) {
      clear_files(); ready_store(&p);
      File *journal=file_named("journal.g0"); assert(journal);
      uint8_t *grown=realloc(journal->bytes,journal->size+1u); assert(grown);
      journal->bytes=grown; grown[journal->size++]=0x5au;
      uint8_t orphan=0x71u;
      add_file(dir,"ready.tmp",&orphan,1u);
      size_t old_size=journal->size;
      fail_mutation=mutations+step;
      char error[256]={0};
      probe_failure[0]='\0';
      ArcInstallStore *s=arc_install_store_open("save",&p,false,true,
                                               store_log,NULL,error,sizeof(error));
      assert(s && arc_install_store_state(s)==ARC_INSTALL_STORE_READY &&
             !arc_install_store_writable(s) && !fail_mutation);
      static const char *const stages[]={"op=vfs-open-write","op=write",
                                          "op=flush","op=rename","op=remove"};
      assert(strstr(probe_failure,stages[step-1u]));
      assert(strstr(probe_failure,"path=rwprobe"));
      assert(journal->size==old_size && file_named("ready.tmp"));
      arc_install_store_close(s);
      s=open_store(&p,false,false,ARC_INSTALL_STORE_READY);
      arc_install_store_close(s);
      for (unsigned initial=0u;initial<2u;++initial) {
         clear_files();
         if (initial) {
            s=open_store(&p,true,true,ARC_INSTALL_STORE_UNPUBLISHED);
            arc_install_store_close(s);
         }
         fail_mutation=mutations+step;
         error[0]='\0';
         probe_failure[0]='\0';
         s=arc_install_store_open("save",&p,true,true,store_log,NULL,
                                   error,sizeof(error));
         assert(s && arc_install_store_state(s)==
                (initial ? ARC_INSTALL_STORE_UNPUBLISHED : ARC_INSTALL_STORE_NEW));
         assert(!arc_install_store_writable(s) && !fail_mutation);
         assert(strstr(probe_failure,stages[step-1u]));
         assert(strstr(probe_failure,"path=rwprobe"));
         assert(strstr(error,"choose Install Saves: Temporary"));
         assert((file_named("disc.g0.hdf") != NULL) == (initial != 0u));
         arc_install_store_close(s);
      }
   }
   clear_files(); ready_store(&p);
   fail_probe_close=true;
   ArcInstallStore *s=arc_install_store_open("save",&p,false,true,
                                            store_log,NULL,NULL,0u);
   assert(s && arc_install_store_state(s)==ARC_INSTALL_STORE_READY &&
          !arc_install_store_writable(s) &&
          strstr(probe_failure,"op=close"));
   arc_install_store_close(s);
   clear_files();
}
static void test_relative_save_paths(void)
{
   static const char *const paths[]={
      "build/retroarch-native/save/ArcDroid",
      "build\\retroarch-native\\save\\ArcDroid"
   };
   ArcInstallPlan p=plan();
   assert(arc_install_store_native_path(paths[0]));
   assert(arc_install_store_native_path(paths[1]));
   assert(!arc_install_store_native_path("content://save/ArcDroid"));
   assert(!arc_install_store_native_path("C:relative"));
   for (unsigned variant=0u;variant<2u;++variant) {
      clear_files();
      strict_directories=true;
      add_directory("build");
      add_directory("build/retroarch-native");
      add_directory("build/retroarch-native/save");
      add_directory(paths[variant]);
      char error[256]={0};
      ArcInstallStore *s=arc_install_store_open(paths[variant],&p,true,true,
                                                store_log,NULL,error,sizeof(error));
      assert(s && arc_install_store_state(s)==ARC_INSTALL_STORE_UNPUBLISHED &&
             arc_install_store_writable(s) && !error[0]);
      assert(directory_exists("build/retroarch-native/save/ArcDroid/arcdroid/install/1234567890abcdef1234567890abcdef12345678"));
      assert(file_named("disc.g0.hdf") && !file_named("rwprobe.tmp") &&
             !file_named("rwprobe2.tmp"));
      assert(arc_install_store_publish(s,error,sizeof(error)));
      arc_install_store_close(s);
      s=arc_install_store_open(paths[variant],&p,false,true,store_log,NULL,
                               error,sizeof(error));
      assert(s && arc_install_store_state(s)==ARC_INSTALL_STORE_READY &&
             arc_install_store_writable(s));
      arc_install_store_close(s);
      clear_files();
      strict_directories=true;
      add_directory("build");
      add_directory("build/retroarch-native");
      add_directory("build/retroarch-native/save");
      add_directory(paths[variant]);
      fail_mutation=mutations+1u;
      s=arc_install_store_open(paths[variant],&p,true,true,store_log,NULL,
                               error,sizeof(error));
      assert(s && arc_install_store_state(s)==ARC_INSTALL_STORE_NEW &&
             !arc_install_store_writable(s));
      assert(strstr(creation_failure,"op=mkdir path=arcdroid") &&
             strstr(error,"choose Install Saves: Temporary"));
      arc_install_store_close(s);
      clear_files();
      strict_directories=true;
      add_directory("build");
      add_directory("build/retroarch-native");
      add_directory("build/retroarch-native/save");
      add_directory(paths[variant]);
      fail_base_open=true;
      creation_failure[0]='\0'; error[0]='\0';
      s=arc_install_store_open(paths[variant],&p,true,true,store_log,NULL,
                               error,sizeof(error));
      assert(s && arc_install_store_state(s)==ARC_INSTALL_STORE_DAMAGED &&
             strstr(creation_failure,"op=vfs-open-write path=disc.g0.hdf.tmp"));
      arc_install_store_close(s);
   }
   clear_files();
}
int main(void)
{
   arc_io_set_vfs(&vfs);
   test_roundtrip(); test_damage(); test_tail(); test_identity();
   test_directory_states(); test_record_damage();
   test_interrupted_operations();
   test_cap_and_compaction();
   test_journal_cap();
   test_compaction_interruptions();
   test_compaction_retry_with_stale_generation();
   test_writable_orphan_cleanup();
   test_probe_temporaries(); test_probe_failures();
   test_relative_save_paths();
   clear_files(); arc_io_set_vfs(NULL);
   puts("install-disc store: pass");
   return 0;
}
