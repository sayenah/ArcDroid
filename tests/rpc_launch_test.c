/* Public host-only fixtures; no firmware or guest execution. GPL-2.0-only. */
#include "rpc_boot_record.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const uint8_t media_digest[32] = {0x42};
static ArcLaunchDiscovery authored(void)
{
   ArcLaunchDiscovery d = {0}; d.readable = true; d.candidate_count = d.application_count = 1;
   d.target_index = 0; d.candidates[0].name.length = 8;
   memcpy(d.candidates[0].name.bytes, "!Example", 8);
   arc_launch_candidate_classify_counts(&d.candidates[0], 1, 1); return d;
}
static ArcRpcLaunchPlan plan(void)
{
   ArcLaunchDiscovery d = authored(); ArcRpcLaunchPlan p;
   assert(arc_riscpc_plan_build(&d, -1, ARC_RPC_PLAN_APPLICATION, false, media_digest, &p) == ARC_RPC_PLAN_OK);
   return p;
}
static ArcLaunchDiscovery discover(const char *directory, const char *leaf)
{
   char path[2048], error[128]; snprintf(path, sizeof(path), "%s/%s", directory, leaf);
   FILE *f = fopen(path, "rb"); assert(f); assert(!fseek(f, 0, SEEK_END)); long n = ftell(f);
   assert(n > 0); rewind(f); uint8_t *data = malloc((size_t)n); assert(data);
   assert(fread(data, 1, (size_t)n, f) == (size_t)n); assert(!fclose(f));
   ArcMediaInfo info; ArcLaunchDiscovery d;
   assert(arc_media_inspect(path, data, (size_t)n, &info, error, sizeof(error)));
   assert(arc_launch_discover(data, (size_t)n, &info, NULL, &d)); free(data); return d;
}
static void put32(uint8_t *p, uint32_t n)
{ for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(n >> (8u * i)); }
static void plans(const char *fixtures)
{
   ArcRpcLaunchPlan p = plan(), decoded; uint8_t wire[129], original[128]; size_t n;
   assert(arc_riscpc_plan_encode(&p, wire, sizeof(wire), &n) == ARC_RPC_PLAN_OK);
   assert(n == 82 && !memcmp(wire, "ARCRLPLN\2\0\0\0", 12)); memcpy(original, wire, n);
   assert(arc_riscpc_plan_decode(wire, n, &decoded) == ARC_RPC_PLAN_OK);
   char command[80]; assert(arc_riscpc_plan_command(&decoded, command, sizeof(command)) == ARC_RPC_PLAN_OK);
   assert(!strcmp(command, "%Filer_Run ADFS::0.$.!Example"));
   assert(!strstr(command, "Desktop") && !strstr(command, "Filer_Boot"));
   for (size_t cut = 0; cut < n; ++cut) assert(arc_riscpc_plan_decode(wire, cut, &decoded) != ARC_RPC_PLAN_OK);
   assert(arc_riscpc_plan_decode(wire, n + 1, &decoded) == ARC_RPC_PLAN_INVALID);
   assert(arc_riscpc_plan_encode(&p, wire, n - 1, &n) == ARC_RPC_PLAN_BUFFER && n == 0);
   n = 82;
   const unsigned fields[] = {0, 8, 12, 16, 20, 56, 60, 64};
   for (unsigned i = 0; i < sizeof(fields)/sizeof(*fields); ++i)
   { memcpy(wire, original, n); wire[fields[i]] ^= 0x80; assert(arc_riscpc_plan_decode(wire, n, &decoded) != ARC_RPC_PLAN_OK); }
   memcpy(wire, original, n); put32(wire + 8, 1);
   assert(arc_riscpc_plan_decode(wire, n, &decoded) == ARC_RPC_PLAN_VERSION_UNKNOWN);
   put32(wire + 8, 2); put32(wire + 16, 0);
   assert(arc_riscpc_plan_decode(wire, n, &decoded) == ARC_RPC_PLAN_ACTION_UNKNOWN);
   memcpy(wire, original, n); memset(wire + 24, 0, 32);
   assert(arc_riscpc_plan_decode(wire, n, &decoded) == ARC_RPC_PLAN_INVALID);
   for (size_t at = 64; at < n; ++at)
   { memcpy(wire, original, n); wire[at] = 0; assert(arc_riscpc_plan_decode(wire, n, &decoded) == ARC_RPC_PLAN_INVALID); }
   const char *bad[] = {"ADFS::1.$.!Example", "ADFS::4.$.!Example", "ADFS::0.$.!", "ADFS::0.$.Example",
      "ADFS::0.$..!Example", "ADFS::0.$.one.two.!Example", "ADFS::0.$.!Example123", "ADFS::0.$.!A\rRun bad",
      "ADFS::0.$.!A*", "ADFS::0.$.!A#", "ADFS::0.$.!A\"", "ADFS::0.$.!A|", "ADFS::0.$.!A<", "ADFS::0.$.!A\\"};
   for (unsigned i = 0; i < sizeof(bad)/sizeof(*bad); ++i)
   { strcpy(p.target, bad[i]); assert(arc_riscpc_plan_command(&p, command, sizeof(command)) == ARC_RPC_PLAN_INVALID); }
   memset(p.target, 'a', sizeof(p.target)); assert(arc_riscpc_plan_digest(&p, wire) == false);
   ArcLaunchDiscovery d = authored(); d.candidates[1] = d.candidates[0];
   memcpy(d.candidates[1].name.bytes, "!Second", 7); d.candidates[1].name.length = 7;
   d.candidate_count = d.application_count = 2;
   assert(arc_riscpc_plan_build(&d, -1, ARC_RPC_PLAN_APPLICATION, false, media_digest, &p) == ARC_RPC_PLAN_AMBIGUOUS);
   assert(arc_riscpc_plan_build(&d, 1, ARC_RPC_PLAN_APPLICATION, false, media_digest, &p) == ARC_RPC_PLAN_OK);
   d = authored(); d.scan_incomplete = true;
   assert(arc_riscpc_plan_build(&d, -1, ARC_RPC_PLAN_APPLICATION, false, media_digest, &p) == ARC_RPC_PLAN_AMBIGUOUS);
   assert(arc_riscpc_plan_build(&d, 0, ARC_RPC_PLAN_APPLICATION, true, media_digest, &p) == ARC_RPC_PLAN_OK);
   assert(p.flags == ARC_RPC_PLAN_RAMFS);
   d.candidate_count = ARC_LAUNCH_MAX_CANDIDATES + 1;
   assert(arc_riscpc_plan_build(&d, 0, ARC_RPC_PLAN_APPLICATION, false, media_digest, &p) == ARC_RPC_PLAN_INVALID);
   d = authored(); arc_launch_candidate_classify_counts(&d.candidates[0], 2, 1);
   assert(arc_riscpc_plan_build(&d, 0, ARC_RPC_PLAN_APPLICATION, false, media_digest, &p) == ARC_RPC_PLAN_INVALID);
   d = authored(); d.candidates[0].resource = true;
   assert(arc_riscpc_plan_build(&d, 0, ARC_RPC_PLAN_APPLICATION, false, media_digest, &p) == ARC_RPC_PLAN_INVALID);
   d = authored(); d.root_boot = d.root_boot_is_file = true; d.root_boot_count = 1;
   assert(arc_riscpc_plan_build(&d, 0, ARC_RPC_PLAN_BOOT, false, media_digest, &p) == ARC_RPC_PLAN_OK);
   assert(arc_riscpc_plan_command(&p, command, sizeof(command)) == ARC_RPC_PLAN_OK && !strcmp(command, "%Run ADFS::0.$.!Boot"));
   d.root_boot_count = 2;
   assert(arc_riscpc_plan_build(&d, 0, ARC_RPC_PLAN_BOOT, false, media_digest, &p) == ARC_RPC_PLAN_INVALID);
   d.root_boot_count = 1; d.root_boot_is_file = false;
   assert(arc_riscpc_plan_build(&d, 0, ARC_RPC_PLAN_BOOT, false, media_digest, &p) == ARC_RPC_PLAN_INVALID);
   d = authored(); uint8_t before[32] = {1}, after[32] = {1};
   assert(arc_riscpc_plan_build(&d, 0, ARC_RPC_PLAN_PRODUCT, false, media_digest, &p) == ARC_RPC_PLAN_STALE_PRODUCT);
   assert(arc_riscpc_plan_build_product(&d, 0, false, media_digest, before, after, &p) == ARC_RPC_PLAN_OK);
   assert(arc_riscpc_plan_command(&p, command, sizeof(command)) == ARC_RPC_PLAN_OK && !strcmp(command, "%Filer_Run ADFS::4.$.!Example"));
   assert(arc_riscpc_plan_encode(&p, wire, sizeof(wire), &n) == ARC_RPC_PLAN_OK && arc_riscpc_plan_decode(wire, n, &decoded) == ARC_RPC_PLAN_OK);
   after[2] = 1;
   assert(arc_riscpc_plan_build_product(&d, 0, false, media_digest, before, after, &p) == ARC_RPC_PLAN_STALE_PRODUCT);
   /* Actual FileCore fixtures exercise the existing discovery and parent
    * ambiguity rules; no forged launchable flags here. */
   const char *good[] = {"arcdroid-application-l.adl", "launch-family/nested-single-e.adf"};
   for (unsigned i = 0; i < 2; ++i)
   { d = discover(fixtures, good[i]); assert(arc_riscpc_plan_build(&d, -1, ARC_RPC_PLAN_APPLICATION, false, media_digest, &p) == ARC_RPC_PLAN_OK); }
   const char *unsafe[] = {"launch-family/nested-pipe-d.adf", "launch-family/nested-quote-d.adf", "launch-family/nested-file-collision-d.adf"};
   for (unsigned i = 0; i < 3; ++i)
   { d = discover(fixtures, unsafe[i]); assert(d.candidate_count); assert(arc_riscpc_plan_build(&d, 0, ARC_RPC_PLAN_APPLICATION, false, media_digest, &p) == ARC_RPC_PLAN_INVALID); }
   d = discover(fixtures, "launch-family/boot-file-3-l.adl");
   assert(arc_riscpc_plan_build(&d, 0, ARC_RPC_PLAN_BOOT, false, media_digest, &p) == ARC_RPC_PLAN_OK);
   d = discover(fixtures, "launch-family/boot-duplicate-root-d.adf");
   assert(d.root_boot_count == 2u);
   assert(arc_riscpc_plan_build(&d, 0, ARC_RPC_PLAN_BOOT, false, media_digest, &p) == ARC_RPC_PLAN_INVALID);
   d = authored(); d.candidates[0].name.bytes[2] = '\n'; char label[84];
   assert(arc_riscpc_plan_label(&d.candidates[0], label, sizeof(label)) && strstr(label, "\\x0A"));
   puts("RPC plan: canonical roundtrip, hostile encoding/paths, structural ambiguity, explicit boot, product proof and escaped diagnostics");
}
static ArcRpcTransition event(ArcRpcLaunchAttempt *a, const ArcRpcAttemptToken *t, ArcRpcAttemptEvent e)
{ return arc_riscpc_attempt_event(a, t, a->last_event_serial + 1u, e); }
static void apply(ArcRpcLaunchAttempt *a, const ArcRpcAttemptToken *t, ArcRpcAttemptEvent e)
{ assert(event(a, t, e) == ARC_RPC_TRANSITION_APPLIED); }
static void attempts(void)
{
   ArcRpcLaunchPlan p = plan(); ArcRpcLaunchAttempt a; ArcRpcAttemptToken t, old;
   assert(!arc_riscpc_attempt_init(NULL,1,1));
   assert(!arc_riscpc_attempt_prepare(NULL,&p,0,&t));
   assert(arc_riscpc_attempt_init(&a,77,1));
   assert(!arc_riscpc_attempt_prepare(&a,&p,0,NULL));
   assert(!arc_riscpc_attempt_prepare(&a,&p,4,&t));
   assert(!arc_riscpc_attempt_prepare(&a,NULL,0,&t));
   assert(!arc_riscpc_attempt_interrupt(NULL,ARC_RPC_INTERRUPT_RESET));
   assert(!arc_riscpc_attempt_interrupt(&a,(ArcRpcAttemptInterrupt)-1));
   assert(!arc_riscpc_attempt_interrupt(&a,(ArcRpcAttemptInterrupt)99));
   assert(!arc_riscpc_attempt_noop_ready(NULL,true));
   assert(!arc_riscpc_attempt_noop_ready(&a,true));
   assert(arc_riscpc_attempt_interrupt(&a,ARC_RPC_INTERRUPT_UNLOAD));
   assert(!arc_riscpc_attempt_prepare(&a,&p,0,&t));
   assert(!arc_riscpc_attempt_interrupt(&a,ARC_RPC_INTERRUPT_RESET));
   assert(!arc_riscpc_attempt_matches(NULL,&t) && !arc_riscpc_attempt_matches(&a,NULL));
   for(unsigned cut=0;cut<4;++cut){
      assert(arc_riscpc_attempt_init(&a,77,1));assert(arc_riscpc_attempt_prepare(&a,&p,3,&t));
      if(cut){assert(arc_riscpc_attempt_consume(&a,&t)==ARC_RPC_TRANSITION_APPLIED);
         assert(!arc_riscpc_attempt_prepare(&a,&p,3,&old));}
      if(cut>1){apply(&a,&t,ARC_RPC_EVENT_DISPATCH);
         assert(arc_riscpc_attempt_consume(&a,&t)==ARC_RPC_TRANSITION_DUPLICATE);
         assert(!arc_riscpc_attempt_prepare(&a,&p,3,&old));}
      if(cut>2)apply(&a,&t,ARC_RPC_EVENT_DESKTOP);
      assert(event(&a,&t,ARC_RPC_EVENT_REJECTED)==(cut<2?ARC_RPC_TRANSITION_APPLIED:ARC_RPC_TRANSITION_REFUSED));
      if(cut<2){assert(a.outcome==ARC_RPC_OUTCOME_REJECTED);
         assert(event(&a,&t,ARC_RPC_EVENT_REJECTED)==ARC_RPC_TRANSITION_DUPLICATE);}
      if(cut>2)assert(event(&a,&t,ARC_RPC_EVENT_DISPATCH)==ARC_RPC_TRANSITION_DUPLICATE);
   }
   assert(arc_riscpc_attempt_init(&a,77,1));assert(arc_riscpc_attempt_prepare(&a,&p,3,&t));
   apply(&a,&t,ARC_RPC_EVENT_UNOBSERVED);assert(a.delivery==ARC_RPC_DELIVERY_PREPARED && a.outcome==ARC_RPC_OUTCOME_UNKNOWN && !a.capabilities);
   assert(!arc_riscpc_attempt_prepare(&a,&p,3,&old));
   assert(event(&a,&t,ARC_RPC_EVENT_UNOBSERVED)==ARC_RPC_TRANSITION_DUPLICATE);
   assert(arc_riscpc_attempt_consume(&a,&t)==ARC_RPC_TRANSITION_APPLIED);
   assert(event(&a,&t,ARC_RPC_EVENT_UNOBSERVED)==ARC_RPC_TRANSITION_REFUSED);
   apply(&a,&t,ARC_RPC_EVENT_EXPIRE);assert(!arc_riscpc_attempt_established(&a));
   /* Every reset cut, with and without outcome capabilities. */
   for (unsigned caps = 0; caps <= 3; ++caps) for (unsigned cut = 0; cut < 4; ++cut)
   {
      assert(arc_riscpc_attempt_init(&a, 77, 1));
      assert(arc_riscpc_attempt_prepare(&a, &p, caps, &t));
      assert(event(&a, &t, ARC_RPC_EVENT_DISPATCH) == ARC_RPC_TRANSITION_REFUSED);
      if (cut >= 1) { assert(arc_riscpc_attempt_consume(&a, &t) == ARC_RPC_TRANSITION_APPLIED);
         assert(arc_riscpc_attempt_consume(&a, &t) == ARC_RPC_TRANSITION_DUPLICATE); }
      if (cut >= 2) { apply(&a, &t, ARC_RPC_EVENT_DISPATCH);
         assert(event(&a, &t, ARC_RPC_EVENT_DISPATCH) == ARC_RPC_TRANSITION_DUPLICATE); }
      if (cut >= 3) { if (caps & 1) apply(&a, &t, ARC_RPC_EVENT_DESKTOP); else apply(&a, &t, ARC_RPC_EVENT_START_RETURNED); }
      assert(arc_riscpc_attempt_interrupt(&a, ARC_RPC_INTERRUPT_RESET));
      assert(a.delivery == ARC_RPC_DELIVERY_INTERRUPTED && a.outcome == ARC_RPC_OUTCOME_INTERRUPTED && a.epoch == 2 && !a.resume_allowed);
      assert(!arc_riscpc_attempt_established(&a));
      assert(arc_riscpc_attempt_consume(&a, &t) == ARC_RPC_TRANSITION_REFUSED);
      assert(!arc_riscpc_attempt_noop_ready(&a, false));
      assert(!arc_riscpc_attempt_prepare(&a, &p, caps, &old));
      assert(arc_riscpc_attempt_noop_ready(&a, true));
      old = t; assert(arc_riscpc_attempt_prepare(&a, &p, caps, &t)); assert(t.serial == 2 && t.epoch == 2);
      assert(arc_riscpc_attempt_consume(&a, &old) == ARC_RPC_TRANSITION_REFUSED);
      assert(event(&a, &old, ARC_RPC_EVENT_TERMINATED) == ARC_RPC_TRANSITION_REFUSED);
   }
   assert(arc_riscpc_attempt_init(&a, 77, 1)); assert(arc_riscpc_attempt_prepare(&a, &p, 3, &t));
   assert(!arc_riscpc_attempt_prepare(&a, &p, 3, &old));
   old = t; ++old.session; assert(arc_riscpc_attempt_consume(&a, &old) == ARC_RPC_TRANSITION_REFUSED);
   old = t; ++old.epoch; assert(arc_riscpc_attempt_consume(&a, &old) == ARC_RPC_TRANSITION_REFUSED);
   old = t; ++old.serial; assert(arc_riscpc_attempt_consume(&a, &old) == ARC_RPC_TRANSITION_REFUSED);
   old = t; old.plan[7] ^= 1; assert(arc_riscpc_attempt_consume(&a, &old) == ARC_RPC_TRANSITION_REFUSED);
   assert(event(&a, &t, ARC_RPC_EVENT_TERMINATED) == ARC_RPC_TRANSITION_REFUSED);
   assert(arc_riscpc_attempt_consume(&a, &t) == ARC_RPC_TRANSITION_APPLIED);
   apply(&a, &t, ARC_RPC_EVENT_DISPATCH); apply(&a, &t, ARC_RPC_EVENT_START_RETURNED);
   assert(!arc_riscpc_attempt_established(&a) && a.outcome == ARC_RPC_OUTCOME_PENDING);
   apply(&a, &t, ARC_RPC_EVENT_WAITING); assert(!arc_riscpc_attempt_established(&a));
   assert(event(&a, &t, ARC_RPC_EVENT_WAITING) == ARC_RPC_TRANSITION_DUPLICATE);
   apply(&a, &t, ARC_RPC_EVENT_WAIT_UNOBSERVED);assert(a.outcome==ARC_RPC_OUTCOME_PENDING && a.start_returned);
   assert(event(&a,&t,ARC_RPC_EVENT_WAIT_UNOBSERVED)==ARC_RPC_TRANSITION_DUPLICATE);
   apply(&a,&t,ARC_RPC_EVENT_WAITING);
   apply(&a, &t, ARC_RPC_EVENT_INPUT_RESUMED);
   uint64_t resumed = a.last_event_serial;
   assert(arc_riscpc_attempt_event(&a, &t, resumed - 1u, ARC_RPC_EVENT_WAITING) == ARC_RPC_TRANSITION_REFUSED);
   assert(arc_riscpc_attempt_event(&a, &t, resumed, ARC_RPC_EVENT_INPUT_RESUMED) == ARC_RPC_TRANSITION_DUPLICATE);
   assert(arc_riscpc_attempt_event(&a, &t, resumed, ARC_RPC_EVENT_WAITING) == ARC_RPC_TRANSITION_REFUSED);
   assert(arc_riscpc_attempt_event(&a, &t, 0, ARC_RPC_EVENT_WAITING) == ARC_RPC_TRANSITION_REFUSED);
   assert(a.outcome == ARC_RPC_OUTCOME_PENDING);
   apply(&a, &t, ARC_RPC_EVENT_TERMINATED); assert(arc_riscpc_attempt_established(&a));
   assert(arc_riscpc_attempt_consume(&a, &t) == ARC_RPC_TRANSITION_DUPLICATE);
   assert(event(&a, &t, ARC_RPC_EVENT_TERMINATED) == ARC_RPC_TRANSITION_DUPLICATE);
   assert(event(&a, &t, ARC_RPC_EVENT_RETURNED) == ARC_RPC_TRANSITION_REFUSED);
   assert(event(&a, &t, ARC_RPC_EVENT_EXPIRE) == ARC_RPC_TRANSITION_REFUSED);
   old = t; assert(arc_riscpc_attempt_prepare(&a, &p, 0, &t));
   assert(a.outcome==ARC_RPC_OUTCOME_NONE && !a.start_returned);
   assert(event(&a, &old, ARC_RPC_EVENT_TERMINATED) == ARC_RPC_TRANSITION_REFUSED);
   assert(arc_riscpc_attempt_consume(&a, &t) == ARC_RPC_TRANSITION_APPLIED); apply(&a, &t, ARC_RPC_EVENT_DISPATCH);
   assert(event(&a, &t, ARC_RPC_EVENT_TERMINATED) == ARC_RPC_TRANSITION_REFUSED);
   assert(event(&a, &t, ARC_RPC_EVENT_WAITING) == ARC_RPC_TRANSITION_REFUSED);
   apply(&a, &t, ARC_RPC_EVENT_EXPIRE); assert(a.delivery == ARC_RPC_DELIVERY_UNKNOWN && a.outcome==ARC_RPC_OUTCOME_UNKNOWN && !arc_riscpc_attempt_established(&a));
   assert(arc_riscpc_attempt_consume(&a, &t) == ARC_RPC_TRANSITION_DUPLICATE);
   assert(event(&a, &t, ARC_RPC_EVENT_DISPATCH) == ARC_RPC_TRANSITION_REFUSED);
   assert(arc_riscpc_attempt_prepare(&a, &p, 0, &t)); apply(&a, &t, ARC_RPC_EVENT_REJECTED);
   assert(!arc_riscpc_attempt_established(&a));
   for (unsigned reason = ARC_RPC_INTERRUPT_SWAP_ROLLBACK; reason <= ARC_RPC_INTERRUPT_UNLOAD; ++reason)
   {
      assert(arc_riscpc_attempt_init(&a, 77, 1)); assert(arc_riscpc_attempt_prepare(&a, &p, 0, &t));
      assert(arc_riscpc_attempt_interrupt(&a, (ArcRpcAttemptInterrupt)reason));
      assert(arc_riscpc_attempt_consume(&a, &t) == ARC_RPC_TRANSITION_REFUSED);
      assert(arc_riscpc_attempt_noop_ready(&a, true) == (reason == ARC_RPC_INTERRUPT_SWAP_ROLLBACK));
   }
   assert(arc_riscpc_attempt_init(&a, 77, UINT64_MAX));
   assert(arc_riscpc_attempt_interrupt(&a, ARC_RPC_INTERRUPT_RESET) && !a.live);
   assert(!arc_riscpc_attempt_noop_ready(&a, true));
   assert(arc_riscpc_attempt_init(&a, 77, 1)); a.last_serial = UINT64_MAX;
   assert(!arc_riscpc_attempt_prepare(&a, &p, 0, &t));
   assert(!arc_riscpc_attempt_init(&a, 0, 1) && !arc_riscpc_attempt_init(&a, 1, 0));
   assert(arc_riscpc_attempt_init(&a,77,1));assert(arc_riscpc_attempt_prepare(&a,&p,3,&t));
   apply(&a,&t,ARC_RPC_EVENT_REJECTED);assert(!arc_riscpc_attempt_established(&a));
   puts("RPC attempt: reset cuts, synchronous consume without capabilities, duplicates, stale tokens, pending child, unknown expiry and shutdown barriers");
}
static ArcRpcBootKey key(const ArcRpcLaunchPlan *p)
{
   ArcRpcBootKey k = {0}; k.cpu = 3; k.plan_revision = 2; k.profile_revision = 1; k.action = p->action;
   memset(k.firmware, 1, 32); memset(k.configuration, 2, 32); memset(k.content, 3, 32);
   memcpy(k.media, p->media, 32); memset(k.store, 5, 32); memset(k.helper, 6, 32);
   assert(arc_riscpc_plan_digest(p, k.plan)); return k;
}
static void records(void)
{
   ArcRpcLaunchPlan p = plan(); ArcRpcBootKey k = key(&p), changed; ArcRpcLaunchAttempt a;
   ArcRpcAttemptToken t; ArcRpcBootRecord r, decoded; uint8_t wire[289], original[288];
   assert(!arc_riscpc_boot_record_encode(NULL,wire,sizeof(wire)));
   assert(!arc_riscpc_boot_record_decode(NULL,288,&decoded));
   assert(!arc_riscpc_boot_record_matches(NULL,&k));
   const ArcRpcAttemptEvent events[] = {ARC_RPC_EVENT_RETURNED, ARC_RPC_EVENT_TERMINATED, ARC_RPC_EVENT_DESKTOP};
   for (unsigned i = 0; i < 3; ++i)
   {
      assert(arc_riscpc_attempt_init(&a, 42, 1)); assert(arc_riscpc_attempt_prepare(&a, &p, 3, &t));
      assert(!arc_riscpc_boot_record_capture(&a, &k, &k, &r));
      assert(arc_riscpc_attempt_consume(&a, &t) == ARC_RPC_TRANSITION_APPLIED);
      assert(!arc_riscpc_boot_record_capture(&a, &k, &k, &r));
      apply(&a, &t, ARC_RPC_EVENT_DISPATCH); apply(&a, &t, ARC_RPC_EVENT_START_RETURNED);
      assert(!arc_riscpc_boot_record_capture(&a, &k, &k, &r));
      apply(&a, &t, ARC_RPC_EVENT_WAITING); assert(!arc_riscpc_boot_record_capture(&a, &k, &k, &r));
      apply(&a, &t, events[i]); assert(arc_riscpc_boot_record_capture(&a, &k, &k, &r));
      assert(arc_riscpc_boot_record_encode(&r, wire, sizeof(wire)));
      assert(arc_riscpc_boot_record_decode(wire, 288, &decoded) && arc_riscpc_boot_record_matches(&decoded, &k));
      assert(decoded.observation == r.observation);
   }
   memcpy(original, wire, 288);
   /* Every key byte/scalar is bound; capture rechecks the entire current key. */
   for (unsigned at = 20; at < 264; ++at)
   {
      if (at >= 36 && at < 40) continue;
      memcpy(wire, original, 288); wire[at] ^= 1; put32(wire + 284, arc_crc32(wire, 284));
      if (arc_riscpc_boot_record_decode(wire, 288, &decoded)) assert(!arc_riscpc_boot_record_matches(&decoded, &k));
   }
   uint8_t *digests[] = {changed.firmware, changed.configuration, changed.content, changed.media,
                         changed.store, changed.plan, changed.helper};
   for (unsigned i = 0; i < 7; ++i)
   { changed = k; digests[i][0] ^= 0x80; assert(!arc_riscpc_boot_record_capture(&a, &k, &changed, &r)); }
   changed = k; changed.action = ARC_RPC_PLAN_PRODUCT;
   assert(!arc_riscpc_boot_record_capture(&a, &changed, &changed, &r));
   changed = k; changed.media[0] ^= 1;
   assert(!arc_riscpc_boot_record_capture(&a, &changed, &changed, &r));
   for (size_t n = 0; n < 288; ++n) assert(!arc_riscpc_boot_record_decode(original, n, &decoded));
   assert(!arc_riscpc_boot_record_decode(original, 289, &decoded));
   for (unsigned at = 0; at < 288; ++at)
   { memcpy(wire, original, 288); wire[at] ^= 1; assert(!arc_riscpc_boot_record_decode(wire, 288, &decoded)); }
   const unsigned bad_fields[] = {8, 12, 16, 20, 24, 32, 36, 264};
   for (unsigned i = 0; i < sizeof(bad_fields)/sizeof(*bad_fields); ++i)
   { memcpy(wire, original, 288); put32(wire + bad_fields[i], 99); put32(wire + 284, arc_crc32(wire, 284)); assert(!arc_riscpc_boot_record_decode(wire, 288, &decoded)); }
   /* Any nonzero profile revision is a binding, not an admission table. */
   memcpy(wire, original, 288); put32(wire + 28, 0); put32(wire + 284, arc_crc32(wire, 284));
   assert(!arc_riscpc_boot_record_decode(wire, 288, &decoded));
   assert(arc_riscpc_attempt_interrupt(&a, ARC_RPC_INTERRUPT_RESET));
   assert(!arc_riscpc_boot_record_capture(&a, &k, &k, &r));
   assert(arc_riscpc_attempt_noop_ready(&a, true)); assert(arc_riscpc_attempt_prepare(&a, &p, 0, &t));
   assert(arc_riscpc_attempt_consume(&a, &t) == ARC_RPC_TRANSITION_APPLIED); apply(&a, &t, ARC_RPC_EVENT_DISPATCH);
   apply(&a, &t, ARC_RPC_EVENT_EXPIRE); assert(!arc_riscpc_boot_record_capture(&a, &k, &k, &r));
   for (unsigned i = 0; i <= ARC_RPC_OUTCOME_INTERRUPTED; ++i)
   { r = (ArcRpcBootRecord){k, (ArcRpcAttemptOutcome)i}; bool valid = i >= ARC_RPC_OUTCOME_RETURNED && i <= ARC_RPC_OUTCOME_DESKTOP;
     assert(arc_riscpc_boot_record_encode(&r, wire, sizeof(wire)) == valid); }
   puts("RPC records: separate schema/CRC, all bindings, established observations only, stale/corrupt/unknown refusals");
}
int main(int argc, char **argv)
{
   assert(argc == 3);
   if (!strcmp(argv[1], "plan")) plans(argv[2]);
   else if (!strcmp(argv[1], "attempt")) attempts();
   else if (!strcmp(argv[1], "record")) records();
   else return 2;
   return 0;
}
