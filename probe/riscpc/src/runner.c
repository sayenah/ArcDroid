/* ArcDroid headless libretro runner, GPL-2.0-only. No guest state access. */
#include "libretro.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#define LIB HANDLE
#define OPEN(p) LoadLibraryA(p)
#define SYM(h,s) GetProcAddress(h,s)
#else
#include <dlfcn.h>
#define LIB void *
#define OPEN(p) dlopen(p,RTLD_NOW|RTLD_LOCAL)
#define SYM(h,s) dlsym(h,s)
#endif
static char *vars[64];static unsigned var_count;
static int failed;
static double fps=60;static unsigned frame,w,h;
static uint32_t *shot;
static uint64_t limit_ns,emu_ns;
static FILE *wave,*audio_stats;
static uint64_t wave_pairs;
static void le(uint8_t *p,uint32_t n) {p[0]=n;p[1]=n>>8;p[2]=n>>16;p[3]=n>>24;}
static void wave_header(void) {
 uint8_t header[44]={0};memcpy(header,"RIFF",4);le(header+4,(uint32_t)wave_pairs*4+36);
 memcpy(header+8,"WAVEfmt ",8);le(header+16,16);header[20]=1;header[22]=2;
 le(header+24,48000);le(header+28,48000*4);header[32]=4;header[34]=16;
 memcpy(header+36,"data",4);le(header+40,(uint32_t)wave_pairs*4);
 if(fwrite(header,1,sizeof(header),wave)!=sizeof(header))failed=1;
}
static uint64_t clock_ns(void) {
#ifdef _WIN32
 LARGE_INTEGER c,f;QueryPerformanceCounter(&c);QueryPerformanceFrequency(&f);return (uint64_t)(c.QuadPart/f.QuadPart)*1000000000+(uint64_t)(c.QuadPart%f.QuadPart)*1000000000/f.QuadPart;
#else
 struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);return (uint64_t)ts.tv_sec*1000000000+ts.tv_nsec;
