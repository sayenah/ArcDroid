/*
  RPCEmu - An Acorn system emulator

  Copyright (C) 2005-2010 Sarah Walker

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
/* ARCDROID-PROBE BEGIN: synchronous rendering and VIDC register timing. */


/* VIDC20 emulation

 References:
   ARM VIDC20 Datasheet - ARM DDI 0030E
   Acorn Risc PC Technical Reference Manual - ISBN 1 85250 144 8
   VIDC Datasheet - ISBN 1 85250 027 1 (VIDC 1 Datasheet)
   ARM 7500 Datasheet - ARM DDI 0050C
   ARM 7500FE Datasheet - ARM DDI 0077B
   Cirrus Logic CL-PS7500FE Advance Data Book
*/
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "rpcemu.h"
#include "cp15.h"
#include "vidc20.h"
#include "keyboard.h"
#include "sound.h"
#include "mem.h"
#include "iomd.h"
#include "probe.h" /* ARCDROID-PROBE: passive numeric register observer. */

/* ARCDROID-PROBE BEGIN: scanout state; disabled for legacy oracle calls. */
uint64_t probe_video_due=UINT64_MAX, probe_video_frames, probe_video_last_period;
static uint32_t *scan_front, *scan_back;
static unsigned scan_w, scan_h, scan_front_w, scan_front_h, scan_beam;
static int scan_xorigin, scan_yorigin;
static uint64_t scan_origin, scan_fraction;
static uint32_t scan_row_border[2048];
static int scan_started;
static int scan_flyback; /* ARCDROID-PROBE: edge-triggered flyback delivery. */
static unsigned scan_bit_offset;
/* ARCDROID-PROBE END */

static int current_sizex = -1; /**< Width of the video mode, -1 on invalid */
static unsigned bitmap_pitch;
static int current_sizey = -1; /**< Height of the video mode, -1 on invalid */

// Don't resize the window to smaller than this.
static const int MIN_X_SIZE = 1;
static const int MIN_Y_SIZE = 1;

static uint32_t probe_hcr, probe_vcr, probe_fsyn, probe_con;
static int doublesize = VIDC_DOUBLE_NONE; /**< Current state of doubling X/Y values */

/* This state is written by the machine thread. The video thread should not read it. */
static struct vidc_state {
        uint32_t palette[256];		/**< Video Palette */
        int palindex;			/**< index into the palette[] array to write to */
        uint32_t border_colour;		/**< Border Colour */
        uint32_t cursor_palette[3];	/**< Cursor Palette */
        uint32_t hdsr,hcsr,hder;
        uint32_t vdsr,vcsr,vcer,vder;
        uint32_t b0,b1;
        uint32_t bit8;
        int palchange;
} vidc;

/* This state is a cached version of the machine state, and is read by the video thread.
   The machine thread should only change them when it has the mutex (and so the video
   thread is not running). */
static struct cached_state {
        struct
        {
                uint32_t r,g,b;
        } pal[256];
	uint32_t *bitmap;
        uint32_t palette[256];		/**< Video Palette */
        uint32_t border_colour;		/**< Border Colour */
        uint32_t cursor_palette[3];	/**< Cursor Palette */
        uint32_t iomd_cinit;
        uint32_t iomd_vidstart;
        uint32_t iomd_vidend;
        uint32_t iomd_vidinit;
        unsigned char iomd_vidcr;
        int vidc_xsize;			/**< X pixel size of VIDC displayed area */
        int vidc_ysize;			/**< Y pixel size of VIDC displayed area */
        int host_xsize;			/**< X pixel size of display including any doublesize doubling */
        int host_ysize;			/**< Y pixel size of display including any doublesize doubling */
        int cursorx;
        int cursory;
        int cursorheight;
        int lastblock;
        int doublesize;
        uint32_t bpp;
        uint8_t *dirtybuffer;
        int threadpending;
} thr;

/* Two dirty buffers, so one can be written to by the main thread
   while the display thread is reading the other */
static uint8_t dirtybuffer1[32768];
static uint8_t dirtybuffer2[32768];

/* Dirty buffer currently in use by main thread */
uint8_t *dirtybuffer = dirtybuffer1;


/**
 * Obtain pointer to given row of image data buffer.
 *
 * @param row Row of image
 * @return Pointer to data
 */
static uint32_t *
video_image_scanline(int row)
{
	return thr.bitmap + ((size_t)row * bitmap_pitch);
}

/**
 * Prepare and send a video update message to the GUI.
 *
 * thread: video
 *
 * @param yl
 * @param yh
 */
static void
video_update(int yl, int yh)
{
	rpcemu_video_update(thr.bitmap, current_sizex, current_sizey,
	    yl, yh, thr.doublesize, thr.host_xsize, thr.host_ysize);
}

void
initvideo(void)
{
	current_sizex = -1;
	current_sizey = -1;
 probe_video_due=UINT64_MAX;probe_video_frames=0;scan_started=0;scan_flyback=0; /* ARCDROID-PROBE */
	probe_video_last_period=0;scan_fraction=0;scan_front_w=scan_front_h=0; /* ARCDROID-PROBE */
	memset(&vidc, 0, sizeof(vidc));
	probe_hcr=792; probe_vcr=498; probe_fsyn=0; probe_con=0; /* ARCDROID-PROBE: 500-line reset timing, VCR=N-2. */
	memset(&thr, 0, sizeof(thr));
	memset(dirtybuffer1, 0xff, sizeof(dirtybuffer1));
	memset(dirtybuffer2, 0xff, sizeof(dirtybuffer2));
	vidcstartthread();
}

/**
 * Return the width in pixels of the displayed portion of the screen.
 *
 * @return width
 */
int
vidc_get_xsize(void)
{
	return vidc.hder - vidc.hdsr;
}

/**
 * Return the height in pixels of the displayed portion of the screen.
 *
 * @return height
 */
int
vidc_get_ysize(void)
{
	return vidc.vder - vidc.vdsr;
}

