/* Evidence-backed decoded-JFD native routes. GPL-2.0-only. */
#include "native_route.h"
#include "adffs.h"

#include <stdio.h>
#include <string.h>

static const ArcNativeRoute routes[] = {
   {"6a7011a788240828e2aca124be425158b2801569", 721880u, 10059u, 1u, 1u, "S33 idx 33"},
   {"695a9957ba3b91dd2c2eebb34dabbd844bd223fb", 818375u, 10060u, 1u, 1u, "S33 idx 34"},
   {"df41b18619476b3bba06570c455374e69971a936", 701872u, 10091u, 1u, 1u, "S33 idx 35"},
   {"274385590c4a3dc6b9ddd682e7c9147dc28e25c8", 750561u, 10624u, 1u, 1u, "S33 idx 45"},
   {"de24c33dd035930cac6fe8536dfd83b019a52375", 819777u, 10103u, 1u, 1u, "S33 idx 62"},
   {"de026796317fa0ac1a6012a9bc76062749e36926", 828174u, 10157u, 1u, 1u, "S33 idx 82"},
   {"9feb4b78fc5b3c5ff567d6fe61817aa6c7037107", 826740u, 10192u, 1u, 1u, "S33 idx 98"},
   {"73173ae7ac9d970ab8472ba5cc40c0c0e08508ae", 450481u, 10565u, 1u, 1u, "S33 idx 102"},
   {"cce6330591b7d00daf9529407946bba04444fc5d", 534208u, 10564u, 1u, 1u, "S33 idx 128"},
   {"104d529bd00f6f83984b0e9304fdab6a0df8db1e", 277152u, 10410u, 1u, 1u, "S33 idx 153"},
#if defined(ARCDROID_TEST_FAULTS)
   /* Invented fixture only; never present in the shipping core. */
   {"c4547664ee2d0706596f9ae8b2bd12a114c54224", 636u, 54321u, 1u, 1u, "synthetic route fixture"},
#endif
};

const ArcNativeRoute *arc_native_routes(size_t *count)
{
   if (count) *count = sizeof(routes) / sizeof(routes[0]);
   return routes;
}

const ArcNativeRoute *arc_native_route_match(const char *sha1, size_t size,
   unsigned game_id, unsigned disc_number, unsigned disc_count)
{
   if (!sha1) return NULL;
   size_t count = 0u;
   const ArcNativeRoute *table = arc_native_routes(&count);
   for (size_t i = 0u; i < count; ++i)
      if (size == table[i].decoded_size && game_id == table[i].game_id &&
          disc_number == table[i].disc_number && disc_count == table[i].disc_count &&
          !strcmp(sha1, table[i].sha1)) return &table[i];
   return NULL;
}

bool arc_native_routes_validate_table(const ArcNativeRoute *table, size_t count,
   char *error, size_t error_size)
{
   if (count && !table) goto invalid;
   for (size_t i = 0u; i < count; ++i) {
      const ArcNativeRoute *route = &table[i];
      if (!route->sha1 || strlen(route->sha1) != 40u || !route->decoded_size ||
          !route->game_id || route->disc_number != 1u || route->disc_count != 1u ||
          !route->evidence || !route->evidence[0] ||
          arc_install_profile(route->game_id, route->disc_count)) goto invalid;
      for (unsigned j = 0u; j < 40u; ++j)
         if (!((route->sha1[j] >= '0' && route->sha1[j] <= '9') ||
               (route->sha1[j] >= 'a' && route->sha1[j] <= 'f'))) goto invalid;
      for (size_t prior = 0u; prior < i; ++prior)
         if (!strcmp(route->sha1, table[prior].sha1) ||
             route->game_id == table[prior].game_id) goto invalid;
   }
   return true;
invalid:
   if (error && error_size)
      snprintf(error, error_size, "Invalid or duplicate ArcDroid native route");
   return false;
}

bool arc_native_routes_validate(char *error, size_t error_size)
{
   size_t count = 0u;
   const ArcNativeRoute *table = arc_native_routes(&count);
   return arc_native_routes_validate_table(table, count, error, error_size);
}
