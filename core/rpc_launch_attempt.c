/* No engine, guest memory, observer or transport dependencies. GPL-2.0-only. */
#include "rpc_launch_attempt.h"
#include <string.h>
bool arc_riscpc_attempt_init(ArcRpcLaunchAttempt *a, uint64_t session, uint64_t epoch)
{
   if (!a || !session || !epoch) return false;
   memset(a, 0, sizeof(*a)); a->session = session; a->epoch = epoch;
   a->live = a->resume_allowed = true; return true;
}
bool arc_riscpc_attempt_prepare(ArcRpcLaunchAttempt *a, const ArcRpcLaunchPlan *p,
                             unsigned capabilities, ArcRpcAttemptToken *out)
{
   uint8_t digest[32];
   if (!a || !out || !a->live || !a->resume_allowed || a->last_serial == UINT64_MAX ||
       (capabilities & ~(ARC_RPC_ATTEMPT_OUTCOME_CAP | ARC_RPC_ATTEMPT_PROMPT_CAP)) ||
       a->delivery == ARC_RPC_DELIVERY_PREPARED || a->delivery == ARC_RPC_DELIVERY_CONSUMED ||
       a->delivery == ARC_RPC_DELIVERY_DISPATCH || !arc_riscpc_plan_digest(p, digest)) return false;
   a->token.session = a->session; a->token.epoch = a->epoch;
   a->token.serial = ++a->last_serial; memcpy(a->token.plan, digest, 32);
   a->capabilities = capabilities; a->delivery = ARC_RPC_DELIVERY_PREPARED;
   a->action = p->action; memcpy(a->media, p->media, 32);
   a->last_event_serial = 0;
   a->outcome = ARC_RPC_OUTCOME_NONE; a->start_returned = false; *out = a->token; return true;
}
bool arc_riscpc_attempt_matches(const ArcRpcLaunchAttempt *a, const ArcRpcAttemptToken *t)
{
   return a && t && a->live && a->resume_allowed && t->serial &&
      t->session == a->session && t->epoch == a->epoch &&
      t->session == a->token.session && t->epoch == a->token.epoch &&
      t->serial == a->token.serial && !memcmp(t->plan, a->token.plan, 32);
}
ArcRpcTransition arc_riscpc_attempt_consume(ArcRpcLaunchAttempt *a, const ArcRpcAttemptToken *t)
{
   if (!arc_riscpc_attempt_matches(a, t)) return ARC_RPC_TRANSITION_REFUSED;
   if (a->delivery == ARC_RPC_DELIVERY_PREPARED)
   { a->delivery = ARC_RPC_DELIVERY_CONSUMED; return ARC_RPC_TRANSITION_APPLIED; }
   if (a->delivery == ARC_RPC_DELIVERY_CONSUMED || a->delivery == ARC_RPC_DELIVERY_DISPATCH ||
       a->delivery == ARC_RPC_DELIVERY_OBSERVED || a->delivery == ARC_RPC_DELIVERY_UNKNOWN)
      return ARC_RPC_TRANSITION_DUPLICATE;
   return ARC_RPC_TRANSITION_REFUSED;
}
bool arc_riscpc_attempt_established(const ArcRpcLaunchAttempt *a)
{
   return a && a->live && a->resume_allowed && a->delivery == ARC_RPC_DELIVERY_OBSERVED &&
      (a->capabilities & ARC_RPC_ATTEMPT_OUTCOME_CAP) &&
      (a->outcome == ARC_RPC_OUTCOME_RETURNED || a->outcome == ARC_RPC_OUTCOME_TERMINATED ||
       a->outcome == ARC_RPC_OUTCOME_DESKTOP);
}
static ArcRpcTransition event_transition(ArcRpcLaunchAttempt *a, const ArcRpcAttemptToken *t,
                                       ArcRpcAttemptEvent e)
{
   if (!arc_riscpc_attempt_matches(a, t)) return ARC_RPC_TRANSITION_REFUSED;
   if(e==ARC_RPC_EVENT_UNOBSERVED)
   {
      if(a->delivery!=ARC_RPC_DELIVERY_PREPARED)return ARC_RPC_TRANSITION_REFUSED;
      if(a->outcome==ARC_RPC_OUTCOME_UNKNOWN)return ARC_RPC_TRANSITION_DUPLICATE;
      a->outcome=ARC_RPC_OUTCOME_UNKNOWN;a->capabilities=0;return ARC_RPC_TRANSITION_APPLIED;
   }
   if (e == ARC_RPC_EVENT_DISPATCH)
   {
      if (a->delivery == ARC_RPC_DELIVERY_DISPATCH || a->delivery == ARC_RPC_DELIVERY_OBSERVED)
         return ARC_RPC_TRANSITION_DUPLICATE;
      if (a->delivery != ARC_RPC_DELIVERY_CONSUMED) return ARC_RPC_TRANSITION_REFUSED;
      a->delivery = ARC_RPC_DELIVERY_DISPATCH; a->outcome = ARC_RPC_OUTCOME_PENDING;
      return ARC_RPC_TRANSITION_APPLIED;
   }
   if (e == ARC_RPC_EVENT_REJECTED)
   {
      if (a->delivery == ARC_RPC_DELIVERY_OBSERVED && a->outcome == ARC_RPC_OUTCOME_REJECTED)
         return ARC_RPC_TRANSITION_DUPLICATE;
      if (a->delivery != ARC_RPC_DELIVERY_PREPARED && a->delivery != ARC_RPC_DELIVERY_CONSUMED)
         return ARC_RPC_TRANSITION_REFUSED;
      a->delivery = ARC_RPC_DELIVERY_OBSERVED; a->outcome = ARC_RPC_OUTCOME_REJECTED;
      return ARC_RPC_TRANSITION_APPLIED;
   }
   if (e == ARC_RPC_EVENT_EXPIRE)
   {
      if (a->delivery == ARC_RPC_DELIVERY_UNKNOWN) return ARC_RPC_TRANSITION_DUPLICATE;
      if (a->delivery != ARC_RPC_DELIVERY_PREPARED && a->delivery != ARC_RPC_DELIVERY_CONSUMED &&
          a->delivery != ARC_RPC_DELIVERY_DISPATCH) return ARC_RPC_TRANSITION_REFUSED;
      a->delivery = ARC_RPC_DELIVERY_UNKNOWN; a->outcome = ARC_RPC_OUTCOME_UNKNOWN;
      return ARC_RPC_TRANSITION_APPLIED;
   }
   ArcRpcAttemptOutcome outcome;
   unsigned cap = ARC_RPC_ATTEMPT_OUTCOME_CAP;
   switch (e)
   {
      case ARC_RPC_EVENT_START_RETURNED:
         if (a->delivery != ARC_RPC_DELIVERY_DISPATCH) return ARC_RPC_TRANSITION_REFUSED;
         if (a->start_returned) return ARC_RPC_TRANSITION_DUPLICATE;
         a->start_returned = true; return ARC_RPC_TRANSITION_APPLIED;
      case ARC_RPC_EVENT_RETURNED: outcome = ARC_RPC_OUTCOME_RETURNED; break;
      case ARC_RPC_EVENT_TERMINATED: outcome = ARC_RPC_OUTCOME_TERMINATED; break;
      case ARC_RPC_EVENT_DESKTOP: outcome = ARC_RPC_OUTCOME_DESKTOP; break;
      case ARC_RPC_EVENT_WAITING: outcome = ARC_RPC_OUTCOME_WAITING; cap = ARC_RPC_ATTEMPT_PROMPT_CAP; break;
      case ARC_RPC_EVENT_WAIT_UNOBSERVED:
      case ARC_RPC_EVENT_INPUT_RESUMED: outcome = ARC_RPC_OUTCOME_PENDING; cap = ARC_RPC_ATTEMPT_PROMPT_CAP; break;
      default: return ARC_RPC_TRANSITION_REFUSED;
   }
   if (!(a->capabilities & cap)) return ARC_RPC_TRANSITION_REFUSED;
   if ((a->delivery == ARC_RPC_DELIVERY_DISPATCH || a->delivery == ARC_RPC_DELIVERY_OBSERVED) &&
       a->outcome == outcome) return ARC_RPC_TRANSITION_DUPLICATE;
   if (a->delivery != ARC_RPC_DELIVERY_DISPATCH ||
       ((e == ARC_RPC_EVENT_INPUT_RESUMED || e == ARC_RPC_EVENT_WAIT_UNOBSERVED) && a->outcome != ARC_RPC_OUTCOME_WAITING))
      return ARC_RPC_TRANSITION_REFUSED;
   a->outcome = outcome;
   if (outcome != ARC_RPC_OUTCOME_WAITING && outcome != ARC_RPC_OUTCOME_PENDING)
      a->delivery = ARC_RPC_DELIVERY_OBSERVED;
   return ARC_RPC_TRANSITION_APPLIED;
}
ArcRpcTransition arc_riscpc_attempt_event(ArcRpcLaunchAttempt *a, const ArcRpcAttemptToken *t,
                                         uint64_t serial, ArcRpcAttemptEvent e)
{
   if (!arc_riscpc_attempt_matches(a, t) || !serial || serial < a->last_event_serial)
      return ARC_RPC_TRANSITION_REFUSED;
   if (serial == a->last_event_serial)
      return e == a->last_event ? ARC_RPC_TRANSITION_DUPLICATE : ARC_RPC_TRANSITION_REFUSED;
   ArcRpcTransition r = event_transition(a, t, e);
   if (r != ARC_RPC_TRANSITION_REFUSED) { a->last_event_serial = serial; a->last_event = e; }
   return r;
}
bool arc_riscpc_attempt_interrupt(ArcRpcLaunchAttempt *a, ArcRpcAttemptInterrupt reason)
{
   if (!a || !a->live || reason < ARC_RPC_INTERRUPT_RESET || reason > ARC_RPC_INTERRUPT_UNLOAD)
      return false;
   a->resume_allowed = false; a->delivery = ARC_RPC_DELIVERY_INTERRUPTED;
   a->outcome = ARC_RPC_OUTCOME_INTERRUPTED; a->capabilities = 0;
   if (a->epoch == UINT64_MAX || reason == ARC_RPC_INTERRUPT_FAULT || reason == ARC_RPC_INTERRUPT_UNLOAD)
      a->live = false;
   if (a->epoch != UINT64_MAX) ++a->epoch;
   return true;
}
bool arc_riscpc_attempt_noop_ready(ArcRpcLaunchAttempt *a, bool replaced)
{
   if (!a || !a->live || a->resume_allowed || a->delivery != ARC_RPC_DELIVERY_INTERRUPTED)
      return false;
   if (!replaced) return false;
   a->resume_allowed = true; return true;
}
