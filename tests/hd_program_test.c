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
          strstr(text,"BPUT#file%,field$;:BPUT#file%,13"));
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
      "7f746175b86d3f7b9048eadbaaf6a4fbf91cdfd9ab9f9afbe92ec46e97bb790d");
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
      "eb3af7aea78dd8c7c53356882076a4148a43b4abfd84352ddfe501c8bee47040");
   memcpy(text,program,size); text[size]=0;
   check_basic(program,size);
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
      "722da016d23b1dcbd49c340601816c054844818a54f2dd0e84e1b727bb743494");
   assert(arc_hd_program_build(ARC_HD_HELPER_MARK,&extended,program,
                               sizeof(program),&size));
   check_basic(program,size);
   golden("extended-mark-basic",program,size,
      "94a9c8e3415221295aa2efea23d347857babe3d788e662ecdc0c1599d509c837");
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
      "143929b50817f0262773ba4d228969fe8f0a36c05703de7dfb6e721ad0e7d88c");
   assert(arc_launch_disc_build_mode(&discovery,0u,ARC_HD_HELPER_MARK,
                                     &spec,disc,error,sizeof(error)));
   golden("mark-disc",disc,ARC_LAUNCH_DISC_SIZE,
      "5e83af7f85ed592f7d8f6477a30c55f9927bc77ef178b666ca7dd00b4c9ae630");
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
