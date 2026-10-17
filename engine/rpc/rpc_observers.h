/* RPC profiles over shared pure observers. GPL-2.0-only. */
#ifndef ARC_RPC_OBSERVERS_H
#define ARC_RPC_OBSERVERS_H
#include "arcdroid_engine.h"
#include "rpc_observer.h"
#define ARC_RPC_PROFILE_REVISION 1u
void arc_rpc_observers_retire(void);
void arc_rpc_observers_rebase(unsigned actual_cpu);
void arc_rpc_observers_bind(unsigned catalogue, unsigned actual_cpu);
void arc_rpc_observers_frame(uint64_t now_us);
void arc_rpc_observers_error_snapshot(ArcErrorBox *);
bool arc_rpc_observers_answer(ArcErrorAnswer);
void arc_rpc_observers_discard(void);
void arc_rpc_observers_boot_arm(void);
void arc_rpc_observers_boot_cancel(void);
ArcBootWatchState arc_rpc_observers_boot_state(void);
bool arc_rpc_observers_launch_arm(const char *);
void arc_rpc_observers_launch_cancel(const char *);
void arc_rpc_observers_refused(unsigned);
void arc_rpc_observers_launch_snapshot(ArcLaunchOutcome *);
bool arc_rpc_observers_attempt(const ArcRpcLaunchBinding *);
void arc_rpc_observers_attempt_input(void);
void arc_rpc_observers_attempt_interrupt(ArcRpcAttemptInterrupt);
void arc_rpc_observers_attempt_media(void);
void arc_rpc_observers_disc_configure(bool,const char *,ArcDiscResolveCallback,ArcDiscObservationCallback,void *);
void arc_rpc_observers_disc_clear(void);
void arc_rpc_observers_disc_snapshot(ArcDiscRequest *);
void arc_rpc_observers_disc_notified(void);
void arc_rpc_observers_disc_status(ArcRpcDiscStatus *);
bool arc_rpc_observers_replace(ArcDiscSignal);
bool arc_rpc_observers_capability(unsigned bit);
/* Host-only identity query, supplied by the dispatcher from current media. */
bool arc_rpc_observers_identities(char helper[11],char hd[11]);
#if defined(ARCDROID_TEST_FAULTS) || defined(ARCDROID_TEST_API)
bool arc_rpc_observers_test_error_numbers(ArcErrorNumberClass);
void arc_rpc_observers_test_cells(unsigned);
void arc_rpc_observers_test_boot_snapshot(ArcBootWatch *);
void arc_rpc_observers_test_horizon(unsigned);
void arc_rpc_observers_test_binding(unsigned *,unsigned *);
bool arc_rpc_observers_test_disc_devices(unsigned forms);
void arc_rpc_observers_test_buttons(uint32_t,uint32_t,uint32_t);
void arc_rpc_observers_test_watch_budget(unsigned);
/* White-box snapshots isolate retry guards that the final replace gate also
 * protects. Only test builds can restore a deliberately stale disc identity. */
bool arc_rpc_observers_test_retry(ArcRpcHostWork *);
void arc_rpc_observers_test_disc_state(const ArcDiscRequest *);
void arc_rpc_observers_override(unsigned mask,uint32_t r1,uint32_t r2,uint32_t forms);
#endif
#endif
