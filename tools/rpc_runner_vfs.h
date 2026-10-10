#include <limits.h>
#include <assert.h>
/* Non-shipping confined runner VFS. GPL-2.0-only. */
/* Reads remain unrestricted. Mutations require an existing canonical parent
 * below --save; existing symlinks/junctions must also resolve below that root. */
#ifndef ARC_RUNNER_VFS_AUDIT
#define ARC_RUNNER_VFS_AUDIT(path,mode) ((void)(path), (void)(mode))
#endif
/* Durability/process-kill tests retain sync by default. Timing opts into the
 * frontend's fflush-only behavior explicitly (D-156). */
static bool vfs_flush_fsync = true;
static char save_directory[4096];
static unsigned vfs_open_handles,vfs_open_directories;
static inline void vfs_assert_no_open_handles(void)
{assert(vfs_open_handles==0&&vfs_open_directories==0);}
#if defined(_WIN32)
#include <direct.h>
#include <sys/stat.h>
static bool canonical(const char *p,char out[4096])
{
    HANDLE h=CreateFileA(p,0,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,NULL);
    if(h==INVALID_HANDLE_VALUE)return false;
    DWORD n=GetFinalPathNameByHandleA(h,out,4096,FILE_NAME_NORMALIZED);CloseHandle(h);
    if(!n || n>=4096)return false;
    if(!strncmp(out,"\\\\?\\UNC\\",8)){memmove(out+2,out+8,strlen(out+8)+1);out[0]=out[1]='\\';}
    else if(!strncmp(out,"\\\\?\\",4))memmove(out,out+4,strlen(out+4)+1);
    return true;
}
#else
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
static bool canonical(const char *p,char out[4096]) { return realpath(p,out)!=NULL; }
#endif
static bool below_save(const char *p)
{
    size_t n=strlen(save_directory);
    if(!n)return false;
    while(n && (save_directory[n-1]=='/' || save_directory[n-1]=='\\'))--n;
    if(!n)return p[0]=='/';
#if defined(_WIN32)
    return n && !_strnicmp(p,save_directory,n) && (p[n]=='\\' || p[n]=='/' || !p[n]);
#else
    return n && !strncmp(p,save_directory,n) && (p[n]=='/' || !p[n]);
#endif
}
static bool writable(const char *p)
{
    if(!*save_directory || strlen(p)>=4096)return false;
    char resolved[4096];if(canonical(p,resolved))return below_save(resolved);
#if defined(_WIN32)
    if(GetFileAttributesA(p)!=INVALID_FILE_ATTRIBUTES)return false;
#else
    struct stat existing;if(!lstat(p,&existing))return false; /* dangling link */
#endif
    char parent[4096];strcpy(parent,p);char *slash=strrchr(parent,'/');
#if defined(_WIN32)
    char *back=strrchr(parent,'\\');if(back && (!slash || back>slash))slash=back;
#endif
    if(!slash || !slash[1] || !strcmp(slash+1,"..") || !strcmp(slash+1,"."))return false;
#if defined(_WIN32)
    /* C:\\leaf has C:\\ as its parent, rather than drive-relative C:. */
    if(slash==parent+2 && parent[1]==':')slash[1]=0;
    else *slash=0;
#else
    *slash=0;
#endif
    return canonical(*parent?parent:"/",resolved) && below_save(resolved);
}
/* RetroArch buffered seek and per-handle size semantics (D-150/D-151). */
struct retro_vfs_file_handle { FILE *file; bool writable,readable; int64_t cached_size; };
static struct retro_vfs_file_handle *vfs_open(const char *path,unsigned mode,unsigned hints)
{
    ARC_RUNNER_VFS_AUDIT(path,mode);
    (void)hints;bool write=(mode&RETRO_VFS_FILE_ACCESS_WRITE)!=0;
#if defined(ARC_RUNNER_VFS_WRITE_REFUSE)
    if(write && ARC_RUNNER_VFS_WRITE_REFUSE(path))return NULL;
#endif
    if(write && !writable(path))return NULL;
    if(!(mode&RETRO_VFS_FILE_ACCESS_READ) && !write)return NULL;
    const char *access=write?(mode&RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING?"r+b":((mode&RETRO_VFS_FILE_ACCESS_READ)?"wb+":"wb")):"rb";
    FILE *file=fopen(path,access);if(!file)return NULL;
    struct retro_vfs_file_handle *h=malloc(sizeof(*h));if(!h){fclose(file);return NULL;}
    h->file=file;h->writable=write;h->readable=(mode&RETRO_VFS_FILE_ACCESS_READ)!=0 || (write&&(mode&RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING));
    if(fseek(file,0,SEEK_END)){fclose(file);free(h);return NULL;}
    h->cached_size=ftell(file);
    if(h->cached_size<0 || fseek(file,0,SEEK_SET)){fclose(file);free(h);return NULL;}
    ++vfs_open_handles;return h;
}
static int vfs_close(struct retro_vfs_file_handle *h)
{ assert(vfs_open_handles);int result=fclose(h->file);free(h);--vfs_open_handles;return result; }
static int64_t vfs_size(struct retro_vfs_file_handle *h) { return h->cached_size; }
static int64_t vfs_tell(struct retro_vfs_file_handle *h) { return ftell(h->file); }
static int64_t vfs_seek(struct retro_vfs_file_handle *h,int64_t offset,int position)
{ int origin=position==RETRO_VFS_SEEK_POSITION_START?SEEK_SET:position==RETRO_VFS_SEEK_POSITION_CURRENT?SEEK_CUR:SEEK_END;
  if(offset<LONG_MIN || offset>LONG_MAX || fseek(h->file,(long)offset,origin))return -1;
  return 0; }
