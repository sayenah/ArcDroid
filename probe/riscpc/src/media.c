/* ArcDroid session media and typed RPCEmu FDC bridge. GPL-2.0-only.
 * File bytes are read at load only; floppy writes replace owned RAM records.
 * All data presentation and admission policy remains in ArcDroid's reader. */
#include "probe.h"
#include "media.h"
#include <ctype.h>
#include "presentation.h"
#include "disc.h"
#include "fdc.h"
#include "arcdroid_core.h"
#include <inttypes.h>
#define MAX_DISCS 16
static ArcJfdImage images[MAX_DISCS];
static unsigned image_count,image_index,selected;
static int ejected=1,tracks[2],empty_seeking;
static unsigned empty_polls;
static int active_drive,active_density,active_head;
static ProbeDiscRequest request;
static ArcJfdImage *test_image;
unsigned probe_disc_period_us=32;
int probe_disc_changed[2],probe_requested_fm;
uint64_t probe_media_reads,probe_media_writes,probe_media_refusals;
static unsigned diagnostic_requests;
PresentDriveOps *probe_present_drives[2];
void *probe_present_context;
int probe_present_writeprot[2];
disc_funcs *drive_funcs[2]; /* Upstream loader ABI retained, never attached. */
static void bridge_data(uint8_t x,void *p) {(void)p;fdc_data(x);}
static void bridge_finish(void *p) {(void)p;fdc_finishread();}
static void bridge_nf(void *p) {(void)p;fdc_notfound();}
static void bridge_dc(void *p) {(void)p;fdc_datacrcerror();}
static void bridge_hc(void *p) {(void)p;fdc_headercrcerror();}
static void bridge_wp(void *p) {(void)p;probe_media_refusals++;fdc_writeprotect();}
static int bridge_get(int last,void *p) {(void)p;return fdc_getdata(last);}
static void bridge_id(uint8_t c,uint8_t h,uint8_t r,uint8_t n,uint8_t a,uint8_t b,void *p) {(void)a;(void)b;(void)p;fdc_sectorid(c,h,r,n);}
static void bridge_index(void *p) {(void)p;if(selected==0&&motoron)fdc_indexpulse();}
static void bridge_lost(void *p) {(void)p;fdc_probe_lostdata();}
static PresentFdcOps fdc_ops={.data=bridge_data,.finishread=bridge_finish,.notfound=bridge_nf,.datacrcerror=bridge_dc,.headercrcerror=bridge_hc,.writeprotect=bridge_wp,.getdata=bridge_get,.sectorid=bridge_id,.indexpulse=bridge_index,.lostdata=bridge_lost};
PresentFdcOps *probe_present_fdc=&fdc_ops;
unsigned disc_poll_period_us(void) {return probe_disc_period_us;}
void probe_disc_request(ProbeDiscRequest r) {
 request=r;probe_requested_fm=r.fm;
 jfd_set_requested_ch(r.cylinder,r.head);
 jfd_set_requested_size(r.size_code<8?(int)(128u<<r.size_code):-2);
}
static ArcJfdImage *mounted(void) {return test_image?test_image:image_count?&images[image_index]:NULL;}
static bool writable_record(ArcJfdImage *im,size_t key,const uint8_t *bytes) {
 ArcJfdOverlay *o=im->overlay;
 if(!o||!o->writable||key>=im->sector_count||!o->eligible[key]||arc_jfd_write_veto_reason(im,key,bytes))return false;
 uint8_t *copy=malloc(im->sectors[key].size);if(!copy)fatal("Floppy overlay allocation");
 memcpy(copy,bytes,im->sectors[key].size);free(o->data[key]);o->data[key]=copy;
 o->generation++;probe_media_writes++;arc_jfd_map_refresh(im);return true;
}
static bool writable_created(ArcJfdImage *im,unsigned t,unsigned id,const uint8_t *bytes) {
 ArcJfdOverlay *o=im->overlay;
 if(!o||!o->writable||t>=ARC_JFD_PDD5_TRACKS||id>=5||!(o->creation_mask[t]&(1u<<id)))return false;
 uint8_t *copy=malloc(1024);if(!copy)fatal("Created floppy overlay allocation");
 memcpy(copy,bytes,1024);free(o->created[t][id]);o->created[t][id]=copy;
 o->generation++;probe_media_writes++;arc_jfd_map_refresh(im);return true;
}
static void prepare(ArcJfdImage *im,unsigned index) {
 ArcJfdOverlay *o=calloc(1,sizeof(*o));if(!o)fatal("Floppy policy allocation");im->overlay=o;
 o->image_number=index+1;o->visible=true;
 o->data=calloc(im->sector_count?im->sector_count:1,sizeof(*o->data));o->eligible=calloc(im->sector_count?im->sector_count:1,1);
 if(!o->data||!o->eligible)fatal("Floppy records allocation");
 for(size_t i=0;i<im->sector_count;i++)o->eligible[i]=arc_jfd_sector_write_eligible(im,i);
 o->reason=!im->extended?"legacy-header":(im->flags&1)?"wp-flag":!(im->flags&2)?"no-flag":"layout-unsupported";
 if(im->extended&&(im->flags&3)==2&&arc_jfd_plan_creation(im,o)) {o->writable=true;o->map_enabled=true;o->reason="writable";o->commit=writable_record;o->commit_created=writable_created;arc_jfd_map_prepare(im);}
 arc_jfd_prepare_identification(im,true);
 arc_jfd_file_data_prepare(im,true,false,o->reason);
}
static void media_failure(FILE *list,const char *reason) {
 if(list)fclose(list);fatal(reason);
}
static void add_media(const char *name,FILE *custody,FILE *list) {
 if(image_count==MAX_DISCS)media_failure(list,"Media count bound");
 FILE *in=fopen(name,"rb");if(!in)media_failure(list,"Media open failed");
 uint8_t *b=malloc(ARC_JFD_MAX_COMPRESSED);
 if(!b) {fclose(in);media_failure(list,"Media read allocation");}
 size_t len=fread(b,1,ARC_JFD_MAX_COMPRESSED,in);int tail=fgetc(in),bad=ferror(in),closed=fclose(in);
 if(bad||closed||tail!=EOF) {free(b);media_failure(list,"Media read bound/failure");}
 char sha[41],error[128];arc_sha1_hex(b,len,sha);
 ArcJfdImage *im=&images[image_count++];
 int parsed=arc_jfd_parse(b,len,im,error,sizeof(error));free(b);
 if(!parsed)media_failure(list,"Invalid JFD descriptors");
 /* Decoded private names remain internal and are never logged. */
 prepare(im,image_count-1);
 if(custody&&fprintf(custody,"media,%u,%s,%u,%zu,%04x,%u,%s\n",image_count-1,sha,im->track_count,im->sector_count,im->identification_mask,im->file_data.presented,im->overlay->reason)<0)media_failure(list,"Media custody write");
}
static void open_list(const char *list,FILE *custody) {
 FILE *f=fopen(list,"rb");if(!f)fatal("Media list open failed");char name[1200];
 const char *last=strrchr(list,'/'),*back=strrchr(list,'\\');if(back&&(!last||back>last))last=back;
 while(fgets(name,sizeof(name),f)) {
  size_t n=strlen(name);if(n&&name[n-1]!='\n'&&!feof(f))media_failure(f,"Media path bound");
  while(n&&(name[n-1]=='\n'||name[n-1]=='\r'))name[--n]=0;
  if(!n||name[0]=='#')continue;
  char path[2400];
  if(name[0]=='/'||name[0]=='\\'||(n>2&&name[1]==':')) {
   memcpy(path,name,n+1);
  } else if(last) {
   if(snprintf(path,sizeof(path),"%.*s/%s",(int)(last-list),list,name)>=(int)sizeof(path))media_failure(f,"Media path bound");
  } else memcpy(path,name,n+1);
  add_media(path,custody,f);
 }
 int bad=ferror(f),closed=fclose(f);if(bad||closed||!image_count)fatal("Media list read/empty failure");
}
void probe_media_open(const char *list,FILE *custody) {
 probe_media_close();if(!strcmp(list,"none"))return;
 open_list(list,custody);image_index=0;ejected=0;probe_media_reset();
}
void probe_media_open_content(const char *path,FILE *custody) {
 probe_media_close();const char *suffix=strrchr(path,'.');char ext[5]={0};
 if(!suffix||strlen(suffix)!=4)fatal("Unsupported content extension");
 for(unsigned i=0;i<4;i++)ext[i]=(char)tolower((unsigned char)suffix[i]);
 if(!strcmp(ext,".jfd"))add_media(path,custody,NULL);
 else if(!strcmp(ext,".m3u"))open_list(path,custody);
 else fatal("Unsupported content extension");
 image_index=0;ejected=0;probe_media_reset();
}
void disc_stop(int d) {if(d==0&&probe_present_drives[0])probe_present_drives[0]->stop();if(d==1)probe_helper_stop();empty_seeking=0;}
void probe_media_reset(void) {
 disc_stop(0);disc_stop(1);memset(tracks,0,sizeof(tracks));selected=0;empty_polls=0;
 memset(&request,0,sizeof(request));probe_requested_fm=0;probe_disc_period_us=32;
 jfd_init();probe_present_drives[0]=probe_present_drives[1]=NULL;
 if(!ejected&&mounted()&&!jfd_load(0,mounted()))fatal("JFD mount failed");
 probe_disc_changed[0]=probe_disc_changed[1]=1;
 probe_helper_init();
}
void probe_media_close(void) {
 disc_stop(0);disc_stop(1);jfd_init();probe_present_drives[0]=probe_present_drives[1]=NULL;
 for(unsigned i=0;i<image_count;i++)arc_jfd_free(&images[i]);
 image_count=image_index=0;ejected=1;test_image=NULL;
 diagnostic_requests=0;
}
unsigned probe_media_count(void) {return image_count;}
unsigned probe_media_index(void) {return image_index;}
bool probe_media_ejected(void) {return ejected!=0;}
bool probe_media_eject(bool value) {
 ejected=value;disc_stop(0);disc_stop(1);fdc_probe_media_cancel();
 jfd_init();probe_present_drives[0]=NULL;
 if(!value&&mounted()) {if(!jfd_load(0,mounted()))return false;probe_present_drives[0]->seek(0,tracks[0]);}
 probe_disc_changed[0]=1;return true;
}
bool probe_media_select(unsigned i) {if(!ejected||i>=image_count)return false;image_index=i;return true;}
void probe_media_swap(unsigned i) {if(i>=image_count)fatal("Script disc index bound");probe_media_eject(true);if(!probe_media_select(i)||!probe_media_eject(false))fatal("Script swap failed");}
void probe_media_test_attach(ArcJfdImage *im) {probe_media_close();test_image=im;if(!im->overlay)prepare(im,0);ejected=0;probe_media_reset();}
void disc_set_drivesel(int d) {if(d>=0&&d<2)selected=(unsigned)d;}
int disc_get_current_track(int d) {return d>=0&&d<2?tracks[d]:0;}
void disc_seek(int d,int t) {
 if(d<0||d>=2)return;tracks[d]=t;disc_stop(d);probe_disc_changed[d]=0;
 if(probe_present_drives[d])probe_present_drives[d]->seek(d,t);
}
static bool ready(int d) {return d==0&&!ejected&&probe_present_drives[0];}
static void empty_search(void) {empty_seeking=1;empty_polls=0;}
/* Passive numeric requests distinguish an old-ROM dialog awaiting input from
 * an unchanged screen during copying. No payload, label or guest state read. */
