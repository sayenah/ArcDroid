/* ArcDroid controller-command completion latch. GPL-2.0-only. */
#include "arc_install_doorbell.h"
static ArcInstallDoorbell target;
static bool enabled, latched;
void arc_install_doorbell_configure(const ArcInstallDoorbell *doorbell)
{
   enabled = doorbell != 0;
   latched = false;
   if (doorbell) target = *doorbell;
}
void arc_install_doorbell_read(unsigned drive, unsigned track,
                               unsigned side, unsigned sector,
                               unsigned density)
{
   if (enabled && !latched && drive == 0u && density == 1u &&
       track == target.track && side == target.side &&
       sector == target.sector) latched = true;
}
bool arc_install_doorbell_latched(void)
{ return latched; }
