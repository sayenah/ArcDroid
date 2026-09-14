/* ArcDroid Windows LoadLibrary and callback-order harness. GPL-2.0-only. */
#include "libretro.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <windows.h>

enum Phase { PHASE_NONE, PHASE_ENV, PHASE_INIT, PHASE_LOAD, PHASE_RUN };
static enum Phase phase;
static bool saw_options, saw_no_game, saw_disk, saw_pixel, saw_controllers, saw_keyboard;

static bool environment(unsigned command, void *data)
{
   switch (command)
   {
      case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
         assert(phase == PHASE_ENV && !*(bool *)data); saw_no_game = true; return true;
      case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
         assert(phase == PHASE_ENV); saw_options = true; return true;
      case RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE:
         assert(phase == PHASE_INIT); saw_disk = true; return true;
      case RETRO_ENVIRONMENT_GET_VFS_INTERFACE:
         assert(phase == PHASE_INIT); return false;
      case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
         assert(phase == PHASE_LOAD && *(enum retro_pixel_format *)data == RETRO_PIXEL_FORMAT_XRGB8888);
         saw_pixel = true; return true;
      case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
         assert(phase == PHASE_LOAD); saw_controllers = true; return true;
      case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK:
         assert(phase == PHASE_LOAD); saw_keyboard = true; return true;
      case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
      case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
         *(const char **)data = "."; return true;
      case RETRO_ENVIRONMENT_SET_MESSAGE_EXT:
      case RETRO_ENVIRONMENT_SET_MESSAGE:
         return true;
      case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
         assert(phase == PHASE_RUN); return true;
      default:
         return false;
   }
}

#define LOAD(type, name) type name = (type)GetProcAddress(module, #name); assert(name)

typedef void (*SetEnvironmentFn)(retro_environment_t);
typedef void (*VoidFn)(void);
typedef bool (*LoadGameFn)(const struct retro_game_info *);

int main(int argc, char **argv)
{
   assert(argc == 2);
   HMODULE module = LoadLibraryA(argv[1]);
   assert(module);
   LOAD(SetEnvironmentFn, retro_set_environment);
   LOAD(VoidFn, retro_init);
   LOAD(VoidFn, retro_deinit);
   LOAD(LoadGameFn, retro_load_game);

   phase = PHASE_ENV;
   retro_set_environment(environment);
   phase = PHASE_INIT;
   retro_init();
   struct retro_game_info game = {"missing.adf", NULL, 0u, NULL};
   phase = PHASE_LOAD;
   assert(!retro_load_game(&game));
   assert(saw_options && saw_no_game && saw_disk && saw_pixel && saw_controllers && saw_keyboard);
   retro_deinit();
   assert(FreeLibrary(module));
   puts("windows_load_harness: DLL loaded and environment callback order verified");
   return 0;
}
