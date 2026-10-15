/* Flag-free frontend link-time allocator injection. GPL-2.0-only. */
#include "libretro.h"
#include "arc_session.h"
#include "arcdroid_engine.h"
#include <assert.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#if defined(ARCDROID_TEST_FAULTS) || defined(ARCDROID_TEST_PLANS)
#error Exhaustion must be reachable without test flags
#endif
static unsigned allocations,notices,io,build_lines,identity_failures;
static void log_line(enum retro_log_level level,const char *format,...)
{
    (void)level;char line[1024];va_list args;va_start(args,format);vsnprintf(line,sizeof(line),format,args);va_end(args);
    if(strstr(line,"identity exhausted")){assert(level==RETRO_LOG_ERROR);++identity_failures;}
    if(strstr(line,"ArcDroid build:")){assert(strstr(line,"substitutions=session-id-exhaustion"));++build_lines;}
}
bool arc_session_id_allocate(ArcSessionIdSource *source,uint64_t *out)
{ (void)source;++allocations;*out=0;return false; }
static struct retro_vfs_file_handle *unexpected_open(const char *path,unsigned mode,unsigned hint)
{(void)path;(void)mode;(void)hint;++io;return NULL;}
static struct retro_vfs_interface vfs={.open=unexpected_open};
static bool environment(unsigned command,void *data)
{
    switch(command){
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:((struct retro_log_callback *)data)->log=log_line;return true;
    case RETRO_ENVIRONMENT_GET_VFS_INTERFACE:((struct retro_vfs_interface_info *)data)->iface=&vfs;return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:*(const char **)data="/save";return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:return true;
    case RETRO_ENVIRONMENT_SET_MESSAGE_EXT:{struct retro_message_ext *m=data;
        (void)m;++notices;return true;}
    default:return false;
    }
}
int main(void)
{
    retro_set_environment(environment);retro_init();assert(build_lines==1);struct retro_game_info game={.path="/game.adf"};
    for(unsigned i=0;i<3;++i){
        assert(!retro_load_game(&game));assert(allocations==i+1&&identity_failures==i+1&&!notices&&!io);
        assert(arc_frontend_session_kind()==ARC_SESSION_KIND_EMPTY&&!arc_frontend_session_id());
        assert(!arc_engine_active()&&!arc_engine_is_rpc());
        retro_reset();retro_unload_game();
    }
    retro_deinit();return 0;
}
