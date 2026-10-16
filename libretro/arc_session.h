/* Frontend session lifetime, independent of engine construction. GPL-2.0-only. */
#ifndef ARC_SESSION_H
#define ARC_SESSION_H
#include <stdbool.h>
#include <stdint.h>
typedef enum { ARC_SESSION_KIND_EMPTY, ARC_SESSION_KIND_ARCULATOR,
    ARC_SESSION_KIND_RISCPC } ArcSessionKind;
/* Inject an instance for deterministic/exhaustion tests. Never persist it. */
typedef struct { uint64_t last; } ArcSessionIdSource;
bool arc_session_id_allocate(ArcSessionIdSource *source, uint64_t *out);
/* Current frontend admission; zero/EMPTY outside a session. The ID survives
 * Restart and preparation recovery, but never library unload. Part 2 binds its
 * attempt invalidation epoch separately; the RTC/observer epochs are unrelated. */
ArcSessionKind arc_frontend_session_kind(void);
uint64_t arc_frontend_session_id(void);
#endif
