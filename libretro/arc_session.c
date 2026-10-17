/* Library-lifetime monotonic session IDs. GPL-2.0-only. */
#include "arc_session.h"
bool arc_session_id_allocate(ArcSessionIdSource *source, uint64_t *out)
{
    if (!out) return false;
    *out = 0;
    if (!source || source->last == UINT64_MAX) return false;
    *out = ++source->last;
    return true;
}
