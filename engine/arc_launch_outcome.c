/* Native application launch outcome observer. GPL-2.0-only. */
#include "arc_launch_outcome.h"
#include <string.h>

enum { ARC_OUTCOME_HORIZON_US = 600000000u };

void arc_outcome_clear(ArcLaunchOutcome *outcome)
{ memset(outcome, 0, sizeof(*outcome)); }

bool arc_outcome_arm(ArcLaunchOutcome *outcome, const char *command,
                     uint64_t now_us)
{
   size_t length = command ? strlen(command) : 0u;
   if (!outcome || length < 6u || length > 256u) return false;
   arc_outcome_clear(outcome);
   memcpy(outcome->command, command, length + 1u);
   outcome->state = ARC_OUTCOME_WAIT;
   outcome->start_us = now_us;
   return true;
}

void arc_outcome_cancel(ArcLaunchOutcome *outcome, const char *reason)
{
   if (outcome->state != ARC_OUTCOME_OFF &&
       outcome->state != ARC_OUTCOME_CANCEL)
   { outcome->state = ARC_OUTCOME_CANCEL; outcome->cancel_reason = reason; }
}

void arc_outcome_refused(ArcLaunchOutcome *outcome, unsigned drive)
{
   if (outcome->state == ARC_OUTCOME_EXIT ||
       outcome->state == ARC_OUTCOME_CLOSE ||
       outcome->state == ARC_OUTCOME_END ||
       outcome->state == ARC_OUTCOME_POLL)
   {
      unsigned *count = drive == 0u ? &outcome->refused_floppy :
                        drive == 4u ? &outcome->refused_hd : NULL;
      if (count && *count < UINT32_MAX) ++*count;
   }
}

void arc_outcome_swi(ArcLaunchOutcome *outcome, uint32_t swi,
                     uint32_t r0, uint32_t r1, uint32_t r2, bool in_rom,
                     const char *start_command, uint64_t now_us)
{
   if (outcome->state == ARC_OUTCOME_OFF ||
       outcome->state == ARC_OUTCOME_CANCEL) return;
   if (now_us - outcome->start_us >= (outcome->profile ? outcome->profile->horizon_us : ARC_OUTCOME_HORIZON_US))
   { arc_outcome_cancel(outcome, "timeout"); return; }
   swi = (swi & 0x00ffffffu) & ~0x00020000u;
   if (swi == 0x11u && r1 == UINT32_C(0x58454241) && r2 != 0u)
   {
      if (outcome->failed_exits < UINT32_MAX) ++outcome->failed_exits;
      outcome->last_rc = r2;
      outcome->has_rc = true;
   }
   if (outcome->state == ARC_OUTCOME_WAIT)
   {
      if (swi == 0x400deu && start_command &&
          !strcmp(start_command,outcome->command))
      { outcome->matched=true; outcome->state=ARC_OUTCOME_EXIT; }
      return;
   }
   if (swi == 0x400deu)
   {
      arc_outcome_cancel(outcome, "start-task");
      return;
   }
   if (swi == 0x400c0u || swi == 0x400dfu)
   { arc_outcome_cancel(outcome, swi == 0x400c0u ? "initialise" : "report-error"); return; }
   if (outcome->state == ARC_OUTCOME_EXIT)
   {
      if (swi == 0x11u && (in_rom || (outcome->profile && !outcome->profile->exit_rom)))
         outcome->state = ARC_OUTCOME_CLOSE;
   }
   else if (outcome->state == ARC_OUTCOME_CLOSE)
   {
      if (swi == 0x400ddu && (in_rom || (outcome->profile && !outcome->profile->close_rom)) && r0 == 0u)
      { outcome->close_r1=r1; outcome->state = ARC_OUTCOME_END; }
   }
   else if (outcome->state == ARC_OUTCOME_END &&
            (swi == 0x400c7u || swi == 0x400e1u) && (in_rom || (outcome->profile && !outcome->profile->poll_rom)))
      outcome->state = ARC_OUTCOME_POLL;
}

void arc_outcome_frame(ArcLaunchOutcome *outcome, uint64_t now_us)
{
   if (outcome->state != ARC_OUTCOME_OFF &&
       outcome->state != ARC_OUTCOME_CANCEL &&
       now_us - outcome->start_us >= (outcome->profile ? outcome->profile->horizon_us : ARC_OUTCOME_HORIZON_US))
      arc_outcome_cancel(outcome, "timeout");
}

bool arc_outcome_arm_profile(ArcLaunchOutcome *outcome,const char *command,
   uint64_t now_us,const ArcOutcomeProfile *profile)
{
   if(!profile || !profile->horizon_us || !arc_outcome_arm(outcome,command,now_us))return false;
   outcome->profile=profile;return true;
}
