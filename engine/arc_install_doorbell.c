/* ArcDroid controller-command completion latch. GPL-2.0-only. */
#include "arc_install_doorbell.h"
static ArcInstallDoorbell target;
static bool enabled, latched;
#if defined(ARCDROID_TEST_FAULTS)
static unsigned read_commands[2];
unsigned arc_install_test_read_commands(unsigned drive)
{ return drive < 2u ? read_commands[drive] : 0u; }
#endif
void arc_install_doorbell_configure(const ArcInstallDoorbell *doorbell)
{
   enabled = doorbell != 0;
   latched = false;
#if defined(ARCDROID_TEST_FAULTS)
   read_commands[0] = read_commands[1] = 0u;
#endif
   if (doorbell) target = *doorbell;
}
void arc_install_doorbell_read(unsigned drive, unsigned track,
                               unsigned side, unsigned sector,
                               unsigned density)
{
#if defined(ARCDROID_TEST_FAULTS)
   if (drive < 2u) ++read_commands[drive];
#endif
   if (enabled && !latched && drive == 0u && density == 1u &&
       track == target.track && side == target.side &&
       sector == target.sector) latched = true;
}
bool arc_install_doorbell_latched(void)
{ return latched; }
