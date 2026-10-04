/* ArcDroid disposable RPCEmu libretro probe. GPL-2.0-only. */
#include "probe.h"
#include "libretro.h"
#include "arcdroid_core.h"
#include "keyboard.h"
#include "vidc20.h"
#include "iomd.h"
#include "media.h"
#include "ide_probe.h"
#include "frontend.h"
#include <inttypes.h>
#include <ctype.h>
static retro_environment_t env;
static retro_video_refresh_t video;
static retro_audio_sample_t audio_one;
static retro_audio_sample_batch_t audio_batch;
static retro_input_poll_t input_poll;
static retro_input_state_t input_state;
static int loaded, replay, machine_live;
static FILE *csv,*events;
static uint64_t frame,next_frame,frame_period,wall_origin,flyback_fall;
static unsigned keys[512],keyboard_held[512],pad_held[512],mouse_buttons;
static uint32_t blank[320*256];
static char out_dir[1024];
typedef struct {uint64_t ns;char verb[24];int a,b;char tag[64];} Event;
static Event script[65536];
static unsigned script_count,script_at;
static Event boot_script[256];
static unsigned boot_count,boot_at;
static const struct retro_variable options[]={
 {"rpcprobe_model","Model; SA110|ARM710|ARM610|A7000"},
 {"rpcprobe_rate","Million instructions per emulated second; 100|20|35|150|200|250|300"},
 {"rpcprobe_firmware","Combined firmware (verified, system/arcdroid/firmware); ROM371|ROM370|ROM360|ROM350|none"},
 {"rpcprobe_workload","Replay directory in system/arcdroid/rpcprobe; none|trial-a|trial-b|trial-c|trial-d|authored"},
 {"rpcprobe_script","Input script file; none"},
 {"rpcprobe_output","Private session output directory; none"},
 {"rpcprobe_authored","Authored ROM workload; disabled|enabled"},
 {"rpcprobe_media_list","External JFD paths, one per line in disc order; none"},
 {"rpcprobe_ide","Session scratch hard disc; disabled|enabled"},
 {"rpcprobe_boot","Boot medium; automatic|standard|helper"},
 {"rpcprobe_ide_baseline","Read-only post-install baseline; none"},
 {"rpcprobe_ide_baseline_sha1","Required baseline SHA-1; none"},
 {"rpcprobe_cmos_factory","Capture firmware factory reset; disabled|enabled"},
 {"rpcprobe_cmos_baseline","Firmware-generated cold CMOS; none"},
 {"rpcprobe_cmos_sha1","Required cold CMOS SHA-1; none"},
 {"rpcprobe_diagnostics","Passive device/mode diagnostics; disabled|enabled"},
 {"rpcprobe_register_trace","Passive bounded raster/sound register trace; disabled|enabled"},
 {NULL,NULL}};
