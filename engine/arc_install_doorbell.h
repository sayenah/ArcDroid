/* Controller-command install completion latch. GPL-2.0-only. */
#ifndef ARCDROID_INSTALL_DOORBELL_H
#define ARCDROID_INSTALL_DOORBELL_H
#include "adffs.h"
#include <stdbool.h>
void arc_install_doorbell_configure(const ArcInstallDoorbell *doorbell);
void arc_install_doorbell_read(unsigned drive, unsigned track,
                               unsigned side, unsigned sector,
                               unsigned density);
bool arc_install_doorbell_latched(void);
#endif
