/*Arculator 2.2 by Sarah Walker
  ADFS disc support (really all raw sector formats)*/
/* ArcDroid modifications (2026-09-14, 2026-09-23): unavailable data never
 * terminates the frontend; command/rotation state resets across sessions;
 * index pulses count elapsed guest time at each controller poll period;
 * READ ID sector positions stay with their respective drives.
 * ArcDroid modifications (Slice 15): bounded prefix-sector presence and writeback. */
#include <string.h>
#include "arc.h"
#include "arc_io.h"
#include "disc.h"
#include "disc_adf.h"
/* ArcDroid refusal callback observes the controller's write-protect result. */
static void (*adf_refusal_callback)(unsigned, unsigned, unsigned, unsigned);
void adf_set_refusal_callback(
   void (*callback)(unsigned, unsigned, unsigned, unsigned))
{ adf_refusal_callback = callback; }

static disc_funcs_t adf_disc_funcs;

static void adf_seek(int drive, int track);
static void adf_stop(void);

static struct
{
	ArcIoFile *f;
	uint8_t track_data[2][20*1024];

	int dblside;
	int sectors, size, track;
	int dblstep;
	int density;
	int maxsector;
	int sector_offset;
	int64_t image_size;
	int64_t effective_size;
	int read_id_sector;
} adf[4];

static int adf_sector,   adf_track,   adf_side,    adf_drive;
static int adf_inread,   adf_readpos, adf_inwrite, adf_inreadaddr;
static int adf_notfound;
static unsigned adf_search_indexes;
static size_t adf_extent[4];
static int adf_informat=0;

static int adf_pause = 0;
static unsigned adf_phase_us;
static int adf_writable[4] = {1, 1, 1, 1};

void adf_set_extent(int drive, size_t effective_extent)
{
	if (drive >= 0 && drive < 4) adf_extent[drive] = effective_extent;
}

void adf_set_writable(int drive, int writable)
{
	if (drive >= 0 && drive < 4) adf_writable[drive] = writable != 0;
}

int adf_flush_all(void)
{
	for (int drive = 0; drive < 4; ++drive)
		if (adf[drive].f && !arc_io_flush_file(adf[drive].f))
			return -1;
	return 0;
}

void adf_init()
{
	memset(adf, 0, sizeof(adf));
	adf_sector = adf_track = adf_side = adf_drive = 0;
	adf_inread = adf_readpos = adf_inwrite = adf_inreadaddr = 0;
	adf_notfound = adf_informat = adf_pause = 0;
	adf_search_indexes = 0;
	memset(adf_extent, 0, sizeof(adf_extent));
	adf_phase_us = 0u;
	for (int drive = 0; drive < 4; ++drive) adf_writable[drive] = 1;
}

void adf_loadex(int drive, char *fn, int sectors, int size, int sides, int dblstep, int density, int sector_offset)
{
	writeprot[drive] = 0;
	adf[drive].f = adf_writable[drive] ? arc_io_open_file(fn, true) : NULL;
	if (!adf[drive].f)
	{
		adf[drive].f = arc_io_open_file(fn, false);
		if (!adf[drive].f) return;
		writeprot[drive] = 1;
	}
	adf[drive].image_size = arc_io_file_length(adf[drive].f);
	if (adf[drive].image_size < 0)
	{
		arc_io_close_file(adf[drive].f);
		adf[drive].f = NULL;
		return;
	}
	adf[drive].effective_size = adf_extent[drive] ? (int64_t)adf_extent[drive] : adf[drive].image_size;
	drive_funcs[drive] = &adf_disc_funcs;
	adf[drive].sectors = sectors;
	adf[drive].size = size;
	adf[drive].dblside = sides;
	adf[drive].dblstep = dblstep;
	adf[drive].density = density;
	adf[drive].maxsector = (int)(adf[drive].effective_size / size);
	adf[drive].sector_offset = sector_offset;
	adf[drive].read_id_sector = 0;

	adf_seek(drive, disc_get_current_track(drive));
}

void adf_load(int drive, char *fn)
{
	adf_loadex(drive, fn, 16, 256, 0, 0, 1, 0);
}