static void log_request(const char *kind,int d,int r,int c,int h,int den) {
 if(!probe_boot_log||d!=0)return;
 if(diagnostic_requests++==2000000)fatal("Media diagnostic bound exceeded");
 if(fprintf(probe_boot_log,
    "%"PRIu64",%s,index=%u drive=%d id=%d cylinder=%d head=%d density=%d size_code=%u\n",
    probe_now,kind,image_index,d,r,c,h,den,(unsigned)request.size_code)<0)
  fatal("Media diagnostic write failed");
}
void disc_readsector(int d,int r,int c,int h,int den) {
 probe_media_reads++;active_drive=d;active_density=den;active_head=h;
 log_request("media-read",d,r,c,h,den);
 if(d==1&&probe_helper_enabled)probe_helper_request(r,c,h,tracks[1],den,request);
 else if(ready(d))probe_present_drives[0]->readsector(d,r,c,h,den);else empty_search();
}
void disc_writesector(int d,int r,int c,int h,int den) {
 active_drive=d;active_density=den;active_head=h;
 if(ready(d))probe_present_drives[0]->writesector(d,r,c,h,den,request.size_code<8?(int)(128u<<request.size_code):-2,request.deleted);else bridge_wp(NULL);
}
void disc_readaddress(int d,int c,int h,int den) {
 active_drive=d;active_density=den;active_head=h;
 log_request("media-id",d,-1,c,h,den);
 if(d==1&&probe_helper_enabled)probe_helper_request(-1,c,h,tracks[1],den,request);
 else if(ready(d))probe_present_drives[0]->readaddress(d,c,h,den);else empty_search();
}
void disc_format(int d,int c,int h,int den) {if(ready(d))probe_present_drives[0]->format(d,c,h,den);else bridge_wp(NULL);}
void disc_poll(void) {
 (void)selected;
 if(!ejected&&probe_present_drives[0])probe_present_drives[0]->poll();
 if(probe_helper_enabled)probe_helper_poll(selected==1&&motoron);
 if(empty_seeking&&(uint64_t)++empty_polls*probe_disc_period_us>=400000) {empty_seeking=0;fdc_notfound();}
}

void probe_present_latch_id(uint8_t c,uint8_t h,uint8_t r,uint8_t n) {fdc_probe_id_latch(c,h,r,n);}
unsigned probe_disc_notfound_status(void) {
 if(active_drive!=0||!ready(0))return 5;
 ArcJfdImage *im=mounted();unsigned ts=(unsigned)tracks[0]*2+((unsigned)active_head&1u);
 unsigned density=request.fm?1u:active_density==2?4u:active_density==3?8u:2u;
 bool ids=false;if(ts<im->track_count)for(unsigned i=0;i<im->tracks[ts].sector_count+ARC_JFD_MAX_STANDARD_IDS;i++) {
  ArcJfdView v;if(arc_jfd_effective_sector(im,ts,i,false,&v)&&v.sector.density==density)ids=true;
 }
 return ids?4u|((request.cylinder!=(unsigned)tracks[0]?0x10u:0u)<<8):5u;
}