/**
 * Return the values of whether the display is being doubled in either
 * direction
 *
 * @param double_x filled in with a bool of X doubling
 * @param double_y filled in with a bool of Y doubling
 */
void
vidc_get_doublesize(int *double_x, int *double_y)
{
	*double_x = doublesize & VIDC_DOUBLE_X;
	*double_y = doublesize & VIDC_DOUBLE_Y;
}

/**
 * Called when emulated screen size changes, resize the display buffer
 *
 * Called when machine thread has video mutex.
 *
 * @param x Width
 * @param y Height
 */
static void
resizedisplay(int x, int y)
{
	if (x < MIN_X_SIZE) {
		x = MIN_X_SIZE;
	}
	if (y < MIN_Y_SIZE) {
		y = MIN_Y_SIZE;
	}

	current_sizex = x;
	current_sizey = y;

	if (x > 2048 || y > 2048) fatal("Probe geometry exceeds bounds");
	bitmap_pitch=((unsigned)x+31u)&~31u;
	void *pixels = realloc(thr.bitmap, (size_t)bitmap_pitch * y * sizeof(uint32_t));
	if (!pixels) fatal("Probe video allocation failure");
	thr.bitmap=pixels;
	memset(thr.bitmap, 0, (size_t)bitmap_pitch * y * sizeof(uint32_t));

	resetbuffer();
}

void
closevideo(void)
{
	vidcendthread();
	free(thr.bitmap); thr.bitmap = NULL;
	free(scan_front);free(scan_back);scan_front=scan_back=NULL; /* ARCDROID-PROBE */
	probe_video_due=UINT64_MAX;scan_started=0; /* ARCDROID-PROBE */
}

/**
 * Arrange RGB values for placing directly in QImage.
 *
 * @param r Red value
 * @param g Green value
 * @param b Blue value
 * @return Combined RGB value
 */
static uint32_t
makecol(uint32_t r, uint32_t g, uint32_t b)
{
	return (UINT32_C(0xff) << 24) | (r << 16) | (g << 8) | b;
}

static void
vidc_palette_update(void)
{
	int i;

	for (i = 0; i < 256; i++) {
		thr.pal[i].r = makecol(vidc.palette[i] & 0xff, 0, 0);
		thr.pal[i].g = makecol(0, (vidc.palette[i] >> 8) & 0xff, 0);
		thr.pal[i].b = makecol(0, 0, (vidc.palette[i] >> 16) & 0xff);

		thr.palette[i] = makecol(vidc.palette[i] & 0xff,
		                         (vidc.palette[i] >> 8) & 0xff,
		                         (vidc.palette[i] >> 16) & 0xff);
	}
	for (i = 0; i < 3; i++) {
		thr.cursor_palette[i] = makecol(vidc.cursor_palette[i] & 0xff,
		                                (vidc.cursor_palette[i] >> 8) & 0xff,
		                                (vidc.cursor_palette[i] >> 16) & 0xff);
	}
	thr.border_colour = makecol(vidc.border_colour & 0xff,
	                            (vidc.border_colour >> 8) & 0xff,
	                            (vidc.border_colour >> 16) & 0xff);
}

/**
 * Called periodically from the machine thread when the refresh timer indicates
 * it is time for a new frame.
 */
void
drawscr(void)
{
	static int lastframeborder = 0;

	// Must get the mutex before altering the thread's state.
	if (!vidctrymutex()) {
		return;
	}

	// If the thread hasn't run since the last request then don't request it again
	if (thr.threadpending) {
		goto unlock_mutex_return;
	}

	thr.vidc_xsize = vidc.hder - vidc.hdsr;
	thr.vidc_ysize = vidc.vder - vidc.vdsr;

	thr.cursorx = vidc.hcsr - vidc.hdsr;
	thr.cursory = vidc.vcsr - vidc.vdsr;
	if (mousehack) {
		mouse_hack_get_pos(&thr.cursorx, &thr.cursory);
	}
	thr.cursorheight = vidc.vcer - vidc.vcsr;

	if (vidc.palchange) {
		vidc_palette_update();
	}

	thr.iomd_cinit = cinit;
	thr.iomd_vidstart = iomd.vidstart;
	thr.iomd_vidend = iomd.vidend;
	thr.iomd_vidinit = iomd.vidinit;
	thr.iomd_vidcr = iomd.vidcr;
	thr.bpp = vidc.bit8;

	if (thr.vidc_xsize < 2) {
		thr.vidc_xsize = 2;
	}
	if (thr.vidc_ysize < 1) {
		thr.vidc_ysize = 480;
	}

	// Have we changed screen size since the last draw?
	if (thr.vidc_xsize != current_sizex || thr.vidc_ysize != current_sizey) {
		resizedisplay(thr.vidc_xsize, thr.vidc_ysize);
	}

	thr.host_xsize = thr.vidc_xsize;
	thr.host_ysize = thr.vidc_ysize;
	thr.doublesize = VIDC_DOUBLE_NONE;

	// Modes below certain sizes are scaled up, e.g. 320x256. Modes with rectangular
	// pixels, e.g. 640x256, are doubled up in the Y direction to look better on
	// square pixel hosts
	if (thr.vidc_xsize <= 448 || (thr.vidc_xsize <= 480 && thr.vidc_ysize <= 352)) {
		thr.host_xsize = thr.vidc_xsize << 1;
		thr.doublesize |= VIDC_DOUBLE_X;
	}
	if (thr.vidc_ysize <= 352) {
		thr.host_ysize = thr.vidc_ysize << 1;
		thr.doublesize |= VIDC_DOUBLE_Y;
	}

	// Store the value of this screen's pixel doubling, used in keyboard.c for mousehack
	doublesize = thr.doublesize;

	// Handle full screen border plotting
	// If not Video cursor DMA enabled or vertical start > vertical end
	if ((thr.iomd_vidcr & 0x20) == 0 || vidc.vdsr > vidc.vder) {
		lastframeborder = 1;
		if (dirtybuffer[0] || vidc.palchange) {
			uint32_t *p;
			int i;

			dirtybuffer[0] = 0;
			vidc.palchange = 0;

			// Fill the bitmap with the border colour
			p = thr.bitmap;
			for (i = 0; i < (int)(bitmap_pitch * current_sizey); i++) {
				p[i] = thr.border_colour;
			}

			video_update(0, thr.vidc_ysize);
		}
		goto unlock_mutex_return;
	}

	{
		int x, y;
		int c;
		int lastblock;

		if (vidc.palchange) {
			resetbuffer();
			vidc.palchange = 0;
		}

		if (lastframeborder) {
			lastframeborder = 0;
			resetbuffer();
		}

		x = y = c = 0;
		lastblock = -1;
		while (y < thr.vidc_ysize) {
			static const int xdiff[8] = { 8192, 4096, 2048, 1024, 512, 512, 256, 256 };

			if (dirtybuffer[c++]) {
				lastblock = c;
			}
			x += xdiff[thr.bpp] << 2;
			while (x > thr.vidc_xsize) {
				x -= thr.vidc_xsize;
				y++;
			}
		}
		thr.lastblock = lastblock;
		thr.dirtybuffer = dirtybuffer;
		dirtybuffer = (dirtybuffer == dirtybuffer1) ? dirtybuffer2 : dirtybuffer1;
	}

	{
		const uint32_t invalidate = thr.iomd_vidinit & 0x1f000000;

		// Invalidate Write-TLB entries corresponding to the screen memory,
		// so that writes to this region will cause the dirtybuffer[] to be modified.
		cp15_tlb_invalidate_physical(invalidate);
	}

	thr.threadpending = 1;

	/* ARCDROID-PROBE: caller schedules flyback falling edge in guest time. */

	vidcwakeupthread();

unlock_mutex_return:
	// Unlock the mutex before leaving the function
	vidcreleasemutex();
}

