/* Real plan-core stop and runner refusal cleanup. GPL-2.0-only. */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include <dlfcn.h>
static void *test_symbol(void *,const char *);
#define dlsym test_symbol
#else
#include <windows.h>
static FARPROC test_symbol(HMODULE,LPCSTR);
#define GetProcAddress test_symbol
#endif
static int hide_profile;
static unsigned cleanup_checks;
#define ARC_RUNNER_VFS_CHECK(files,dirs) do { ++cleanup_checks; assert((files)==0 && (dirs)==0); } while(0)
#define main runner_fixture_main
#include "../tools/rpc_runner.c"
#undef main
#ifdef _WIN32
#undef GetProcAddress
static FARPROC test_symbol(HMODULE handle,LPCSTR name)
{ if(hide_profile && !strcmp(name,"arc_rpc_profile_configure"))return NULL;return GetProcAddress(handle,name); }
#else
#undef dlsym
static void *test_symbol(void *handle,const char *name)
{ if(hide_profile && !strcmp(name,"arc_rpc_profile_configure"))return NULL;return dlsym(handle,name); }
#endif

int main(int argc,char **argv)
{
    assert(argc>=2);
    if(!strcmp(argv[1],"missing")){
        /* Exercise the real CLI with one missing API, retaining real VFS and
         * core teardown. The shim changes symbol lookup only in this fixture. */
        hide_profile=1;
        int result=runner_fixture_main(argc-1,argv+1);
        assert(result==2 && cleanup_checks==1);
#ifdef _WIN32
        FreeLibrary(library);
#else
        dlclose(library);
#endif
        puts("profile lifecycle: missing API checked VFS cleanup");return 0;
    }
    assert(argc==5 && !strcmp(argv[1],"stop"));
#ifdef _WIN32
    library=LoadLibraryA(argv[2]);
#else
    library=dlopen(argv[2],RTLD_NOW|RTLD_LOCAL);
#endif
    assert(library);system_directory=argv[4];assert(canonical(argv[4],save_directory));
    void (*set_env)(retro_environment_t),(*init)(void),(*deinit)(void),(*unload)(void),(*run)(void);
    bool (*load)(const struct retro_game_info *);
    void (*configure)(unsigned,uint64_t (*)(void));bool (*snapshot)(ArcRpcProfile *,unsigned,size_t);
    LOAD(set_env,"retro_set_environment");LOAD(init,"retro_init");LOAD(deinit,"retro_deinit");
    LOAD(load,"retro_load_game");LOAD(unload,"retro_unload_game");LOAD(run,"retro_run");
    LOAD(configure,"arc_rpc_profile_configure");LOAD(snapshot,"arc_rpc_profile_snapshot_v4");
    void (*set_video)(retro_video_refresh_t),(*set_audio)(retro_audio_sample_batch_t),(*set_poll)(retro_input_poll_t),(*set_input)(retro_input_state_t);
    LOAD(set_video,"retro_set_video_refresh");LOAD(set_audio,"retro_set_audio_sample_batch");
    LOAD(set_poll,"retro_set_input_poll");LOAD(set_input,"retro_set_input_state");
    set_env(environment);init();set_video(video);set_audio(audio);set_poll(poll);set_input(input);
    struct retro_game_info game={.path=argv[3]};assert(load(&game));configure(0,NULL);run();
    ArcRpcProfile before,after;assert(snapshot(&before,ARC_RPC_COUNTER_ABI,sizeof(before)));
    assert(before.attempts>0 && before.open_block>0 && before.resets==0);
    unload();assert(snapshot(&after,ARC_RPC_COUNTER_ABI,sizeof(after)));
    assert(after.resets==1 && after.epoch==1 && after.open_block==0);
    assert(after.attempts==before.attempts && after.censored_blocks==before.censored_blocks+1);
    assert(after.censored_instructions==before.censored_instructions+before.open_block);
    deinit();vfs_assert_no_open_handles();
#ifdef _WIN32
    FreeLibrary(library);
#else
    dlclose(library);
#endif
    puts("profile lifecycle: unload censored open block and reset epoch/history");return 0;
}