void adf_arcdd_load(int drive, char *fn)
{
	adf_loadex(drive, fn, 5, 1024, 1, 0, 1, 0);
}

void adf_archd_load(int drive, char *fn)
{
	rpclog("HDload\n");
	adf_loadex(drive, fn, 10, 1024, 1, 0, 2, 0);
}

void adl_load(int drive, char *fn)
{
	adf_loadex(drive, fn, 16, 256, 1, 0, 1, 0);
}

void ssd_load(int drive, char *fn)
{
	adf_loadex(drive, fn, 10, 256, 0, 0, 0, 0);
}

void dsd_load(int drive, char *fn)
{
	adf_loadex(drive, fn, 10, 256, 1, 0, 0, 0);
}

static void adf_close(int drive)
{
	if (adf_drive == drive) adf_stop();
	if (adf[drive].f) arc_io_close_file(adf[drive].f);
	adf[drive].f = NULL;
}

/* ArcDroid modifications: a prefix may end inside a cylinder. */
static int adf_sector_present(int drive, int track, int side, int sector)
{
    int64_t side_bytes = (int64_t)adf[drive].sectors * adf[drive].size;
    int64_t offset = ((int64_t)track * (adf[drive].dblside ? 2 : 1) + side)
                   * side_bytes + (int64_t)sector * adf[drive].size;
    return track >= 0 && side >= 0 && side <= adf[drive].dblside &&
           sector >= 0 && sector < adf[drive].sectors &&
           offset >= 0 && offset <= adf[drive].effective_size &&
           adf[drive].size <= adf[drive].effective_size - offset;
}

/* ArcDroid modifications: only a sector hidden by a restricted prefix uses
 * index-pulse search. */
static int adf_sector_hidden(int drive, int track, int side, int sector)
{
    int64_t side_bytes = (int64_t)adf[drive].sectors * adf[drive].size;
    int64_t offset = ((int64_t)track * (adf[drive].dblside ? 2 : 1) + side)
                   * side_bytes + (int64_t)sector * adf[drive].size;
    return adf_extent[drive] && track >= 0 && side >= 0 &&
           side <= adf[drive].dblside && sector >= 0 &&
           sector < adf[drive].sectors && offset >= adf[drive].effective_size &&
           offset >= 0 && offset <= adf[drive].image_size &&
           adf[drive].size <= adf[drive].image_size - offset;
}

static void adf_missing(void)
{
    adf_notfound = -1;
    adf_search_indexes = 0;
}

static void adf_seek(int drive, int track)
{
    if (!adf[drive].f) return;
    if (adf[drive].dblstep) track /= 2;
    int64_t side_bytes = (int64_t)adf[drive].sectors * adf[drive].size;
    int64_t track_bytes = side_bytes * (adf[drive].dblside ? 2 : 1);
    int64_t offset = (int64_t)track * track_bytes;
    memset(adf[drive].track_data, 0, sizeof(adf[drive].track_data));
    adf[drive].track = -1;
    if (adf_extent[drive] && track >= 0 && offset >= adf[drive].effective_size &&
        offset < adf[drive].image_size)
    { adf[drive].track = track; return; }
    if (track < 0 || track_bytes <= 0 || offset < 0 ||
        offset >= adf[drive].effective_size ||
        arc_io_seek_file(adf[drive].f, offset, RETRO_VFS_SEEK_POSITION_START) < 0)
        return;
    int64_t remaining = adf[drive].effective_size - offset;
    int64_t bytes = remaining < track_bytes ? remaining : track_bytes;
    for (int side = 0; side <= adf[drive].dblside; ++side)
    {
        size_t amount = bytes > side_bytes ? (size_t)side_bytes : (size_t)bytes;
        if (amount && arc_io_read_file(adf[drive].f, adf[drive].track_data[side], amount) != amount)
            return;
        bytes -= (int64_t)amount;
    }
    adf[drive].track = track;
}