#endif
}
static bool environment(unsigned cmd,void *data) {
 switch(cmd) {
 case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:return *(enum retro_pixel_format *)data==RETRO_PIXEL_FORMAT_XRGB8888;
 case RETRO_ENVIRONMENT_GET_VARIABLE: {
  struct retro_variable *v=data;for(unsigned i=0;i<var_count;i++) {size_t n=strlen(v->key);if(!strncmp(vars[i],v->key,n)&&vars[i][n]=='=') {v->value=vars[i]+n+1;return true;}}
  return false;
 }
 case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:*(bool *)data=false;return true;
 case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:fps=((struct retro_system_av_info *)data)->timing.fps;return true;
 case RETRO_ENVIRONMENT_SET_MESSAGE:fprintf(stderr,"Probe reported failure\n");failed=1;return true;
 case RETRO_ENVIRONMENT_SHUTDOWN:failed=1;return true;
 case RETRO_ENVIRONMENT_SET_VARIABLES:case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK:return true;
 default:return false;
 }
}
static void video(const void *p,unsigned x,unsigned y,size_t pitch) {
 if(!p||!x||!y||x>2048||y>2048) {failed=1;return;}
 shot=realloc(shot,(size_t)x*y*4);if(!shot)exit(2);w=x;h=y;
 for(unsigned r=0;r<y;r++)memcpy(shot+(size_t)r*x,(const uint8_t *)p+(size_t)r*pitch,(size_t)x*4);
}
static size_t audio(const int16_t *p,size_t n) {
 if(!wave)return n;
 if(n>16384||wave_pairs+n>(UINT32_MAX-36)/4) {failed=1;return 0;}
 uint8_t bytes[16384*4];unsigned nonzero=0,clipped=0,peak=0;
 for(size_t i=0;i<n;i++) {
  if(p[i*2]||p[i*2+1])nonzero++;
  if(p[i*2]==INT16_MIN||p[i*2]==INT16_MAX||p[i*2+1]==INT16_MIN||p[i*2+1]==INT16_MAX)clipped++;
  for(unsigned ch=0;ch<2;ch++) {int s=p[i*2+ch];unsigned a=s<0?(unsigned)-s:(unsigned)s;if(a>peak)peak=a;uint16_t v=(uint16_t)s;bytes[i*4+ch*2]=v;bytes[i*4+ch*2+1]=v>>8;}
 }
 if(fwrite(bytes,4,n,wave)!=n||fprintf(audio_stats,"%u,%zu,%u,%u,%u\n",frame,n,nonzero,peak,clipped)<0) {failed=1;return 0;}
 wave_pairs+=n;return n;
}
static void poll(void) {}
static int16_t input(unsigned a,unsigned b,unsigned c,unsigned d) {(void)a;(void)b;(void)c;(void)d;return 0;}
/* PNG: project-authored stored-DEFLATE encoder, RGB, no external dependency. */
static uint32_t crc(const uint8_t *p,size_t n) {uint32_t c=~0u;for(size_t i=0;i<n;i++){c^=p[i];for(unsigned b=0;b<8;b++)c=(c>>1)^(0xedb88320u&-(int)(c&1));}return ~c;}
static void be(uint8_t *p,uint32_t n) {p[0]=n>>24;p[1]=n>>16;p[2]=n>>8;p[3]=n;}
static void chunk(FILE *f,const char *tag,const uint8_t *p,size_t n) {uint8_t b[4];be(b,(uint32_t)n);fwrite(b,1,4,f);fwrite(tag,1,4,f);fwrite(p,1,n,f);uint8_t *tmp=malloc(n+4);memcpy(tmp,tag,4);if(n)memcpy(tmp+4,p,n);be(b,crc(tmp,n+4));free(tmp);fwrite(b,1,4,f);}
static void png(const char *name) {
 if(!shot) {failed=1;return;}size_t n=(size_t)h*(w*3+1),at=0;uint8_t *raw=malloc(n);
 for(unsigned y=0;y<h;y++) {raw[at++]=0;for(unsigned x=0;x<w;x++){uint32_t v=shot[(size_t)y*w+x];raw[at++]=v>>16;raw[at++]=v>>8;raw[at++]=v;}}
 size_t cap=n+n/65535*5+32;uint8_t *z=malloc(cap);size_t za=0;z[za++]=0x78;z[za++]=0x01;
 for(size_t a=0;a<n;) {size_t len=n-a;if(len>65535)len=65535;z[za++]=a+len==n?1:0;z[za++]=len;z[za++]=len>>8;z[za++]=~len;z[za++]=(~len)>>8;memcpy(z+za,raw+a,len);za+=len;a+=len;}
 uint32_t a=1,b=0;for(size_t i=0;i<n;i++){a=(a+raw[i])%65521;b=(b+a)%65521;}be(z+za,b<<16|a);za+=4;
 FILE *f=fopen(name,"wb");if(!f) {failed=1;free(z);free(raw);return;}
 fwrite("\x89PNG\r\n\x1a\n",1,8,f);uint8_t ihdr[13]={0};be(ihdr,w);be(ihdr+4,h);ihdr[8]=8;ihdr[9]=2;chunk(f,"IHDR",ihdr,13);chunk(f,"IDAT",z,za);chunk(f,"IEND",NULL,0);
 if(ferror(f)||fclose(f))failed=1;free(z);free(raw);
}
#define LOAD(ret,name,args) ret (*name)args=(ret (*)args)SYM(lib,#name);if(!name){fprintf(stderr,"Missing libretro symbol: %s\n",#name);return 2;}
int main(int argc,char **argv) {
 if(argc<4) {fprintf(stderr,"usage: probe_runner CORE EMULATED_SECONDS unthrottled|realtime [key=value ...] [--png=FILE] [--shot-every=N] [--wav=FILE]\n");return 2;}
 char *end;double sec=strtod(argv[2],&end);if(*end||sec<=0||sec>3600) return 2;limit_ns=(uint64_t)(sec*1e9);
 int realtime=!strcmp(argv[3],"realtime"),staggered=!strcmp(argv[3],"staggered");if(!realtime&&!staggered&&strcmp(argv[3],"unthrottled"))return 2;
 const char *png_name=NULL,*wave_name=NULL;unsigned shot_every=0;
 for(int i=4;i<argc;i++) {if(!strncmp(argv[i],"--png=",6))png_name=argv[i]+6;else if(!strncmp(argv[i],"--wav=",6))wave_name=argv[i]+6;else if(!strncmp(argv[i],"--shot-every=",13))shot_every=(unsigned)atoi(argv[i]+13);else if(var_count<64)vars[var_count++]=argv[i];else return 2;}
 if(wave_name) {
  char stats_name[1200];if(snprintf(stats_name,sizeof(stats_name),"%s.csv",wave_name)>=(int)sizeof(stats_name))return 2;
  wave=fopen(wave_name,"wb");audio_stats=fopen(stats_name,"wb");
  if(!wave||!audio_stats) {if(wave)fclose(wave);if(audio_stats)fclose(audio_stats);return 2;}
  wave_header();if(fprintf(audio_stats,"frame,audio_frames,nonzero_pairs,peak,clipped_pairs\n")<0||failed)return 2;
 }
 LIB lib=OPEN(argv[1]);if(!lib) {fprintf(stderr,"Core load failed\n");return 2;}
 LOAD(void,retro_set_environment,(retro_environment_t));LOAD(void,retro_set_video_refresh,(retro_video_refresh_t));LOAD(void,retro_set_audio_sample_batch,(retro_audio_sample_batch_t));LOAD(void,retro_set_input_poll,(retro_input_poll_t));LOAD(void,retro_set_input_state,(retro_input_state_t));LOAD(void,retro_init,(void));LOAD(bool,retro_load_game,(const struct retro_game_info *));LOAD(void,retro_run,(void));LOAD(void,retro_unload_game,(void));LOAD(void,retro_deinit,(void));LOAD(void,retro_get_system_av_info,(struct retro_system_av_info *));
 retro_set_environment(environment);retro_set_video_refresh(video);retro_set_audio_sample_batch(audio);retro_set_input_poll(poll);retro_set_input_state(input);retro_init();
 if(!retro_load_game(NULL)) {fprintf(stderr,"Session load failed\n");return 1;}
 struct retro_system_av_info av;retro_get_system_av_info(&av);fps=av.timing.fps;
 uint64_t origin=clock_ns();
 while(emu_ns<limit_ns&&!failed) {
  double before=fps;retro_run();if(failed)break;
  emu_ns+=(uint64_t)(1e9/before);frame++;
  if(png_name&&shot_every&&frame%shot_every==0) {char p[1200];snprintf(p,sizeof(p),"%s.%06u.png",png_name,frame);png(p);}
  if(staggered && frame%5==0) {
#ifdef _WIN32
 Sleep(1);
#else
 struct timespec delay={0,1000000};nanosleep(&delay,NULL);
#endif
 }
  if(realtime) {
   uint64_t now=clock_ns(),deadline=origin+emu_ns;
   if(now<deadline) {
#ifdef _WIN32
    Sleep((DWORD)((deadline-now)/1000000));
#else
    uint64_t delta=deadline-now;struct timespec ts={(time_t)(delta/1000000000),(long)(delta%1000000000)};nanosleep(&ts,NULL);
#endif
   }
  }
 }
 if(png_name)png(png_name);
 retro_unload_game();retro_deinit();free(shot);
 if(wave) {
  if(fseek(wave,0,SEEK_SET))failed=1;else wave_header();
  if(ferror(wave)||ferror(audio_stats))failed=1;
  if(fclose(wave))failed=1;if(fclose(audio_stats))failed=1;
 }
 fprintf(stderr,"runner status=%s frames=%u requested_ns=%llu advanced_ns=%llu wall_ns=%llu\n",failed?"FAIL":"COMPLETE",frame,(unsigned long long)limit_ns,(unsigned long long)emu_ns,(unsigned long long)(clock_ns()-origin));
 return failed?1:0;
}
