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
   const char *options=strstr(text,"XOS_ReadVarVal");
   if (options) {
      const char *clear=strstr(text,"UnSet Copy$Options");
      const char *copy=strstr(text,"OSCLI \"Copy ");
      const char *restore=strstr(text,"GOSUB 5500");
      const char *on_error=strstr(text,"XOS_SetVarVal");
      assert(clear && copy && restore && on_error &&
             options<clear && clear<copy && copy<restore &&
             strstr(text,"copyneed%<-129") &&
             strstr(text,"copyhad%=(copyneed%<>0)") &&
             strstr(text,"SYS \"OS_SetVarVal\",\"Copy$Options\"") &&
             strstr(text,"IF copyhad%<>0 THEN SYS \"XOS_SetVarVal\""));
   }
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
      "d8fee347996c108a4aaebd2da0dfd1d50c6aaabd7a88bc0e3b015f2fdadfdadc");
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
      "31234118f43486bbcc43bcc402ab8c45cc7ad6c58f73660c0fb1f3ebdfbf5dfa");
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
      "7705823a8ef2822f375bf5bc35b2c7d531e6faa32e9c30f949212e858891c702");
   assert(arc_hd_program_build(ARC_HD_HELPER_MARK,&extended,program,
                               sizeof(program),&size));
   check_basic(program,size);
   golden("extended-mark-basic",program,size,
      "d3b598367856160af072db66c053b4d6e2a6525ac312c8edfd6cf54ed126e466");
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
      "e6551871066279e5e995b805526b7530888f4832784cbcb288366da0f84b293d");
   assert(arc_launch_disc_build_mode(&discovery,0u,ARC_HD_HELPER_MARK,
                                     &spec,disc,error,sizeof(error)));
   golden("mark-disc",disc,ARC_LAUNCH_DISC_SIZE,
      "5b523fddc1a07c074f3cb5dc392e467525ba40b58f9d71f5fd2525310ff6804b");
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
