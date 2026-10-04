/* ArcDroid trial frontend paths. GPL-2.0-only. */
#ifndef PROBE_FRONTEND_H
#define PROBE_FRONTEND_H
#include "libretro.h"
void probe_frontend_paths(retro_environment_t environment);
const char *probe_frontend_option(const char *key,const char *value);
#endif
