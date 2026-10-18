/* ArcDroid boot observer and recovery decision tests. GPL-2.0-only. */
#include "arc_boot_watch.h"
#include "arcdroid_core.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fixture_decisions(const char *directory)
{
   static const struct { const char *leaf; ArcLaunchRecovery action; } cases[] = {
      {"boot-matrix-3-file-one.adf", ARC_LAUNCH_RECOVERY_HELPER},
      {"boot-matrix-3-directory-one.adf", ARC_LAUNCH_RECOVERY_HELPER},
      {"boot-matrix-3-file-multiple.adf", ARC_LAUNCH_RECOVERY_CHOOSER},
      {"boot-matrix-3-file-zero.adf", ARC_LAUNCH_RECOVERY_NONE},
      {"boot-matrix-3-file-unknown.adf", ARC_LAUNCH_RECOVERY_NONE},
   };
   for (unsigned index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index)
   {
      char path[4096];
      assert(snprintf(path, sizeof(path), "%s/launch-family/%s",
                      directory, cases[index].leaf) < (int)sizeof(path));
      FILE *file = fopen(path, "rb");
      assert(file && fseek(file, 0, SEEK_END) == 0);
      long length = ftell(file);
      assert(length > 0 && fseek(file, 0, SEEK_SET) == 0);
      uint8_t *bytes = malloc((size_t)length);
      assert(bytes && fread(bytes, 1u, (size_t)length, file) == (size_t)length);
      assert(fclose(file) == 0);
      ArcMediaInfo media;
      ArcLaunchDiscovery discovery;
      char error[160] = {0};
      assert(arc_media_inspect(path, bytes, (size_t)length,
                               &media, error, sizeof(error)));
      assert(arc_launch_discover(bytes, (size_t)length, &media, NULL,
                                 &discovery));
      assert(discovery.reason == ARC_LAUNCH_REASON_DISC_BOOTS_ITSELF);
      int target = -1;
      assert(arc_launch_recovery_decide(&discovery, &target) == cases[index].action);
      if (cases[index].action == ARC_LAUNCH_RECOVERY_HELPER)
         assert(target >= 0 && arc_launch_candidate_is_target(&discovery.candidates[target]));
      else assert(target == -1);
      free(bytes);
   }
}

static void swi(ArcBootWatch *w, unsigned number, unsigned r0,
                bool rom, uint64_t us)
{ arc_boot_watch_observe(w, number, r0, 0, 0, 0, 0x3800020u, 3u, rom, us); }

