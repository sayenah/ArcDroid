/* Byte-exact helper and BASIC goldens use invented paths. GPL-2.0-only. */
#include "arcdroid_core.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void golden(const char *label, const uint8_t *data, size_t size,
                   const char *expected)
{
   char digest[65];
   arc_sha256_hex(data,size,digest);
   if (strcmp(digest,expected))
      fprintf(stderr,"%s: got %s\n",label,digest);
   assert(!strcmp(digest,expected));
}
static void check_basic(const uint8_t *program, size_t size)
{
   char text[8193];
   assert(size < sizeof(text));
   memcpy(text,program,size); text[size]=0;
   assert(strstr(text,"STR$(attempt%)") &&
          !strstr(text,"STR$(attempt)") &&
          !strstr(text,"%%") &&
          !strstr(text,"data$") && !strstr(text,"raw$") &&
          strstr(text,"BPUT#file%,field$;:BPUT#file%,13") &&
          strstr(text,"SYS \"Wimp_Initialise\",200,&4B534154") &&
          strstr(text,"file%=0:owned%=0:task%=0") &&
          strstr(text,"SYS \"Wimp_StartTask\",\"Run \"+root$") &&
          strstr(text,"IF task%<>0 THEN SYS \"Wimp_CloseDown\",task%,&4B534154") &&
          strstr(text,"IF owned%=0 THEN GOTO 9390") &&
          strstr(text,"ON ERROR GOTO 9390") &&
          strstr(text,"GOTO 9390") && !strstr(text,"Filer_Run"));
   const char *quit=strstr(text,"QUIT");
   assert(quit && !strstr(quit+4,"QUIT") &&
          quit>strstr(text,"Wimp_CloseDown"));
   const char *at=text;
   while (*at) {
      const char *end=strchr(at,'\r');
      assert(end && (size_t)(end-at)<255u);
      at=end+1u;
   }
}
int main(void)
{
   ArcLaunchDiscovery discovery={0};
   discovery.candidate_count=1u;
   ArcLaunchCandidate *candidate=&discovery.candidates[0];
   const char *parent="Place-2", *leaf="!Mock_2";
   candidate->parent.length=(uint8_t)strlen(parent);
   memcpy(candidate->parent.bytes,parent,candidate->parent.length);
   candidate->parent_address=candidate->parent;
   candidate->name.length=(uint8_t)strlen(leaf);
   memcpy(candidate->name.bytes,leaf,candidate->name.length);
   candidate->state=ARC_LAUNCH_CANDIDATE_APPLICATION;
   candidate->launchable=candidate->addressable=true;
   memcpy(discovery.candidate_fingerprint,
          "abcdefabcdefabcdefabcdefabcdefabcdefabcd",41u);
   ArcHdProgramSpec spec={"ADFS::0.$.Place-2.!Mock_2","!Mock_2","!Output",
      "0123456789012345678901234567890123456789",
      "abcdefabcdefabcdefabcdefabcdefabcdefabcd",
      "fedcbafedcbafedcbafedcbafedcbafedcbafedc","riscos311",1u};
   uint8_t program[8192]; size_t size=0u;
   assert(arc_hd_program_build(ARC_HD_HELPER_INSTALL,&spec,program,
                               sizeof(program),&size));
   check_basic(program,size);
   golden("install-basic",program,size,
      "014d1e64f7d28352d916c296f9964346ebc83de077c6c9207184eae6ab4248c6");
   char text[8193];
   memcpy(text,program,size); text[size]=0;
   const char *check=strstr(text,"SYS \"OS_File\",5,root$ TO kind%");
   const char *guard=strstr(text,"IF kind%<>0 THEN ERROR 100,\"Destination exists\"");
   const char *make=strstr(text,"OSCLI \"CDir \"+root$");
   const char *pending=strstr(text,"state=pending");
   const char *copy=strstr(text,"Copy ADFS::0.$.Place-2.!Mock_2");
   const char *started=strstr(text,"state=started");
   const char *run=strstr(text,"SYS \"Wimp_StartTask\",\"Run \"+root$+\".!Mock_2.!Run\"");
   const char *remove=strstr(text,"Remove \"+root$+\".Installed");
   const char *rename=strstr(text,"Rename \"+root$+\".Installed~ ");
   assert(check && guard && make && pending && copy && started && run &&
          check<guard && guard<make && make<pending && pending<copy && copy<started &&
          started<run && remove && rename && remove<rename);
   assert(arc_hd_program_build(ARC_HD_HELPER_MARK,&spec,program,
                               sizeof(program),&size));
   golden("mark-basic",program,size,
      "61bf85c71a7226ea70883f1f27307c23d8c7d7a6b93cb9306d607b4efcb16c52");
   memcpy(text,program,size); text[size]=0;
   check_basic(program,size);
   assert(strstr(text,"\"ArcDroid mark\" TO ,task%"));
   assert(strstr(text,"state=started") && strstr(text,"attempt=\"+STR$(attempt%)") &&
          strstr(text,"state=completed") && strstr(text,"product=\"+product$") &&
          strstr(text,"Remove \"+root$+\".Installed") &&
          strstr(text,"Rename \"+root$+\".Installed~ "));
   ArcHdProgramSpec extended={"ADFS::0.$.Dir\x80.!Qu\x81rk",
      "!Qu\x81rk","!Res\x82lt",spec.media,spec.fingerprint,
      spec.content,spec.os,3u};
   assert(arc_hd_program_build(ARC_HD_HELPER_INSTALL,&extended,program,
                               sizeof(program),&size));
   check_basic(program,size);
   golden("extended-install-basic",program,size,
      "41518571037c22a0a531eaf152ed90804b8f797a12a5cc05b290550e5f8b7e6e");
   assert(arc_hd_program_build(ARC_HD_HELPER_MARK,&extended,program,
                               sizeof(program),&size));
   check_basic(program,size);
   golden("extended-mark-basic",program,size,
      "9548b39c6639b77f48aa06a75296717967efe324d936975a6fd7102494779e22");
   char source[129], target[11], product_name[11], forty[41], os[21];
   memset(source,'S',128u); source[128]=0;
   memcpy(target,"!ABCDEFGHI",11u);
   memcpy(product_name,"!JKLMNOPQR",11u);
   memset(forty,'a',40u); forty[40]=0;
   memset(os,'o',20u); os[20]=0;
   ArcHdProgramSpec maximum={source,target,product_name,forty,forty,
      forty,os,4294967295u};
   const size_t root_length=strlen("ADFS::4.$.ArcDroid");
   assert(strlen("source=")+strlen(source)<=160u);
   assert(strlen("Copy ")+strlen(source)+1u+root_length+
          strlen(".Copying R F ~C ~V")<=255u);
   assert(strlen("Rename ")+2u*root_length+
          strlen(".Copying ")+1u+strlen(target)<=255u);
   assert(strlen("Run ")+root_length+1u+strlen(target)+
          strlen(".!Run")<=255u);
   assert(strlen("error=")+80u<=255u);
   assert(arc_hd_program_build(ARC_HD_HELPER_INSTALL,&maximum,program,
                               sizeof(program),&size));
   check_basic(program,size);
   assert(arc_hd_program_build(ARC_HD_HELPER_MARK,&maximum,program,
                               sizeof(program),&size));
   check_basic(program,size);
   memcpy(text,program,size); text[size]=0;
   assert(strstr(text,"LEN(field$)>=160"));
   uint8_t *disc=malloc(ARC_LAUNCH_DISC_SIZE);
   assert(disc);
   char error[160]={0};
   assert(arc_launch_disc_build_mode(&discovery,0u,ARC_HD_HELPER_INSTALL,
                                     &spec,disc,error,sizeof(error)));
   golden("install-disc",disc,ARC_LAUNCH_DISC_SIZE,
      "12cf250b4350f7bd068f2963d96400e0e14c57a0ac9d729064f773d0e2d0b9d5");
   assert(arc_launch_disc_build_mode(&discovery,0u,ARC_HD_HELPER_MARK,
                                     &spec,disc,error,sizeof(error)));
   golden("mark-disc",disc,ARC_LAUNCH_DISC_SIZE,
      "3fb8e15a560fb280ac88ed6ac7a41cfbc49fa39594b3dd98ad0fcda13f8c099b");
   assert(arc_launch_disc_build_mode(&discovery,0u,ARC_HD_HELPER_PRODUCT,
                                     &spec,disc,error,sizeof(error)));
   golden("product-disc",disc,ARC_LAUNCH_DISC_SIZE,
      "5ae25d27ed0f016a621eff58e36fa19a6d9dea907fe9b2edb95480ac5829c23d");
   ArcGuestName product={{'!','O','u','t','p','u','t'},7u};
   uint8_t choice[ARC_LAUNCH_CHOICE_MAX_SIZE]; size_t choice_size=0u;
   assert(arc_launch_choice_format_installed(&product,
      discovery.candidate_fingerprint,3u,choice,sizeof(choice),&choice_size));
   ArcLaunchChoiceRecord parsed;
   assert(arc_launch_choice_parse(choice,choice_size,&parsed));
   assert(parsed.installed && parsed.version==4u && parsed.attempt==3u &&
          arc_launch_choice_match(&discovery,&parsed)==-2);
   assert(arc_launch_choice_format(&product,false,
      discovery.candidate_fingerprint,choice,sizeof(choice),&choice_size));
   assert(arc_launch_choice_parse(choice,choice_size,&parsed));
   assert(!parsed.installed && arc_launch_choice_match(&discovery,&parsed)<0);
   free(disc);
   return 0;
}
