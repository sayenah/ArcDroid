/* Exact-command launch-outcome state machine tests. GPL-2.0-only. */
#include "arc_launch_outcome.h"
#include <assert.h>
#include <string.h>

static const char command[]="Run ADFS::0.$.!Mock.!Run";
static void swi(ArcLaunchOutcome *o, uint32_t code, uint32_t r0,
                uint32_t r1, uint32_t r2, bool rom)
{ arc_outcome_swi(o,code,r0,r1,r2,rom,NULL,100u); }
static void start(ArcLaunchOutcome *o, const char *s, bool rom)
{ arc_outcome_swi(o,0x400deu,0u,0u,0u,rom,s,100u); }
static void end(ArcLaunchOutcome *o)
{
   swi(o,0x11u,0u,0u,0u,true);
   swi(o,0x400ddu,0u,42u,0u,true);
}
int main(void)
{
   ArcLaunchOutcome o;
   const char *other[]={"Filer_Boot ADFS::0.$.!Mock",
      "Dir ADFS::0.$", "URD ADFS::0.$",
      "Filer_Run ADFS::0.$.!Mock",
      "Run ADFS::0.$.!Moc.!Run",
      "Run ADFS::0.$.!Mocker.!Run"};
   for (unsigned i=0u; i<sizeof(other)/sizeof(other[0]); ++i) {
      assert(arc_outcome_arm(&o,command,0u));
      start(&o,other[i],true);
      assert(o.state == ARC_OUTCOME_WAIT);
   }
   assert(arc_outcome_arm(&o,command,0u));
   swi(&o,0x11u,0u,0x58454241u,3u,false);
   assert(o.state == ARC_OUTCOME_WAIT && o.failed_exits == 1u &&
          o.last_rc == 3u);
   swi(&o,0x400c0u,0u,0u,0u,false);
   assert(o.state == ARC_OUTCOME_WAIT);
   start(&o,command,false);
   assert(o.state == ARC_OUTCOME_EXIT && o.matched);
   swi(&o,0x400ddu,42u,0u,0u,false);
   assert(o.state == ARC_OUTCOME_EXIT);
   swi(&o,0x11u,0u,0u,0u,true);
   swi(&o,0x400ddu,0u,0u,0u,true);
   assert(o.state == ARC_OUTCOME_END);
   swi(&o,0x400ddu,42u,0u,0u,false);
   assert(o.state == ARC_OUTCOME_END);
   swi(&o,0x400c7u,0u,0u,0u,true);
   assert(o.state == ARC_OUTCOME_POLL);
   assert(arc_outcome_arm(&o,command,0u));
   arc_outcome_refused(&o,0u);
   arc_outcome_refused(&o,4u);
   assert(o.refused_floppy == 0u && o.refused_hd == 0u);
   start(&o,command,true);
   assert(o.state == ARC_OUTCOME_EXIT);
   start(&o,command,true);
   assert(o.state == ARC_OUTCOME_CANCEL);
   assert(arc_outcome_arm(&o,command,0u));
   start(&o,command,true);
   start(&o,other[0],true);
   assert(o.state == ARC_OUTCOME_CANCEL);
   assert(arc_outcome_arm(&o,command,0u));
   start(&o,command,true);
   arc_outcome_refused(&o,0u);
   end(&o);
   assert(o.state == ARC_OUTCOME_END);
   swi(&o,0x400c7u,0u,0u,0u,true);
   assert(o.state == ARC_OUTCOME_POLL && o.refused_floppy == 1u);
   arc_outcome_refused(&o,0u);
   assert(o.refused_floppy == 2u);
   arc_outcome_cancel(&o,"input");
   assert(o.state == ARC_OUTCOME_CANCEL);
   const uint32_t cancels[]={0x400c0u,0x400dfu};
   for (unsigned i=0u;i<sizeof(cancels)/sizeof(cancels[0]);++i)
      for (unsigned from_rom=0u;from_rom<2u;++from_rom) {
         assert(arc_outcome_arm(&o,command,0u));
         start(&o,command,true);
         arc_outcome_refused(&o,0u);
         swi(&o,cancels[i],0u,0u,0u,from_rom!=0u);
         assert(o.state==ARC_OUTCOME_CANCEL);
      }
   assert(arc_outcome_arm(&o,command,0u));
   start(&o,command,true);
   arc_outcome_refused(&o,0u);
   arc_outcome_cancel(&o,"input");
   assert(o.state==ARC_OUTCOME_CANCEL);
   assert(arc_outcome_arm(&o,command,0u));
   start(&o,command,true);
   arc_outcome_cancel(&o,"media");
   assert(o.state==ARC_OUTCOME_CANCEL);
   assert(arc_outcome_arm(&o,command,0u));
   start(&o,command,true);
   arc_outcome_cancel(&o,"reset");
   assert(o.state==ARC_OUTCOME_CANCEL);
   assert(arc_outcome_arm(&o,command,0u));
   start(&o,command,true);
   end(&o);
   assert(o.state == ARC_OUTCOME_END);
   swi(&o,0x400c0u,0u,0u,0u,false);
   assert(o.state == ARC_OUTCOME_CANCEL);
   assert(arc_outcome_arm(&o,command,0u));
   start(&o,command,true);
   swi(&o,0x11u,0u,0x58454241u,7u,true);
   swi(&o,0x11u,0u,0x58454241u,0u,true);
   swi(&o,0x400ddu,0u,0u,0u,true);
   swi(&o,0x400e1u,0u,0u,0u,true);
   assert(o.state == ARC_OUTCOME_POLL && o.failed_exits == 1u &&
          o.has_rc && o.last_rc == 7u);
   swi(&o,0x11u,0u,0x58454241u,9u,true);
   assert(o.failed_exits == 2u && o.last_rc == 9u);
   assert(arc_outcome_arm(&o,command,0u));
   start(&o,command,true);
   end(&o);
   assert(o.state == ARC_OUTCOME_END);
   swi(&o,0x11u,0u,0x58454241u,11u,true);
   assert(o.failed_exits == 1u && o.last_rc == 11u);
   swi(&o,0x400c7u,0u,0u,0u,true);
   assert(o.state == ARC_OUTCOME_POLL);
   assert(arc_outcome_arm(&o,command,0u));
   start(&o,command,false);
   swi(&o,0x11u,0u,0x58454241u,13u,false);
   assert(o.state == ARC_OUTCOME_EXIT && o.failed_exits == 1u &&
          o.last_rc == 13u);
   end(&o);
   swi(&o,0x400c7u,0u,0u,0u,true);
   assert(o.state == ARC_OUTCOME_POLL && o.failed_exits == 1u);
   assert(arc_outcome_arm(&o,command,0u));
   start(&o,command,true);
   swi(&o,0x11u,0u,0u,0u,true);
   swi(&o,0x400ddu,1u,0u,0u,true);
   swi(&o,0x400c7u,0u,0u,0u,true);
   assert(o.state == ARC_OUTCOME_CLOSE);
   swi(&o,0x400ddu,0u,0u,0u,false);
   assert(o.state == ARC_OUTCOME_CLOSE);
   swi(&o,0x400ddu,0u,0u,0u,true);
   assert(o.state == ARC_OUTCOME_END);
   assert(arc_outcome_arm(&o,command,0u));
   start(&o,command,true);
   arc_outcome_refused(&o,1u);
   arc_outcome_refused(&o,4u);
   assert(o.refused_floppy == 0u && o.refused_hd == 1u);
   arc_outcome_frame(&o,600000000u);
   assert(o.state == ARC_OUTCOME_CANCEL &&
          !strcmp(o.cancel_reason,"timeout"));
   const ArcOutcomeProfile rpc={120000000u,false,false,false,true};
   assert(arc_outcome_arm_profile(&o,command,0,&rpc));
   start(&o,"Run Other",false);assert(o.state==ARC_OUTCOME_WAIT);
   start(&o,command,false);assert(o.matched && o.state==ARC_OUTCOME_EXIT);
   end(&o);swi(&o,0x400c7,0,0,0,true);swi(&o,0x400c0,0,0,0,true);
   assert(o.state==ARC_OUTCOME_EXIT); /* no global task-exit inference */
   return 0;
}