int main(int argc, char **argv)
{
   assert(argc == 2);
   ArcBootWatch w;
   arc_boot_watch_arm(&w, 0);
   swi(&w, 0x2000eu, 0x7fffu, true, 0);
   arc_boot_watch_frame(&w, 1999999u);
   assert(w.state == ARC_BOOT_WATCH_ARMED);
   arc_boot_watch_frame(&w, 2000000u);
   assert(w.state == ARC_BOOT_WATCH_PROMPT);
   for (unsigned hz = 50u; hz <= 60u; hz += 10u)
   {
      arc_boot_watch_arm(&w, 0);
      swi(&w, 0x0eu, 0, true, 0);
      for (unsigned frame = 1u; frame < 2u * hz; ++frame)
         arc_boot_watch_frame(&w, (uint64_t)frame * 1000000u / hz);
      assert(w.state == ARC_BOOT_WATCH_ARMED);
      arc_boot_watch_frame(&w, 2000000u);
      assert(w.state == ARC_BOOT_WATCH_PROMPT);
   }
   arc_boot_watch_arm(&w, 0);
   swi(&w, 0x0eu, 0, true, 0);
   swi(&w, 5u, 0, true, 2000000u);
   arc_boot_watch_frame(&w, 2000000u);
   assert(w.state == ARC_BOOT_WATCH_ARMED);
   arc_boot_watch_clear(&w);
   assert(w.state == ARC_BOOT_WATCH_DISABLED);
   arc_boot_watch_arm(&w, 1000000u);
   swi(&w, 0x0eu, 0x8000u, true, 1000000u);
   swi(&w, 0x0eu, 0x100u, false, 1000000u);
   swi(&w, 0x4000eu, 0x100u, true, 1000000u);
   arc_boot_watch_frame(&w, 3500000u);
   assert(w.state == ARC_BOOT_WATCH_ARMED);
   swi(&w, 0x0eu, 0x100u, true, 4000000u);
   swi(&w, 5u, 0, true, 6000000u);
   arc_boot_watch_frame(&w, 6000000u);
   assert(w.state == ARC_BOOT_WATCH_ARMED && !w.pending);
   swi(&w, 0x0eu, 0x100u, true, 6100000u);
   swi(&w, 0x400c0u, 0, true, 6200000u);
   arc_boot_watch_frame(&w, 9000000u);
   assert(w.state == ARC_BOOT_WATCH_DISABLED);
   arc_boot_watch_arm(&w, 0);
   swi(&w, 0x0eu, 0, true, 39000000u);
   arc_boot_watch_frame(&w, 40000000u);
   assert(w.state == ARC_BOOT_WATCH_EXPIRED);
   arc_boot_watch_arm(&w, 0);
   swi(&w, 0x0eu, 0, true, 38000000u);
   arc_boot_watch_frame(&w, 39999999u);
   assert(w.state == ARC_BOOT_WATCH_ARMED);
   arc_boot_watch_frame(&w, 40000000u);
   assert(w.state == ARC_BOOT_WATCH_EXPIRED);
   arc_boot_watch_arm(&w, 0);
   swi(&w, 0x600c0u, 0, true, 100u);
   swi(&w, 0x0eu, 0, true, 200u);
   arc_boot_watch_frame(&w, 3000000u);
   assert(w.state == ARC_BOOT_WATCH_DISABLED);
   arc_boot_watch_arm(&w, 0);
   swi(&w, 0x0eu, 0, true, 1000000u);
   arc_boot_watch_cancel(&w);
   assert(w.state == ARC_BOOT_WATCH_CANCELLED);
   arc_boot_watch_arm(&w, 0);
   assert(w.state == ARC_BOOT_WATCH_ARMED && !w.pending);
   ArcLaunchDiscovery d = {0};
   int selected = 99;
   d.readable = true;
   d.candidate_count = 2;
   d.application_count = 1;
   d.candidates[0].resource = true;
   d.candidates[1].state = ARC_LAUNCH_CANDIDATE_APPLICATION;
   d.candidates[1].launchable = true;
   assert(arc_launch_recovery_decide(&d, &selected) == ARC_LAUNCH_RECOVERY_HELPER);
   assert(selected == 1);
   d.scan_incomplete = true;
   assert(arc_launch_recovery_decide(&d, &selected) == ARC_LAUNCH_RECOVERY_CHOOSER);
   d.scan_incomplete = false;
   d.unknown_count = 1;
   assert(arc_launch_recovery_decide(&d, &selected) == ARC_LAUNCH_RECOVERY_CHOOSER);
   ArcLaunchChooser chooser;
   d.reason = ARC_LAUNCH_REASON_AMBIGUOUS;
   assert(arc_launch_chooser_init(&chooser, &d, false, INT_MIN));
   assert(!chooser.inspection_incomplete && !chooser.post_boot);
   arc_launch_chooser_post_boot(&chooser, &d);
   assert(chooser.inspection_incomplete && chooser.post_boot);
   d.unknown_count = 0;
   d.application_count = 2;
   d.candidate_count = 3;
   d.candidates[2].state = ARC_LAUNCH_CANDIDATE_APPLICATION;
   d.candidates[2].launchable = false;
   assert(arc_launch_recovery_decide(&d, &selected) == ARC_LAUNCH_RECOVERY_CHOOSER);
   d.candidates[1].launchable = false;
   assert(arc_launch_recovery_decide(&d, &selected) == ARC_LAUNCH_RECOVERY_NONE);
   assert(selected == -1);
   fixture_decisions(argv[1]);
   ArcBootWatch rpc;const ArcBootProfile profile={120000000,250000,0,true,false,true};
   arc_boot_watch_arm_profile(&rpc,0,&profile);
   arc_boot_watch_observe(&rpc,0x2000e,0x10020000,255,0,255,0,16,false,0);
   assert(!rpc.pending);
   arc_boot_watch_observe(&rpc,0x2000e,0x10020000,255,32,126,0,16,false,0);
   arc_boot_watch_frame(&rpc,249999);assert(rpc.state==ARC_BOOT_WATCH_ARMED && rpc.pending);
   arc_boot_watch_frame(&rpc,250000);assert(rpc.state==ARC_BOOT_WATCH_PROMPT);
   for(unsigned mode=0;mode<32;++mode){
      arc_boot_watch_arm_profile(&rpc,0,&profile);
      arc_boot_watch_observe(&rpc,0x2000e,0x10020000,255,32,126,0,mode,false,0);
      assert(rpc.pending==(mode==0 || mode==16));
   }
   puts("boot-watch: passed");
   return 0;
}
