/* Bounded test-owned in-memory filesystem, never product storage. GPL-2.0-only. */
#include "arc_io.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define TEST_FILES 1024u
typedef struct {char path[1100];uint8_t *data;size_t size,capacity;bool present;} TestFile;
static TestFile files[TEST_FILES],baseline[TEST_FILES];
/* RetroArch buffered VFS: seek returns zero; size is owned by this handle. */
struct retro_vfs_file_handle {unsigned file;size_t at,cached_size;bool writable,readable,used;};
struct retro_vfs_dir_handle {char path[1100];unsigned at,current;bool used;};
static struct retro_vfs_file_handle handles[16];static struct retro_vfs_dir_handle directory;
static bool silent_readdir,full_storage,case_folded,refuse_update_open,refuse_truncate;
static uint64_t metadata_calls,namespace_calls,vfs_requested_bytes;
static unsigned steps,fail_at,kill_at;static bool killed;static unsigned fault;
static char directories[16][1100];static unsigned dir_count;
static char baseline_directories[16][1100];static unsigned baseline_dir_count;
static uint64_t physical_high,temp_high;static unsigned physical_chunks_high;
static void test_vfs_no_open_handles(void)
{for(unsigned i=0;i<16;++i)assert(!handles[i].used);assert(!directory.used);}
static void measure_files(void)
{
 uint64_t physical=0,temp=0,journals=0;unsigned chunks=0,manifests=0,journal_count=0;
 for(unsigned i=0;i<TEST_FILES;++i)if(files[i].present){const char *leaf=strrchr(files[i].path,'/')+1;bool owned=false;
  if(!strncmp(leaf,"manifest.",9)){++manifests;owned=true;}
  else if(!strncmp(leaf,"journal.",8)){++journal_count;journals+=files[i].size;owned=true;}
  else if(leaf[0]=='c'&&strlen(leaf)==22){++chunks;owned=true;}
  if(!strcmp(leaf,"chunk.tmp")||!strcmp(leaf,"manifest.tmp")){temp+=files[i].size;owned=true;}
  if(owned)physical+=files[i].size;
 }
 assert(chunks<=1008&&manifests<=2&&journal_count<=2&&journals<=8388608&&temp<=286600);
 assert(physical<=272811464+UINT64_C(286600));
 if(physical>physical_high)physical_high=physical;if(temp>temp_high)temp_high=temp;
 if(chunks>physical_chunks_high)physical_chunks_high=chunks;
}
static bool same_name(const char *a,const char *b)
{if(!case_folded)return !strcmp(a,b);while(*a&&*b){unsigned x=(unsigned char)*a++,y=(unsigned char)*b++;
 if(x>='A'&&x<='Z')x+='a'-'A';if(y>='A'&&y<='Z')y+='a'-'A';if(x!=y)return false;}return *a==*b;}
static int find(const char *p,bool create)
{for(unsigned i=0;i<TEST_FILES;++i)if(files[i].present&&same_name(files[i].path,p))return (int)i;
 if(create)for(unsigned i=0;i<TEST_FILES;++i)if(!files[i].present){snprintf(files[i].path,sizeof(files[i].path),"%s",p);files[i].present=true;files[i].size=0;return (int)i;}return -1;}
/* Test-only sanitizer profile: record callback classes, never guest bytes.
 * All mutations remain exhaustive; only repeated read/inspection callbacks
 * are sampled at both ends of each file-kind/transfer-size class. */
#define TEST_CALLBACK_CAP 16384u
static uint64_t callback_tags[TEST_CALLBACK_CAP],callback_detail;
static bool sanitizer_faults,callback_selected[TEST_CALLBACK_CAP];
static unsigned test_file_kind(const char *p)
{const char *leaf=strrchr(p,'/');leaf=leaf?leaf+1:p;
 if(!strcmp(leaf,"chunk.tmp"))return 1;if(!strncmp(leaf,"c",1)&&strlen(leaf)==22)return 2;
 if(!strcmp(leaf,"manifest.tmp"))return 3;if(!strncmp(leaf,"manifest.",9))return 4;
 if(!strncmp(leaf,"journal.",8))return 5;return 0;}
