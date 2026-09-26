/* Exact native-route validation with invented identities only. GPL-2.0-only. */
#include "native_route.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
   char error[128] = {0};
   assert(arc_native_routes_validate(error, sizeof(error)));
   size_t count = 0u;
   const ArcNativeRoute *table = arc_native_routes(&count);
   assert(table && count == 11u);
   const ArcNativeRoute fixture = table[count - 1u];
   assert(fixture.game_id == 54321u && fixture.decoded_size == 636u);
   assert(arc_native_route_match(fixture.sha1, fixture.decoded_size,
      fixture.game_id, 1u, 1u) == &table[count - 1u]);
   assert(!arc_native_route_match("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
      fixture.decoded_size, fixture.game_id, 1u, 1u));
   assert(!arc_native_route_match(fixture.sha1, fixture.decoded_size + 1u,
      fixture.game_id, 1u, 1u));
   assert(!arc_native_route_match(fixture.sha1, fixture.decoded_size,
      fixture.game_id + 1u, 1u, 1u));
   assert(!arc_native_route_match(fixture.sha1, fixture.decoded_size,
      fixture.game_id, 2u, 1u));
   assert(!arc_native_route_match(fixture.sha1, fixture.decoded_size,
      fixture.game_id, 1u, 2u));

   ArcNativeRoute rows[2] = {fixture, fixture};
   assert(!arc_native_routes_validate_table(NULL, 1u, error, sizeof(error)));
   assert(!arc_native_routes_validate_table(rows, 2u, error, sizeof(error)));
   rows[1].sha1 = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
   assert(!arc_native_routes_validate_table(rows, 2u, error, sizeof(error)));
   rows[1].game_id++;
   assert(arc_native_routes_validate_table(rows, 2u, error, sizeof(error)));
   rows[1].game_id = 10587u; /* runtime install classification is (id, count) */
   assert(!arc_native_routes_validate_table(rows, 2u, error, sizeof(error)));
   rows[1].game_id = 54322u;
   rows[1].sha1 = "CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC";
   assert(!arc_native_routes_validate_table(rows, 2u, error, sizeof(error)));
   rows[1].sha1 = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
   rows[1].disc_number = 2u;
   assert(!arc_native_routes_validate_table(rows, 2u, error, sizeof(error)));
   rows[1].disc_number = 1u;
   rows[1].disc_count = 2u;
   assert(!arc_native_routes_validate_table(rows, 2u, error, sizeof(error)));
   rows[1].disc_count = 1u;
   rows[1].decoded_size = 0u;
   assert(!arc_native_routes_validate_table(rows, 2u, error, sizeof(error)));
   rows[1].decoded_size = 636u;
   rows[1].evidence = "";
   assert(!arc_native_routes_validate_table(rows, 2u, error, sizeof(error)));
   rows[1].evidence = NULL;
   assert(!arc_native_routes_validate_table(rows, 2u, error, sizeof(error)));
   rows[1].evidence = "synthetic second route";
   rows[1].sha1 = NULL;
   assert(!arc_native_routes_validate_table(rows, 2u, error, sizeof(error)));
   rows[1].sha1 = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
   assert(!arc_native_routes_validate_table(rows, 2u, error, sizeof(error)));
   rows[1].sha1 = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
   assert(!arc_native_routes_validate_table(rows, 2u, error, sizeof(error)));
   puts("native route table passed");
   return 0;
}
