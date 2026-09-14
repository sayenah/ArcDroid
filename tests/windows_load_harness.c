/* Windows DLL loading/export smoke harness. GPL-2.0-only. */
#include <assert.h>
#include <stdio.h>
#include <windows.h>

int main(int argc, char **argv)
{
   static const char *symbols[] = {
      "retro_api_version", "retro_cheat_reset", "retro_cheat_set", "retro_deinit",
      "retro_get_memory_data", "retro_get_memory_size", "retro_get_region",
      "retro_get_system_av_info", "retro_get_system_info", "retro_init",
      "retro_load_game", "retro_load_game_special", "retro_reset", "retro_run",
      "retro_serialize", "retro_serialize_size", "retro_set_audio_sample",
      "retro_set_audio_sample_batch", "retro_set_controller_port_device",
      "retro_set_environment", "retro_set_input_poll", "retro_set_input_state",
      "retro_set_video_refresh", "retro_unload_game", "retro_unserialize"
   };
   assert(argc == 2);
   HMODULE module = LoadLibraryA(argv[1]);
   assert(module);
   for (unsigned i = 0; i < sizeof(symbols) / sizeof(symbols[0]); ++i)
      assert(GetProcAddress(module, symbols[i]));
   assert(FreeLibrary(module));
   puts("windows_load_harness: DLL loaded and all 25 entry points resolved");
   return 0;
}
