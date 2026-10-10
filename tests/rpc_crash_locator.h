/* Non-shipping Windows plan-core diagnostics. GPL-2.0-only. */
#ifndef ARC_RPC_CRASH_LOCATOR_H
#define ARC_RPC_CRASH_LOCATOR_H
#include "libretro.h"
#include <stdbool.h>
bool arc_test_crash_locator_start(const char *save,retro_log_printf_t log);
void arc_test_crash_locator_stop(void);
#endif
