/* ArcDroid boot outcome observer. Copyright 2026 ArcDroid contributors. GPL-2.0-only. */
#ifndef ARC_BOOT_WATCH_H
#define ARC_BOOT_WATCH_H
#include <stdbool.h>
#include <stdint.h>

typedef enum {
   ARC_BOOT_WATCH_DISABLED = 0,
   ARC_BOOT_WATCH_ARMED,
   ARC_BOOT_WATCH_EXPIRED,
   ARC_BOOT_WATCH_CANCELLED,
   ARC_BOOT_WATCH_PROMPT
} ArcBootWatchState;

typedef struct { uint64_t horizon_us, wait_us; uint32_t buffer_limit; bool user_only, rom;
   bool contextual; /* RPC: caller supplies a bound launch phase + return watch. */
} ArcBootProfile;
typedef struct {
   const ArcBootProfile *profile;
   ArcBootWatchState state;
   uint64_t armed_us;
   uint64_t read_us;
   uint64_t last_event_us;
   bool pending;
} ArcBootWatch;

void arc_boot_watch_arm_profile(ArcBootWatch *,uint64_t,const ArcBootProfile *);
void arc_boot_watch_clear(ArcBootWatch *watch);
void arc_boot_watch_arm(ArcBootWatch *watch, uint64_t now_us);
void arc_boot_watch_cancel(ArcBootWatch *watch);
void arc_boot_watch_observe(ArcBootWatch *watch, uint32_t swi,
                            uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3,
                            uint32_t address, uint32_t mode, bool in_rom,
                            uint64_t now_us);
void arc_boot_watch_frame(ArcBootWatch *watch, uint64_t now_us);

/* Scalar bridge from the pinned ARM interpreter into the engine owner. */
void arc_engine_boot_watch_swi(uint32_t swi, uint32_t r0, uint32_t r1,
                               uint32_t r2, uint32_t r3, uint32_t address,
                               uint32_t mode);

#endif
