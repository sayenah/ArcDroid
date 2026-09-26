/* Exact decoded-JFD native routing. GPL-2.0-only. */
#ifndef ARCDROID_NATIVE_ROUTE_H
#define ARCDROID_NATIVE_ROUTE_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
   const char *sha1;
   size_t decoded_size;
   unsigned game_id;
   unsigned disc_number;
   unsigned disc_count;
   const char *evidence;
} ArcNativeRoute;

const ArcNativeRoute *arc_native_routes(size_t *count);
const ArcNativeRoute *arc_native_route_match(const char *sha1, size_t size,
   unsigned game_id, unsigned disc_number, unsigned disc_count);
bool arc_native_routes_validate_table(const ArcNativeRoute *routes, size_t count,
   char *error, size_t error_size);
bool arc_native_routes_validate(char *error, size_t error_size);

#endif