static int64_t vfs_read(struct retro_vfs_file_handle *h,void *data,uint64_t size)
{ if(!h->readable||size>SIZE_MAX)return -1;size_t n=fread(data,1,(size_t)size,h->file);return ferror(h->file)?-1:(int64_t)n; }
static int64_t vfs_write(struct retro_vfs_file_handle *h,const void *data,uint64_t size)
{ if(!h->writable || size>SIZE_MAX)return -1;int64_t at=ftell(h->file);if(at<0)return -1;
  size_t n=fwrite(data,1,(size_t)size,h->file);
  if(at+(int64_t)n>h->cached_size)h->cached_size=at+(int64_t)n;
  return ferror(h->file)?-1:(int64_t)n; }
static int vfs_flush(struct retro_vfs_file_handle *h)
{
    if(!h->writable || fflush(h->file))return -1;
    if(!vfs_flush_fsync)return 0;
#if defined(_WIN32)
    return _commit(_fileno(h->file));
#else
    return fsync(fileno(h->file));
#endif
}
static int vfs_remove(const char *p) { return writable(p)?remove(p):-1; }
static int vfs_rename(const char *a,const char *b) { return writable(a)&&writable(b)?rename(a,b):-1; }
static int32_t vfs_stat(const char *p,int32_t *size)
{
    struct stat s;if(stat(p,&s))return 0;if(size)*size=s.st_size>INT32_MAX?INT32_MAX:(int32_t)s.st_size;
    return RETRO_VFS_STAT_IS_VALID|(S_ISDIR(s.st_mode)?RETRO_VFS_STAT_IS_DIRECTORY:0);
}
static int vfs_mkdir(const char *p)
{
    if(!writable(p))return -1;
#if defined(_WIN32)
    return _mkdir(p);
#else
    return mkdir(p,0700);
#endif
}
struct retro_vfs_dir_handle {
    char path[4096], name[4096];bool directory;
#if defined(_WIN32)
    HANDLE handle;WIN32_FIND_DATAA data;bool first;
#else
    DIR *handle;
#endif
};
static struct retro_vfs_dir_handle *vfs_opendir(const char *p,bool hidden)
{
    (void)hidden;struct retro_vfs_dir_handle *d=calloc(1,sizeof(*d));if(!d)return NULL;
    if(strlen(p)>=sizeof(d->path)){free(d);return NULL;}strcpy(d->path,p);
#if defined(_WIN32)
    char pattern[4096];if(snprintf(pattern,sizeof(pattern),"%s/*",p)>=(int)sizeof(pattern)){free(d);return NULL;}
    d->handle=FindFirstFileA(pattern,&d->data);d->first=true;
    if(d->handle==INVALID_HANDLE_VALUE){free(d);return NULL;}
#else
    d->handle=opendir(p);if(!d->handle){free(d);return NULL;}
#endif
    ++vfs_open_directories;return d;
}
static bool vfs_readdir(struct retro_vfs_dir_handle *d)
{
#if defined(_WIN32)
    if(!d->first && !FindNextFileA(d->handle,&d->data))return false;
    d->first=false;
    strcpy(d->name,d->data.cFileName);d->directory=(d->data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0;
#else
    struct dirent *e=readdir(d->handle);if(!e)return false;strcpy(d->name,e->d_name);
    char p[8192];snprintf(p,sizeof(p),"%s/%s",d->path,d->name);struct stat s;d->directory=!stat(p,&s)&&S_ISDIR(s.st_mode);
#endif
    return true;
}
static const char *vfs_dirname(struct retro_vfs_dir_handle *d) { return d->name; }
static bool vfs_isdir(struct retro_vfs_dir_handle *d) { return d->directory; }
static int vfs_closedir(struct retro_vfs_dir_handle *d)
{
#if defined(_WIN32)
    int result=FindClose(d->handle)?0:-1;
#else
    int result=closedir(d->handle);
#endif
    assert(vfs_open_directories);free(d);--vfs_open_directories;return result;
}
static struct retro_vfs_interface vfs={.open=vfs_open,.close=vfs_close,.size=vfs_size,.tell=vfs_tell,.read=vfs_read,.seek=vfs_seek,
    .write=vfs_write,.flush=vfs_flush,.remove=vfs_remove,.rename=vfs_rename,.stat=vfs_stat,.mkdir=vfs_mkdir,
    .opendir=vfs_opendir,.readdir=vfs_readdir,.dirent_get_name=vfs_dirname,.dirent_is_dir=vfs_isdir,.closedir=vfs_closedir};