static const char *option(const char *key) {
 struct retro_variable v={key,NULL};const char *value=env&&env(RETRO_ENVIRONMENT_GET_VARIABLE,&v)&&v.value?v.value:"none";
 return probe_frontend_option(key,value);
}
static FILE *output_file(const char *leaf) {
 char p[1200];if(!out_dir[0])return NULL;
 if(snprintf(p,sizeof(p),"%s/%s",out_dir,leaf)>=(int)sizeof(p))fatal("Output path bound");
 FILE *f=fopen(p,"wb");if(!f)fatal("Output creation failed");return f;
}
static void key(unsigned code,int down) {
 if(code>=512)fatal("Scan code out of range");
 if(keys[code]==(unsigned)down)return;
 uint8_t scan[2]={code>=256?0xe0:(uint8_t)code,code>=256?(uint8_t)code:0};
 if(down)keyboard_key_press(scan);else keyboard_key_release(scan);
 keys[code]=down;
}
static void clear_held(void) {
 for(unsigned i=0;i<512;i++)if(keys[i])key(i,0);
 mouse_mouse_release(7);mouse_buttons=0;
 memset(keyboard_held,0,sizeof(keyboard_held));memset(pad_held,0,sizeof(pad_held));
}
static void script_load(const char *name) {
 script_count=script_at=0;replay=strcmp(name,"none")!=0;
 if(!replay)return;
 FILE *f=fopen(name,"rb");if(!f)fatal("Script open failed");
 char line[256];uint64_t last=0;
 while(fgets(line,sizeof(line),f)) {
  if(line[0]=='#'||line[0]=='\n')continue;
  Event e={0};unsigned long long us=0;
  int n=sscanf(line,"%llu %23s %i %i %63s",&us,e.verb,&e.a,&e.b,e.tag);
  if(n<2||us>UINT64_MAX/1000||us*1000<last||script_count==65536) {fclose(f);fatal("Invalid script timestamp/order/bound");}
  e.ns=us*1000;last=e.ns;
  if(strcmp(e.verb,"key_down")&&strcmp(e.verb,"key_up")&&strcmp(e.verb,"mouse")&&strcmp(e.verb,"buttons")&&strcmp(e.verb,"checkpoint")&&strcmp(e.verb,"end")&&strcmp(e.verb,"swap")) {fclose(f);fatal("Unsupported script verb");}
  if((!strcmp(e.verb,"key_down")||!strcmp(e.verb,"key_up"))&&(n<3||e.a<0||e.a>511)) {fclose(f);fatal("Invalid script key");}
  script[script_count++]=e;
 }
 if(ferror(f)||fclose(f))fatal("Script read failure");
}
/* A general helper invocation, owned by launch machinery, never a replay
 * command. Fixed reset-origin virtual timestamps, normal PS/2 keyboard path.
 * All private title input begins after 26 seconds; factory/authored excluded.
 */
