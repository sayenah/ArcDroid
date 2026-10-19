/* Output ownership, ordering and exhaustion policy, without a guest. GPL-2.0-only. */
#include "libretro.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#define ARC_RPC_ORDINARY_LOAD 1
#define ARC_PLAYLIST_MAX_ENTRIES 32
#define ARC_MEDIA_PATH_CAPACITY 4096
static unsigned logs,notices,private_lines;
static enum retro_log_level last;
static void log_capture(enum retro_log_level level,const char *format,...)
{
    char text[32768];va_list args;va_start(args,format);vsnprintf(text,sizeof(text),format,args);va_end(args);
    ++logs;last=level;if(strstr(text,"STAGING-SENTINEL"))++private_lines;
}
static bool environment(unsigned command,void *data){(void)data;if(command==RETRO_ENVIRONMENT_SET_MESSAGE_EXT){++notices;return true;}return false;}
static retro_log_printf_t arc_log=log_capture;
static retro_environment_t arc_environment=environment;
#include "../libretro/rpc_staging.h"
static void clear(void){assert(arc_log==log_capture && !arc_rpc_staging.active && !arc_rpc_staging.overflowed);logs=notices=private_lines=0;}
int main(void)
{
    arc_rpc_staging.bytes[ARC_RPC_STAGE_BYTES-1]='z';
    arc_rpc_staging_begin();assert(arc_rpc_staging.bytes[ARC_RPC_STAGE_BYTES-1]=='z');
    arc_rpc_staging_capture("used bytes",RETRO_LOG_INFO,false);
    size_t extent=arc_rpc_staging.used;arc_rpc_staging_finish(false);
    for(size_t i=0;i<extent;++i)assert(!arc_rpc_staging.bytes[i]);
    assert(arc_rpc_staging.bytes[ARC_RPC_STAGE_BYTES-1]=='z');clear();
    char path[8192];memset(path,'x',sizeof(path)-1);path[sizeof(path)-1]=0;memcpy(path,"STAGING-SENTINEL",16);
    arc_rpc_staging_begin();arc_log(RETRO_LOG_INFO,"%s",path);memset(path,'y',sizeof(path)-1);
    arc_rpc_staging_capture("final STAGING-SENTINEL error",RETRO_LOG_ERROR,true);
    assert(!logs && !notices);arc_rpc_staging_finish(false);assert(logs==2 && notices==1 && private_lines==2 && last==RETRO_LOG_ERROR);clear();
    arc_rpc_staging_begin();arc_log(RETRO_LOG_WARN,"discarded STAGING-SENTINEL candidate");arc_rpc_staging.flagged=true;
    arc_rpc_staging_capture("final STAGING-SENTINEL error",RETRO_LOG_ERROR,true);arc_rpc_staging_finish(true);
    assert(logs==2 && notices==1 && !private_lines && last==RETRO_LOG_ERROR);clear();
    for(unsigned flagged=0;flagged<2;++flagged){
        arc_rpc_staging_begin();arc_rpc_staging.flagged=flagged;
        for(unsigned i=0;i<ARC_RPC_STAGE_EVENTS+3;++i)arc_log(RETRO_LOG_INFO,"STAGING-SENTINEL %u",i);
        arc_rpc_staging_capture("STAGING-SENTINEL returned error",RETRO_LOG_ERROR,true);
        assert(logs==ARC_RPC_STAGE_EVENTS+4 && notices==1 && (flagged?!private_lines:private_lines==logs));
        arc_rpc_staging_finish(flagged);clear();
    }
    arc_rpc_staging_begin();
    for(unsigned i=0;i<=ARC_RPC_STAGE_EVENTS;++i)arc_log(RETRO_LOG_INFO,"STAGING-SENTINEL %u",i);
    unsigned permitted=private_lines;assert(permitted==ARC_RPC_STAGE_EVENTS+1 && arc_rpc_staging.overflowed);
    arc_rpc_staging_flag();arc_log(RETRO_LOG_WARN,"later STAGING-SENTINEL flagged member");
    arc_rpc_staging_capture("later STAGING-SENTINEL failure",RETRO_LOG_ERROR,true);
    assert(private_lines==permitted && logs==permitted+2 && notices==1 && last==RETRO_LOG_ERROR);
    arc_rpc_staging_finish(true);clear();
    arc_rpc_staging_begin();arc_rpc_staging.flagged=true;arc_rpc_staging.used=ARC_RPC_STAGE_BYTES-1;
    arc_log(RETRO_LOG_ERROR,"STAGING-SENTINEL byte exhaustion");arc_rpc_staging_finish(true);assert(logs==1 && !private_lines);clear();
    puts("rpc-staging: copied payloads, raw/safe failure, event/byte exhaustion, severity and cleanup pass");return 0;
}
