/* Deterministic RPC delivery/outcome lifecycle. GPL-2.0-only. */
#ifndef ARC_RPC_LAUNCH_ATTEMPT_H
#define ARC_RPC_LAUNCH_ATTEMPT_H
#include "rpc_launch_plan.h"
typedef struct {
   uint64_t session, epoch, serial;
   uint8_t plan[32];
} ArcRpcAttemptToken;
typedef enum { ARC_RPC_DELIVERY_OFF, ARC_RPC_DELIVERY_PREPARED,
   ARC_RPC_DELIVERY_CONSUMED, ARC_RPC_DELIVERY_DISPATCH,
   ARC_RPC_DELIVERY_OBSERVED, ARC_RPC_DELIVERY_INTERRUPTED,
   ARC_RPC_DELIVERY_UNKNOWN } ArcRpcDelivery;
typedef enum { ARC_RPC_OUTCOME_NONE, ARC_RPC_OUTCOME_PENDING,
   ARC_RPC_OUTCOME_WAITING, ARC_RPC_OUTCOME_RETURNED,
   ARC_RPC_OUTCOME_TERMINATED, ARC_RPC_OUTCOME_DESKTOP,
   ARC_RPC_OUTCOME_REJECTED, ARC_RPC_OUTCOME_UNKNOWN,
   ARC_RPC_OUTCOME_INTERRUPTED } ArcRpcAttemptOutcome;
typedef enum { ARC_RPC_EVENT_DISPATCH, ARC_RPC_EVENT_START_RETURNED,
   ARC_RPC_EVENT_RETURNED, ARC_RPC_EVENT_TERMINATED, ARC_RPC_EVENT_DESKTOP,
   ARC_RPC_EVENT_WAITING, ARC_RPC_EVENT_INPUT_RESUMED, ARC_RPC_EVENT_REJECTED,
   ARC_RPC_EVENT_EXPIRE, ARC_RPC_EVENT_UNOBSERVED, ARC_RPC_EVENT_WAIT_UNOBSERVED } ArcRpcAttemptEvent;
typedef enum { ARC_RPC_INTERRUPT_RESET, ARC_RPC_INTERRUPT_SWAP_ROLLBACK,
   ARC_RPC_INTERRUPT_FAULT, ARC_RPC_INTERRUPT_UNLOAD } ArcRpcAttemptInterrupt;
typedef enum { ARC_RPC_TRANSITION_APPLIED, ARC_RPC_TRANSITION_DUPLICATE,
   ARC_RPC_TRANSITION_REFUSED } ArcRpcTransition;
#define ARC_RPC_ATTEMPT_OUTCOME_CAP 1u
#define ARC_RPC_ATTEMPT_PROMPT_CAP 2u
typedef struct {
   uint64_t session, epoch, last_serial;
   ArcRpcAttemptToken token;
   ArcRpcDelivery delivery;
   ArcRpcAttemptOutcome outcome;
   unsigned capabilities;
   uint32_t action;
   uint8_t media[32];
   uint64_t last_event_serial;
   ArcRpcAttemptEvent last_event;
   bool live, resume_allowed, start_returned;
} ArcRpcLaunchAttempt;
bool arc_riscpc_attempt_init(ArcRpcLaunchAttempt *, uint64_t session, uint64_t epoch);
/* The only token issuance API. An explicit user/policy attempt calls this;
 * input, swaps, expiry, reset and duplicate invocation never call it. */
bool arc_riscpc_attempt_prepare(ArcRpcLaunchAttempt *, const ArcRpcLaunchPlan *,
                             unsigned capabilities, ArcRpcAttemptToken *);
bool arc_riscpc_attempt_matches(const ArcRpcLaunchAttempt *, const ArcRpcAttemptToken *);
/* Only APPLIED grants permission for a first effect. No capability gate. */
ArcRpcTransition arc_riscpc_attempt_consume(ArcRpcLaunchAttempt *, const ArcRpcAttemptToken *);
ArcRpcTransition arc_riscpc_attempt_event(ArcRpcLaunchAttempt *, const ArcRpcAttemptToken *,
                                       uint64_t event_serial, ArcRpcAttemptEvent);
bool arc_riscpc_attempt_interrupt(ArcRpcLaunchAttempt *, ArcRpcAttemptInterrupt);
/* Caller certifies successful replacement; failure keeps guest stopped. */
bool arc_riscpc_attempt_noop_ready(ArcRpcLaunchAttempt *, bool replaced);
bool arc_riscpc_attempt_established(const ArcRpcLaunchAttempt *);
#endif
