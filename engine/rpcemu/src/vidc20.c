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

/* ArcDroid modification (2026-10-06): contained static reset and explicit virtual device timing; desktop services removed; D1 line scanout and clipping-independent DMA. */


/* VIDC20 emulation

 References:
   ARM VIDC20 Datasheet - ARM DDI 0030E
   Acorn Risc PC Technical Reference Manual - ISBN 1 85250 144 8
   VIDC Datasheet - ISBN 1 85250 027 1 (VIDC 1 Datasheet)
   ARM 7500 Datasheet - ARM DDI 0050C
   ARM 7500FE Datasheet - ARM DDI 0077B
   Cirrus Logic CL-PS7500FE Advance Data Book
*/
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "rpcemu.h"
#include "rpc_timing.h"
#include "rpc_video.h"
#include "rpc_memory.h"
#include "cp15.h"
#include "vidc20.h"
#include "keyboard.h"
#include "sound.h"
#include "rpc_audio.h"
#include "mem.h"
#include "iomd.h"

static struct vidc_state {
        uint32_t palette[256];		/**< Video Palette */
        int palindex;			/**< index into the palette[] array to write to */
        uint32_t border_colour;		/**< Border Colour */
        uint32_t cursor_palette[3];	/**< Cursor Palette */
        uint32_t hdsr,hcsr,hder;
        uint32_t vdsr,vcsr,vcer,vder;
        uint32_t b0,b1;
        uint32_t hcycle, vcycle, synth, control, external, data_control;
        uint32_t bit8;
        int palchange;
} vidc;

/* A line samples all registers and memory at its leading scheduler edge.
 * Coincident device events precede the instruction at that timestamp. Writes
 * after that edge affect the next line. An in-flight line is never re-timed.
 * No pixel-cycle accuracy is claimed. Storage has no influence on progression.
 * The viewport is anchored at the active-display top-left at field start;
 * growth extends it to the right/bottom, capped only for stored pixels.
 * Earlier rows are never redrawn, including after palette/memory/size writes.
 * run_frame stops at completion: one buffer remains valid until next entry. */
static uint8_t dirty[512 * 4];
uint8_t *dirtybuffer = dirty;
static uint32_t *presentation;
static uint8_t rows[ARC_RPC_VIDEO_LIMIT];
static uint32_t row_border[ARC_RPC_VIDEO_LIMIT];
static ArcRpcVideoStatus video;
static unsigned raster, bit_offset, crop_width, crop_height;
static uint32_t serial_quad;
static bool vram_last;
static int xorigin, yorigin;
static uint32_t initial_border;
static uint64_t line_fraction, fraction_clock;
static bool control_written, line_armed, begun, new_field, clip_pending;

