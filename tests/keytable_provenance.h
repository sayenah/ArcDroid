/* Test-only bridge to Arculator's compiled keyboard table. GPL-2.0-only. */
#ifndef ARCDROID_TEST_KEYTABLE_PROVENANCE_H
#define ARCDROID_TEST_KEYTABLE_PROVENANCE_H

#include <stdbool.h>

bool arc_test_keytable_position(unsigned key, unsigned *row, unsigned *column);

#endif
