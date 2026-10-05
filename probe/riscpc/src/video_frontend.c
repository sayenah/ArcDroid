/* Authored guest video/frontend notification checks. GPL-2.0-only. */
#include "probe.h"
#include "libretro.h"
#include <math.h>
#include <string.h>
static unsigned at,av_updates,geometry_updates,videos;
static int failed;
static double last_fps;
static bool environment(unsigned command,void *data) {
 if(command==RETRO_ENVIRONMENT_GET_VARIABLE) {struct retro_variable *v=data;v->value=!strcmp(v->key,"rpcprobe_authored")?"enabled":!strcmp(v->key,"rpcprobe_model")?"SA110":!strcmp(v->key,"rpcprobe_rate")?"100":"none";return true;}
 if(command==RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO) {av_updates++;last_fps=((struct retro_system_av_info *)data)->timing.fps;return true;}
 if(command==RETRO_ENVIRONMENT_SET_GEOMETRY) {geometry_updates++;return true;}
 if(command==RETRO_ENVIRONMENT_SET_MESSAGE||command==RETRO_ENVIRONMENT_SHUTDOWN)failed=1;
 return true;
}
static void video(const void *p,unsigned w,unsigned h,size_t pitch) {if(!p||!w||!h||pitch<w*4)failed=1;videos++;}
static size_t audio(const int16_t *p,size_t n) {(void)p;return n;}
static void poll(void) {}
static int16_t input(unsigned a,unsigned b,unsigned c,unsigned d) {(void)a;(void)b;(void)c;(void)d;return 0;}
static void write_guest(uint32_t address,uint32_t value) {
 uint32_t words[]={0xe59f0008,0xe59f1008,0xe5801000,0xea000001,address,value};memcpy(rom+at,words,sizeof(words));at+=6;
}
static void wait_guest(uint32_t count) {
 uint32_t words[]={0xe59f1000,0xea000000,count,0xe2511001,0x1afffffd};memcpy(rom+at,words,sizeof(words));at+=5;
}
static void run(unsigned n) {for(unsigned i=0;i<n&&!failed;i++)retro_run();}
int main(void) {
 retro_set_environment(environment);retro_set_video_refresh(video);retro_set_audio_sample_batch(audio);retro_set_input_poll(poll);retro_set_input_state(input);retro_init();
 if(!retro_load_game(NULL))return 1;
 /* Synthetic ROM constructed before its first instruction. All timing/mode
  * changes execute ordinary ARM stores; no host pokes of video registers. */
 memset(rom,0,ROMSIZE);at=0;
 uint32_t values[]={0xe0000060,0xd0000000,0x80000318,0x900001f2,0x83000000,0x84000140,0x93000000,0x94000100};
 for(unsigned i=0;i<sizeof(values)/sizeof(values[0]);i++)write_guest(0x03400000,values[i]);
 write_guest(0x030001d8,0x02000000);write_guest(0x030001dc,0x02000000);write_guest(0x030001d4,0x021ff800);write_guest(0x030001e0,0x20);
 wait_guest(100000000); /* two instructions/count: two emulated seconds */
 write_guest(0x03400000,0x90000256); /*600 rasters ->50Hz, N-2*/
 write_guest(0x03400000,0x84000280); /*640 wide, same max geometry*/
 wait_guest(50000000); /* one more emulated second */
 write_guest(0x03400000,0x84000320); /*800 wide: geometry only*/
 rom[at]=0xeafffffe;
 run(3);av_updates=geometry_updates=0;run(80);
 printf("video frontend stable frames=%u av=%u geometry=%u\n",videos,av_updates,geometry_updates);
 if(failed||av_updates||geometry_updates)return 1;
 run(60);
 printf("video frontend timing av=%u geometry=%u fps=%.6f\n",av_updates,geometry_updates,last_fps);
 /* A mid-frame VCR change can produce one mixed-duration frame followed
  * by the stable50Hz frame; never repeated calls for nanosecond rounding. */
 if(failed||av_updates<1||av_updates>2||fabs(last_fps-50)>1e-4)return 1;
 av_updates=geometry_updates=0;run(60);
 struct retro_system_av_info av;retro_get_system_av_info(&av);
 printf("video frontend geometry av=%u geometry=%u width=%u fps=%.6f\n",av_updates,geometry_updates,av.geometry.base_width,av.timing.fps);
 if(failed||av_updates||geometry_updates!=1||av.geometry.base_width!=800||fabs(av.timing.fps-50)>1e-4)return 1;
 retro_unload_game();retro_deinit();puts("video frontend PASS");return 0;
}
