/* ArcDroid authored frontend contract checks. GPL-2.0-only. */
#include "probe.h"
#include "frontend.h"
#include <errno.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define MAKE_DIR(p) _mkdir(p)
#else
#define MAKE_DIR(p) mkdir(p,0700)
#endif
static const char *work="trial-a",*system_root="frontend-test-root",*firmware_value="ROM371";
static unsigned checks;
static void expect(int success) {checks++;if(!success) {fprintf(stderr,"Frontend check %u failed\n",checks);exit(1);}}
static bool environment(unsigned command,void *p) {
 if(command==RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY) {*(const char **)p=system_root;return system_root!=NULL;}
 if(command==RETRO_ENVIRONMENT_GET_VARIABLE) {
  struct retro_variable *v=p;v->value=!strcmp(v->key,"rpcprobe_workload")?work:!strcmp(v->key,"rpcprobe_firmware")?firmware_value:"none";return true;
 }
 return false;
}
static void directory(const char *name) {if(MAKE_DIR(name)&&errno!=EEXIST)exit(2);}
static void write_hash(const char *text) {
 FILE *f=fopen("frontend-test-root/arcdroid/rpcprobe/trial-a/cmos.sha1","wb");if(!f)exit(2);
 if(fputs(text,f)==EOF||fclose(f))exit(2);
}
static int resolve(void) {probe_can_escape=1;if(setjmp(probe_escape))return 0;probe_frontend_paths(environment);return 1;}
int main(void) {
 directory("frontend-test-root");directory("frontend-test-root/arcdroid");
 directory("frontend-test-root/arcdroid/rpcprobe");directory("frontend-test-root/arcdroid/rpcprobe/trial-a");
 write_hash("b376885ac8452b6cbf9ced81b1080bfd570d9b91\n");
 system_root=NULL;expect(!resolve());system_root="frontend-test-root";
 work="../trial-a";expect(!resolve());work="trial-a/child";expect(!resolve());work="";expect(!resolve());work="trial-a";
 write_hash("broken\n");expect(!resolve());write_hash("b376885ac8452b6cbf9ced81b1080bfd570d9b91\n");
 expect(resolve());
 expect(!strcmp(probe_frontend_option("rpcprobe_firmware",firmware_value),"frontend-test-root/arcdroid/firmware/ROM371"));
 expect(!strcmp(probe_frontend_option("rpcprobe_script","none"),"frontend-test-root/arcdroid/rpcprobe/trial-a/script.txt"));
 expect(!strcmp(probe_frontend_option("rpcprobe_cmos_sha1","none"),"b376885ac8452b6cbf9ced81b1080bfd570d9b91"));
 expect(!strcmp(probe_frontend_option("rpcprobe_script","override"),"override"));
 expect(!strcmp(probe_frontend_option("rpcprobe_boot","automatic"),"standard"));
 char previous[1200];strcpy(previous,probe_frontend_option("rpcprobe_output","none"));expect(resolve());
 expect(strcmp(previous,probe_frontend_option("rpcprobe_output","none"))!=0);
 work="none";firmware_value="external-file";expect(resolve());
 expect(!strcmp(probe_frontend_option("rpcprobe_firmware",firmware_value),firmware_value));
 expect(!strcmp(probe_frontend_option("rpcprobe_script","none"),"none"));
 expect(!strcmp(probe_frontend_option("rpcprobe_boot","automatic"),"standard"));
 probe_can_escape=0;printf("frontend PASS: %u checks; system_root firmware, workload sidecars, exclusive sessions, explicit overrides, traversal and malformed digest refusal\n",checks);return 0;
}