static void adf_writeback(int drive, int track, int side, int first, int count)
{
    if (!adf[drive].f) return;
    if (adf[drive].dblstep) track /= 2;
    int64_t side_bytes = (int64_t)adf[drive].sectors * adf[drive].size;
    int64_t offset = ((int64_t)track * (adf[drive].dblside ? 2 : 1) + side)
                   * side_bytes + (int64_t)first * adf[drive].size;
    size_t bytes = (size_t)count * adf[drive].size;
    if (offset < 0 || offset > adf[drive].effective_size ||
        bytes > (size_t)(adf[drive].effective_size - offset) ||
        arc_io_seek_file(adf[drive].f, offset, RETRO_VFS_SEEK_POSITION_START) < 0 ||
        arc_io_write_file(adf[drive].f,
            adf[drive].track_data[side] + (size_t)first * adf[drive].size,
            bytes) != bytes)
        fatal("Unable to write complete floppy track");
}

static void adf_readsector(int drive, int sector, int track, int side, int density)
{
	int sector_nr = (sector - adf[drive].sector_offset) + adf[drive].sectors * (track * (adf[drive].dblside ? 2 : 1) + (side ? 1 : 0));

	adf_sector = sector - adf[drive].sector_offset;
	adf_track  = track;
	adf_side   = side;
	adf_drive  = drive;
	rpclog("ADFS Read sector %i %i %i %i\n",drive,side,track,sector);

	if (!adf[drive].f || (side && !adf[drive].dblside) || (density != adf[drive].density) ||
		(track != adf[drive].track) || (sector_nr >= adf[drive].maxsector) ||
		!adf_sector_present(drive, track, side, adf_sector) ||
		(adf_sector < 0) || (adf_sector >= adf[drive].sectors))
	{
//                printf("Not found! %08X (%i %i) %i (%i %i)\n",adff[drive],side,adl[drive],density,track,adftrackc[drive]);
		if (adf[drive].f && density == adf[drive].density &&
		    track == adf[drive].track &&
		    adf_sector_hidden(drive, track, side, adf_sector))
			adf_missing();
		else adf_notfound = 500;
		return;
	}
//        printf("Found\n");
	adf_inread  = 1;
	adf_readpos = 0;
}

static void adf_writesector(int drive, int sector, int track, int side, int density,
                            int requested_size, int deleted)
{
	(void)requested_size;
	(void)deleted;
	int sector_nr = (sector - adf[drive].sector_offset) + adf[drive].sectors * (track * (adf[drive].dblside ? 2 : 1) + (side ? 1 : 0));

//        if (adfdblstep[drive]) track/=2;
	adf_sector = sector - adf[drive].sector_offset;
	adf_track  = track;
	adf_side   = side;
	adf_drive  = drive;
//        printf("ADFS Write sector %i %i %i %i\n",drive,side,track,sector);

	if (!adf[drive].f || (side && !adf[drive].dblside) || (density != adf[drive].density) ||
		(track != adf[drive].track) || (sector_nr >= adf[drive].maxsector) ||
		!adf_sector_present(drive, track, side, adf_sector) ||
		(adf_sector < 0) || (adf_sector >= adf[drive].sectors))
	{
		if (adf[drive].f && density == adf[drive].density &&
		    track == adf[drive].track &&
		    adf_sector_hidden(drive, track, side, adf_sector))
			adf_missing();
		else adf_notfound = 500;
		return;
	}
	adf_inwrite = 1;
	adf_readpos = 0;
}

static void adf_readaddress(int drive, int track, int side, int density)
{
	if (adf[drive].dblstep)
		track /= 2;

	adf_drive = drive;
	adf_track = track;
	adf_side  = side;
	rpclog("Read address %i %i %i  %i\n",drive,side,track, ins);

	if (!adf[drive].f || (side && !adf[drive].dblside) ||
		(density != adf[drive].density) || (track != adf[drive].track))
	{
		adf_notfound=500;
		return;
	}
	/* ArcDroid modifications: retain the drive-local cursor, skip absent IDs. */
	int found = -1;
	for (int n = 0; n < adf[drive].sectors; ++n)
	{
		int candidate = (adf[drive].read_id_sector + n) % adf[drive].sectors;
		if (adf_sector_present(drive, track, side, candidate))
		{ found = candidate; break; }
	}
	if (found < 0)
	{
		if (adf_sector_hidden(drive, track, side, 0)) adf_missing();
		else adf_notfound = 500;
		return;
	}
	adf[drive].read_id_sector = found;
	adf_inreadaddr = 1;
	adf_readpos    = 0;
	adf_pause = 100;//500;
}

