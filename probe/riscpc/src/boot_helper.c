/* ArcDroid-authored immutable boot floppy, GPL-2.0-only.
 * FileCore L formatter follows ArcDroid core/launch_disc.c by marked copy.
 * Only authored !Boot/MDF bytes: no supplied firmware/content is staged here.
 * Ordinary FDC ID/data/DMA path, DD 16x256 sectors, side-sequential FileCore.
 */
#include "probe.h"
#include "media.h"
#include "fdc.h"
#include "boot_assets.h"
int probe_helper_enabled;
static uint8_t bytes[655360];
static unsigned phase,waits,pos,sector,head,cylinder,requested;
static int state;
static void u24(uint8_t *p,unsigned n) {p[0]=n;p[1]=n>>8;p[2]=n>>16;}
static uint8_t check(const uint8_t *p) {
 unsigned c=255;for(unsigned i=255;i-->0;) {if(c>255)c=(c+1)&255;c+=p[i];}return c;
}
static size_t offset(unsigned l) {
 unsigned t=l/16;return (size_t)((t%80*2+t/80)*16+l%16)*256;
}
static void write_file(unsigned l,const char *p,size_t n) {
 for(size_t i=0;i<n;i++)bytes[offset(l+(unsigned)i/256)+i%256]=(uint8_t)p[i];
}
static void entry(uint8_t *p,const char *name,unsigned l,size_t n,unsigned type) {
 memset(p,'\r',10);memcpy(p,name,strlen(name));p[0]|=128;
 uint32_t load=0xfff00000|(type<<8);
 for(unsigned i=0;i<4;i++) {p[10+i]=load>>(8*i);p[18+i]=(uint32_t)n>>(8*i);}
 u24(p+22,l);
}
void probe_helper_init(void) {
 if(!probe_helper_enabled)return;
 if(sizeof(probe_boot_obey)>4097||sizeof(probe_mdf)>60000||sizeof(probe_boot_env)>6000||sizeof(probe_boot_tasks)>6000)fatal("Authored helper asset extent bound");
 unsigned mdf_sector=7+(sizeof(probe_boot_obey)+254)/256;
 unsigned env_sector=mdf_sector+(sizeof(probe_mdf)+254)/256;
 unsigned tasks_sector=env_sector+(sizeof(probe_boot_env)+254)/256;
 memset(bytes,0,sizeof(bytes));unsigned used=tasks_sector+(sizeof(probe_boot_tasks)+254)/256;
 u24(bytes,used);u24(bytes+252,2560);u24(bytes+256,2560-used);
 memcpy(bytes+247,"Acri\r",5);memcpy(bytes+502,"rDod\r",5);
 bytes[507]=0x44;bytes[508]=0x41;bytes[509]=2;bytes[510]=3;
 bytes[255]=check(bytes);bytes[511]=check(bytes+256);
 uint8_t *r=bytes+offset(2);r[0]=1;memcpy(r+1,"Hugo",4);
 entry(r+5,"!Boot",7,sizeof(probe_boot_obey)-1,0xfeb);
 entry(r+31,"Multisync",mdf_sector,sizeof(probe_mdf)-1,0xfed);
 entry(r+57,"BootEnv",env_sector,sizeof(probe_boot_env)-1,0xfff);
 entry(r+83,"BootTasks",tasks_sector,sizeof(probe_boot_tasks)-1,0xfff);
 uint8_t *t=r+1227;t[1]='$';memset(t+2,'\r',9);u24(t+11,2);
 memcpy(t+14,"ArcDroid boot",13);t[47]=1;memcpy(t+48,"Hugo",4);
 write_file(7,probe_boot_obey,sizeof(probe_boot_obey)-1);
 write_file(mdf_sector,probe_mdf,sizeof(probe_mdf)-1);
 write_file(env_sector,probe_boot_env,sizeof(probe_boot_env)-1);
 write_file(tasks_sector,probe_boot_tasks,sizeof(probe_boot_tasks)-1);probe_helper_stop();phase=0;
}
void probe_helper_stop(void) {state=0;pos=waits=0;}
void probe_helper_request(int id,unsigned c,unsigned h,unsigned physical,int density,ProbeDiscRequest r) {
 if(probe_boot_log)fprintf(probe_boot_log,"%llu,helper_request,%d,%u,%u,%u,%d,%u,%u,%u\n",(unsigned long long)probe_now,id,c,h,physical,density,r.cylinder,r.head,r.size_code);
 probe_helper_stop();requested=id<0?16u:(unsigned)id;cylinder=physical;head=h;
 /* Wrong CHRN/density never succeeds. READ ID doesn't carry N or C. */
 if(c>=80||h>1||id>=16||density!=1||r.fm||
    (id>=0&&(c!=physical||r.cylinder!=physical||r.head!=h||r.size_code!=1)))state=3;
 else state=1;
}
void probe_helper_poll(int index_enabled) {
 unsigned old=phase;phase=(phase+probe_disc_period_us)%200000;
 if(phase<old&&index_enabled)fdc_indexpulse();
 if(!state)return;
 if(state==1||state==3) {
  if(++waits*probe_disc_period_us>=400000) {probe_helper_stop();fdc_notfound();return;}
  if(state==3)return;
  unsigned id=phase/12500;
  if((phase%12500)>=probe_disc_period_us||id>=16|| (requested<16&&id!=requested))return;
  sector=id;fdc_probe_id_latch(cylinder,head,sector,1);
  if(requested==16) {probe_helper_stop();fdc_sectorid(cylinder,head,sector,1);return;}
  state=2;pos=0;return;
 }
 if(state==2) {
  fdc_data(bytes[((size_t)cylinder*2+head)*4096+sector*256+pos]);
  if(state!=2)return;
  if(++pos==256) {probe_helper_stop();fdc_finishread();}
 }
}
