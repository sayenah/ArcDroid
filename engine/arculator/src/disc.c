/*Arculator 2.2 by Sarah Walker
  Disc support*/
/* ArcDroid modification (2026-09-14): Slice 1 registers only the validated ADF/ADL subset.
 * WD1772 seek completion is kept independent of the excluded drive-noise
 * mixer, which upstream also (incorrectly) used as its command timer owner. */
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include "arc.h"
#include "config.h"

#include "disc.h"

#include "disc_adf.h"
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

static int driveloaders[4];

void disc_load(int drive, char *fn)
{
	int size;
	ArcIoFile *file;
	rpclog("disc_load %i %s\n", drive, fn);
//        setejecttext(drive, "");
	if (!fn) return;
	file = arc_io_open_file(fn, false);
	if (!file) return;
	int64_t length = arc_io_file_length(file);
	arc_io_close_file(file);
	if (length < 0 || length > INT_MAX) return;
	size = (int)length;
	/* ArcDroid validates extensions at the frontend boundary, then selects the
	 * raw geometry by exact size so .adf and .adl remain interchangeable hints. */
	rpclog("Size %i\n", size);
	if (size == (800*1024)) /*800k ADFS/DOS - 80*2*5*1024*/
	{
		driveloaders[drive] = 1;
		adf_arcdd_load(drive, fn);
		return;
	}
	if (size == (640*1024)) /*640k ADFS/DOS - 80*2*16*256*/
	{
		driveloaders[drive] = 2;
		adl_load(drive, fn);
		return;
	}
	if (size == (320 * 1024))
	{
		driveloaders[drive] = 0;
		adf_load(drive, fn);
		return;
	}
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
		drive_funcs[drive]->close(drive);
		drive_funcs[drive] = NULL;
	}
	disc_stop(drive);
}

int disc_empty(int drive)
{
	return !drive_funcs[drive];
}

void disc_init()
{
	memset(drive_funcs, 0, sizeof(drive_funcs));
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
		if (disc_notfound)
		{
			disc_notfound--;
			if (!disc_notfound)
				fdc_funcs->notfound(fdc_p);
		}
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
		timer_set_delay_u64(&disc_timer, disc_poll_time);
}

void disc_set_density(int density)
{
	disc_poll_time = (disc_poll_times[density] * TIMER_USEC);
}
