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
   golden("install-basic",program,size,
      "1cb52fd7a272c64817457468992907a60b3c1f7aa824753466c5f0b20ec2f2e8");
   char text[8193];
   memcpy(text,program,size); text[size]=0;
   const char *check=strstr(text,"SYS \"OS_File\",5,root$ TO kind%");
   const char *guard=strstr(text,"IF kind%<>0 THEN ERROR 100,\"Destination exists\"");
   const char *make=strstr(text,"OSCLI \"CDir \"+root$");
   const char *pending=strstr(text,"state=pending");
   const char *copy=strstr(text,"Copy ADFS::0.$.Place-2.!Mock_2");
   const char *started=strstr(text,"state=started");
   const char *run=strstr(text,"Filer_Run \"+root$");
   const char *remove=strstr(text,"Remove \"+root$+\".Installed");
   const char *rename=strstr(text,"Rename \"+root$+\".Installed~ ");
   assert(check && guard && make && pending && copy && started && run &&
          check<guard && guard<make && make<pending && pending<copy && copy<started &&
          started<run && remove && rename && remove<rename);
   assert(arc_hd_program_build(ARC_HD_HELPER_MARK,&spec,program,
                               sizeof(program),&size));
   golden("mark-basic",program,size,
      "8254ba9f6109cc6eb4a0b8b4b73abef73d0d528a2b5bb249e59a8624a67ac2dc");
   memcpy(text,program,size); text[size]=0;
   assert(strstr(text,"state=started") && strstr(text,"attempt=1") &&
          strstr(text,"state=completed") && strstr(text,"product=\"+product$") &&
          strstr(text,"Remove \"+root$+\".Installed") &&
          strstr(text,"Rename \"+root$+\".Installed~ "));
   ArcHdProgramSpec extended={"ADFS::0.$.Dir\x80.!Qu\x81rk",
      "!Qu\x81rk","!Res\x82lt",spec.media,spec.fingerprint,
      spec.content,spec.os,3u};
   assert(arc_hd_program_build(ARC_HD_HELPER_INSTALL,&extended,program,
                               sizeof(program),&size));
   golden("extended-install-basic",program,size,
      "09a8906ccb05e18aadaf8a7ff354d39c365af074ae2179484c3bb4ae095a0411");
   assert(arc_hd_program_build(ARC_HD_HELPER_MARK,&extended,program,
                               sizeof(program),&size));
   golden("extended-mark-basic",program,size,
      "a3b3c6eafd809a2a34e3d2c52d0c79cb9999c5ef45c6573ad86c108f2244c10f");
   uint8_t *disc=malloc(ARC_LAUNCH_DISC_SIZE);
   assert(disc);
   char error[160]={0};
   assert(arc_launch_disc_build_mode(&discovery,0u,ARC_HD_HELPER_INSTALL,
                                     &spec,disc,error,sizeof(error)));
   golden("install-disc",disc,ARC_LAUNCH_DISC_SIZE,
      "38df9fb3882bf28b3805c4a0cb70aca2a0cbf9765110d8f505724c84e520c257");
   assert(arc_launch_disc_build_mode(&discovery,0u,ARC_HD_HELPER_MARK,
                                     &spec,disc,error,sizeof(error)));
   golden("mark-disc",disc,ARC_LAUNCH_DISC_SIZE,
      "f71a807db52f1d71f2d4b6a6fc38beadf78124404c4dc83550f8cb238e7960a8");
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
