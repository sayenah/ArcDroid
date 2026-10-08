/* Non-shipping plan frontend. GPL-2.0-only. */
#ifndef ARC_TEST_PLAN_H
#define ARC_TEST_PLAN_H
#include "arcdroid_core.h"
#include "libretro.h"
#include <stdbool.h>
#include <stddef.h>
#define ARC_TEST_PLAN_EXTENSION "arcplan"
bool arc_test_plan_path(const char *path);
bool arc_test_plan_load(const char *path, const char *system, const char *save,
                        char *error, size_t error_size);
void arc_test_plan_clear(void);
void arc_test_plan_set_notice(void (*notice)(const char *, bool));
bool arc_test_plan_pending(void);
bool arc_test_plan_slice(char *error, size_t size);
bool arc_test_plan_persist(char *error, size_t size);
RETRO_API unsigned arc_test_plan_cmos_phase(void);
RETRO_API bool arc_test_plan_cmos_pacing(unsigned frames);
void arc_test_plan_reset(void);
void arc_test_plan_input(ArcInputState *input);
#endif
