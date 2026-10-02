/* Disc-request authority and continuation tests. GPL-2.0-only. */
#include "arc_disc_request.h"
#include <assert.h>
#include <limits.h>
#include <string.h>

static ArcDiscSwi lookup(const char *name, unsigned candidate)
{
   ArcDiscSwi swi = {0};
   swi.swi = 0x20008u;
   swi.r0 = 5u;
   swi.address = 0x8000u;
   swi.mode = 0u;
   swi.user_sp = 0x10000u;
   swi.name_readable = true;
   swi.name = name;
   swi.candidate = candidate;
   return swi;
}
int main(void)
{
   char name[256];
   assert(arc_disc_request_name("ADFS::0.$.Maps.Level", "DiskOne", name));
   assert(!strcmp(name, "$.Maps.Level"));
   assert(arc_disc_request_name("adfs::diskone.$", "DiskOne", name));
   const char *invalid[] = {"$.Maps.Level", ":0.$.Maps", "adfs::1.$.Maps",
      "adfs::Other.$.Maps", "adfs::0.@.Maps", "adfs::0.^.Maps",
      "adfs::0.&.Maps", "adfs::0.%.Maps", "adfs::0.\\.Maps",
      "adfs::0.$.<var>", "adfs::0.$.M*ps", "adfs::0.$.M#ps",
      "adfs::0.$.One;Two", "adfs::0.$.", "adfs::0.$..Map",
      "adfs::0.$.12345678901", "adfs::0.$.Map:0",
      "adfs::0.$.Map$"};
   for (unsigned i = 0u; i < sizeof(invalid)/sizeof(invalid[0]); ++i)
      assert(!arc_disc_request_name(invalid[i], "DiskOne", name));
   ArcDiscOperation operation;
   const unsigned catalogue[] = {5u,13u,15u,17u,20u,21u,22u,23u};
   const unsigned load[] = {12u,14u,16u,255u};
   for (unsigned i = 0u; i < sizeof(catalogue)/sizeof(catalogue[0]); ++i)
      assert(arc_disc_request_operation(8u,catalogue[i],&operation) &&
             operation == ARC_DISC_CATALOGUE);
   for (unsigned i = 0u; i < sizeof(load)/sizeof(load[0]); ++i)
      assert(arc_disc_request_operation(8u,load[i],&operation) &&
             operation == ARC_DISC_LOAD);
   assert(!arc_disc_request_operation(8u,1u,&operation));
   assert(arc_disc_request_operation(13u,0x40u,&operation) &&
          operation == ARC_DISC_OPEN_DIR);
   assert(arc_disc_request_operation(13u,0xc4u,&operation) &&
          operation == ARC_DISC_OPEN_FILE);
   assert(!arc_disc_request_operation(13u,0x80u,&operation));
   assert(!arc_disc_request_operation(13u,0u,&operation));

   ArcDiscRequest request = {0};
   ArcDiscSwi swi = lookup("adfs::0.$.Maps.Level", 1u);
   arc_disc_request_swi(&request,&swi,"DiskOne");
   assert(request.armed && request.continuation == 0x8004u);
   ArcDiscResult result = arc_disc_request_continue(&request,0x8004u,0u,
      0x10000u,0u,false,true,0u,100u);
   assert(result.observed && result.observation == 1u && !result.trigger);
   arc_disc_request_swi(&request,&swi,"DiskOne");
   result = arc_disc_request_continue(&request,0x8004u,0u,
      0x10000u,0u,false,true,0u,200u);
   assert(result.trigger && result.observation == 2u);

   arc_disc_request_clear(&request);
   swi=lookup("adfs::0.$.Maps.Level",1u);
   swi.swi=13u; swi.r0=0xc4u;
   arc_disc_request_swi(&request,&swi,"DiskOne");
   result=arc_disc_request_continue(&request,0x8004u,3u,0x10000u,
      0u,false,true,0u,100u);
   assert(request.armed && !result.observed); /* SVC work is not a user instruction. */
   result=arc_disc_request_continue(&request,0x8004u,0u,0x10000u,
      0u,false,true,0u,100u);
   assert(result.observation==1u && !result.trigger);
   arc_disc_request_swi(&request,&swi,"DiskOne");
   result=arc_disc_request_continue(&request,0x8004u,0u,0x10000u,
      12u,false,true,0u,101u);
   assert(!result.observed && !request.observations);

   arc_disc_request_clear(&request);
   arc_disc_request_swi(&request,&swi,"DiskOne");
   result = arc_disc_request_continue(&request,0x9000u,0u,
      0x10000u,0u,false,true,0u,100u);
   assert(result.discarded && !request.armed);
   result = arc_disc_request_continue(&request,0x8004u,0u,
      0x10000u,0u,false,true,0u,200u);
   assert(!result.observed);
   arc_disc_request_swi(&request,&swi,"DiskOne");
   result = arc_disc_request_continue(&request,0x8004u,0u,
      0x10004u,0u,false,true,0u,300u);
   assert(result.discarded);

   arc_disc_request_swi(&request,&swi,"DiskOne");
   swi.swi = 0x400c7u;
   arc_disc_request_swi(&request,&swi,"DiskOne");
   assert(!request.armed && !request.observations);
   swi = lookup("adfs::0.$.Maps.Level",1u);
   arc_disc_request_swi(&request,&swi,"DiskOne");
   ArcDiscSwi upcall = {0};
   upcall.swi=0x20033u; upcall.r0=2u; upcall.r1=8u; upcall.r3=UINT_MAX;
   upcall.mode=3u; upcall.in_rom=true; upcall.name_readable=true;
   upcall.name="DiskTwo";
   arc_disc_request_swi(&request,&upcall,"DiskOne");
   assert(request.a_active && !request.armed && !request.observations);
   upcall.r0=4u;
   arc_disc_request_swi(&request,&upcall,"DiskOne");
   assert(!request.a_active);

   swi = lookup("adfs::0.$.Maps.Level",1u);
   swi.r0=255u;
   arc_disc_request_swi(&request,&swi,"DiskOne");
   result = arc_disc_request_continue(&request,0x8004u,0u,
      0x10000u,0x2000u,true,true,0xd6u,400u);
   assert(result.observed && result.observation == 1u);
   arc_disc_request_swi(&request,&swi,"DiskOne");
   result = arc_disc_request_continue(&request,0x8004u,0u,
      0x10000u,0x2000u,true,true,0x108d3u,500u);
   assert(!result.observed && !result.trigger);

   /* The A signal requires an actual ROM SVC call, ADFS and a bounded name. */
   upcall.r0=1u; upcall.name="Second";
   arc_disc_request_clear(&request);
   arc_disc_request_swi(&request,&upcall,"DiskOne");
   assert(request.a_active && !strcmp(request.a_name,"Second"));
   upcall.in_rom=false; arc_disc_request_clear(&request);
   arc_disc_request_swi(&request,&upcall,"DiskOne"); assert(!request.a_active);
   upcall.in_rom=true; upcall.mode=0u;
   arc_disc_request_swi(&request,&upcall,"DiskOne"); assert(!request.a_active);
   upcall.mode=3u; upcall.r1=7u;
   arc_disc_request_swi(&request,&upcall,"DiskOne"); assert(!request.a_active);
   upcall.r1=8u; upcall.r3=1u;
   arc_disc_request_swi(&request,&upcall,"DiskOne"); assert(!request.a_active);
   upcall.r3=0u; upcall.name="12345678901";
   arc_disc_request_swi(&request,&upcall,"DiskOne"); assert(!request.a_active);
   upcall.name="Second"; upcall.r0=4u;
   arc_disc_request_swi(&request,&upcall,"DiskOne"); assert(!request.a_active);

   /* Each observation must return through the exact user continuation. */
   swi=lookup("adfs::0.$.Maps.Level",1u);
   arc_disc_request_swi(&request,&swi,"DiskOne");
   result=arc_disc_request_continue(&request,0x8004u,0u,0x10000u,
      0u,false,true,0u,100u);
   assert(result.observation==1u);
   arc_disc_request_swi(&request,&swi,"DiskOne");
   result=arc_disc_request_continue(&request,0x8004u,0u,0x10000u,
      0u,false,true,0u,UINT64_C(120000101));
   assert(result.observation==1u && !result.trigger);
   arc_disc_request_swi(&request,&swi,"DiskOne");
   result=arc_disc_request_continue(&request,0x8004u,0u,0x10000u,
      1u,false,true,0u,UINT64_C(120000102));
   assert(!result.observed && !request.observations);
   arc_disc_request_swi(&request,&swi,"DiskOne");
   result=arc_disc_request_continue(&request,0x8004u,0u,0x10000u,
      0u,false,true,0u,UINT64_C(120000103));
   assert(result.observation==1u);
   swi.swi=0x400c7u; arc_disc_request_swi(&request,&swi,"DiskOne");
   assert(!request.armed && !request.observations);

   /* An unreadable or unrelated error block is never a not-found outcome. */
   swi=lookup("adfs::0.$.Maps.Level",1u); swi.r0=255u;
   arc_disc_request_swi(&request,&swi,"DiskOne");
   result=arc_disc_request_continue(&request,0x8004u,0u,0x10000u,
      0x2000u,true,false,0u,200u);
   assert(!result.observed);
   arc_disc_request_swi(&request,&swi,"DiskOne");
   result=arc_disc_request_continue(&request,0x8004u,0u,0x10000u,
      0x2000u,true,true,0x108d3u,201u);
   assert(!result.observed);
   swi.swi=13u; swi.r0=0x80u;
   arc_disc_request_swi(&request,&swi,"DiskOne"); assert(!request.armed);

   const uint32_t episode_end[] = {0x400c7u,0x400e1u,0x400c0u,
      0x400ddu,0x400deu,0x11u,0x40u};
   for (unsigned i=0u; i<sizeof(episode_end)/sizeof(episode_end[0]); ++i)
   {
      arc_disc_request_clear(&request);
      swi=lookup("adfs::0.$.Maps.Level",1u);
      arc_disc_request_swi(&request,&swi,"DiskOne");
      result=arc_disc_request_continue(&request,0x8004u,0u,0x10000u,
         0u,false,true,0u,100u);
      assert(result.observation==1u);
      swi.swi=episode_end[i];
      arc_disc_request_swi(&request,&swi,"DiskOne");
      assert(!request.armed && !request.observations);
   }
   arc_disc_request_clear(&request);
   swi=lookup("adfs::0.$.Maps.Level",1u);
   arc_disc_request_swi(&request,&swi,"DiskOne");
   ArcDiscSwi other=lookup("adfs::0.$.Other",UINT_MAX);
   arc_disc_request_swi(&request,&other,"DiskOne");
   assert(!request.armed);
   result=arc_disc_request_continue(&request,0x8004u,0u,0x10000u,
      0u,false,true,0u,100u);
   assert(!result.observed); /* the old watch cannot resume */

   arc_disc_request_clear(&request);
   arc_disc_request_swi(&request,&swi,"DiskOne");
   result=arc_disc_request_continue(&request,0x8004u,0u,0x10000u,
      0u,false,true,0u,100u);
   assert(result.observation==1u);
   other=lookup("adfs::0.$.Other",1u);
   arc_disc_request_swi(&request,&other,"DiskOne");
   result=arc_disc_request_continue(&request,0x8004u,0u,0x10000u,
      0u,false,true,0u,101u);
   assert(result.observation==1u && !result.trigger);
   other=swi; other.address=0x8100u;
   arc_disc_request_swi(&request,&other,"DiskOne");
   result=arc_disc_request_continue(&request,0x8104u,0u,0x10000u,
      0u,false,true,0u,102u);
   assert(result.observation==1u && !result.trigger);
   return 0;
}