static void adf_format(int drive, int track, int side, int density)
{
	if (adf[drive].dblstep)
		track /= 2;

	adf_drive = drive;
	adf_track = track;
	adf_side  = side;

	if (!adf[drive].f || (side && !adf[drive].dblside) || (density != adf[drive].density) ||
		track != adf[drive].track)
	{
		adf_notfound = 500;
		return;
	}
	/* ArcDroid modifications: refuse the whole side before changing a byte. */
	for (int n = 0; n < adf[drive].sectors; ++n)
		if (!adf_sector_present(drive, track, side, n))
		{
			if (adf_sector_hidden(drive, track, side, n)) adf_missing();
			else adf_notfound = 500;
			return;
		}
	adf_sector  = 0;
	adf_readpos = 0;
	adf_informat  = 1;
}

static void adf_stop()
{
	adf_pause = adf_notfound = adf_inread = adf_inwrite = adf_inreadaddr = adf_informat = 0;
	adf_search_indexes = 0;
}

static void adf_poll()
{
	int c;

	adf_phase_us += disc_poll_period_us();
	if (adf_phase_us >= 200000u)
	{
		adf_phase_us -= 200000u;
		if (adf_notfound < 0) ++adf_search_indexes;
		fdc_funcs->indexpulse(fdc_p);
		if (adf_notfound < 0 && adf_search_indexes >=
		    (fdc_funcs->sectorid ? 2u : 5u))
		{
			adf_notfound = 0;
			if (fdc_funcs->sectorid) fdc_funcs->notfound(fdc_p);
		}
	}

	if (adf_pause)
	{
		adf_pause--;
		if (adf_pause)
		   return;
	}

	if (adf_notfound > 0)
	{
		adf_notfound--;
		if (!adf_notfound)
		{
//                        rpclog("Not found!\n");
			fdc_funcs->notfound(fdc_p);
		}
	}
	if (adf_inread && adf[adf_drive].f)
	{
//                rpclog("Read pos %i\n", adf_readpos);
//                if (!adfreadpos) rpclog("%i\n",adfsector*adfsize[adfdrive]);
		fdc_funcs->data(adf[adf_drive].track_data[adf_side][(adf_sector * adf[adf_drive].size) + adf_readpos], fdc_p);
		if (!adf_inread) return;
		adf_readpos++;
		if (adf_readpos == adf[adf_drive].size)
		{
//                        rpclog("Read %i bytes\n",adf_readpos);
			adf_inread = 0;
			fdc_funcs->finishread(fdc_p);
		}
	}
	if (adf_inwrite && adf[adf_drive].f)
	{
		if (writeprot[adf_drive])
		{
			if (adf_refusal_callback)
				adf_refusal_callback((unsigned)adf_drive, (unsigned)adf_track,
				                     (unsigned)adf_side, (unsigned)adf_sector);
//                        rpclog("writeprotect\n");
			fdc_funcs->writeprotect(fdc_p);
			adf_inwrite = 0;
			return;
		}
//                rpclog("Write data %i\n",adf_readpos);
		c = fdc_funcs->getdata(adf_readpos == (adf[adf_drive].size - 1), fdc_p);
		if (c == -1)
		{
//Carlo Concari: do not write if data not ready yet
			  return;
//                        printf("Data overflow!\n");
//                        ArcDroid reports an unavailable data byte as a fault.
		}
		adf[adf_drive].track_data[adf_side][(adf_sector * adf[adf_drive].size) + adf_readpos] = c;
		adf_readpos++;
		if (adf_readpos == adf[adf_drive].size)
		{
//                        rpclog("write over\n");
			adf_inwrite = 0;
			fdc_funcs->finishread(fdc_p);
			adf_writeback(adf_drive, adf_track, adf_side, adf_sector, 1);
		}
	}
	if (adf_inreadaddr && adf[adf_drive].f)
	{
//                rpclog("adf_inreadaddr %08X\n", fdc_sectorid);
		if (fdc_funcs->sectorid)
		{
			fdc_funcs->sectorid(adf_track, adf_side, adf[adf_drive].read_id_sector + ((adf[adf_drive].size == 512) ? 1 : 0), (adf[adf_drive].size == 256) ? 1 : ((adf[adf_drive].size == 512) ? 2 : 3), 0, 0, fdc_p);
			adf_inreadaddr = 0;
			adf[adf_drive].read_id_sector++;
			if (adf[adf_drive].read_id_sector >= adf[adf_drive].sectors)
			{
				adf[adf_drive].read_id_sector = 0;
				rpclog("adf_rsector reset\n");
			}
		}
		else
		{
			switch (adf_readpos)
			{
				case 0: fdc_funcs->data(adf_track, fdc_p); break;
				case 1: fdc_funcs->data(adf_side, fdc_p); break;
				case 2: fdc_funcs->data(adf[adf_drive].read_id_sector + adf[adf_drive].sector_offset, fdc_p); break;
				case 3: fdc_funcs->data((adf[adf_drive].size == 256) ? 1 : ((adf[adf_drive].size == 512) ? 2 : 3), fdc_p); break;
				case 4: fdc_funcs->data(0, fdc_p); break;
				case 5: fdc_funcs->data(0, fdc_p); break;
				case 6:
				adf_inreadaddr = 0;
				fdc_funcs->finishread(fdc_p);
				rpclog("Read addr - %i %i %i %i 0 0 (%i %i %i)\n", adf_track, adf_side, adf[adf_drive].read_id_sector + adf[adf_drive].sector_offset, (adf[adf_drive].size == 256) ? 1 : ((adf[adf_drive].size == 512) ? 2 : 3), adf[adf_drive].sectors, adf_drive, adf[adf_drive].read_id_sector);
				adf[adf_drive].read_id_sector++;
				if (adf[adf_drive].read_id_sector >= adf[adf_drive].sectors)
				{
					adf[adf_drive].read_id_sector = 0;
					rpclog("adf_rsector reset\n");
				}
				break;
			}
		}
		adf_readpos++;
	}
	if (adf_informat && adf[adf_drive].f)
	{
		if (writeprot[adf_drive])
		{
			if (adf_refusal_callback)
				adf_refusal_callback((unsigned)adf_drive, (unsigned)adf_track,
				                     (unsigned)adf_side, (unsigned)adf_sector);
			fdc_funcs->writeprotect(fdc_p);
			adf_informat = 0;
			return;
		}
		adf[adf_drive].track_data[adf_side][(adf_sector * adf[adf_drive].size) + adf_readpos] = 0;
		adf_readpos++;
		if (adf_readpos == adf[adf_drive].size)
		{
			adf_readpos = 0;
			adf_sector++;
			if (adf_sector == adf[adf_drive].sectors)
			{
				adf_informat = 0;
				fdc_funcs->finishread(fdc_p);
				adf_writeback(adf_drive, adf_track, adf_side, 0, adf[adf_drive].sectors);
			}
		}
	}
}

