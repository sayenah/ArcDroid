/* ArcDroid boot outcome observer. Copyright 2026 ArcDroid contributors. GPL-2.0-only. */
#include "arc_boot_watch.h"
#include <string.h>

enum { ARC_BOOT_HORIZON_US = 40000000u, ARC_BOOT_WAIT_US = 2000000u };

void arc_boot_watch_clear(ArcBootWatch *watch)
{ memset(watch, 0, sizeof(*watch)); }

void arc_boot_watch_arm(ArcBootWatch *watch, uint64_t now_us)
{
   arc_boot_watch_clear(watch);
   watch->state = ARC_BOOT_WATCH_ARMED;
   watch->armed_us = now_us;
}

void arc_boot_watch_cancel(ArcBootWatch *watch)
{
   if (watch->state == ARC_BOOT_WATCH_ARMED)
   { watch->state = ARC_BOOT_WATCH_CANCELLED; watch->pending = false; }
}

void arc_boot_watch_observe(ArcBootWatch *watch, uint32_t swi,
                            uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3,
                            uint32_t address, uint32_t mode, bool in_rom,
                            uint64_t now_us)
{
   (void)r1; (void)r2; (void)r3; (void)address; (void)mode;
   if (watch->state != ARC_BOOT_WATCH_ARMED) return;
   if (now_us - watch->armed_us >= (watch->profile ? watch->profile->horizon_us : ARC_BOOT_HORIZON_US))
   { watch->state = ARC_BOOT_WATCH_EXPIRED; watch->pending = false; return; }
   watch->last_event_us = now_us;
   /* The X bit is the only decoration removed from the 24-bit SWI field. */
   swi = (swi & 0x00ffffffu) & ~0x00020000u;
   if (swi == 0x400c0u)
   { watch->state = ARC_BOOT_WATCH_DISABLED; watch->pending = false; }
   else if (swi == 5u) watch->pending = false;
   else if (swi == 0x0eu && r0 < (watch->profile ? watch->profile->buffer_limit : 0x8000u) &&
       (in_rom || (watch->profile && !watch->profile->rom)) &&
       (!watch->profile || !watch->profile->user_only || mode == 0u || mode == 16u))
   { watch->read_us = now_us; watch->pending = true; }
}

void arc_boot_watch_frame(ArcBootWatch *watch, uint64_t now_us)
{
   if (watch->state != ARC_BOOT_WATCH_ARMED) return;
   if (now_us - watch->armed_us >= (watch->profile ? watch->profile->horizon_us : ARC_BOOT_HORIZON_US))
   { watch->state = ARC_BOOT_WATCH_EXPIRED; watch->pending = false; }
   else if (watch->pending && now_us - watch->read_us >= (watch->profile ? watch->profile->wait_us : ARC_BOOT_WAIT_US))
   { watch->state = ARC_BOOT_WATCH_PROMPT; watch->pending = false; }
}

void arc_boot_watch_arm_profile(ArcBootWatch *watch,uint64_t now_us,const ArcBootProfile *profile)
{
   if(!watch || !profile || !profile->horizon_us || !profile->wait_us)return;
   arc_boot_watch_arm(watch,now_us);watch->profile=profile;
}
