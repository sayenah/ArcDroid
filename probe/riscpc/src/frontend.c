/* ArcDroid trial frontend paths. GPL-2.0-only.
 * Paths and exclusive session creation do not affect guest virtual time. */
#include "probe.h"
#include "frontend.h"
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define MAKE_DIR(p) _mkdir(p)
#else
#define MAKE_DIR(p) mkdir(p,0700)
#endif
static char directory[1200],firmware[1200],output[1200];
static struct {const char *key,*leaf;char path[1200];} files[]={
 {"rpcprobe_script","script.txt",{0}},
 {"rpcprobe_cmos_baseline","cmos.bin",{0}},
 {"rpcprobe_cmos_sha1","cmos.sha1",{0}},
 {"rpcprobe_ide_baseline","baseline.hdf",{0}},
 {"rpcprobe_ide_baseline_sha1","baseline.sha1",{0}}
};
static const char *get(retro_environment_t env,const char *key) {
 struct retro_variable v={key,NULL};return env&&env(RETRO_ENVIRONMENT_GET_VARIABLE,&v)&&v.value?v.value:"none";
}
static void join(char *out,size_t size,const char *parent,const char *leaf) {
 if(snprintf(out,size,"%s/%s",parent,leaf)>=(int)size)fatal("Frontend path bound");
}
static void digest(char *out,const char *path) {
 FILE *f=fopen(path,"rb");if(!f)fatal("Workload digest missing");
 char line[64];int bad=!fgets(line,sizeof(line),f);
 if(!bad) {
  size_t n=strcspn(line,"\r\n");bad=n!=40;
  for(size_t i=0;i<n&&!bad;i++)if(!isxdigit((unsigned char)line[i]))bad=1;
  if(!bad) {line[n]=0;memcpy(out,line,n+1);}
 }
 int read_bad=ferror(f),closed=fclose(f);if(read_bad||closed)bad=1;if(bad)fatal("Workload SHA-1 sidecar invalid");
}
void probe_frontend_paths(retro_environment_t env) {
 directory[0]=firmware[0]=output[0]=0;
 for(unsigned i=0;i<sizeof(files)/sizeof(files[0]);i++)files[i].path[0]=0;
 const char *work=get(env,"rpcprobe_workload"),*rom=get(env,"rpcprobe_firmware");
 int builtin=!strcmp(rom,"ROM350")||!strcmp(rom,"ROM360")||!strcmp(rom,"ROM370")||!strcmp(rom,"ROM371");
 int selected=strcmp(work,"none")!=0;
 if(!builtin&&!selected)return;
 const char *system=NULL;
 if(!env||!env(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY,&system)||!system||!system[0])fatal("Frontend system directory missing");
 char root[1200];join(root,sizeof(root),system,"arcdroid");
 if(builtin) {char folder[1200];join(folder,sizeof(folder),root,"firmware");join(firmware,sizeof(firmware),folder,rom);}
 if(!selected)return;
 if(!work[0]||strlen(work)>64)fatal("Workload name bound");
 for(const char *p=work;*p;p++)if(!isalnum((unsigned char)*p)&&*p!='-'&&*p!='_')fatal("Invalid workload name");
 char folder[1200];join(folder,sizeof(folder),root,"rpcprobe");join(directory,sizeof(directory),folder,work);
 for(unsigned i=0;i<sizeof(files)/sizeof(files[0]);i++) {
  if(strcmp(get(env,files[i].key),"none"))continue; /* Explicit headless overrides. */
  if(i>=3&&strcmp(get(env,"rpcprobe_ide"),"enabled"))continue;
  join(files[i].path,sizeof(files[i].path),directory,files[i].leaf);
  if(i==2||i==4) {char path[1200];strcpy(path,files[i].path);digest(files[i].path,path);}
 }
 if(strcmp(get(env,"rpcprobe_output"),"none"))return;
 /* Never reopen an earlier scratch session or write to a baseline. */
 for(unsigned i=0;i<1000000;i++) {
  char leaf[32];snprintf(leaf,sizeof(leaf),"run-%06u",i);join(output,sizeof(output),directory,leaf);
  if(!MAKE_DIR(output))return;
  if(errno!=EEXIST)fatal("Workload session directory creation failed");
 }
 fatal("Workload session count bound");
}
const char *probe_frontend_option(const char *key,const char *value) {
 if(!strcmp(key,"rpcprobe_firmware")&&firmware[0])return firmware;
 if(!strcmp(key,"rpcprobe_boot")&&(!strcmp(value,"automatic")||!strcmp(value,"none")))return "standard"; /* R10-1: firmware boots the authored standard IDE environment. */
 if(strcmp(value,"none"))return value;
 if(!strcmp(key,"rpcprobe_output")&&output[0])return output;
 for(unsigned i=0;i<sizeof(files)/sizeof(files[0]);i++)if(!strcmp(key,files[i].key)&&files[i].path[0])return files[i].path;
 return value;
}
