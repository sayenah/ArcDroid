/* Ordinary retro_load_game preparation, authored catalogue seam and legacy
 * non-interference. No private firmware. GPL-2.0-only. */
#if !defined(_WIN32)
#define _XOPEN_SOURCE 700
#endif
#include "libretro.h"
#include "rpc_selection.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#include <io.h>
static HMODULE library;
static void *symbol(const char *name)
{ FARPROC fn=GetProcAddress(library,name);void *p;memcpy(&p,&fn,sizeof(p));return p; }
#else
#include <dlfcn.h>
#include <unistd.h>
static void *library;
static void *symbol(const char *name){return dlsym(library,name);}
#endif
#include "../tools/rpc_runner_vfs.h"

static void (*set_environment)(retro_environment_t);
static void (*init_core)(void),(*deinit_core)(void),(*unload_core)(void);
static bool (*load_core)(const struct retro_game_info *);
static void (*set_catalogue)(const ArcRpcFirmwareFingerprint *,size_t);
static bool (*media_masks)(unsigned,ArcRpcCompatibility *);
static const char *(*content_id)(void),*(*cmos_path)(void);
static const char *(*staged_path)(unsigned);
static char root[4096],system_path[4096],firmware_path[4096],diagnostic[256],logs[32768],notices[8192];
static char identity[41],persistent_path[4096];
static const char *machine_option="Auto",*os_option="Auto";
static unsigned log_count,notice_count,mask_count;
static unsigned firmware_dirs,firmware_files,diagnostic_dirs,diagnostic_files;
static ArcRpcCompatibility masks[32];
static bool root_failure,close_failure,read_failure,name_failure,silent_listing,budget_listing;
static unsigned listing_mode,listing_at;static bool listing_armed;static struct retro_vfs_dir_handle *scan_dir;
static const char *const permuted_names[]={"rpc-3", "legacy-rom", "rpc-1", "rpc-0", "rpc-2", "rpc-copy", "tiny"};
static struct retro_vfs_interface wrapper;
static struct retro_vfs_file_handle *test_open(const char *p,unsigned mode,unsigned hints)
{if(!strncmp(p,firmware_path,strlen(firmware_path))&&p[strlen(firmware_path)]=='/')++firmware_files;
   return vfs_open(p,mode,hints);}
