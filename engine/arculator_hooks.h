/* ArcDroid private Arculator CPU hooks. GPL-2.0-only. */
#ifndef ARCDROID_ARCULATOR_HOOKS_H
#define ARCDROID_ARCULATOR_HOOKS_H

#include <stdbool.h>
#include <stdint.h>

#if defined(ARCDROID_TEST_FAULTS)
#include "arc_boot_watch.h"
#include <stddef.h>
void arc_engine_test_boot_watch_snapshot(ArcBootWatch *out);
bool arc_engine_test_guest_word(uint32_t address, uint32_t *out);
size_t arc_engine_test_media_extent(void);
unsigned arc_engine_test_disc_resolve_calls(void);
bool arc_engine_test_disc_timer_busy(bool busy);
void arc_engine_test_disc_hold_busy(bool busy);
unsigned arc_engine_test_disc_eject_count(void);
#endif

void arc_engine_error_mode(uint32_t old_mode, uint32_t new_mode);
bool arc_engine_error_poll(uint32_t address, uint32_t mode,
                           uint32_t sp, uint32_t lr, uint8_t *key);
void arc_engine_disc_poll(uint32_t address, uint32_t mode, uint32_t sp,
                           uint32_t r0, uint32_t flags);
extern bool arc_engine_disc_watch_armed;
void arc_engine_adf_refused(unsigned drive, unsigned track,
                           unsigned side, unsigned sector);

#endif
