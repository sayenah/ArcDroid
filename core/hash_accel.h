/* ArcDroid hash implementation identity probes. GPL-2.0-only. */
#ifndef ARC_HASH_ACCEL_H
#define ARC_HASH_ACCEL_H
#include <stdbool.h>
#include <stdint.h>
enum { ARC_HASH_PORTABLE=1, ARC_HASH_SLICE8=2, ARC_HASH_ARM_SHA2=4,
       ARC_HASH_ARM_CRC32=8, ARC_HASH_X86_SHA=16 };
#ifdef ARCDROID_HASH_TEST
/* Test executable only: never bypass capability detection to execute an ISA. */
unsigned arc_hash_test_available(void);
bool arc_hash_test_select(unsigned implementation); /* zero = automatic */
#endif
#endif