/**
 * VIDC display thread. This is called whenever vidcwakeupthread() signals it.
 * It will only be called when it has the vidc mutex.
 *
 * Update bitmap backbuffer with values from hardware video ram, then update screen
 *
 * thread: video
 */
void
vidcthread(void)
{
	uint32_t video_mask = (thr.iomd_vidinit & 0x10000000) ? mem_rammask : mem_vrammask;
	const uint32_t vidstart = thr.iomd_vidstart & video_mask & ~15u;
	uint32_t vidend;
	int drawit = 0;
	int x, y;
	const uint8_t *ramp;
	uint32_t addr;
	int yl = -1, yh = -1;
	static int oldcursorheight;
	static int oldcursory;

	/* Deal with the possibility of a spurious thread wakeup */
	if (thr.threadpending == 0) {
		return;
	}

	thr.threadpending = 0;

	if (thr.iomd_vidinit & 0x10000000) {
		/* Using DRAM for video */
		/* TODO video could be in DRAM other than simm 0 bank 0 */
		ramp = (const uint8_t *) ((thr.iomd_vidinit & 0x04000000) ? ram01 : ram00);
		vidend = (thr.iomd_vidend + 16) & video_mask & ~15u;
	} else {
		/* Using VRAM for video */
		ramp = (const uint8_t *) vram;
		vidend = (thr.iomd_vidend + 2048) & video_mask & ~15u;
		if (vidend > 0x800000) {
			vidend &= 0x7ffff0;
		}
	}

	addr = thr.iomd_vidinit & video_mask;

	drawit = thr.dirtybuffer[addr >> 12];
	if (drawit) {
		yl = 0;
	}

	switch (thr.bpp) {
	case 0: /* 1 bpp on 32 bpp */
		for (y = 0; y < thr.vidc_ysize; y++) {
			uint32_t *vidp = video_image_scanline(y);

			if (y < (oldcursorheight + oldcursory) && (y >= (oldcursory - 2))) {
				drawit = 1;
				yh = y + 8;
				if (yl == -1) {
					yl = y;
				}
			}
			if (drawit) {
				yh = y + 1;
			}
			for (x = 0; x < thr.vidc_xsize; x += 8) {
				if (drawit) {
					int xx;
					for (xx = 0; xx < 8; xx += 8) {
#ifdef _RPCEMU_BIG_ENDIAN
						addr ^= 3;
#endif
						vidp[x + xx]     = thr.palette[ramp[addr] & 1];
						vidp[x + xx + 1] = thr.palette[(ramp[addr] >> 1) & 1];
						vidp[x + xx + 2] = thr.palette[(ramp[addr] >> 2) & 1];
						vidp[x + xx + 3] = thr.palette[(ramp[addr] >> 3) & 1];
						vidp[x + xx + 4] = thr.palette[(ramp[addr] >> 4) & 1];
						vidp[x + xx + 5] = thr.palette[(ramp[addr] >> 5) & 1];
						vidp[x + xx + 6] = thr.palette[(ramp[addr] >> 6) & 1];
						vidp[x + xx + 7] = thr.palette[(ramp[addr] >> 7) & 1];
#ifdef _RPCEMU_BIG_ENDIAN
						addr ^= 3;
#endif
						addr = (addr + 1) & video_mask;
					}
				} else {
					addr = (addr + 1) & video_mask;
				}
				if (addr == vidend) {
					addr = vidstart;
				}
				if ((addr & 0xfff) == 0) {
					drawit = thr.dirtybuffer[addr >> 12];
					if (y < (oldcursorheight + oldcursory) && (y >= (oldcursory - 2))) {
						drawit = 1;
					}
					if (drawit) {
						yh = y + 8;
					}
					if (yl == -1 && drawit) {
						yl = y;
					}
				}
			}
		}
		break;
	case 1: /* 2 bpp on 32 bpp */
		for (y = 0; y < thr.vidc_ysize; y++) {
			uint32_t *vidp = video_image_scanline(y);

			if (y < (oldcursorheight + oldcursory) && (y >= (oldcursory - 2))) {
				drawit = 1;
				yh = y + 8;
				if (yl == -1) {
					yl = y;
				}
			}
			if (drawit) {
				yh = y + 1;
			}
			for (x = 0; x < thr.vidc_xsize; x += 4) {
				if (drawit) {
					int xx;
					for (xx = 0; xx < 4; xx += 4) {
#ifdef _RPCEMU_BIG_ENDIAN
						addr ^= 3;
#endif
						vidp[x + xx]     = thr.palette[ramp[addr] & 3];
						vidp[x + xx + 1] = thr.palette[(ramp[addr] >> 2) & 3];
						vidp[x + xx + 2] = thr.palette[(ramp[addr] >> 4) & 3];
						vidp[x + xx + 3] = thr.palette[(ramp[addr] >> 6) & 3];
#ifdef _RPCEMU_BIG_ENDIAN
						addr ^= 3;
#endif
						addr = (addr + 1) & video_mask;
					}
				} else {
					addr = (addr + 1) & video_mask;
				}
				if (addr == vidend) {
					addr = vidstart;
				}
				if ((addr & 0xfff) == 0) {
					drawit = thr.dirtybuffer[addr >> 12];
					if (y < (oldcursorheight + oldcursory) && (y >= (oldcursory - 2))) {
						drawit = 1;
					}
					if (drawit) {
						yh = y + 8;
					}
					if (yl == -1 && drawit) {
						yl = y;
					}
				}
			}
		}
		break;
	case 2: /* 4 bpp on 32 bpp */
		for (y = 0; y < thr.vidc_ysize; y++) {
			uint32_t *vidp = video_image_scanline(y);

			if (y < (oldcursorheight + oldcursory) && (y >= (oldcursory - 2))) {
				drawit = 1;
				yh = y + 8;
				if (yl == -1) {
					yl = y;
				}
			}
			if (drawit) {
				yh = y + 1;
			}
			for (x = 0; x < thr.vidc_xsize; x += 32) {
				if (drawit) {
					int xx;
					for (xx = 0; xx < 32; xx += 8) {
#ifdef _RPCEMU_BIG_ENDIAN
						vidp[x + xx]     = thr.palette[ramp[addr + 3] & 0xf];
						vidp[x + xx + 1] = thr.palette[(ramp[addr + 3] >> 4) & 0xf];
						vidp[x + xx + 2] = thr.palette[ramp[addr + 2] & 0xf];
						vidp[x + xx + 3] = thr.palette[(ramp[addr + 2] >> 4) & 0xf];
						vidp[x + xx + 4] = thr.palette[ramp[addr + 1] & 0xf];
						vidp[x + xx + 5] = thr.palette[(ramp[addr + 1] >> 4) & 0xf];
						vidp[x + xx + 6] = thr.palette[ramp[addr] & 0xf];
						vidp[x + xx + 7] = thr.palette[(ramp[addr] >> 4) & 0xf];
#else
						vidp[x + xx]     = thr.palette[ramp[addr] & 0xf];
						vidp[x + xx + 1] = thr.palette[(ramp[addr] >> 4) & 0xf];
						vidp[x + xx + 2] = thr.palette[ramp[addr + 1] & 0xf];
						vidp[x + xx + 3] = thr.palette[(ramp[addr + 1] >> 4) & 0xf];
						vidp[x + xx + 4] = thr.palette[ramp[addr + 2] & 0xf];
						vidp[x + xx + 5] = thr.palette[(ramp[addr + 2] >> 4) & 0xf];
						vidp[x + xx + 6] = thr.palette[ramp[addr + 3] & 0xf];
						vidp[x + xx + 7] = thr.palette[(ramp[addr + 3] >> 4) & 0xf];
#endif
						addr = (addr + 4) & video_mask;
					}
				} else {
					addr = (addr + 16) & video_mask;
				}
				if (addr == vidend) {
					addr = vidstart;
				}
				if ((addr & 0xfff) == 0) {
					drawit = thr.dirtybuffer[addr >> 12];
					if (y < (oldcursorheight + oldcursory) && (y >= (oldcursory - 2))) {
						drawit = 1;
					}
					if (drawit) {
						yh = y + 8;
					}
					if (yl == -1 && drawit) {
						yl = y;
					}
				}
			}
		}
		break;
	case 3: /* 8 bpp on 32 bpp */
		for (y = 0; y < thr.vidc_ysize; y++) {
			uint32_t *vidp = video_image_scanline(y);

			if (y < (oldcursorheight + oldcursory) && (y >= (oldcursory - 2))) {
				drawit = 1;
				yh = y + 8;
				if (yl == -1) {
					yl = y;
				}
			}
			if (drawit) {
				yh = y + 1;
			}
			for (x = 0; x < thr.vidc_xsize; x += 16) {
				if (drawit) {
					int xx;
					for (xx = 0; xx < 16; xx += 4) {
#ifdef _RPCEMU_BIG_ENDIAN
						vidp[x + xx]     = thr.palette[ramp[addr + 3]];
						vidp[x + xx + 1] = thr.palette[ramp[addr + 2]];
						vidp[x + xx + 2] = thr.palette[ramp[addr + 1]];
						vidp[x + xx + 3] = thr.palette[ramp[addr]];
#else
						vidp[x + xx]     = thr.palette[ramp[addr]];
						vidp[x + xx + 1] = thr.palette[ramp[addr + 1]];
						vidp[x + xx + 2] = thr.palette[ramp[addr + 2]];
						vidp[x + xx + 3] = thr.palette[ramp[addr + 3]];
#endif
						addr = (addr + 4) & video_mask;
					}
				} else {
					addr = (addr + 16) & video_mask;
				}
				if (addr == vidend) {
					addr = vidstart;
				}
				if ((addr & 0xfff) == 0) {
					drawit = thr.dirtybuffer[addr >> 12];
					if (y < (oldcursorheight + oldcursory) && (y >= (oldcursory - 2))) {
						drawit = 1;
					}
					if (drawit) {
						yh = y + 8;
					}
					if (yl == -1 && drawit) {
						yl = y;
					}
				}
			}
		}
		break;
	case 4: /* 16 bpp on 32 bpp */
		for (y = 0; y < thr.vidc_ysize; y++) {
			uint32_t *vidp = video_image_scanline(y);

			if (y < (oldcursorheight + oldcursory) && (y >= (oldcursory - 2))) {
				drawit = 1;
				yh = y + 8;
				if (yl == -1) {
					yl = y;
				}
			}
			if (drawit) {
				yh = y + 1;
			}
			for (x = 0; x < thr.vidc_xsize; x += 8) {
				if (drawit) {
					int xx;
					for (xx = 0; xx < 8; xx += 2) {
						uint16_t temp16;
						/* VIDC20 format :                      xBBB BBGG GGGR RRRR
						   Windows format : xxxx xxxx RRRR RRRR GGGG GGGG BBBB BBBB */
#ifdef _RPCEMU_BIG_ENDIAN
						temp16 = ramp[addr + 3] | (ramp[addr + 2] << 8);
#else
						temp16 = ramp[addr] | (ramp[addr + 1] << 8);
#endif
						vidp[x + xx] = thr.pal[temp16 & 0xff].r | thr.pal[(temp16 >> 4) & 0xff].g | thr.pal[(temp16 >> 8) & 0xff].b;
#ifdef _RPCEMU_BIG_ENDIAN
						temp16 = ramp[addr + 1] | (ramp[addr] << 8);
#else
						temp16 = ramp[addr + 2] | (ramp[addr + 3] << 8);
#endif
						vidp[x + xx + 1] = thr.pal[temp16 & 0xff].r | thr.pal[(temp16 >> 4) & 0xff].g | thr.pal[(temp16 >> 8) & 0xff].b;
						addr = (addr + 4) & video_mask;
					}
				} else {
					addr = (addr + 16) & video_mask;
				}
				if (addr == vidend) {
					addr = vidstart;
				}
				if ((addr & 0xfff) == 0) {
					drawit = thr.dirtybuffer[addr >> 12];
					if (y < (oldcursorheight + oldcursory) && (y >= (oldcursory - 2))) {
						drawit = 1;
					}
					if (drawit) {
						yh = y + 8;
					}
					if (yl == -1 && drawit) {
						yl = y;
					}
				}
			}
		}
		break;
	case 6: /* 32 bpp on 32 bpp */
		for (y = 0; y < thr.vidc_ysize; y++) {
			uint32_t *vidp = video_image_scanline(y);

			if (y < (oldcursorheight + oldcursory) && (y >= (oldcursory - 2))) {
				drawit = 1;
				yh = y + 8;
				if (yl == -1) {
					yl = y;
				}
			}
			if (drawit) {
				yh = y + 1;
			}
			for (x = 0; x < thr.vidc_xsize; x += 4) {
				if (drawit) {
					int xx;
					for (xx = 0; xx < 4; xx++) {
#ifdef _RPCEMU_BIG_ENDIAN
						vidp[x + xx] = thr.pal[ramp[addr + 3]].r | thr.pal[ramp[addr + 2]].g | thr.pal[ramp[addr + 1]].b;
#else
						vidp[x + xx] = thr.pal[ramp[addr]].r | thr.pal[ramp[addr + 1]].g | thr.pal[ramp[addr + 2]].b;
#endif
						addr = (addr + 4) & video_mask;
					}
				} else {
					addr = (addr + 16) & video_mask;
				}
				if (addr == vidend) {
					addr = vidstart;
				}
				if ((addr & 0xfff) == 0) {
					drawit = thr.dirtybuffer[addr >> 12];
					if (y < (oldcursorheight + oldcursory) && (y >= (oldcursory - 2))) {
						drawit = 1;
					}
					if (drawit) {
						yh = y + 8;
					}
					if (yl == -1 && drawit) {
						yl = y;
					}
				}
			}
		}
		break;
	default:
		fatal("Bad BPP %i\n", thr.bpp);
	}

	/* Cursor layer is plotted over regular display */
	if (thr.cursorheight > 1) {
		addr = thr.iomd_cinit;
		// printf("Mouse now at %i,%i\n", thr.cursorx, thr.cursory);
		for (y = 0; y < thr.cursorheight; y++) {
			if ((y + thr.cursory) >= thr.vidc_ysize) {
				break;
			}
			if ((y + thr.cursory) >= 0) {
				uint32_t *vidp = video_image_scanline(y + thr.cursory);

				for (x = 0; x < 32; x += 4) {
					uint8_t cursor_byte=(uint8_t)(mem_phys_read32(addr & ~3u) >> ((addr & 3u)*8));
#ifdef _RPCEMU_BIG_ENDIAN
					addr ^= 3;
#endif
					if ((x + thr.cursorx)     >= 0 && (x + thr.cursorx)     < thr.vidc_xsize && cursor_byte        & 3) {
						vidp[x + thr.cursorx]     = thr.cursor_palette[(cursor_byte & 3) - 1];
					}
					if ((x + thr.cursorx + 1) >= 0 && (x + thr.cursorx + 1) < thr.vidc_xsize && (cursor_byte >> 2) & 3) {
						vidp[x + thr.cursorx + 1] = thr.cursor_palette[((cursor_byte >> 2) & 3) - 1];
					}
					if ((x + thr.cursorx + 2) >= 0 && (x + thr.cursorx + 2) < thr.vidc_xsize && (cursor_byte >> 4) & 3) {
						vidp[x + thr.cursorx + 2] = thr.cursor_palette[((cursor_byte >> 4) & 3) - 1];
					}
					if ((x + thr.cursorx + 3) >= 0 && (x + thr.cursorx + 3) < thr.vidc_xsize && (cursor_byte >> 6) & 3) {
						vidp[x + thr.cursorx + 3] = thr.cursor_palette[((cursor_byte >> 6) & 3) - 1];
					}
#ifdef _RPCEMU_BIG_ENDIAN
					addr ^= 3;
#endif
					addr++;
				}
			}
		}

		if (yl > thr.cursory) {
			yl = thr.cursory;
		}
		if (yl == -1) {
			yl = thr.cursory;
		}
		if (thr.cursory < 0) {
			yl = 0;
		}
		if (yh < (thr.cursorheight + thr.cursory)) {
			yh = thr.cursorheight + thr.cursory;
		}
	}
	oldcursorheight = thr.cursorheight;
	oldcursory = thr.cursory;

	/* Clean the dirtybuffer now we have updated eveything in it */
	memset(thr.dirtybuffer, 0, sizeof(dirtybuffer1));

	if (yl == -1 || yh == -1) {
		return;
	}

	if (yh > thr.vidc_ysize) {
		yh = thr.vidc_ysize;
	}

	if (yl >= thr.vidc_ysize) {
		yl = thr.vidc_ysize - 1;
	}
	
	/* Copy backbuffer to screen */
	video_update(yl, yh);
}