static disc_funcs_t adf_disc_funcs =
{
	.seek        = adf_seek,
	.readsector  = adf_readsector,
	.writesector = adf_writesector,
	.readaddress = adf_readaddress,
	.poll        = adf_poll,
	.format      = adf_format,
	.stop        = adf_stop,
	.close       = adf_close
};

#if defined(ARCDROID_SAVE_STATES)
/* ArcDroid modification (2026-10-18): expose reviewed state fields to the transactional save-state codec. */
#include "../../state-generated/raw_cache.inc"
#include "../../state-generated/disc_adf_state.inc"
void arc_state_visit_engine_arculator_src_disc_adf_extra(ArcState *s) {
 for(unsigned i=0;i<4u;++i){char name[48];snprintf(name,sizeof(name),"adf.file.%u",i);arc_io_state_file(s,name,adf[i].f);}
}
#endif

#if defined(ARCDROID_TEST_FAULTS)
/* ArcDroid modification (2026-10-18): authored in-flight cache delta fixture. */
void arc_engine_test_seed_raw_cache(void)
{ if(adf[0].f)adf[0].track_data[0][511]^=0x5au; }
/* ArcDroid modification (2026-10-18): borrowed handle for authored sector-cost probes. */
void *arc_engine_test_raw_file(unsigned drive)
{ return drive<4u?adf[drive].f:NULL; }

#endif
