/* Compile the engine's own RETROK aliases and A3000 table into core_tests so
 * catalogue provenance is checked against its source rather than duplicated
 * coordinates. GPL-2.0-only. */
#include "keytable_provenance.h"
#include "libretro.h"
#include "../engine/arculator/src/plat_input.h"
#include "../engine/arculator/src/keytable.h"

bool arc_test_keytable_position(unsigned key, unsigned *row, unsigned *column)
{
   bool found = false;
   unsigned expected_row = 0u, expected_column = 0u;
   for (unsigned i = 0u; keys[i][0] >= 0; ++i)
      if ((unsigned)keys[i][0] == key)
      {
         unsigned candidate_row = (unsigned)keys[i][1];
         unsigned candidate_column = (unsigned)keys[i][2];
         if (found && (candidate_row != expected_row ||
                       candidate_column != expected_column))
            return false;
         found = true;
         expected_row = candidate_row;
         expected_column = candidate_column;
      }
   if (found)
   {
      if (row) *row = expected_row;
      if (column) *column = expected_column;
   }
   return found;
}