void
writevidc20(uint32_t val)
{
	int index;
	float freq;
	probe_register_trace(val >> 28 ? 1 : 0, val >> 28 ? val >> 24 : (unsigned)vidc.palindex, val); /* ARCDROID-PROBE: passive register evidence. */

	switch (val >> 28) {
	case 0: /* Video Palette */
		if (val != vidc.palette[vidc.palindex]) {
			vidc.palette[vidc.palindex] = val;
			vidc.palchange = 1;
		}
		/* Increment Video Palette Address, wraparound from 255 to 0 */
		vidc.palindex = (vidc.palindex + 1) & 0xff;
		break;

	case 1: /* Video Palette Address */
		/* These bits should not be set */
		if ((val & 0x0fffff00) != 0) {
			return;
		}
		vidc.palindex = val & 0xff;
		break;

	case 4: /* Border Colour */
		if (val != vidc.border_colour) {
			vidc.border_colour = val;
			vidc.palchange = 1;
		}
		break;

	case 5: /* Cursor Palette Colour 1 */
	case 6: /* Cursor Palette Colour 2 */
	case 7: /* Cursor Palette Colour 3 */
		index = (val >> 28) - 5;
		if (val != vidc.cursor_palette[index]) {
			vidc.cursor_palette[index] = val;
			vidc.palchange = 1;
		}
		break;

	case 8: /* Horizontal Registers */
	case 9: /* Vertical Registers */
		switch (val >> 24) {
		case 0x80: /* Horizontal Cycle Register */
            probe_hcr = val & 0x3ffc; break;
		case 0x81: /* Horizontal Sync Width Register */
		case 0x82: /* Horizontal Border Start Register */
			/* No need to emulate */
			break;

		case 0x83: /* Horizontal Display Start Register */
			vidc.hdsr = val & 0x3ffe;
			break;

		case 0x84: /* Horizontal Display End Register */
			vidc.hder = val & 0x3ffe;
			break;

		case 0x85: /* Horizontal Border End Register */
			/* No need to emulate */
			break;

		case 0x86: /* Horizontal Cursor Start Register */
			if (vidc.hcsr != (val & 0x3fff)) {
				vidc.hcsr = val & 0x3fff;
			}
			break;

		case 0x87: /* Horizontal interlace register */
			/* Program for interlaced display.
			   (VIDC20 only, not ARM7500/FE).
			   No need to emulate */
			break;

		case 0x90: /* Vertical Cycle Register */
            probe_vcr = val & 0x1fff; break;
		case 0x91: /* Vertical Sync Width Register */
		case 0x92: /* Vertical Border Start Register */
			/* No need to emulate */
			break;

		case 0x93: /* Vertical Display Start Register */
			vidc.vdsr = val & 0x1fff;
			vidc.palchange = 1;
			break;

		case 0x94: /* Vertical Display End Register */
			vidc.vder = val & 0x1fff;
			vidc.palchange = 1;
			break;

		case 0x95: /* Vertical Border End Register */
			/* No need to emulate */
			break;

		case 0x96: /* Vertical Cursor Start Register */
			if (vidc.vcsr != (val & 0x1fff)) {
				vidc.vcsr = val & 0x1fff;
			}
			break;

		case 0x97: /* Vertical Cursor End Register */
			if (vidc.vcer != (val & 0x1fff)) {
				vidc.vcer = val & 0x1fff;
			}
			break;

		default:
			UNIMPLEMENTED("VIDC Horiz/Vert",
			              "Unknown register 0x%08x 0x%02x",
			              val, val >> 24);
		}
		break;

	case 0xa: /* Stereo Image Registers */
		/* VIDC20 only (for 8-bit VIDC10 compatible system).
		   No need to emulate. */
		break;

	case 0xb: /* Sound Registers */
		switch (val >> 24) {
		case 0xb0: /* Sound Frequency Register */
			vidc.b0 = val;
			break;

		case 0xb1: /* Sound Control Register */
			vidc.b1 = val;
			break;

		default: /* Not a valid register - ignore */
			return;
		}

		/* Because either the Sound Frequency or Control register has
		   changed recalculate the sample frequency. */

		/* We only support 16 bit sound (serial sound mode) */
		if(vidc.b1 & 2) {
			val = (vidc.b0 & 0xff) + 2;
			/* Calculated frequency depends on selected clock: */
			if (vidc.b1 & 1) {
				freq = (1000000.0f / (float) val) / 4.0f;
			} else {
				freq = (705600.0f / (float) val) / 4.0f;
			}
			sound_samplefreq_change((int) freq);
		}
		break;

	case 0xc: /* External Register */
		/* Used to control things such as the Sync polarity
		   and the 'external port', no need to emulate. */
		break;

	case 0xd: /* Frequency Synthesizer Register */
        probe_fsyn = val;
		/* Used to set the frequency of the pixel clock.
		   No need to emulate */
		break;

	case 0xe: /* Control Register */
        probe_con = val;
		/* These bits should not be set */
		if ((val & 0x0ff00000) != 0) {
			return;
		}

		if (((val >> 5) & 7) != vidc.bit8) {
			//printf("Change mode - %08X %i\n", val, (val>>5)&7);
			vidc.bit8 = (val >> 5) & 7;
			resetbuffer();
			vidc.palchange = 1;
		}
		break;

	case 0xf: /* Data Control Register */
		/* Used to program the length of the raster, this enables
		   DMA access near the end of the raster line. No need to
		   emulate. */
		break;

	default:
		UNIMPLEMENTED("VIDC register",
		              "Unknown register 0x%08x 0x%x",
		              val, val >> 28);
		break;
	}
}