static uint32_t rgb(uint32_t value)
{
    return 0xff000000u | ((value & 255u) << 16) |
        (value & 0xff00u) | ((value >> 16) & 255u);
}
static bool interlace(void)
{
    return machine.model != Model_A7000 && (vidc.control & 0x1000u);
}
static void pixel_clock(uint64_t *num, uint64_t *den)
{
    *num = machine.model == Model_A7000 ? 32000000u : 24000000u;
    *den = 1;
    if (!(vidc.control & 3u)) {
        *num *= ((vidc.synth >> 8) & 63u) + 1u;
        *den = (vidc.synth & 63u) + 1u;
    }
    *den *= ((vidc.control >> 2) & 7u) + 1u;
}
static void video_arm_line(void)
{
    unsigned source = vidc.control & 3u;
    /* Unconnected HCLK/reserved clock has no edges. Powerdown suppresses
     * data, not the raster counters. Starting/stopping a clock is explicit. */
    if (!control_written || source == 1 || source == 3) {
        arc_rpc_event_cancel(ARC_RPC_EVENT_VIDEO); line_armed = false; return;
    }
    if (line_armed) return;
    uint64_t num, den; pixel_clock(&num, &den);
    if (num != fraction_clock) { line_fraction = 0; fraction_clock = num; }
    uint64_t clocks = (uint64_t)vidc.hcycle + 8u;
    /* Interlace fields have VCR+1.5 lines, with the final half-line.
     * The HIR sync phase does not change active pixels at line resolution. */
    if (interlace() && raster == vidc.vcycle + 1u) clocks /= 2u;
    uint64_t value = clocks * 1000000000u * den + line_fraction;
    uint64_t delay = value / num;
    line_fraction = value % num;
    arc_rpc_event_after(ARC_RPC_EVENT_VIDEO, delay ? delay : 1u);
    line_armed = true;
}
static void crop_grow(void)
{
    int w = (int)vidc.hder - xorigin, h = (int)vidc.vder + 1 - yorigin;
    if (w > (int)crop_width) crop_width = (unsigned)w;
    if (h > (int)crop_height) crop_height = (unsigned)h;
    if ((crop_width > ARC_RPC_VIDEO_LIMIT || crop_height > ARC_RPC_VIDEO_LIMIT) && !video.clipped) {
        video.clipped = true; clip_pending = true;
    }
}
static void begin_field(void)
{
    xorigin = (int)vidc.hdsr; yorigin = (int)vidc.vdsr + 1;
    crop_width = crop_height = 0; crop_grow();
    initial_border = rgb(vidc.border_colour);
    memset(rows, 0, sizeof(rows));
    for (unsigned y = 0; y < ARC_RPC_VIDEO_LIMIT; ++y) row_border[y] = initial_border;
    begun = true; new_field = false;
}
static bool active(void)
{
    return raster >= vidc.vdsr + 1u && raster < vidc.vder + 1u;
}
static void dma_reload(bool init)
{
    if (init) iomd.vidcur = iomd.vidinit & 0x1ffffff0u;
    serial_quad = iomd.vidcur; bit_offset = 0;
    unsigned inc = (iomd.vidcr & 31u) * 256u;
    uint32_t mask = inc && !(inc & (inc - 1u)) ? 0xffffffu & ~(inc - 1u) : 0xffffffu;
    vram_last = init ? (iomd.vidinit & 0x40000000u) != 0 :
        (iomd.vidcur & mask) == (iomd.vidend & mask);
}
void arc_rpc_video_dma_write(unsigned reg)
{
    if (reg == 0x1d0) dma_reload(false);
    if (reg == 0x1dc && !active()) {
        dma_reload(true);
    }
}
/* DMA reads only the memory aperture: never MMIO or a host service. */
static uint32_t dma_word(uint32_t address)
{
    const uint32_t *page = arc_rpc_phys_page(address);
    return page ? page[(address & 1023u) >> 2] : UINT32_MAX;
}
static unsigned dma_byte(uint32_t address)
{
    return (dma_word(address) >> ((address & 3u) * 8u)) & 255u;
}
static uint32_t next_quad(uint32_t address, unsigned *transfers)
{
    if (iomd.vidcr & 0x40u) {
        ++*transfers;
        uint32_t next = (address + 16u) & 0x1fffffffu;
        if ((address & 0xffffffu) == (iomd.vidend & 0xffffffu)) {
            ++video.wraps; next = iomd.vidstart;
        }
        iomd.vidcur = next;
        return next;
    }
    /* Serial pixels do not advance the IOMD register. QSF half-SAM changes
     * request one transfer with the programmed increment (Inc*256 bytes).
     * END comparison ignores bits below that increment; INIT's L flag seeds
     * the last-transfer state for a field beginning in the final half-SAM. */
    uint32_t next = (address + 16u) & 0x1fffffffu;
    if (next & 2047u) return next;
    ++*transfers;
    unsigned increment = (iomd.vidcr & 31u) * 256u;
    if (vram_last) { iomd.vidcur = iomd.vidstart; ++video.wraps; }
    else iomd.vidcur = (iomd.vidcur + increment) & 0x1fffffffu;
    uint32_t mask = increment && !(increment & (increment - 1u)) ?
        0xffffffu & ~(increment - 1u) : 0xffffffu;
    vram_last = (iomd.vidcur & mask) == (iomd.vidend & mask);
    return iomd.vidcur & ~2047u;
}
static void scan_line(void)
{
    static const unsigned depths[8] = {1,2,4,8,16,0,32,0};
    crop_grow();
    int row = (int)raster - yorigin;
    uint32_t *out = NULL;
    if (row >= 0 && row < (int)ARC_RPC_VIDEO_LIMIT) row_border[row] = rgb(vidc.border_colour);
    if (row >= 0 && row < (int)ARC_RPC_VIDEO_LIMIT && (unsigned)row < crop_height && crop_width) {
        if (!presentation) {
            presentation = arc_rpc_alloc((size_t)ARC_RPC_VIDEO_LIMIT * ARC_RPC_VIDEO_LIMIT * sizeof(uint32_t));
            if (!presentation) fatal("RPC presentation allocation failed");
        }
        out = presentation + (size_t)row * ARC_RPC_VIDEO_LIMIT;
        uint32_t border = rgb(vidc.border_colour);
        for (unsigned x = 0; x < ARC_RPC_VIDEO_LIMIT; ++x) out[x] = border;
        rows[row] = 1;
    }
    ++video.lines;
    if (!active() || !(iomd.vidcr & 0x20u) || (vidc.control & 0x4000u)) return;
    unsigned width = vidc.hder >= vidc.hdsr ? vidc.hder - vidc.hdsr : 0;
    unsigned bpp = depths[vidc.bit8];
    if (!bpp) return; /* Reserved hardware depth: border, no fabricated DMA. */
    uint32_t quad = serial_quad;
    unsigned offset = bit_offset, transfers = 0;
    /* Every guest column is consumed, even with no stored row/column. */
    for (unsigned pixel = 0; pixel < width; ++pixel) {
        unsigned value = 0, remaining = bpp, shift = 0;
        /* A depth change may leave a partial byte/quad in the stream. Consume
         * bits across DMA wrap before interpreting the sampled palette. */
        while (remaining) {
            unsigned bits = 8u - (offset & 7u);
            if (bits > remaining) bits = remaining;
            unsigned byte = dma_byte(quad + (offset >> 3));
            value |= ((byte >> (offset & 7u)) & ((1u << bits) - 1u)) << shift;
            offset += bits; remaining -= bits; shift += bits;
            if (offset == 128u) { offset = 0; quad = next_quad(quad, &transfers); }
        }
        uint32_t colour;
        if (bpp <= 8) colour = rgb(vidc.palette[value]);
        else {
            unsigned green = bpp == 16 ? (value >> 4) & 255u : (value >> 8) & 255u;
            unsigned blue = bpp == 16 ? (value >> 8) & 255u : (value >> 16) & 255u;
            colour = 0xff000000u | ((vidc.palette[value & 255u] & 255u) << 16) |
                (vidc.palette[green] & 0xff00u) | ((vidc.palette[blue] >> 16) & 255u);
        }
        int x = (int)vidc.hdsr - xorigin + (int)pixel;
        if (out && x >= 0 && x < (int)ARC_RPC_VIDEO_LIMIT) out[x] = colour;
    }
    video.pixels += width; video.bytes += ((uint64_t)width * bpp) / 8u;
    /* Synchronous HDWR may pad a partial word at each line's end. The
     * discarded bits still advance DMA, never presentation geometry. */
    if ((vidc.data_control & 0x1000u) && !(vidc.data_control & 0x2000u)) {
        unsigned words = vidc.data_control & 1023u;
        unsigned consumed = width * bpp;
        unsigned padding = words * 32u > consumed ? words * 32u - consumed : 0;
        while (padding) {
            unsigned step = padding < 128u - offset ? padding : 128u - offset;
            offset += step; padding -= step;
            if (offset == 128u) { offset = 0; quad = next_quad(quad, &transfers); }
        }
    }
    serial_quad = quad; bit_offset = offset;
    /* Model the aggregate bus occupation at line granularity. DRAM transfers
     * are quadwords; VRAM serial consumption needs only half-SAM copies.
     * No penalty per serial VRAM pixel. See IOMD FS issue 4 section 7. */
    bool dram = (iomd.vidcr & 0x40u) != 0;
    video.transfers += transfers;
    arc_rpc_video_bus(transfers, dram);
    int cursor_row = (int)raster - ((int)vidc.vcsr + 1);
    if (cursor_row >= 0 && raster < vidc.vcer + 1u) {
        uint32_t address = cinit + (unsigned)cursor_row * 8u;
        for (unsigned pixel = 0; pixel < 32u; ++pixel) {
            unsigned index = (dma_byte(address + pixel / 4u) >> ((pixel & 3u) * 2u)) & 3u;
            int x = (int)vidc.hcsr - xorigin + (int)pixel;
            if (out && index && x >= 0 && x < (int)ARC_RPC_VIDEO_LIMIT)
                out[x] = rgb(vidc.cursor_palette[index - 1u]);
        }
        if (!(cursor_row & 1)) arc_rpc_video_bus(1, true);
    }
}
void initvideo(void)
{
    memset(&vidc, 0, sizeof(vidc)); memset(dirty, 0xff, sizeof(dirty));
    memset(&video, 0, sizeof(video)); memset(rows, 0, sizeof(rows));
    raster = bit_offset = serial_quad = 0; vram_last = false; line_fraction = fraction_clock = 0;
    control_written = line_armed = begun = new_field = clip_pending = false;
    video.flyback = true; iomd_flyback(1);
    arc_rpc_event_cancel(ARC_RPC_EVENT_VIDEO);
}
void closevideo(void)
{
    arc_rpc_event_cancel(ARC_RPC_EVENT_VIDEO); free(presentation); presentation = NULL;
    line_armed = false; memset(&video, 0, sizeof(video));
}
void arc_rpc_video_event(void)
{
    line_armed = false;
    if (!begun || new_field) begin_field();
    raster = (raster + 1u) % (vidc.vcycle + 2u);
    bool blank = !active();
    if (blank != video.flyback) {
        if (blank) ++video.flyback_rises; else ++video.flyback_falls;
        video.flyback = blank; iomd_flyback(blank);
    }
    if (blank) dma_reload(true);
    if (!raster) {
        unsigned w = crop_width < ARC_RPC_VIDEO_LIMIT ? crop_width : ARC_RPC_VIDEO_LIMIT;
        unsigned h = crop_height < ARC_RPC_VIDEO_LIMIT ? crop_height : ARC_RPC_VIDEO_LIMIT;
        if (presentation && w && h) {
            for (unsigned y = 0; y < h; ++y) if (!rows[y])
                for (unsigned x = 0; x < ARC_RPC_VIDEO_LIMIT; ++x)
                    presentation[(size_t)y * ARC_RPC_VIDEO_LIMIT + x] = row_border[y];
        }
        video.width = presentation ? w : 0; video.height = presentation ? h : 0;
        ++video.fields;
        uint64_t num, den; pixel_clock(&num, &den);
        /* Report the programmed rational rate, not a rounded measured frame.
         * Interlace publishes field-sized presentation. */
        uint64_t half_lines = interlace() ? (uint64_t)vidc.vcycle * 2u + 3u : ((uint64_t)vidc.vcycle + 2u) * 2u;
        video.refresh_hz = (double)(num * 2u) / (double)(den * ((uint64_t)vidc.hcycle + 8u) * half_lines);
        new_field = true;
    } else scan_line();
    video_arm_line();
}
uint32_t arc_rpc_video_current(void)
{ return iomd.vidcur | (!(iomd.vidcr & 0x40u) && vram_last ? 0x40000000u : 0); }
void arc_rpc_video_status(ArcRpcVideoStatus *out)
{
    *out = video; out->raster = raster; out->vidcur = arc_rpc_video_current(); out->bit_offset = bit_offset;
}
const uint32_t *arc_rpc_video_pixels(void) { return video.width && video.height ? presentation : NULL; }
bool arc_rpc_video_take_clip(void) { bool pending = clip_pending; clip_pending = false; return pending; }
int vidc_get_xsize(void) { return vidc.hder >= vidc.hdsr ? (int)(vidc.hder - vidc.hdsr) : 0; }
int vidc_get_ysize(void) { return vidc.vder >= vidc.vdsr ? (int)(vidc.vder - vidc.vdsr) : 0; }
void vidc_get_doublesize(int *x, int *y) { *x = *y = 0; }
void drawscr(void) { }

