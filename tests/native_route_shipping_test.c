/* Shipping route table excludes invented test entries. GPL-2.0-only. */
#include "native_route.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
   size_t count = 0u;
   assert(arc_native_routes_validate(NULL, 0u));
   assert(arc_native_routes(&count) && count == 10u);
   assert(!arc_native_route_match("c4547664ee2d0706596f9ae8b2bd12a114c54224",
      636u, 54321u, 1u, 1u));
   puts("shipping native routes passed");
   return 0;
}
