/* Bounded test-owned in-memory filesystem, never product storage. GPL-2.0-only. */
#include "arc_io.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define TEST_FILES 1024u
typedef struct {char path[1100];uint8_t *data;size_t size,capacity;bool present;} TestFile;
static TestFile files[TEST_FILES],baseline[TEST_FILES];
struct retro_vfs_file_handle {unsigned file;size_t at;bool writable,used;};
struct retro_vfs_dir_handle {char path[1100];unsigned at,current;};
static struct retro_vfs_file_handle handles[16];static struct retro_vfs_dir_handle directory;
static bool silent_readdir,full_storage,case_folded;
static unsigned steps,fail_at,kill_at;static bool killed;static unsigned fault;
static char directories[16][1100];static unsigned dir_count;
static char baseline_directories[16][1100];static unsigned baseline_dir_count;
static uint64_t physical_high,temp_high;static unsigned physical_chunks_high;
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
static bool before(unsigned kind)
{++steps;if(kill_at&&steps==kill_at)killed=true;return !killed&&!(steps==fail_at&&(fault==0||fault==kind));}
static struct retro_vfs_file_handle *test_open(const char *p,unsigned mode,unsigned hint)
{(void)hint;if(!before(1))return NULL;bool write=(mode&2)!=0,update=(mode&4)!=0;int i=find(p,write&&!update);if(i<0)return NULL;
 if(write&&!update)files[i].size=0;measure_files();for(unsigned h=0;h<16;++h)if(!handles[h].used){handles[h]=(struct retro_vfs_file_handle){(unsigned)i,0,write,true};return handles+h;}assert(0);return NULL;}
static int test_close(struct retro_vfs_file_handle *f) {bool ok=before(2);f->used=false;return ok?0:-1;}
static int64_t test_size(struct retro_vfs_file_handle *f) {return before(3)?(int64_t)files[f->file].size:-1;}
static int64_t test_seek(struct retro_vfs_file_handle *f,int64_t at,int origin)
{if(!before(4))return -1;int64_t base=origin==0?0:origin==1?(int64_t)f->at:(int64_t)files[f->file].size;if(at< -base)return -1;f->at=(size_t)(base+at);return (int64_t)f->at;}
static int64_t test_read(struct retro_vfs_file_handle *f,void *p,uint64_t n)
{if(!before(5))return -1;TestFile *file=files+f->file;if(f->at>file->size)return -1;if(n>file->size-f->at)n=file->size-f->at;memcpy(p,file->data+f->at,(size_t)n);f->at+=(size_t)n;return (int64_t)n;}
static int64_t test_write(struct retro_vfs_file_handle *f,const void *p,uint64_t n)
{bool ok=before(6);if(killed||!f->writable||(!ok&&full_storage))return -1;if(!ok)n=n?n/2:0;TestFile *file=files+f->file;size_t end=f->at+(size_t)n;
 if(end>file->capacity){size_t cap=end+4096;uint8_t *q=realloc(file->data,cap);assert(q);file->data=q;file->capacity=cap;}
 if(f->at>file->size)memset(file->data+file->size,0,f->at-file->size);memcpy(file->data+f->at,p,(size_t)n);f->at=end;if(end>file->size)file->size=end;measure_files();return (int64_t)n;}
static int test_flush(struct retro_vfs_file_handle *f) {(void)f;return before(7)?0:-1;}
static int test_remove(const char *p) {if(!before(8))return -1;int i=find(p,false);if(i<0)return -1;files[i].present=false;measure_files();return 0;}
static int test_rename(const char *a,const char *b) {if(!before(9))return -1;int i=find(a,false);if(i<0||find(b,false)>=0)return -1;snprintf(files[i].path,sizeof(files[i].path),"%s",b);measure_files();return 0;}
static int32_t test_stat(const char *p,int32_t *n)
{if(!before(10))return 0;int i=find(p,false);if(i>=0){if(n)*n=(int32_t)files[i].size;return 1;}for(unsigned d=0;d<dir_count;++d)if(!strcmp(p,directories[d]))return 3;return 0;}
static int test_mkdir(const char *p) {if(!before(11)||dir_count==16)return -1;snprintf(directories[dir_count++],1100,"%s",p);return 0;}
static struct retro_vfs_dir_handle *test_opendir(const char *p,bool hidden)
{(void)hidden;if(!before(12))return NULL;memset(&directory,0,sizeof(directory));snprintf(directory.path,sizeof(directory.path),"%s",p);return &directory;}
static bool test_readdir(struct retro_vfs_dir_handle *d)
{if(!before(13)||silent_readdir)return false;while(d->at<TEST_FILES){unsigned i=d->at++;const char *leaf=strrchr(files[i].path,'/');if(files[i].present&&leaf&&(size_t)(leaf-files[i].path)==strlen(d->path)&&!strncmp(files[i].path,d->path,strlen(d->path))){d->current=i;return true;}}return false;}
static const char *test_name(struct retro_vfs_dir_handle *d) {return before(14)?strrchr(files[d->current].path,'/')+1:NULL;}
static bool test_isdir(struct retro_vfs_dir_handle *d) {(void)d;return !before(15);}
static int test_closedir(struct retro_vfs_dir_handle *d) {(void)d;return before(16)?0:-1;}
static int64_t test_truncate(struct retro_vfs_file_handle *f,int64_t n)
{if(!before(17)||n<0||(uint64_t)n>files[f->file].size)return -1;files[f->file].size=(size_t)n;return 0;}
static struct retro_vfs_interface vfs={.open=test_open,.close=test_close,.size=test_size,.seek=test_seek,.read=test_read,.write=test_write,.flush=test_flush,.remove=test_remove,.rename=test_rename,.stat=test_stat,.mkdir=test_mkdir,.opendir=test_opendir,.readdir=test_readdir,.dirent_get_name=test_name,.dirent_is_dir=test_isdir,.closedir=test_closedir,.truncate=test_truncate};
static void reset_fault(void) {steps=fail_at=kill_at=fault=0;killed=false;full_storage=false;memset(handles,0,sizeof(handles));}
static void fresh_files(void) {for(unsigned i=0;i<TEST_FILES;++i){free(files[i].data);memset(files+i,0,sizeof(*files));}physical_high=temp_high=physical_chunks_high=0;dir_count=0;silent_readdir=case_folded=false;test_mkdir("/save");reset_fault();arc_io_set_vfs(&vfs);}
static void snapshot(void)
{baseline_dir_count=dir_count;memcpy(baseline_directories,directories,sizeof(directories));
 for(unsigned i=0;i<TEST_FILES;++i){free(baseline[i].data);baseline[i]=files[i];baseline[i].data=NULL;baseline[i].capacity=baseline[i].size;
 if(baseline[i].present&&baseline[i].size){baseline[i].data=malloc(baseline[i].size);assert(baseline[i].data);memcpy(baseline[i].data,files[i].data,baseline[i].size);}}}
static void restore(void)
{dir_count=baseline_dir_count;memcpy(directories,baseline_directories,sizeof(directories));
 for(unsigned i=0;i<TEST_FILES;++i){free(files[i].data);files[i]=baseline[i];files[i].data=NULL;files[i].capacity=files[i].size;
 if(files[i].present&&files[i].size){files[i].data=malloc(files[i].size);assert(files[i].data);memcpy(files[i].data,baseline[i].data,files[i].size);}}reset_fault();}