static void callback_file(const char *p,uint64_t bytes,bool create)
{callback_detail=((uint64_t)test_file_kind(p)<<8)|(bytes<<16)|((uint64_t)create<<63);}
static void select_callbacks(unsigned count)
{
 assert(count<TEST_CALLBACK_CAP);memset(callback_selected,0,sizeof(callback_selected));
 for(unsigned at=1;at<=count;++at){uint64_t tag=callback_tags[at];unsigned kind=(unsigned)(tag&255);
  bool mutation=kind==6||kind==7||kind==8||kind==9||kind==11||kind==17||(tag>>63);
  bool first=true,last=true;for(unsigned i=1;i<at;++i)if(callback_tags[i]==tag){first=false;break;}
  for(unsigned i=at+1;i<=count;++i)if(callback_tags[i]==tag){last=false;break;}
  callback_selected[at]=!sanitizer_faults||mutation||first||last;
 }
 unsigned selected=0;for(unsigned at=1;at<=count;++at)selected+=callback_selected[at];
 printf("memory fault profile=%s cuts=%u/%u (all mutations; first/last inspection classes)\n",sanitizer_faults?"sanitizer":"full",selected,count);fflush(stdout);
}
static bool before(unsigned kind)
{if(kind==8||kind==9||kind==11)++namespace_calls;
 if(kind==1||kind==2||(kind>=7&&kind<=11))++metadata_calls;
 ++steps;if(steps<TEST_CALLBACK_CAP)callback_tags[steps]=callback_detail|kind;callback_detail=0;if(kill_at&&steps==kill_at)killed=true;return !killed&&!(steps==fail_at&&(fault==0||fault==kind));}
static struct retro_vfs_file_handle *test_open(const char *p,unsigned mode,unsigned hint)
{(void)hint;callback_file(p,0,(mode&2)&&!(mode&4));if((mode&2)&&!(mode&4))++namespace_calls;if(!before(1)|| (refuse_update_open&&(mode&2)&&(mode&4)))return NULL;bool write=(mode&2)!=0,update=(mode&4)!=0;int i=find(p,write&&!update);if(i<0)return NULL;
 if(write&&!update)files[i].size=0;measure_files();for(unsigned h=0;h<16;++h)if(!handles[h].used){handles[h]=(struct retro_vfs_file_handle){(unsigned)i,0,files[i].size,write,(mode&1)!=0||(write&&update),true};return handles+h;}assert(0);return NULL;}
static int test_close(struct retro_vfs_file_handle *f) {assert(f->used);callback_file(files[f->file].path,0,false);bool ok=before(2);f->used=false;return ok?0:-1;}
static int64_t test_size(struct retro_vfs_file_handle *f) {callback_file(files[f->file].path,0,false);return before(3)?(int64_t)f->cached_size:-1;}
static int64_t test_tell(struct retro_vfs_file_handle *f) {callback_file(files[f->file].path,0,false);return before(18)?(int64_t)f->at:-1;}
static int64_t test_seek(struct retro_vfs_file_handle *f,int64_t at,int origin)
{callback_file(files[f->file].path,0,false);if(!before(4))return -1;int64_t base=origin==0?0:origin==1?(int64_t)f->at:(int64_t)files[f->file].size;if(at< -base)return -1;f->at=(size_t)(base+at);return 0;}
static int64_t test_read(struct retro_vfs_file_handle *f,void *p,uint64_t n)
{vfs_requested_bytes+=n;callback_file(files[f->file].path,n,false);if(!before(5)||!f->readable)return -1;TestFile *file=files+f->file;if(f->at>file->size)return -1;if(n>file->size-f->at)n=file->size-f->at;memcpy(p,file->data+f->at,(size_t)n);f->at+=(size_t)n;return (int64_t)n;}
static int64_t test_write(struct retro_vfs_file_handle *f,const void *p,uint64_t n)
{vfs_requested_bytes+=n;callback_file(files[f->file].path,n,false);bool ok=before(6);if(killed||!f->writable||(!ok&&full_storage))return -1;if(!ok)n=n?n/2:0;TestFile *file=files+f->file;size_t end=f->at+(size_t)n;
 if(end>file->capacity){size_t cap=end+4096;uint8_t *q=realloc(file->data,cap);assert(q);file->data=q;file->capacity=cap;}
 if(f->at>file->size)memset(file->data+file->size,0,f->at-file->size);memcpy(file->data+f->at,p,(size_t)n);f->at=end;if(end>file->size)file->size=end;if(end>f->cached_size)f->cached_size=end;measure_files();return (int64_t)n;}
