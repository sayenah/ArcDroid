/*Arculator 2.2 by Sarah Walker
  Disc support*/
/* ArcDroid modification (2026-09-14): validated media kind and geometry cross
 * the adapter boundary explicitly; empty-drive commands terminate normally. */
#include <stdio.h>
#include <string.h>
#include "arc.h"
#include "config.h"

#include "disc.h"

#include "disc_adf.h"
#include "disc_jfd.h"
#include "ioc.h"
#include "arc_io.h"
#include "timer.h"

char discname[4][512];
int defaultwriteprot = 0;
int disc_noise_gain;

static emu_timer_t disc_timer;

disc_funcs_t *drive_funcs[4];

int disc_drivesel = 0;
static int disc_notfound = 0;

int curdrive = 0;
int discchange[4];
int fdctype;
int readflash[4];
int motoron;
int writeprot[4];

static int disc_current_track[4] = {0, 0, 0, 0};

static uint64_t disc_poll_time;
static const int disc_poll_times[4] =
{
	32, /*Double density*/
	32, /*Double density*/
	16, /*High density*/
	8   /*Extended density - supported by SuperIO but never used on the Arc*/
};

fdc_funcs_t *fdc_funcs;
void *fdc_p;
emu_timer_t *fdc_timer;
int fdc_overridden;
int fdc_ready;

bool disc_load_raw(int drive, char *fn, ArcMediaGeometry geometry)
{
	if (!fn || drive < 0 || drive >= 4) return false;
	rpclog("disc_load %i %s geometry=%i\n", drive, fn, (int)geometry);
	switch (geometry)
	{
		case ARC_GEOMETRY_320K: adf_load(drive, fn); break;
		case ARC_GEOMETRY_640K_L: adl_load(drive, fn); break;
		case ARC_GEOMETRY_800K_D:
		case ARC_GEOMETRY_800K_E: adf_arcdd_load(drive, fn); break;
		default: return false;
	}
	return !disc_empty(drive);
}

bool disc_load_jfd(int drive, const ArcJfdImage *image)
{
	return jfd_load(drive, image);
}

void disc_new(int drive, char *fn)
{
	(void)drive; (void)fn;
	fatal("Creating floppy images is outside ArcDroid Slice 1");
}

void disc_close(int drive)
{
	rpclog("disc_close %i\n", drive);
	if (drive_funcs[drive])
	{
		drive_funcs[drive]->stop();
		drive_funcs[drive]->close(drive);
		drive_funcs[drive] = NULL;
	}
	disc_notfound = 0;
}

int disc_empty(int drive)
{
	return !drive_funcs[drive];
}

void disc_init()
{
	memset(drive_funcs, 0, sizeof(drive_funcs));
	memset(disc_current_track, 0, sizeof(disc_current_track));
	memset(discchange, 0, sizeof(discchange));
	memset(readflash, 0, sizeof(readflash));
	disc_notfound = 0;
	jfd_init();
}

void disc_reset()
{
	curdrive = 0;
	timer_add(&disc_timer, disc_poll, NULL, 0);
}

void disc_poll(void *p)
{
	if (drive_funcs[disc_drivesel] && drive_funcs[disc_drivesel]->high_res_poll)
		timer_advance_u64(&disc_timer, disc_poll_time >> 4);
	else
		timer_advance_u64(&disc_timer, disc_poll_time);
	if (drive_funcs[disc_drivesel])
	{
		if (drive_funcs[disc_drivesel]->poll)
			drive_funcs[disc_drivesel]->poll();
	}
	if (disc_notfound)
	{
		disc_notfound--;
		if (!disc_notfound && fdc_funcs)
			fdc_funcs->notfound(fdc_p);
	}
}

int disc_get_current_track(int drive)
{
	return disc_current_track[drive];
}

void disc_seek(int drive, int new_track)
{
	int distance = new_track - disc_current_track[drive];
	if (drive_funcs[drive] && drive_funcs[drive]->seek)
		drive_funcs[drive]->seek(drive, new_track);
	if (new_track != disc_current_track[drive] && !disc_empty(drive))
		ioc_discchange_clear(drive);
	if (fdc_timer)
	{
		if (distance < 0) distance = -distance;
		timer_set_delay_u64(fdc_timer,
		                    (distance ? (uint64_t)distance * 6000u : 200u) * TIMER_USEC);
	}
	disc_current_track[drive] = new_track;
}

void disc_readsector(int drive, int sector, int track, int side, int density)
{
	if (drive_funcs[drive] && drive_funcs[drive]->readsector)
		drive_funcs[drive]->readsector(drive, sector, track, side, density);
	else
		disc_notfound = 10000;
}

void disc_writesector(int drive, int sector, int track, int side, int density)
{
	if (drive_funcs[drive] && drive_funcs[drive]->writesector)
		drive_funcs[drive]->writesector(drive, sector, track, side, density);
	else
		disc_notfound = 10000;
}

void disc_readaddress(int drive, int track, int side, int density)
{
	if (drive_funcs[drive] && drive_funcs[drive]->readaddress)
		drive_funcs[drive]->readaddress(drive, track, side, density);
	else
		disc_notfound = 10000;
}

void disc_format(int drive, int track, int side, int density)
{
	if (drive_funcs[drive] && drive_funcs[drive]->format)
		drive_funcs[drive]->format(drive, track, side, density);
	else
		disc_notfound = 10000;
}

void disc_stop(int drive)
{
	if (drive_funcs[drive] && drive_funcs[drive]->stop)
		drive_funcs[drive]->stop();
}

void disc_set_motor(int enable)
{
	if (!enable)
		timer_disable(&disc_timer);
	else if (!timer_is_enabled(&disc_timer))
	{
		jfd_motor_start();
		timer_set_delay_u64(&disc_timer, disc_poll_time);
	}
}

void disc_set_density(int density)
{
	disc_poll_time = (disc_poll_times[density] * TIMER_USEC);
}
