/* AY-1: persisted chooser/boot decisions across two calendar years. GPL-2.0-only. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#endif
#include "libretro.h"
#include "arcdroid_core.h"
#include "boot_observation.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
static HMODULE library;
static void *symbol(const char *s){return (void *)GetProcAddress(library,s);}
#else
#include <dlfcn.h>
static void *library;
static void *symbol(const char *s){return dlsym(library,s);}
#endif
#include "../tools/rpc_runner_vfs.h"
#define LOAD(v,n) do{void *p=symbol(n);assert(p);memcpy(&(v),&p,sizeof(v));}while(0)
#define API(n) __typeof__(&n) p_##n;LOAD(p_##n,#n)
static const char *system_directory;
static bool confirm,session_mode;
static bool environment(unsigned command,void *data)
{
   switch(command){
   case RETRO_ENVIRONMENT_GET_VFS_INTERFACE:((struct retro_vfs_interface_info *)data)->iface=&vfs;return true;
   case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:*(const char **)data=system_directory;return true;
   case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:*(const char **)data=save_directory;return true;
   case RETRO_ENVIRONMENT_GET_CAN_DUPE:*(bool *)data=true;return true;
   case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:return true;
   case RETRO_ENVIRONMENT_GET_VARIABLE:{struct retro_variable *v=data;
      if(!strcmp(v->key,"arcdroid_machine"))v->value="A3000";
      else if(!strcmp(v->key,"arcdroid_launch"))v->value=session_mode?"off":"auto";
      else if(!strcmp(v->key,"arcdroid_hard_disc"))v->value="off";
      else return false;return true;}
   case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:*(bool *)data=false;return true;
   default:return false;
   }
}
static void video(const void *p,unsigned w,unsigned h,size_t pitch){(void)p;(void)w;(void)h;(void)pitch;}
static size_t audio(const int16_t *p,size_t n){(void)p;return n;}
static void poll(void){}
static int16_t input(unsigned p,unsigned d,unsigned i,unsigned id)
{(void)i;return p==0 && d==RETRO_DEVICE_JOYPAD && id==RETRO_DEVICE_ID_JOYPAD_A && confirm;}
int main(int argc,char **argv)
{
   assert(argc==7);unsigned base=(unsigned)strtoul(argv[6],NULL,10);system_directory=argv[2];assert(canonical(argv[3],save_directory));
#ifdef _WIN32
   library=LoadLibraryA(argv[1]);
#else
   library=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
#endif
   assert(library);
   API(retro_set_environment);API(retro_init);API(retro_deinit);API(retro_load_game);
   API(retro_unload_game);API(retro_run);API(retro_reset);API(retro_set_video_refresh);
   API(retro_set_audio_sample_batch);API(retro_set_input_poll);API(retro_set_input_state);
   API(arc_boot_observation_format);API(arc_boot_observation_equal);
   unsigned (*state)(void);LOAD(state,"arc_libretro_test_session_state");
   const char *(*content)(void);LOAD(content,"arc_libretro_test_content_id");
   const char *(*choice)(void);LOAD(choice,"arc_libretro_test_choice_path");
   bool (*stamp)(ArcBootObservation *);LOAD(stamp,"arc_libretro_test_boot_stamp");
   bool (*start_stamp)(ArcBootObservation *);LOAD(start_stamp,"arc_libretro_test_boot_start_stamp");
   bool (*hit)(void);LOAD(hit,"arc_libretro_test_boot_record_hit");
   bool (*image)(uint8_t *);LOAD(image,"arc_engine_test_cmos_image");
   bool (*write)(unsigned,uint8_t);LOAD(write,"arc_engine_test_cmos_bus_write");
   void (*clock)(int,int,int,int,int,int,int);LOAD(clock,"cmos_test_clock");
   session_mode=!strcmp(argv[5],"session");
   p_retro_set_environment(environment);p_retro_set_video_refresh(video);
   p_retro_set_audio_sample_batch(audio);p_retro_set_input_poll(poll);p_retro_set_input_state(input);p_retro_init();
   bool chooser=!strcmp(argv[5],"chooser");
   struct retro_game_info game={.path=argv[4]};
   ArcBootObservation first={0},later={0};char record_path[4096]={0};
   if(session_mode){
      uint32_t (*read_word)(uint32_t);LOAD(read_word,"readmemf_debug");
      for(unsigned reopen=0;reopen<2;++reopen){
         clock((int)base,7,15,12,0,0,0);assert(p_retro_load_game(&game));
         for(unsigned i=0;read_word(0x02000408)!=0xa5;++i){assert(i<10);p_retro_run();}
         assert(read_word(0x02000400)==base && read_word(0x02000404)==1);
         uint8_t live[256];assert(image(live));unsigned edited=base+8;
         live[192]=(uint8_t)(edited%100);live[193]=(uint8_t)(edited/100);
         unsigned sum=1;for(unsigned i=16;i<256;++i)if(i!=63)sum+=live[i];
         assert(write(192,live[192]) && write(193,live[193]) && write(63,(uint8_t)sum));
         clock((int)base+4,7,15,12,0,0,0);p_retro_reset();
         for(unsigned i=0;read_word(0x02000408)!=0xa5;++i){assert(i<10);p_retro_run();}
         assert(read_word(0x02000400)==edited && read_word(0x02000404)==1);
         p_retro_unload_game();
      }
      p_retro_deinit();vfs_assert_no_open_handles();
      puts("calendar frontend: guest year/checksum across retro_reset and reopen passed");return 0;
   }

   for(unsigned pass=0;pass<3;++pass){
      unsigned year=base+(pass!=0);clock((int)year,7,15,12,0,0,0);
      assert(p_retro_load_game(&game));
      if(chooser){
         if(!pass){assert(state()==2);for(unsigned i=0;i<3;++i)p_retro_run();confirm=true;p_retro_run();confirm=false;}
         assert(state()==3);FILE *f=fopen(choice(),"rb");assert(f && !fclose(f));
      }else{
         assert(state()==3);
         if(pass==2)assert(hit());
         assert(start_stamp(&later));
         assert(snprintf(record_path,sizeof(record_path),"%s/arcdroid/launch/%s.boot",save_directory,content())<(int)sizeof(record_path));
      }
      assert(stamp(&later));
      if(!pass)first=later;else assert(p_arc_boot_observation_equal(&first,&later));
      uint8_t live[256];assert(image(live));live[192]=(uint8_t)(year%100);live[193]=(uint8_t)(year/100);
      unsigned sum=1;for(unsigned i=16;i<256;++i)if(i!=63)sum+=live[i];live[63]=(uint8_t)sum;
      assert(write(192,live[192]) && write(193,live[193]) && write(63,live[63]));
      p_retro_unload_game();
      if(!chooser && pass==1){
         uint8_t record[ARC_BOOT_OBSERVATION_SIZE];assert(p_arc_boot_observation_format(&later,record,sizeof(record)));
         char parent[4096];assert(snprintf(parent,sizeof(parent),"%s/arcdroid/launch",save_directory)<(int)sizeof(parent));
         (void)vfs_mkdir(parent);FILE *f=fopen(record_path,"wb");assert(f && fwrite(record,1,sizeof(record),f)==sizeof(record) && !fclose(f));
      }
   }
   p_retro_deinit();vfs_assert_no_open_handles();
   printf("calendar %s: %u/%u boot stamps and persisted decision survive guest year/checksum writes\n",argv[5],base,base+1);
   return 0;
}