void
writevidc20(uint32_t val)
{
	int index;

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
            vidc.hcycle = val & 0x3ffcu;
            video_arm_line();
            break;
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
            vidc.vcycle = val & 0x1fff;
            video_arm_line();
            break;
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

        arc_rpc_sound_format(vidc.b0 & 255, vidc.b1 & 3);
		break;

	case 0xc: /* External Register */
        vidc.external = val;
		/* Used to control things such as the Sync polarity
		   and the 'external port', no need to emulate. */
		break;

	case 0xd: /* Frequency Synthesizer Register */
        vidc.synth = val;
        video_arm_line();
		/* Used to set the frequency of the pixel clock.
		   No need to emulate */
		break;

	case 0xe: /* Control Register */
		/* These bits should not be set */
		if ((val & 0x0ff00000) != 0) {
			return;
		}

		vidc.control = val; control_written = true;
        video_arm_line();
		if (((val >> 5) & 7) != vidc.bit8) {
			//printf("Change mode - %08X %i\n", val, (val>>5)&7);
			vidc.bit8 = (val >> 5) & 7;
			resetbuffer();
			vidc.palchange = 1;
		}
		break;

	case 0xf: /* Data Control Register */
        vidc.data_control = val;
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
	memset(dirtybuffer, 0xff, 512 * 4);
}