void
resetbuffer(void)
{
	memset(dirtybuffer, 0xff, sizeof(dirtybuffer1));
}

/* Integer timing from VIDC20/ARM7500 HCR, VCR and pixel prescaler.
 * Clock wiring: 24MHz reference on RiscPC, ARM7500 32MHz I/O reference;
 * synthesised VCLK, reference RCLK (ARM DDI 0050C section 11.1).
 * HCLK is unsupported rather than assigning it an invented frequency. */
uint64_t probe_video_period(void) {
 if ((probe_con & 3) == 1 || (probe_con & 3) == 3) fatal("Unsupported video clock source");
 uint64_t num = machine.model == Model_A7000 ? 32000000 : 24000000, den = 1;
 if ((probe_con & 3) == 0) {
  num *= ((probe_fsyn >> 8) & 63) + 1;
  den = (probe_fsyn & 63) + 1;
 }
 den *= ((probe_con >> 2) & 7) + 1;
 /* ARCDROID-PROBE: ARM VIDC20 4.1.15 / ARM7500 DDI0050C 9.16:
  * progressive N rasters are encoded N-2, not N-1. */
 uint64_t period = ((uint64_t)(probe_hcr + 8) * (probe_vcr + 2) * 1000000000 * den) / num;
 if (period < 1000000 || period > 100000000) fatal("Unsupported video period");
 return period;
}
const uint32_t *probe_video_pixels(unsigned *w, unsigned *h) {
 if(scan_started) {*w=scan_front_w;*h=scan_front_h;return scan_front;}
 *w = current_sizex > 0 ? (unsigned)current_sizex : 0;
 *h = current_sizey > 0 ? (unsigned)current_sizey : 0;
 return thr.bitmap;
}
uint64_t probe_video_blank_ns(void) {
 unsigned total=probe_vcr+2; /* ARCDROID-PROBE: same N-2 encoding. */
 int active=vidc.vder-vidc.vdsr;
 unsigned blank=active>0&&(unsigned)active<=total?total-(unsigned)active:total;
 return probe_video_period()*blank/total;
}
size_t probe_video_pitch(void) { return scan_started?2048*4:(size_t)bitmap_pitch*4; }
/* ARCDROID-PROBE END */