static int test_flush(struct retro_vfs_file_handle *f) {callback_file(files[f->file].path,0,false);return before(7)?0:-1;}
static int test_remove(const char *p) {callback_file(p,0,false);if(!before(8))return -1;int i=find(p,false);if(i<0)return -1;files[i].present=false;measure_files();return 0;}
static int test_rename(const char *a,const char *b) {callback_file(b,0,false);if(!before(9))return -1;int i=find(a,false);if(i<0||find(b,false)>=0)return -1;snprintf(files[i].path,sizeof(files[i].path),"%s",b);measure_files();return 0;}
static int32_t test_stat(const char *p,int32_t *n)
{callback_file(p,0,false);if(!before(10))return 0;int i=find(p,false);if(i>=0){if(n)*n=(int32_t)files[i].size;return 1;}for(unsigned d=0;d<dir_count;++d)if(!strcmp(p,directories[d]))return 3;return 0;}
static int test_mkdir(const char *p) {callback_file(p,0,false);if(!before(11)||dir_count==16)return -1;snprintf(directories[dir_count++],1100,"%s",p);return 0;}
static struct retro_vfs_dir_handle *test_opendir(const char *p,bool hidden)
{(void)hidden;if(!before(12))return NULL;assert(!directory.used);memset(&directory,0,sizeof(directory));directory.used=true;snprintf(directory.path,sizeof(directory.path),"%s",p);return &directory;}
static bool test_readdir(struct retro_vfs_dir_handle *d)
{if(!before(13)||silent_readdir)return false;while(d->at<TEST_FILES){unsigned i=d->at++;const char *leaf=strrchr(files[i].path,'/');if(files[i].present&&leaf&&(size_t)(leaf-files[i].path)==strlen(d->path)&&!strncmp(files[i].path,d->path,strlen(d->path))){d->current=i;return true;}}return false;}
static const char *test_name(struct retro_vfs_dir_handle *d) {return before(14)?strrchr(files[d->current].path,'/')+1:NULL;}
static bool test_isdir(struct retro_vfs_dir_handle *d) {(void)d;return !before(15);}
static int test_closedir(struct retro_vfs_dir_handle *d) {assert(d->used);bool ok=before(16);d->used=false;return ok?0:-1;}
static int64_t test_truncate(struct retro_vfs_file_handle *f,int64_t n)
{callback_file(files[f->file].path,0,false);if(!before(17)||refuse_truncate||n<0||(uint64_t)n>files[f->file].size)return -1;files[f->file].size=f->cached_size=(size_t)n;return 0;}
static struct retro_vfs_interface vfs={.open=test_open,.close=test_close,.size=test_size,.tell=test_tell,.seek=test_seek,.read=test_read,.write=test_write,.flush=test_flush,.remove=test_remove,.rename=test_rename,.stat=test_stat,.mkdir=test_mkdir,.opendir=test_opendir,.readdir=test_readdir,.dirent_get_name=test_name,.dirent_is_dir=test_isdir,.closedir=test_closedir,.truncate=test_truncate};
static void reset_fault(void) {test_vfs_no_open_handles();steps=fail_at=kill_at=fault=0;killed=false;full_storage=false;}
static void fresh_files(void) {for(unsigned i=0;i<TEST_FILES;++i){free(files[i].data);memset(files+i,0,sizeof(*files));}physical_high=temp_high=physical_chunks_high=0;dir_count=0;silent_readdir=case_folded=refuse_update_open=refuse_truncate=false;test_mkdir("/save");reset_fault();arc_io_set_vfs(&vfs);}
static void snapshot(void)
{baseline_dir_count=dir_count;memcpy(baseline_directories,directories,sizeof(directories));
 for(unsigned i=0;i<TEST_FILES;++i){free(baseline[i].data);baseline[i]=files[i];baseline[i].data=NULL;baseline[i].capacity=baseline[i].size;
 if(baseline[i].present&&baseline[i].size){baseline[i].data=malloc(baseline[i].size);assert(baseline[i].data);memcpy(baseline[i].data,files[i].data,baseline[i].size);}}}
static void restore(void)
{dir_count=baseline_dir_count;memcpy(directories,baseline_directories,sizeof(directories));
 for(unsigned i=0;i<TEST_FILES;++i){free(files[i].data);files[i]=baseline[i];files[i].data=NULL;files[i].capacity=files[i].size;
 if(files[i].present&&files[i].size){files[i].data=malloc(files[i].size);assert(files[i].data);memcpy(files[i].data,baseline[i].data,files[i].size);}}reset_fault();}
static void test_vfs_rotation_writer(void)
{
 unsigned count=0;
 for(unsigned i=0;i<16;++i)if(handles[i].used){++count;assert(handles[i].writable&&!handles[i].readable&&handles[i].at==0&&handles[i].cached_size==0);
  const char *leaf=strrchr(files[handles[i].file].path,'/')+1;
  assert((!strcmp(leaf,"journal.a")||!strcmp(leaf,"journal.b"))&&files[handles[i].file].size==0);}
 assert(count==1&&!directory.used);
}
#define rpc_hd_test_rotation_writer test_vfs_rotation_writer
#define rpc_hd_test_metadata_calls() metadata_calls
#define rpc_hd_test_namespace_calls() namespace_calls
#define rpc_hd_test_no_open_handles test_vfs_no_open_handles
#include "rpc_hd_handle_checks.h"
