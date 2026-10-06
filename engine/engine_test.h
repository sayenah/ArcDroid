/* ArcDroid test-only dispatcher admission/measurement. GPL-2.0-only. */
#ifndef ARC_ENGINE_TEST_H
#define ARC_ENGINE_TEST_H
#include "arcdroid_engine.h"
#if defined(ARCDROID_TEST_FAULTS)
#include "libretro.h"
/* Model order: ARM610, ARM710, SA-110, ARM7500. Rate zero selects the
 * accepted model default; explicit overrides are 1..100 MIPS. */
bool arc_engine_test_plan_start(unsigned model, unsigned rate, bool factory,
    const uint8_t *rom, size_t size, const uint8_t seed[256], uint64_t epoch,
    char *error, size_t error_size);
uint64_t arc_test_clock_ns(void);
/* Plan frontend has already retired shared input before engine reset. Consume
 * the backend's pending retirement before gathering the restarted script. */
void arc_engine_test_plan_input_ready(void);
RETRO_API uint64_t arc_engine_test_frame_ns(void);
#endif
#endif
