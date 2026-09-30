/* Numbered BASIC text copied to the launch disc; all paths come from the
 * runtime discovery and the current content session. GPL-2.0-only. */
#include "hd_program.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct { uint8_t *out; size_t capacity, size; bool ok; } Writer;
static void line(Writer *w, unsigned number, const char *format, ...)
{
   char body[512], rendered[540];
   va_list args;
   va_start(args,format);
   int b=vsnprintf(body,sizeof(body),format,args);
   va_end(args);
   if (b < 0 || (size_t)b >= sizeof(body)) { w->ok=false; return; }
   int n=snprintf(rendered,sizeof(rendered),"%u %s\r",number,body);
   if (n < 0 || (size_t)n >= sizeof(rendered) ||
       (size_t)n > SIZE_MAX-w->size) { w->ok=false; return; }
   if (w->out) {
      if (w->size > w->capacity || (size_t)n > w->capacity-w->size)
      { w->ok=false; return; }
      memcpy(w->out+w->size,rendered,(size_t)n);
   }
   w->size+=(size_t)n;
}
static bool literal_safe(const char *s, size_t maximum)
{
   if (!s || !*s || strlen(s)>maximum) return false;
   for (const unsigned char *p=(const unsigned char *)s; *p; ++p)
      if (*p < 0x20u || *p == 0x7fu || *p == '"') return false;
   return true;
}
static void field(Writer *w, unsigned *number, const char *name,
                  const char *value)
{
   line(w,*number,"field$=\"%s=%s\":GOSUB 5200",name,value);
   *number+=10u;
}
static void record(Writer *w, unsigned first, const ArcHdProgramSpec *spec,
                    const char *state, bool product, bool failed)
{
   line(w,first,"field$=\"ARCINST1\":GOSUB 5200"); first+=10u;
   field(w,&first,"revision","1");
   field(w,&first,"state",state);
   line(w,first,"field$=\"attempt=\"+STR$(attempt%%):GOSUB 5200"); first+=10u;
   field(w,&first,"source",spec->source);
   field(w,&first,"media",spec->media);
   field(w,&first,"fingerprint",spec->fingerprint);
   field(w,&first,"content",spec->content);
   field(w,&first,"os",spec->os);
   if (product) {
      line(w,first,"field$=\"product=\"+product$:GOSUB 5200"); first+=10u;
   }
   if (failed) line(w,first,"field$=\"error=\"+error$:GOSUB 5200");
}
bool arc_hd_program_build(ArcHdHelperMode mode,
                           const ArcHdProgramSpec *spec,
                           uint8_t *out, size_t capacity, size_t *size)
{
   if (size) *size=0u;
   if ((mode != ARC_HD_HELPER_INSTALL && mode != ARC_HD_HELPER_MARK) ||
       !spec || !literal_safe(spec->source,128u) ||
       !literal_safe(spec->target,10u) ||
       !literal_safe(spec->media,40u) ||
       !literal_safe(spec->fingerprint,40u) ||
       !literal_safe(spec->content,40u) ||
       !literal_safe(spec->os,20u) ||
       (mode == ARC_HD_HELPER_MARK &&
        (!literal_safe(spec->product,10u) || !spec->attempt))) return false;
   Writer w={out,capacity,0u,true};
   line(&w,10,"ON ERROR GOTO 9000");
   line(&w,20,"file%%=0:owned%%=0:root$=\"ADFS::4.$.ArcDroid\"");
   line(&w,30,"attempt%%=%u",mode==ARC_HD_HELPER_INSTALL?1u:spec->attempt);
   if (mode==ARC_HD_HELPER_INSTALL) {
      line(&w,40,"SYS \"OS_File\",5,root$ TO kind%%");
      line(&w,50,"IF kind%%<>0 THEN ERROR 100,\"Destination exists\"");
      line(&w,60,"OSCLI \"CDir \"+root$:owned%%=1");
      line(&w,90,"GOSUB 5000");
      record(&w,100,spec,"pending",false,false);
      line(&w,290,"GOSUB 5300");
      line(&w,310,"OSCLI \"UnSet Copy$Options\"");
      line(&w,320,"OSCLI \"Copy %s \"+root$+\".Copying R F ~C ~V\"",spec->source);
      line(&w,330,"OSCLI \"Rename \"+root$+\".Copying \"+root$+\".%s\"",spec->target);
      line(&w,390,"GOSUB 5000");
      record(&w,400,spec,"started",false,false);
      line(&w,590,"GOSUB 5300");
      line(&w,610,"OSCLI \"Filer_Boot \"+root$+\".%s\"",spec->target);
      line(&w,620,"OSCLI \"Dir \"+root$");
      line(&w,630,"OSCLI \"URD \"+root$");
      line(&w,640,"OSCLI \"Filer_Run \"+root$+\".%s\"",spec->target);
      line(&w,650,"QUIT");
   } else {
      line(&w,40,"file%%=OPENIN(root$+\".Installed\")");
      line(&w,50,"IF file%%=0 THEN ERROR 100,\"Record missing\"");
      line(&w,60,"GOSUB 5400:IF field$<>\"ARCINST1\" THEN ERROR 100,\"Record invalid\"");
      line(&w,70,"GOSUB 5400:IF field$<>\"revision=1\" THEN ERROR 100,\"Revision changed\"");
      line(&w,80,"GOSUB 5400:IF field$<>\"state=started\" THEN ERROR 100,\"State changed\"");
      line(&w,90,"GOSUB 5400:IF field$<>\"attempt=\"+STR$(attempt%%) THEN ERROR 100,\"Attempt changed\"");
      line(&w,100,"GOSUB 5400:IF field$<>\"source=%s\" THEN ERROR 100,\"Source changed\"",spec->source);
      line(&w,110,"GOSUB 5400:IF field$<>\"media=%s\" THEN ERROR 100,\"Media changed\"",spec->media);
      line(&w,120,"GOSUB 5400:IF field$<>\"fingerprint=%s\" THEN ERROR 100,\"Candidate changed\"",spec->fingerprint);
      line(&w,130,"GOSUB 5400:IF field$<>\"content=%s\" THEN ERROR 100,\"Content changed\"",spec->content);
      line(&w,140,"GOSUB 5400:IF field$<>\"os=%s\" THEN ERROR 100,\"OS changed\"",spec->os);
      line(&w,150,"IF NOT EOF#file%% THEN ERROR 100,\"Record has extra fields\"");
      line(&w,160,"CLOSE#file%%:file%%=0");
      line(&w,170,"owned%%=1:product$=\"%s\"",spec->product);
      line(&w,190,"GOSUB 5000");
      record(&w,200,spec,"completed",true,false);
      line(&w,390,"GOSUB 5300");
      line(&w,410,"OSCLI \"Filer_Boot \"+root$+\".\"+product$");
      line(&w,420,"OSCLI \"Dir \"+root$");
      line(&w,430,"OSCLI \"URD \"+root$");
      line(&w,440,"OSCLI \"Filer_Run \"+root$+\".\"+product$");
      line(&w,450,"QUIT");
   }
   line(&w,5000,"file%%=OPENOUT(root$+\".Installed~\"):RETURN");
   line(&w,5200,"BPUT#file%%,field$;:BPUT#file%%,13:RETURN");
   line(&w,5300,"CLOSE#file%%:file%%=0");
   line(&w,5310,"SYS \"OS_File\",5,root$+\".Installed\" TO kind%%");
   line(&w,5320,"IF kind%%<>0 THEN OSCLI \"Remove \"+root$+\".Installed\"");
   line(&w,5330,"OSCLI \"Rename \"+root$+\".Installed~ \"+root$+\".Installed\"");
   line(&w,5340,"RETURN");
   line(&w,5400,"field$=\"\":byte%%=0");
   line(&w,5410,"WHILE byte%%<>13");
   line(&w,5420,"IF EOF#file%% THEN ERROR 100,\"Record ended\"");
   line(&w,5430,"byte%%=BGET#file%%");
   line(&w,5440,"IF byte%%<>13 AND LEN(field$)>=160 THEN ERROR 100,\"Field too long\"");
   line(&w,5450,"IF byte%%<>13 THEN field$+=CHR$(byte%%)");
   line(&w,5460,"ENDWHILE");
   line(&w,5470,"RETURN");
   line(&w,9000,"ON ERROR OFF");
   line(&w,9010,"IF file%%<>0 THEN CLOSE#file%%:file%%=0");
   line(&w,9020,"IF owned%%=0 THEN QUIT");
   line(&w,9030,"error$=LEFT$(REPORT$,80)");
   line(&w,9040,"ON ERROR GOTO 9390");
   line(&w,9050,"GOSUB 5000");
   record(&w,9100,spec,"failed",false,true);
   line(&w,9300,"GOSUB 5300");
   line(&w,9390,"QUIT");
   if (size) *size=w.size;
   return w.ok;
}
