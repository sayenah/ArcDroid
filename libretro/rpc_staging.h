/* Frontend-local, compile-time-closed output arena (R-3). GPL-2.0-only.
 * Producer census: arc_message, arc_media_notice and all arc_log calls under
 * arc_prepare_media_list/bytes/source/zip_content/folder_siblings,
 * arc_collect_folder_member/log_folder_result/folder_summary/zip_summary/zip_log, including
 * their returned error. 32 images, <=16 bounded lines per image, <=4096 path
 * bytes per line plus escaping and the single <=18000-byte ZIP order line.
 * Directory enumeration itself has no operation cap. Every producer uses
 * the explicit exhaustion policy, never heap growth.
 * Fixed storage cannot create an allocation-dependent load refusal. */
#if ARC_RPC_ORDINARY_LOAD
#define ARC_RPC_STAGE_EVENTS (ARC_PLAYLIST_MAX_ENTRIES * 16u + 32u)
#define ARC_RPC_STAGE_BYTES (ARC_RPC_STAGE_EVENTS * (ARC_MEDIA_PATH_CAPACITY * 2u + 1024u) + 18000u)
static struct {
    char bytes[ARC_RPC_STAGE_BYTES];
    struct {size_t offset;enum retro_log_level level;bool notice;} event[ARC_RPC_STAGE_EVENTS];
    size_t used;unsigned count;
    bool active,flagged,overflowed,safe_after_overflow;
    retro_log_printf_t log;
} arc_rpc_staging;
static void arc_rpc_staging_finish(bool rpc);
static void arc_rpc_staging_capture_log(enum retro_log_level,const char *,...);
static void arc_rpc_staging_flag(void)
{
    if(!arc_rpc_staging.active && !arc_rpc_staging.overflowed)return;
    arc_rpc_staging.flagged=true;
    if(arc_rpc_staging.overflowed)arc_rpc_staging.safe_after_overflow=true;
}
static bool arc_rpc_old_notice(const char *s)
{return !strcmp(s,"This title is flagged for RiscPC-class machines (StrongARM/RISC OS 3.5+), which ArcDroid does not emulate; booting anyway.") ||
    !strcmp(s,"This title is flagged for RiscPC-class machines (StrongARM/RISC OS 3.5+), which ArcDroid does not emulate; booting anyway.\n");}
static void arc_rpc_staging_emit(const char *s,enum retro_log_level level,bool notice,bool safe)
{
    if(safe && arc_rpc_old_notice(s))return;
    if(safe)s=notice?"Some discs in the selected content could not be loaded. Check the selected discs.":"status: RiscPC disc staging completed for the selected content.\n";
    if(arc_rpc_staging.log)arc_rpc_staging.log(level,notice?"%s\n":"%s",s);
    if(notice && arc_environment){
        struct retro_message_ext m={s,5000u,3u,level,safe?RETRO_MESSAGE_TARGET_OSD:RETRO_MESSAGE_TARGET_ALL,RETRO_MESSAGE_TYPE_NOTIFICATION,-1};
        if(!arc_environment(RETRO_ENVIRONMENT_SET_MESSAGE_EXT,&m)){
            struct retro_message legacy={s,300u};arc_environment(RETRO_ENVIRONMENT_SET_MESSAGE,&legacy);
        }
    }
}
static void arc_rpc_staging_capture(const char *s,enum retro_log_level level,bool notice)
{
    if(!arc_rpc_staging.active){arc_rpc_staging_emit(s,level,notice,arc_rpc_staging.safe_after_overflow);return;}
    size_t length=strlen(s)+1;
    if(arc_rpc_staging.count==ARC_RPC_STAGE_EVENTS || length>sizeof(arc_rpc_staging.bytes)-arc_rpc_staging.used){
        bool safe=arc_rpc_staging.flagged;
        arc_rpc_staging_finish(safe);
        arc_rpc_staging.overflowed=true;arc_rpc_staging.safe_after_overflow=safe;
        /* Raw output is permitted before a flag is known. Keep watching:
         * a later flagged member must switch subsequent events to safe forms. */
        if(arc_rpc_staging.log)arc_log=arc_rpc_staging_capture_log;
        arc_rpc_staging_emit(s,level,notice,safe);return;
    }
    unsigned at=arc_rpc_staging.count++;
    arc_rpc_staging.event[at].offset=arc_rpc_staging.used;
    arc_rpc_staging.event[at].level=level;arc_rpc_staging.event[at].notice=notice;
    memcpy(arc_rpc_staging.bytes+arc_rpc_staging.used,s,length);arc_rpc_staging.used+=length;
}
static void arc_rpc_staging_capture_log(enum retro_log_level level,const char *format,...)
{
    char text[32768];va_list args;va_start(args,format);vsnprintf(text,sizeof(text),format,args);va_end(args);
    arc_rpc_staging_capture(text,level,false);
}
static void arc_rpc_staging_begin(void)
{
    memset(&arc_rpc_staging,0,sizeof(arc_rpc_staging));
    arc_rpc_staging.log=arc_log;arc_rpc_staging.active=true;
    if(arc_log)arc_log=arc_rpc_staging_capture_log;
}
static void arc_rpc_staging_finish(bool rpc)
{
    if(!arc_rpc_staging.active){
        if(arc_rpc_staging.overflowed)arc_log=arc_rpc_staging.log;
        arc_rpc_staging.overflowed=arc_rpc_staging.safe_after_overflow=false;return;
    }
    arc_log=arc_rpc_staging.log;arc_rpc_staging.active=false;
    for(unsigned i=0;i<arc_rpc_staging.count;++i)
        arc_rpc_staging_emit(arc_rpc_staging.bytes+arc_rpc_staging.event[i].offset,arc_rpc_staging.event[i].level,arc_rpc_staging.event[i].notice,rpc);
    memset(arc_rpc_staging.bytes,0,arc_rpc_staging.used);arc_rpc_staging.used=arc_rpc_staging.count=0;
}
#endif
