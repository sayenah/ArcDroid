/* ArcDroid public libretro lifecycle checks. GPL-2.0-only. Authored input only. */
#include "libretro.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
static int reject_format,external_firmware,failed;
static const char *cmos_path="none",*cmos_hash="none",*factory="disabled";
static unsigned videos;
static uint64_t video_hash,audio_hash,audio_frames;
static uint64_t hash(uint64_t h,const void *p,size_t n) {const uint8_t *b=p;while(n--){h^=*b++;h*=UINT64_C(1099511628211);}return h;}
static bool environment(unsigned cmd,void *p) {
 if(cmd==RETRO_ENVIRONMENT_GET_VARIABLE) {
  struct retro_variable *v=p;
  v->value=!strcmp(v->key,"rpcprobe_model")?"SA110":!strcmp(v->key,"rpcprobe_rate")?"100":!strcmp(v->key,"rpcprobe_authored")?(external_firmware?"disabled":"enabled"):!strcmp(v->key,"rpcprobe_cmos_baseline")?cmos_path:!strcmp(v->key,"rpcprobe_cmos_sha1")?cmos_hash:!strcmp(v->key,"rpcprobe_cmos_factory")?factory:"none";
  return true;
 }
 if(cmd==RETRO_ENVIRONMENT_SET_PIXEL_FORMAT)return !reject_format;
 if(cmd==RETRO_ENVIRONMENT_SET_MESSAGE||cmd==RETRO_ENVIRONMENT_SHUTDOWN) {failed=1;return true;}
 return true;
}
static void video(const void *p,unsigned w,unsigned h,size_t pitch) {
 if(!p||!w||!h||pitch<w*4||w>2048||h>2048) {failed=1;return;}
 for(unsigned y=0;y<h;y++)video_hash=hash(video_hash,(const uint8_t *)p+y*pitch,w*4);
 videos++;
}
static size_t audio(const int16_t *p,size_t n) {audio_hash=hash(audio_hash,p,n*4);audio_frames+=n;return n;}
static void poll(void) {}
static int16_t input(unsigned p,unsigned d,unsigned i,unsigned id) {(void)p;(void)d;(void)i;(void)id;return 0;}
static void clear(void) {video_hash=audio_hash=UINT64_C(1469598103934665603);audio_frames=0;videos=0;failed=0;}
static void run(void) {clear();for(unsigned i=0;i<20&&!failed;i++)retro_run();}
int main(void) {
 retro_set_environment(environment);retro_set_video_refresh(video);retro_set_audio_sample_batch(audio);retro_set_input_poll(poll);retro_set_input_state(input);retro_init();
 reject_format=1;if(retro_load_game(NULL))return 1;reject_format=0;
 external_firmware=1;if(retro_load_game(NULL))return 1;external_firmware=0;
 /* Authored zero bytes test the boundary independently of private firmware. */
 const char *test_path="probe-cmos-lifecycle.bin";uint8_t zeros[256]={0};
 FILE *f=fopen(test_path,"wb");if(!f)return 1;
 if(fwrite(zeros,1,255,f)!=255||fclose(f))return 1;
 cmos_path=test_path;if(retro_load_game(NULL))return 1;
 f=fopen(test_path,"wb");if(!f)return 1;
 if(fwrite(zeros,1,256,f)!=256||fclose(f))return 1;
 cmos_hash="0000000000000000000000000000000000000000";
 if(retro_load_game(NULL))return 1;
 cmos_hash="b376885ac8452b6cbf9ced81b1080bfd570d9b91";
 factory="enabled";if(retro_load_game(NULL))return 1;factory="disabled";
 if(!retro_load_game(NULL))return 1;retro_unload_game();
 if(remove(test_path))return 1;cmos_path=cmos_hash="none";
 if(!retro_load_game(NULL))return 1;run();
 uint64_t vh=video_hash,ah=audio_hash,n=audio_frames;if(failed||videos!=20||n==0)return 1;
 retro_reset();run();if(failed||videos!=20||video_hash!=vh||audio_hash!=ah||audio_frames!=n) {
 fprintf(stderr,"reset differs: failure=%d videos=%u video=%016llx/%016llx audio=%016llx/%016llx samples=%llu/%llu\n",failed,videos,(unsigned long long)video_hash,(unsigned long long)vh,(unsigned long long)audio_hash,(unsigned long long)ah,(unsigned long long)audio_frames,(unsigned long long)n);return 1;}
 retro_unload_game();
 for(unsigned cycle=0;cycle<4;cycle++) {
  if(!retro_load_game(NULL))return 1;run();
  if(failed||videos!=20||video_hash!=vh||audio_hash!=ah||audio_frames!=n)return 1;
  retro_unload_game();
 }
 retro_deinit();printf("lifecycle PASS: refused format, missing factory baseline, truncated/mismatched CMOS and nonempty factory capture; accepted exact CMOS digest; reset and four reloads match 20 public AV frames (%llu stereo pairs)\n",(unsigned long long)n);return 0;
}
