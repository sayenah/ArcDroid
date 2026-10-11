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
bool arc_engine_test_cmos_slice(uint64_t ns);
bool arc_engine_test_cmos_schedule(uint64_t deadline_ns, uint64_t delete_ns);
RETRO_API void arc_engine_test_cmos_evidence(uint8_t bytes[256], uint8_t mask[32], uint64_t *writes, uint64_t *last, uint64_t *now, unsigned *state);
RETRO_API bool arc_engine_test_floppy_mount(unsigned drive, const ArcEngineMedia *media, char *error, size_t size);
RETRO_API void arc_engine_test_floppy_eject(unsigned drive);
uint64_t arc_test_clock_ns(void);
/* Plan frontend has already retired shared input before engine reset. Consume
 * the backend's pending retirement before gathering the restarted script. */
void arc_engine_test_plan_input_ready(void);
RETRO_API uint64_t arc_engine_test_frame_ns(void);
RETRO_API bool arc_engine_test_ide_backend(const ArcIdeBackend *backend);
void arc_engine_test_frame_add_ns(uint64_t elapsed);
#endif
#endif