static void boot_key(uint64_t ns,unsigned scan) {
 if(boot_count+2>256)fatal("Boot input bound");
 boot_script[boot_count++]=(Event){.ns=ns,.verb="key_down",.a=(int)scan};
 boot_script[boot_count++]=(Event){.ns=ns+40000000,.verb="key_up",.a=(int)scan};
}
static void boot_load(void) {
 boot_count=boot_at=0;if(!probe_helper_enabled)return;
 boot_key(UINT64_C(14000000000),7); /* F12, normal desktop command prompt */
 const char *text=probe_ide_enabled?"obey adfs::1.$.!boot 1":"obey adfs::1.$.!boot 0";
 const char *letters="abcdefghijklmnopqrstuvwxyz";
 const unsigned scans[]={0x1c,0x32,0x21,0x23,0x24,0x2b,0x34,0x33,0x43,0x3b,0x42,0x4b,0x3a,0x31,0x44,0x4d,0x15,0x2d,0x1b,0x2c,0x3c,0x2a,0x1d,0x22,0x35,0x1a};
 uint64_t ns=UINT64_C(15000000000);
 for(const char *p=text;*p;p++) {
  const char *l=strchr(letters,*p);unsigned scan=l?scans[l-letters]:*p==' '?0x29:*p=='1'||*p=='!'?0x16:*p=='0'?0x45:*p=='.'?0x49:*p==':'?0x4c:*p=='$'?0x25:0;
  if(!scan)fatal("Boot text scan unavailable");
  int shift=*p==':'||*p=='$'||*p=='!';
  if(shift)boot_script[boot_count++]=(Event){.ns=ns,.verb="key_down",.a=0x12};
  boot_key(ns+10000000,scan);
  if(shift)boot_script[boot_count++]=(Event){.ns=ns+60000000,.verb="key_up",.a=0x12};
  ns+=100000000;
 }
 boot_key(ns+200000000,0x5a);boot_key(UINT64_C(25000000000),0x5a);
 for(unsigned i=0;i<script_count;i++)if(script[i].ns<UINT64_C(26000000000)&&strcmp(script[i].verb,"checkpoint"))fatal("Replay overlaps general helper boot input");
}
static void deliver(void) {
 while(boot_at<boot_count&&boot_script[boot_at].ns<=probe_now) {
  Event *e=&boot_script[boot_at++];key((unsigned)e->a,!strcmp(e->verb,"key_down"));
  if(events&&fprintf(events,"boot-%u,%"PRIu64",%"PRIu64",%s,%d,0,0,%08x\n",boot_at,e->ns,probe_now,e->verb,e->a,PC)<0)fatal("Boot log write failure");
 }
 while(script_at<script_count&&script[script_at].ns<=probe_now) {
  Event *e=&script[script_at++];
  if(!strcmp(e->verb,"key_down"))key((unsigned)e->a,1);
  else if(!strcmp(e->verb,"key_up"))key((unsigned)e->a,0);
  else if(!strcmp(e->verb,"mouse")) {
   if(e->a < -255 || e->a>255||e->b < -255||e->b>255)fatal("Mouse script deltas exceed one PS/2 packet");
   mouse_mouse_move_relative(e->a,e->b);
  } else if(!strcmp(e->verb,"buttons")) {
   if(e->a<0||e->a>7)fatal("Invalid button mask");
   mouse_mouse_release(mouse_buttons&~e->a);mouse_mouse_press(e->a&~mouse_buttons);mouse_buttons=e->a;
  } else if(!strcmp(e->verb,"end"))clear_held();
  else if(!strcmp(e->verb,"swap")) {if(e->a<0)fatal("Negative disc index");probe_media_swap((unsigned)e->a);}
  if(events) {
   unsigned w,h;const uint32_t *p=probe_video_pixels(&w,&h);
   uint64_t hash=p?probe_video_hash():0;
   if(fprintf(events,"%u,%"PRIu64",%"PRIu64",%s,%d,%d,%016"PRIx64",%08x\n",script_at,e->ns,probe_now,e->verb,e->a,e->b,hash,PC)<0)fatal("Event log write failed");
  }
 }
}
/* Host keys and pad mappings arbitrate into one normal set-2 device. */
static void keyboard_callback(bool down,unsigned code,uint32_t c,uint16_t mods) {
 (void)c;(void)mods;if(replay||!loaded||(probe_helper_enabled&&probe_now<UINT64_C(26000000000)))return;
 static const struct {unsigned host,scan;} mapping[]={
 {RETROK_a,0x1c},{RETROK_b,0x32},{RETROK_c,0x21},{RETROK_d,0x23},{RETROK_e,0x24},{RETROK_f,0x2b},
 {RETROK_g,0x34},{RETROK_h,0x33},{RETROK_i,0x43},{RETROK_j,0x3b},{RETROK_k,0x42},{RETROK_l,0x4b},
 {RETROK_m,0x3a},{RETROK_n,0x31},{RETROK_o,0x44},{RETROK_p,0x4d},{RETROK_q,0x15},{RETROK_r,0x2d},
 {RETROK_s,0x1b},{RETROK_t,0x2c},{RETROK_u,0x3c},{RETROK_v,0x2a},{RETROK_w,0x1d},{RETROK_x,0x22},
 {RETROK_y,0x35},{RETROK_z,0x1a},
 {RETROK_1,0x16},{RETROK_2,0x1e},{RETROK_3,0x26},{RETROK_4,0x25},{RETROK_5,0x2e},
 {RETROK_6,0x36},{RETROK_7,0x3d},{RETROK_8,0x3e},{RETROK_9,0x46},{RETROK_0,0x45},
 {RETROK_SPACE,0x29},{RETROK_RETURN,0x5a},{RETROK_ESCAPE,0x76},{RETROK_TAB,0x0d},{RETROK_BACKSPACE,0x66},
 {RETROK_UP,0x175},{RETROK_DOWN,0x172},{RETROK_LEFT,0x16b},{RETROK_RIGHT,0x174},
 {RETROK_LSHIFT,0x12},{RETROK_RSHIFT,0x59},{RETROK_LCTRL,0x14},{RETROK_RCTRL,0x114},
 {RETROK_LALT,0x11},{RETROK_RALT,0x111},{RETROK_CAPSLOCK,0x58},
 {RETROK_MINUS,0x4e},{RETROK_EQUALS,0x55},{RETROK_LEFTBRACKET,0x54},{RETROK_RIGHTBRACKET,0x5b},
 {RETROK_SEMICOLON,0x4c},{RETROK_QUOTE,0x52},{RETROK_BACKSLASH,0x5d},{RETROK_BACKQUOTE,0x0e},
 {RETROK_COMMA,0x41},{RETROK_PERIOD,0x49},{RETROK_SLASH,0x4a},
 {RETROK_F1,0x05},{RETROK_F2,0x06},{RETROK_F3,0x04},{RETROK_F4,0x0c},{RETROK_F5,0x03},{RETROK_F6,0x0b},
 {RETROK_F7,0x83},{RETROK_F8,0x0a},{RETROK_F9,0x01},{RETROK_F10,0x09},{RETROK_F11,0x78},{RETROK_F12,0x07},
 {RETROK_INSERT,0x170},{RETROK_DELETE,0x171},{RETROK_HOME,0x16c},{RETROK_END,0x169},{RETROK_PAGEUP,0x17d},{RETROK_PAGEDOWN,0x17a}
 };
 for(unsigned i=0;i<sizeof(mapping)/sizeof(mapping[0]);i++)if(mapping[i].host==code) {
  unsigned sc=mapping[i].scan;keyboard_held[sc]=down;key(sc,down||pad_held[sc]);return;
 }
}
static bool disk_replace(unsigned i,const struct retro_game_info *g) {(void)i;(void)g;return false;}
static bool disk_add(void) {return false;}
static struct retro_disk_control_callback disks={probe_media_eject,probe_media_ejected,probe_media_index,probe_media_select,probe_media_count,disk_replace,disk_add};
RETRO_API void retro_set_environment(retro_environment_t cb) {env=cb;bool yes=true;env(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME,&yes);env(RETRO_ENVIRONMENT_SET_VARIABLES,(void *)options);}
RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb) {video=cb;}
RETRO_API void retro_set_audio_sample(retro_audio_sample_t cb) {audio_one=cb;}
RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) {audio_batch=cb;}
RETRO_API void retro_set_input_poll(retro_input_poll_t cb) {input_poll=cb;}
RETRO_API void retro_set_input_state(retro_input_state_t cb) {input_state=cb;}
RETRO_API unsigned retro_api_version(void) {return RETRO_API_VERSION;}
RETRO_API void retro_init(void) {}
RETRO_API void retro_deinit(void) {retro_unload_game();}
RETRO_API void retro_get_system_info(struct retro_system_info *i) {*i=(struct retro_system_info){"ArcDroid RPC Probe","P-incomplete","jfd|m3u",true,false};}
RETRO_API void retro_get_system_av_info(struct retro_system_av_info *i) {
 unsigned w,h;probe_video_pixels(&w,&h);if(!w)w=320;if(!h)h=256;
 *i=(struct retro_system_av_info){{w,h,2048,2048,(float)w/h},{frame_period?1e9/frame_period:60,48000}};
}
RETRO_API void retro_set_controller_port_device(unsigned p,unsigned d) {(void)p;(void)d;}
RETRO_API bool retro_load_game(const struct retro_game_info *g) {
 probe_can_escape=1;
 if(setjmp(probe_escape)) {fprintf(stderr,"Probe load failed: %s\n",probe_error);probe_can_escape=0;retro_unload_game();return false;}
 enum retro_pixel_format format=RETRO_PIXEL_FORMAT_XRGB8888;
 if(!env(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT,&format))fatal("Frontend refuses XRGB8888");
 probe_frontend_paths(env);
 const char *model=option("rpcprobe_model");Model m=Model_RPCSA110;
 if(!strcmp(model,"ARM710"))m=Model_RPCARM710;else if(!strcmp(model,"ARM610"))m=Model_RPCARM610;else if(!strcmp(model,"A7000"))m=Model_A7000;
 unsigned rate=(unsigned)strtoul(option("rpcprobe_rate"),NULL,10);
 if(rate!=20&&rate!=35&&rate!=100&&rate!=150&&rate!=200&&rate!=250&&rate!=300)fatal("Invalid instruction rate");
 probe_ide_enabled=!strcmp(option("rpcprobe_ide"),"enabled");
 int authored=!strcmp(option("rpcprobe_authored"),"enabled");
 int factory=!strcmp(option("rpcprobe_cmos_factory"),"enabled");
 probe_helper_enabled=!authored&&!factory&&strcmp(option("rpcprobe_boot"),"standard");
 if(!authored&&!factory&&!strcmp(option("rpcprobe_cmos_baseline"),"none"))fatal("Firmware session requires factory-reset CMOS baseline");
 probe_cmos_configure(option("rpcprobe_cmos_baseline"),option("rpcprobe_cmos_sha1"),factory);
 machine_live=1;probe_machine_init(m,rate);
 if(!strcmp(option("rpcprobe_authored"),"enabled"))probe_workload_rom();
 else {
  FILE *f=fopen(option("rpcprobe_firmware"),"rb");if(!f)fatal("Firmware open failed");
  size_t n=fread(rom,1,ROMSIZE,f);int tail=fgetc(f),bad=ferror(f);fclose(f);
  if(bad||tail!=EOF||(n!=2*1024*1024&&n!=4*1024*1024&&n!=8*1024*1024))fatal("Firmware size/read failure");
  char hash[41];arc_sha1_hex(rom,n,hash);
  const char *accepted[]={"c31318e712ee7de92e46f9df6c791b614d4c9304","3cf75aa4d4dc8fe57110a124fbd1560d46c549b1","71cbc9f12debaa7aa2aeb91d38682e6c5c4a34a5","c5fe0645e48894fb4b245abeefdc9a65d659af59"};
  int match=0;for(unsigned i=0;i<4;i++)if(!strcmp(hash,accepted[i]))match=1;
  if(!match)fatal("Firmware fingerprint refused");
  for(size_t at=n;at<ROMSIZE;at+=n)memcpy((uint8_t *)rom+at,rom,n);
 }
 script_load(option("rpcprobe_script"));
 boot_load();
 const char *out=option("rpcprobe_output");out_dir[0]=0;
 if(strcmp(out,"none")) {if(strlen(out)>=sizeof(out_dir))fatal("Output directory bound");strcpy(out_dir,out);}
 csv=output_file("frames.csv");events=output_file("events.csv");
 probe_register_trace_reset();
 if(!strcmp(option("rpcprobe_register_trace"),"enabled")) {
  probe_register_log=output_file("registers.csv");
  if(probe_register_log)fprintf(probe_register_log,"emu_ns,device,register,value,pc\n");
 }
 if(factory||!strcmp(option("rpcprobe_diagnostics"),"enabled")) {
  probe_boot_log=output_file("boot-devices.csv");probe_mode_log=output_file("mode-requests.csv");
 }
 FILE *custody=output_file("media-custody.csv");
 if(g&&g->path)probe_media_open_content(g->path,custody);
 else probe_media_open(option("rpcprobe_media_list"),custody);
 probe_ide_open(out_dir,option("rpcprobe_ide_baseline"),option("rpcprobe_ide_baseline_sha1"),custody);
 if(custody&&fclose(custody))fatal("Media custody close failed");
 env(RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE,&disks);
 if(csv)fprintf(csv,"frame,emu_ns,period_ns,exec_ns,instructions,audio_frames,underruns,overruns,video_hash,audio_hash,wall_ns,instrument_ns,status\n");
 if(events)fprintf(events,"ordinal,scheduled_ns,delivered_ns,event,a,b,video_hash,pc\n");
 struct retro_keyboard_callback kb={keyboard_callback};env(RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK,&kb);
 frame=0;frame_period=probe_video_period();next_frame=frame_period;flyback_fall=UINT64_MAX;wall_origin=probe_wall_ns();
 memset(keys,0,sizeof(keys));memset(keyboard_held,0,sizeof(keyboard_held));memset(pad_held,0,sizeof(pad_held));mouse_buttons=0;loaded=1;probe_can_escape=0;return true;
}
RETRO_API void retro_run(void) {
 if(!loaded||probe_failed) {if(video)video(blank,320,256,320*4);return;}
 probe_can_escape=1;
 if(setjmp(probe_escape)) {
  fprintf(stderr,"Probe frame failed: %s\n",probe_error);
  if(csv) {fprintf(csv,"%"PRIu64",%"PRIu64",0,0,0,0,0,0,0,0,0,0,FAIL\n",frame,probe_now);fflush(csv);}
  struct retro_message msg={"RPC probe session failed; see status row",600};env(RETRO_ENVIRONMENT_SET_MESSAGE,&msg);
  bool shutdown=true;env(RETRO_ENVIRONMENT_SHUTDOWN,&shutdown);probe_can_escape=0;return;
 }
 uint64_t start=probe_wall_ns(),ins=probe_instructions;
 probe_audio_count=0;
 if(input_poll)input_poll();
 if(!replay&&input_state&&(!probe_helper_enabled||probe_now>=UINT64_C(26000000000))) {
  const unsigned buttons[]={RETRO_DEVICE_ID_JOYPAD_UP,RETRO_DEVICE_ID_JOYPAD_DOWN,RETRO_DEVICE_ID_JOYPAD_LEFT,RETRO_DEVICE_ID_JOYPAD_RIGHT,RETRO_DEVICE_ID_JOYPAD_A,RETRO_DEVICE_ID_JOYPAD_B,RETRO_DEVICE_ID_JOYPAD_X};
  const unsigned codes[]={0x175,0x172,0x16b,0x174,0x29,0x5a,0x76};
  for(unsigned i=0;i<7;i++) {pad_held[codes[i]]=input_state(0,RETRO_DEVICE_JOYPAD,0,buttons[i])!=0;key(codes[i],pad_held[codes[i]]||keyboard_held[codes[i]]);}
  int dx=input_state(0,RETRO_DEVICE_MOUSE,0,RETRO_DEVICE_ID_MOUSE_X),dy=input_state(0,RETRO_DEVICE_MOUSE,0,RETRO_DEVICE_ID_MOUSE_Y);
  mouse_mouse_move_relative(dx,dy);
  unsigned mask=(input_state(0,RETRO_DEVICE_MOUSE,0,RETRO_DEVICE_ID_MOUSE_LEFT)?1u:0u)|(input_state(0,RETRO_DEVICE_MOUSE,0,RETRO_DEVICE_ID_MOUSE_RIGHT)?2u:0u)|(input_state(0,RETRO_DEVICE_MOUSE,0,RETRO_DEVICE_ID_MOUSE_MIDDLE)?4u:0u);
  mouse_mouse_release(mouse_buttons&~mask);mouse_mouse_press(mask&~mouse_buttons);mouse_buttons=mask;
 }
 while(probe_now<next_frame) {
  if(flyback_fall<=probe_now){iomd_flyback(0);flyback_fall=UINT64_MAX;}
  deliver();uint64_t target=next_frame;
  if(script_at<script_count&&script[script_at].ns<target)target=script[script_at].ns;
  if(boot_at<boot_count&&boot_script[boot_at].ns<target)target=boot_script[boot_at].ns;
  if(flyback_fall<target)target=flyback_fall;
  probe_advance(target);
 }
 if(flyback_fall<=probe_now)iomd_flyback(0);
 deliver();iomd_flyback(1);drawscr();flyback_fall=probe_now+probe_video_blank_ns();
 uint64_t period=probe_video_period();if(period!=frame_period) {
  frame_period=period;struct retro_system_av_info av;retro_get_system_av_info(&av);env(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO,&av);
 }
 next_frame+=period;
 probe_register_trace(3,0,(uint32_t)period);
 unsigned w,h;const uint32_t *pixels=probe_video_pixels(&w,&h);
 if(!pixels) {pixels=blank;w=320;h=256;}
 if(video)video(pixels,w,h,pixels==blank?(size_t)w*4:probe_video_pitch());
 if(audio_batch) {size_t n=audio_batch(probe_audio,probe_audio_count);if(n!=probe_audio_count)fatal("Frontend audio short acceptance");}
 else if(audio_one)for(unsigned i=0;i<probe_audio_count;i++)audio_one(probe_audio[i*2],probe_audio[i*2+1]);
 uint64_t instrument_start=probe_wall_ns();
 uint64_t vh=pixels==blank?probe_hash(UINT64_C(1469598103934665603),pixels,(size_t)w*h*4):probe_video_hash();
 probe_audio_hash=probe_hash(probe_audio_hash,probe_audio,(size_t)probe_audio_count*4);
 uint64_t end=probe_wall_ns(),overhead=end-instrument_start;
 if(csv&&fprintf(csv,"%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64",%u,%"PRIu64",%"PRIu64",%016"PRIx64",%016"PRIx64",%"PRIu64",%"PRIu64",OK\n",frame,probe_now,period,end-start,probe_instructions-ins,probe_audio_count,probe_underruns,probe_overruns,vh,probe_audio_hash,end-wall_origin,overhead)<0)fatal("CSV write failed");
 frame++;probe_can_escape=0;
}
RETRO_API void retro_reset(void) {
 if(!loaded)return;clear_held();probe_can_escape=1;
 if(!setjmp(probe_escape)) {probe_machine_reset();script_at=boot_at=0;frame_period=probe_video_period();next_frame=frame_period;flyback_fall=UINT64_MAX;}
 probe_can_escape=0;
}
RETRO_API void retro_unload_game(void) {
 if(loaded&&out_dir[0]) {
  uint8_t data[256];probe_cmos_snapshot(data);char name[1200];
  int bad=snprintf(name,sizeof(name),"%s/cmos-final.bin",out_dir)>=(int)sizeof(name);FILE *f=bad?NULL:fopen(name,"wb");
  if(!f)bad=1;else {bad=fwrite(data,1,sizeof(data),f)!=sizeof(data);if(fclose(f))bad=1;}
  if(bad) {probe_failed=1;bool stop=true;env(RETRO_ENVIRONMENT_SHUTDOWN,&stop);}
 }
 if(loaded)clear_held();
 if(machine_live) {probe_machine_close();machine_live=0;}probe_media_close();loaded=0;
 if(!probe_ide_close()) {
  probe_failed=1;if(csv)fprintf(csv,"%"PRIu64",%"PRIu64",0,0,0,0,0,0,0,0,0,0,FAIL\n",frame,probe_now);
  struct retro_message message={"Scratch IDE close failed",600};env(RETRO_ENVIRONMENT_SET_MESSAGE,&message);bool stop=true;env(RETRO_ENVIRONMENT_SHUTDOWN,&stop);
 }
 FILE **outputs[]={&csv,&events,&probe_boot_log,&probe_mode_log,&probe_register_log};int output_bad=0;
 for(unsigned i=0;i<sizeof(outputs)/sizeof(outputs[0]);i++)if(*outputs[i]) {
  if(ferror(*outputs[i]))output_bad=1;
  if(fclose(*outputs[i]))output_bad=1;*outputs[i]=NULL;
 }
 if(output_bad) {probe_failed=1;bool stop=true;env(RETRO_ENVIRONMENT_SHUTDOWN,&stop);}
}
RETRO_API bool retro_load_game_special(unsigned n,const struct retro_game_info *g,size_t count) {(void)n;(void)g;(void)count;return false;}
RETRO_API size_t retro_serialize_size(void) {return 0;}
RETRO_API bool retro_serialize(void *p,size_t n) {(void)p;(void)n;return false;}
RETRO_API bool retro_unserialize(const void *p,size_t n) {(void)p;(void)n;return false;}
RETRO_API void retro_cheat_reset(void) {}
RETRO_API void retro_cheat_set(unsigned n,bool b,const char *s) {(void)n;(void)b;(void)s;}
RETRO_API unsigned retro_get_region(void) {return RETRO_REGION_PAL;}
RETRO_API void *retro_get_memory_data(unsigned n) {(void)n;return NULL;}
RETRO_API size_t retro_get_memory_size(unsigned n) {(void)n;return 0;}
