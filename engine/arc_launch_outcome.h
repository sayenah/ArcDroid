/* Native application launch outcome observer. GPL-2.0-only. */
#ifndef ARC_LAUNCH_OUTCOME_H
#define ARC_LAUNCH_OUTCOME_H
#include <stdbool.h>
#include <stdint.h>

typedef enum {
   ARC_OUTCOME_OFF, ARC_OUTCOME_WAIT, ARC_OUTCOME_EXIT,
   ARC_OUTCOME_CLOSE, ARC_OUTCOME_END, ARC_OUTCOME_POLL,
   ARC_OUTCOME_CANCEL
} ArcOutcomeState;
typedef struct {
   ArcOutcomeState state;
   uint64_t start_us;
   unsigned refused_floppy;
   unsigned refused_hd;
   unsigned failed_exits;
   uint32_t last_rc;
   uint32_t close_r1;
   bool has_rc;
   bool matched;
   char command[257];
   const char *cancel_reason;
} ArcLaunchOutcome;

void arc_outcome_clear(ArcLaunchOutcome *outcome);
bool arc_outcome_arm(ArcLaunchOutcome *outcome, const char *command,
                     uint64_t now_us);
void arc_outcome_cancel(ArcLaunchOutcome *outcome, const char *reason);
void arc_outcome_refused(ArcLaunchOutcome *outcome, unsigned drive);
void arc_outcome_swi(ArcLaunchOutcome *outcome, uint32_t swi,
                     uint32_t r0, uint32_t r1, uint32_t r2, bool in_rom,
                     const char *start_command, uint64_t now_us);
void arc_outcome_frame(ArcLaunchOutcome *outcome, uint64_t now_us);
#endif