static int64_t test_read(struct retro_vfs_file_handle *h,void *data,uint64_t size)
{if(read_failure&&size==35)return -1;return vfs_read(h,data,size);}
static struct retro_vfs_dir_handle *test_opendir(const char *p,bool hidden)
{
   bool firmware=!strcmp(p,firmware_path);
   if(firmware)++firmware_dirs;
   if(firmware&&root_failure){root_failure=false;return NULL;}
   struct retro_vfs_dir_handle *d=vfs_opendir(p,hidden);
   if(firmware&&listing_armed){listing_armed=false;scan_dir=d;listing_at=0;}
   return d;
}
static bool test_readdir(struct retro_vfs_dir_handle *d)
{
   if(d!=scan_dir)return vfs_readdir(d);
   if(silent_listing)return false;
   if(budget_listing){if(listing_at++==1100)return false;strcpy(d->name,"tiny");d->directory=false;return true;}
   if(listing_mode){if(listing_at==7)return false;unsigned i=listing_mode==1?listing_at:6-listing_at;
      ++listing_at;strcpy(d->name,permuted_names[i]);d->directory=false;return true;}
   return vfs_readdir(d);
}
static const char *test_dir_name(struct retro_vfs_dir_handle *d)
{return d==scan_dir&&name_failure?NULL:vfs_dirname(d);}
static int test_closedir(struct retro_vfs_dir_handle *d)
{bool scan=d==scan_dir;if(scan)scan_dir=NULL;int rc=vfs_closedir(d);return scan&&close_failure?-1:rc;}
static void log_line(enum retro_log_level level,const char *format,...)
{
   (void)level;char line[4096];va_list a;va_start(a,format);vsnprintf(line,sizeof(line),format,a);va_end(a);
   if(!strncmp(line,"RPC selection:",14)){
      ++log_count;snprintf(diagnostic,sizeof(diagnostic),"%s",line);mask_count=0;
      diagnostic_dirs=firmware_dirs;diagnostic_files=firmware_files;
      for(unsigned i=0;i<32;++i){ArcRpcCompatibility m;if(!media_masks(i,&m))break;masks[mask_count++]=m;}
   }else if(strlen(logs)+strlen(line)<sizeof(logs))strcat(logs,line);
}
static bool environment(unsigned command,void *p)
{
   switch(command){
      case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:((struct retro_log_callback *)p)->log=log_line;return true;
      case RETRO_ENVIRONMENT_GET_VFS_INTERFACE:((struct retro_vfs_interface_info *)p)->iface=&wrapper;return true;
      case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:*(const char **)p=system_path;return true;
      case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:*(const char **)p=save_directory;return true;
      case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:return *(enum retro_pixel_format *)p==RETRO_PIXEL_FORMAT_XRGB8888;
      case RETRO_ENVIRONMENT_GET_VARIABLE:{struct retro_variable *v=p;
         if(!strcmp(v->key,"arcdroid_machine")){v->value=machine_option;return true;}
         if(!strcmp(v->key,"arcdroid_os")){v->value=os_option;return true;}
         if(!strcmp(v->key,"arcdroid_disk_writes")){v->value="off";return true;}
         if(!strcmp(v->key,"arcdroid_hard_disc")){v->value="off";return true;}
         if(!strcmp(v->key,"arcdroid_jfd_boot")){v->value="native";return true;}
         return false;}
      case RETRO_ENVIRONMENT_SET_MESSAGE:{const struct retro_message *m=p;++notice_count;
         if(strlen(notices)+strlen(m->msg)+2<sizeof(notices)){strcat(notices,m->msg);strcat(notices,"\n");}return true;}
      case RETRO_ENVIRONMENT_SET_MESSAGE_EXT:{const struct retro_message_ext *m=p;++notice_count;
         if(strlen(notices)+strlen(m->msg)+2<sizeof(notices)){strcat(notices,m->msg);strcat(notices,"\n");}return true;}
      default:return false;
   }
}
#define LOAD(fn,name) do{void *p=symbol(name);assert(p);memcpy(&fn,&p,sizeof(fn));}while(0)
static void path(char *out,const char *base,const char *leaf)
{int n=snprintf(out,4096,"%s/%s",base,leaf);assert(n>0&&n<4096);}
static void write_bytes(const char *base,const char *leaf,const void *data,size_t n)
{char p[4096];path(p,base,leaf);FILE *f=fopen(p,"wb");assert(f&&fwrite(data,1,n,f)==n&&fclose(f)==0);}
static void put32(uint8_t *b,unsigned offset,unsigned value)
{for(unsigned j=0;j<4;++j)b[offset+j]=(uint8_t)(value>>(j*8));}
static void make_jfd(const char *leaf,unsigned cpu,unsigned os,bool legacy,unsigned disc,unsigned count)
{
   uint8_t b[592]={0};unsigned t=legacy?304:316,s=t+8,d=s+12;
   memcpy(b,"JFDI",4);put32(b,8,d+256);put32(b,12,(count<<16)|disc);
   put32(b,24,t);put32(b,28,s);put32(b,32,d);memcpy(b+48,"Authored selection disc",23);
   if(!legacy)put32(b,304,(cpu<<8)|(os<<16));
   put32(b,t,0);put32(b,t+4,0xffffffff);put32(b,s,(2<<16)|1);put32(b,s+8,0xffffffff);
   write_bytes(root,leaf,b,d+256);
}
static bool prepare(const char *leaf,const char *reason,const char *model,const char *os)
{
   diagnostic[0]=logs[0]=notices[0]=0;notice_count=log_count=mask_count=0;
   firmware_dirs=firmware_files=diagnostic_dirs=diagnostic_files=0;
   listing_armed=true;char file[4096];path(file,root,leaf);
   struct retro_game_info g={.path=file};bool ok=load_core(&g);
   if(log_count!=1||!strstr(diagnostic,reason)||!strstr(diagnostic,model)||!strstr(diagnostic,os)){
      fprintf(stderr,"load %s -> %d diag=%s logs=%s\n",leaf,ok,diagnostic,logs);assert(0);}
   if(!strcmp(reason,"reason=legacy")){
      /* At the diagnostic line no firmware has been touched. The one later
       * directory walk belongs to the unchanged legacy firmware search. */
      assert(diagnostic_dirs==0&&diagnostic_files==0&&firmware_dirs==1);
      assert(strstr(diagnostic,"inventory=complete")&&!strstr(notices,"RiscPC-class"));
   }else assert(diagnostic_dirs==1&&firmware_dirs==2);
   return ok;
}
static ArcDigest file_digest(const char *p)
{
   FILE *f=fopen(p,"rb");assert(f);assert(fseek(f,0,SEEK_END)==0);long n=ftell(f);assert(n>=0);
   assert(fseek(f,0,SEEK_SET)==0);void *b=malloc(n?(size_t)n:1);assert(b);
   assert(fread(b,1,(size_t)n,f)==(size_t)n&&fclose(f)==0);ArcDigest d=arc_digest(b,(size_t)n);free(b);return d;
}
static void reset_faults(void)
{root_failure=close_failure=read_failure=name_failure=silent_listing=budget_listing=false;listing_mode=0;}
static void require_legacy(const char *leaf,const char *reason,const char *model,const char *os)
{
   assert(prepare(leaf,reason,model,os));
   assert(strstr(logs,"resolved=A3000")&&strstr(logs,"cpu=ARM2")&&strstr(logs,"os=ArcDroid test ROM"));
   assert(!strstr(logs,"resolved=RiscPC")&&!strstr(logs,"resolved=A7000"));
   assert(strstr(logs,"flagged for RiscPC-class machines"));
}
int main(int argc,char **argv)
{
   assert(argc==4);
#if defined(_WIN32)
   library=LoadLibraryA(argv[1]);
#else
   library=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
#endif
   assert(library);
   LOAD(set_environment,"retro_set_environment");LOAD(init_core,"retro_init");LOAD(deinit_core,"retro_deinit");
   LOAD(load_core,"retro_load_game");LOAD(unload_core,"retro_unload_game");
   LOAD(set_catalogue,"arc_libretro_test_rpc_catalogue");LOAD(media_masks,"arc_libretro_test_rpc_media");
   LOAD(content_id,"arc_libretro_test_content_id");LOAD(cmos_path,"arc_libretro_test_cmos_path");
   LOAD(staged_path,"arc_libretro_test_staged_path");
   snprintf(root,sizeof(root),"%s",argv[3]);
#if defined(_WIN32)
   (void)_mkdir(root);
#else
   (void)mkdir(root,0700);
#endif
   assert(canonical(root,save_directory));
   path(system_path,root,"system");assert(vfs_mkdir(system_path)==0||vfs_stat(system_path,NULL));
   char arc[4096];path(arc,system_path,"arcdroid");assert(vfs_mkdir(arc)==0||vfs_stat(arc,NULL));
   path(firmware_path,arc,"firmware");assert(vfs_mkdir(firmware_path)==0||vfs_stat(firmware_path,NULL));
   FILE *romfile=fopen(argv[2],"rb");assert(romfile);uint8_t *rom=malloc(524288);assert(rom);
   assert(fread(rom,1,524288,romfile)==524288&&fclose(romfile)==0);
   write_bytes(firmware_path,"legacy-rom",rom,524288);free(rom);
   uint8_t authored[4][40];ArcDigest digests[4];ArcRpcFirmwareFingerprint table[4];
   char leaf[64];
   for(unsigned i=0;i<4;++i){for(unsigned j=0;j<40;++j)authored[i][j]=(uint8_t)(i*40+j);
      digests[i]=arc_digest(authored[i],32+i);table[i]=(ArcRpcFirmwareFingerprint){(ArcRpcSelectionOs)(i+1),digests[i].size,digests[i].crc32,digests[i].sha1};
      snprintf(leaf,sizeof(leaf),"rpc-%u",i);write_bytes(firmware_path,leaf,authored[i],32+i);}
   write_bytes(firmware_path,"rpc-copy",authored[3],35);write_bytes(firmware_path,"tiny","x",1);
   wrapper=vfs;wrapper.open=test_open;wrapper.read=test_read;wrapper.opendir=test_opendir;wrapper.readdir=test_readdir;wrapper.dirent_get_name=test_dir_name;wrapper.closedir=test_closedir;
   set_environment(environment);init_core();set_catalogue(table,4);
   make_jfd("rpc.jfd",20,12,false,1,1);make_jfd("sa.jfd",16,8,false,1,1);
   make_jfd("first.jfd",4,4,false,1,2);make_jfd("later.jfd",16,8,false,2,2);
   make_jfd("legacy.jfd",0,0,true,1,1);make_jfd("arm3.jfd",1,2,false,1,1);
   make_jfd("unqualified.jfd",16,4,false,1,1);
   make_jfd("impossible.jfd",32,8,false,1,1);make_jfd("unknown.jfd",0,0,false,1,1);
   make_jfd("unknown-cpu.jfd",0,8,false,1,1);make_jfd("unknown-os.jfd",4,0,false,1,1);
   make_jfd("legacy-cpu.jfd",5,8,false,1,1);make_jfd("legacy-os.jfd",4,10,false,1,1);
   write_bytes(root,"split.m3u","unknown-cpu.jfd\nunknown-os.jfd\n",31);
   write_bytes(root,"later-rpc.m3u","unknown.jfd\nrpc.jfd\n",20);
   write_bytes(root,"conflict.m3u","first.jfd\nlater.jfd\n",20);
   uint8_t *raw=calloc(1,327680);assert(raw);write_bytes(root,"raw.adf",raw,327680);free(raw);
   write_bytes(root,"mixed.m3u","raw.adf\nrpc.jfd\nlegacy.jfd\n",27);
   /* ZIP fixtures are written by the test's Python driver. */
   require_legacy("rpc.jfd","reason=selected","model=ARM710","os=3.71");assert(mask_count==1&&masks[0].cpu==20&&masks[0].os==12);
   /* Legacy engine startup globals differ between first load and reload.
    * Compare equivalent warm reloads, without changing those globals. */
   unload_core();require_legacy("rpc.jfd","reason=selected","model=ARM710","os=3.71");
   snprintf(identity,sizeof(identity),"%s",content_id());snprintf(persistent_path,sizeof(persistent_path),"%s",cmos_path());
   char legacy_log[32768],legacy_notice[8192];strcpy(legacy_log,logs);strcpy(legacy_notice,notices);
   char staged[4096];snprintf(staged,sizeof(staged),"%s",staged_path(0));
   ArcDigest staged_before=file_digest(staged);unload_core();
   ArcDigest cmos_before=file_digest(persistent_path);
   for(unsigned order=1;order<=2;++order){listing_mode=order;
      require_legacy("rpc.jfd","reason=selected","model=ARM710","os=3.71");
      assert(!strcmp(identity,content_id())&&!strcmp(persistent_path,cmos_path())&&!strcmp(legacy_log,logs)&&!strcmp(legacy_notice,notices));unload_core();
      ArcDigest cmos_after=file_digest(persistent_path),staged_after=file_digest(staged);
      assert(cmos_before.size==cmos_after.size&&!strcmp(cmos_before.sha1,cmos_after.sha1));
      assert(staged_before.size==staged_after.size&&!strcmp(staged_before.sha1,staged_after.sha1));}
   reset_faults();require_legacy("set.zip","reason=selected","model=ARM710","os=3.71");assert(mask_count==2);unload_core();
   require_legacy("conflict.m3u","reason=conflicting-discs","model=none","os=none");assert(mask_count==2&&masks[1].cpu==16);unload_core();
   require_legacy("conflict.zip","reason=conflicting-discs","model=none","os=none");unload_core();
   require_legacy("mixed.m3u","reason=selected","model=ARM710","os=3.71");assert(mask_count==3&&!masks[0].cpu&&!masks[2].os);unload_core();
   assert(prepare("legacy.jfd","reason=legacy","model=none","os=none")&&!strstr(notices,"RiscPC-class"));unload_core();
   assert(prepare("arm3.jfd","reason=legacy","model=none","os=none"));unload_core();
   assert(prepare("raw.adf","reason=legacy","model=none","os=none"));unload_core();
   assert(prepare("unknown.jfd","reason=legacy","model=none","os=none"));unload_core();
   const char *no_notice[]={"unknown-cpu.jfd","unknown-os.jfd","legacy-cpu.jfd","legacy-os.jfd","split.m3u"};
   for(unsigned i=0;i<sizeof(no_notice)/sizeof(no_notice[0]);++i){
      assert(prepare(no_notice[i],"reason=legacy","model=none","os=none"));unload_core();}
   require_legacy("later-rpc.m3u","reason=selected","model=ARM710","os=3.71");
   assert(mask_count==2&&masks[0].cpu==0&&masks[1].cpu==20);unload_core();
   require_legacy("unqualified.jfd","reason=unqualified-pair","model=none","os=none");unload_core();
   require_legacy("impossible.jfd","reason=unsupported-requirement","model=none","os=none");unload_core();
   /* Catalogue is injectable only in the test core; production tuples unchanged. */
   root_failure=true;require_legacy("rpc.jfd","reason=unreadable-inventory","model=none","os=none");assert(strstr(diagnostic,"incomplete"));unload_core();
   close_failure=true;require_legacy("rpc.jfd","reason=selected","model=ARM710","os=3.71");assert(strstr(diagnostic,"incomplete"));unload_core();reset_faults();
   read_failure=true; /* Both 3.71 copies fail; 3.70 remains a valid candidate. */
   require_legacy("sa.jfd","reason=selected","model=SA-110","os=3.70");assert(strstr(diagnostic,"incomplete"));unload_core();reset_faults();
   name_failure=true;require_legacy("rpc.jfd","reason=unreadable-inventory","model=none","os=none");unload_core();reset_faults();
   silent_listing=true;require_legacy("rpc.jfd","reason=missing-firmware","model=none","os=none");assert(strstr(diagnostic,"inventory=complete"));unload_core();reset_faults();
   budget_listing=true;require_legacy("rpc.jfd","reason=missing-firmware","model=none","os=none");assert(strstr(diagnostic,"incomplete"));unload_core();reset_faults();
   /* Existing manual legacy options do not become manual RPC choices. */
   machine_option="A3010";os_option="RISC OS 3.11";
   assert(!prepare("rpc.jfd","reason=selected","model=ARM710","os=3.71"));
   assert(strstr(notices,"Missing RISC OS 3.11 firmware"));reset_faults();machine_option="Auto";os_option="Auto";
   machine_option="A3010";
   assert(prepare("rpc.jfd","reason=selected","model=ARM710","os=3.71")&&strstr(logs,"resolved=A3010"));unload_core();machine_option="Auto";
   /* Recognised RPC inventory alone still cannot replace legacy firmware. */
   char only_legacy[4096],only_hidden[4096];path(only_legacy,firmware_path,"legacy-rom");path(only_hidden,root,"legacy-hidden");
   assert(rename(only_legacy,only_hidden)==0);
   assert(!prepare("rpc.jfd","reason=selected","model=ARM710","os=3.71"));
   assert(strstr(notices,"Missing RISC OS 3.11 firmware"));assert(rename(only_hidden,only_legacy)==0);
   set_catalogue(NULL,0);require_legacy("rpc.jfd","reason=missing-firmware","model=none","os=none");unload_core();
   /* Wrong-size/corrupt RPC files never alter the legacy suspicious-size result. */
   for(unsigned i=0;i<4;++i){snprintf(leaf,sizeof(leaf),"rpc-%u",i);write_bytes(firmware_path,leaf,"broken",6);}
   write_bytes(firmware_path,"rpc-copy","broken",6);
   require_legacy("rpc.jfd","reason=missing-firmware","model=none","os=none");
   assert(!strcmp(identity,content_id())&&!strcmp(persistent_path,cmos_path()));unload_core();
   char legacy_file[4096],hidden[4096];path(legacy_file,firmware_path,"legacy-rom");path(hidden,root,"hidden-rom");assert(rename(legacy_file,hidden)==0);
   assert(!prepare("rpc.jfd","reason=missing-firmware","model=none","os=none"));assert(strstr(notices,"Missing RISC OS 3.11 firmware"));
   assert(rename(hidden,legacy_file)==0);
   /* Production-size malformed RPC image retains the exact legacy nearest-size diagnostic. */
   uint8_t *malformed=calloc(1,2097152);assert(malformed);write_bytes(firmware_path,"malformed-rpc",malformed,2097152);free(malformed);
   assert(rename(legacy_file,hidden)==0);
   assert(!prepare("rpc.jfd","reason=missing-firmware","model=none","os=none"));
   assert(strstr(notices,"Unrecognised firmware size 2097152 bytes; nearest expected size is 2097152 bytes."));
   assert(rename(hidden,legacy_file)==0);
   deinit_core();vfs_assert_no_open_handles();
#if defined(_WIN32)
   FreeLibrary(library);
#else
   dlclose(library);
#endif
   puts("R3a ordinary load: loose/ZIP/playlist, later conflicts, raw/legacy, inventory faults/bounds, options and legacy identity/diagnostics passed");return 0;
}
