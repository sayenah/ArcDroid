/* ArcDroid JFD engine reader. Copyright 2026 ArcDroid contributors.
 * GPL-2.0-only. Rewritten for ArcDroid; upstream Arculator credited. */
#ifndef ARCDROID_DISC_JFD_H
#define ARCDROID_DISC_JFD_H

#include "jfd.h"

void jfd_init(void);
bool jfd_load(int drive, const ArcJfdImage *image);
bool jfd_active(int drive);
void jfd_motor_start(void);

#endif