/* ARCDROID-PROBE BEGIN: deterministic line scanout, R13-1.
 * Legacy drawscr/vidcthread above are a mechanism-test oracle only.
 * A raster samples state at its leading edge. CPU writes in the preceding
 * <=1us scheduler batch precede that edge; no host state enters this path.
 * VDSR/VDER encode the comparator one raster before the actual edge (N-1).
 * DMA init is reloaded during flyback, current/start/end during scanout.
 * The frontend viewport is the active-display crop at frame start; changes
 * within a frame draw at their physical position in that viewport. Growth
 * extends the crop, without overwriting already scanned pixels.
 * All lines are converted: page dirtiness cannot prove equivalence when
 * register state or an earlier scanout differs. No dirty-page shortcut.
 */
static uint64_t scan_line_duration(void)
{
 uint64_t num=machine.model==Model_A7000?32000000:24000000, den=1;
 if((probe_con&3)==1||(probe_con&3)==3)fatal("Unsupported video clock source");
 if((probe_con&3)==0) {num*=((probe_fsyn>>8)&63)+1;den=(probe_fsyn&63)+1;}
 den*=((probe_con>>2)&7)+1;
 uint64_t value=(uint64_t)(probe_hcr+8)*1000000000*den;
 /* Carry sub-nanosecond time for stable clocks. A changed clock starts a
  * fresh fraction; it cannot retroactively alter the in-flight raster. */
 static uint64_t fraction_clock;
 if(fraction_clock!=num) {scan_fraction=0;fraction_clock=num;}
 value+=scan_fraction;scan_fraction=value%num;
 uint64_t ns=value/num;
 if(!ns||ns>100000000)fatal("Unsupported raster period");
 return ns;
}
static void scan_begin_frame(void)
{
 scan_xorigin=(int)vidc.hdsr;scan_yorigin=(int)vidc.vdsr+1;
 int width=(int)vidc.hder-(int)vidc.hdsr,height=(int)vidc.vder-(int)vidc.vdsr;
 scan_w=width>=2?(unsigned)width:2;scan_h=height>=1?(unsigned)height:480;
 if(scan_w>2048||scan_h>2048)fatal("Probe geometry exceeds bounds");
 scan_origin=probe_now;
 uint32_t border=makecol(vidc.border_colour&255,(vidc.border_colour>>8)&255,(vidc.border_colour>>16)&255);
 for(unsigned y=0;y<2048;y++)scan_row_border[y]=border;
 for(unsigned y=0;y<scan_h;y++)for(unsigned x=0;x<scan_w;x++)scan_back[(size_t)y*2048+x]=border;
 memset(dirtybuffer,0,sizeof(dirtybuffer1));
}
static int scan_active(void)
{
 return scan_beam>=vidc.vdsr+1&&scan_beam<vidc.vder+1;
}
void probe_video_dma_write(unsigned reg)
{
 /* ARM7500 DDI0050C 14.3.2: init -> current during FLYBACK. A write
  * during display is deferred to flyback, not mistaken for VIDCUR. */
 if(!scan_started)return;
 if(reg==0x1d0)scan_bit_offset=0;
 if(reg==0x1dc&&!scan_active()) {iomd.vidcur=iomd.vidinit&0x1ffffff0;scan_bit_offset=0;}
}
static uint8_t scan_byte(uint32_t address)
{
 uint32_t mask;const uint8_t *memory;
 if(address&0x10000000) {
  memory=(const uint8_t *)((address&0x08000000)?ram1:(address&0x04000000)?ram01:ram00);
  mask=(address&0x08000000)?0x7ffffff:mem_rammask;
  if(!memory)return (uint8_t)(mem_phys_read32(address&~3u)>>((address&3)*8));
 } else {memory=(const uint8_t *)vram;mask=mem_vrammask;}
#ifdef _RPCEMU_BIG_ENDIAN
 return memory[(address&mask)^3];
#else
 return memory[address&mask];
#endif
}
static uint32_t scan_next_quad(uint32_t address)
{
 uint32_t mask=address&0x10000000?mem_rammask:mem_vrammask;
 uint32_t end=(iomd.vidend+(address&0x10000000?16:2048))&mask;
 uint32_t next=((address&mask)+16)&mask;
 if(next==end)return iomd.vidstart&0x1ffffff0;
 return (address&~mask)|next;
}
static void scan_draw_line(void)
{
 int row=(int)scan_beam-scan_yorigin;
 if(row<0||row>=2048)return;
 int end=(int)vidc.hder-scan_xorigin;
 if(end>2048)fatal("Probe geometry exceeds bounds");
 if(end>(int)scan_w) {
  unsigned old=scan_w;scan_w=(unsigned)end;
  for(int y=0;y<row;y++)for(unsigned x=old;x<scan_w;x++)scan_back[(size_t)y*2048+x]=scan_row_border[y];
 }
 uint32_t border=makecol(vidc.border_colour&255,(vidc.border_colour>>8)&255,(vidc.border_colour>>16)&255);
 scan_row_border[row]=border;
 uint32_t *out=scan_back+(size_t)row*2048;
 for(unsigned x=0;x<scan_w;x++)out[x]=border;
 if(scan_active()&&(unsigned)(row+1)>scan_h)scan_h=(unsigned)row+1;
 if(!scan_active()||!(iomd.vidcr&0x20)||(probe_con&(1u<<14)))return;
 if(vidc.palchange) {vidc_palette_update();vidc.palchange=0;}
 static const unsigned bits[]={1,2,4,8,16,0,32,0};
 unsigned bpp=bits[vidc.bit8];
 if(!bpp)fatal("Unsupported scanout pixel depth");
 int width=(int)vidc.hder-(int)vidc.hdsr;
 if(width<0||width>2048)fatal("Probe scanout width bound");
 uint32_t quad=iomd.vidcur&0x1ffffff0;
 unsigned byte=0,used=(unsigned)width*bpp+scan_bit_offset;
 for(int pixel=0;pixel<width;pixel++) {
  unsigned offset=(unsigned)pixel*bpp+scan_bit_offset,needed=(offset>>3)/16;
  while(byte<needed) {quad=scan_next_quad(quad);byte++;}
  uint32_t value=scan_byte(quad+((offset>>3)&15));
  if(bpp==16||bpp==32) {
   value|=(uint32_t)scan_byte(quad+(((offset>>3)&15)+1))<<8;
   if(bpp==32)value|=(uint32_t)scan_byte(quad+(((offset>>3)&15)+2))<<16;
  } else value=(value>>(offset&7))&((1u<<bpp)-1);
  uint32_t colour=bpp<=8?thr.palette[value]:bpp==16?
   thr.pal[value&255].r|thr.pal[(value>>4)&255].g|thr.pal[(value>>8)&255].b:
   thr.pal[value&255].r|thr.pal[(value>>8)&255].g|thr.pal[(value>>16)&255].b;
  int x=(int)vidc.hdsr-scan_xorigin+pixel;
  if(x>=0&&x<(int)scan_w)out[x]=colour;
 }
 unsigned quads=used/128;scan_bit_offset=used%128;
 while(byte<quads) {quad=scan_next_quad(quad);byte++;}
 iomd.vidcur=quad;
 /* Cursor position and palette are current for this raster too. */
 int cursor_row=(int)scan_beam-((int)vidc.vcsr+1);
 if(cursor_row>=0&&scan_beam<vidc.vcer+1) {
  uint32_t address=cinit+(unsigned)cursor_row*8;
  for(unsigned x=0;x<32;x++) {
   unsigned index=(mem_phys_read32((address+x/4)&~3u)>>(((address+x/4)&3)*8+(x&3)*2))&3;
   int pos=(int)vidc.hcsr-scan_xorigin+(int)x;
   if(index&&pos>=0&&pos<(int)scan_w)out[pos]=thr.cursor_palette[index-1];
  }
 }
}
void probe_video_start(void)
{
 scan_front=calloc((size_t)2048*2048,sizeof(uint32_t));
 scan_back=calloc((size_t)2048*2048,sizeof(uint32_t));
 if(!scan_front||!scan_back)fatal("Probe scanout allocation failed");
 scan_started=1;scan_beam=vidc.vder+1;scan_begin_frame();
 iomd.vidcur=iomd.vidinit&0x1ffffff0;scan_bit_offset=0;scan_flyback=1;iomd_flyback(1);
 probe_video_due=probe_now+scan_line_duration();
}
void probe_video_step(void)
{
 /* The just-ended raster was sampled at its leading edge. Step into
  * the next one, exposing flyback and rendering before further CPU work. */
 unsigned total=probe_vcr+2;
 scan_beam=(scan_beam+1)%total;
 if(scan_beam==(vidc.vder+1)%total) {
  uint32_t *swap=scan_front;scan_front=scan_back;scan_back=swap;
  scan_front_w=scan_w;scan_front_h=scan_h;
  probe_video_last_period=probe_now-scan_origin;probe_video_frames++;
  scan_flyback=1;iomd_flyback(1);scan_begin_frame();
 }
 if(!scan_active()) {
  if(!scan_flyback) {scan_flyback=1;iomd_flyback(1);}
  iomd.vidcur=iomd.vidinit&0x1ffffff0;scan_bit_offset=0;
 } else if(scan_flyback) {scan_flyback=0;iomd_flyback(0);}
 /* A changed VDSR can blank a line inside the frame-start crop. Sample
  * its current border too; blank rasters outside the crop remain hidden. */
 scan_draw_line();
 probe_video_due=probe_now+scan_line_duration();
}
/* ARCDROID-PROBE END */
