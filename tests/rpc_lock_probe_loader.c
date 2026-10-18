/* Minimal libretro loader for the P10b probe, no emulator. GPL-2.0-only. */
#include "libretro.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif
static bool environment(unsigned command, void *data)
{
    return command == RETRO_ENVIRONMENT_SET_PIXEL_FORMAT &&
        *(enum retro_pixel_format *)data == RETRO_PIXEL_FORMAT_XRGB8888;
}
static unsigned frames, samples;
static void video(const void *data, unsigned width, unsigned height, size_t pitch)
{ assert(data && width == 64 && height == 64 && pitch == 256); ++frames; }
static size_t audio(const int16_t *data, size_t count)
{ assert(data && count == 800); samples += (unsigned)count; return count; }
int main(int argc, char **argv)
{
    assert(argc == 3);
#if defined(_WIN32)
    WCHAR path[4096];
    assert(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, argv[1], -1, path, 4096));
    HMODULE library = LoadLibraryW(path); assert(library);
#define SYMBOL(name) GetProcAddress(library, #name)
#else
    void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) fprintf(stderr, "%s\n", dlerror());
    assert(library);
#define SYMBOL(name) dlsym(library, #name)
#endif
#define LOAD(ret, name, args) ret (*name) args; do { \
    void *symbol = (void *)SYMBOL(name); assert(symbol); \
    _Static_assert(sizeof(name) == sizeof(symbol), "function pointer size"); \
    memcpy(&name, &symbol, sizeof(name)); } while (0)
    LOAD(void, retro_set_environment, (retro_environment_t));
    LOAD(void, retro_set_video_refresh, (retro_video_refresh_t));
    LOAD(void, retro_set_audio_sample_batch, (retro_audio_sample_batch_t));
    LOAD(void, retro_get_system_info, (struct retro_system_info *));
    LOAD(void, retro_get_system_av_info, (struct retro_system_av_info *));
    LOAD(unsigned, retro_api_version, (void));
    LOAD(void, retro_init, (void));
    LOAD(void, retro_deinit, (void));
    LOAD(bool, retro_load_game, (const struct retro_game_info *));
    LOAD(void, retro_unload_game, (void));
    LOAD(void, retro_reset, (void));
    LOAD(void, retro_run, (void));
    retro_set_environment(environment); retro_set_video_refresh(video);
    retro_set_audio_sample_batch(audio);
    struct retro_system_info info; retro_get_system_info(&info);
    assert(info.need_fullpath && info.block_extract && !strcmp(info.valid_extensions, "arcprobe"));
    struct retro_system_av_info av; retro_get_system_av_info(&av);
    assert(av.timing.fps == 60 && av.timing.sample_rate == 48000);
    assert(retro_api_version() == RETRO_API_VERSION);
    retro_init();
    struct retro_game_info game = {.path = argv[2]};
    if (!retro_load_game(&game)) { retro_deinit(); puts("refused"); return 3; }
    puts("loaded"); fflush(stdout);
    int command;
    while ((command = getchar()) != EOF) {
        switch (command) {
        case 'f': retro_run(); assert(samples == frames * 800); puts("frame"); break;
        case 'r': retro_reset(); puts("reset"); break;
        case 'u': retro_unload_game(); puts("unloaded"); break;
        case 'l': assert(retro_load_game(&game)); puts("loaded"); break;
        case 'd': retro_deinit(); puts("deinit"); break;
        case 'x': _Exit(97);
        case 'q': retro_unload_game(); retro_deinit(); puts("quit"); fflush(stdout); return 0;
        default: continue;
        }
        fflush(stdout);
    }
    retro_unload_game(); retro_deinit();
    return 0;
}
