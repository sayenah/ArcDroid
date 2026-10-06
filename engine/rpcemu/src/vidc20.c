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

/* ArcDroid modification (2026-10-06): contained static reset and explicit virtual device timing; desktop services removed. */


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
#include "cp15.h"
#include "vidc20.h"
#include "keyboard.h"
#include "sound.h"
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

/* C2 keeps device registers and timing hooks only. Run D supplies scanout. */
static uint8_t dirty[512 * 4];
static unsigned raster;
static uint64_t line_fraction;
static bool control_written;
/* Rational line timing, independent of any renderer. Board RCLK=24 MHz;
 * synthesized VCLK=RCLK*(v+1)/(r+1). HCLK is unconnected in this slice. */
static void video_arm_line(void)
{
    unsigned source = vidc.control & 3u;
    uint64_t hz_num = 24000000u, hz_den = 1;
    if (!control_written || (vidc.control & 0x4000u) || source == 1 || source == 3) {
        arc_rpc_event_cancel(ARC_RPC_EVENT_VIDEO);
        return;
    }
    if (source == 0) {
        hz_num *= ((vidc.synth >> 8) & 63u) + 1u;
        hz_den = (vidc.synth & 63u) + 1u;
    }
    hz_den *= ((vidc.control >> 2) & 7u) + 1u;
    uint64_t numerator = ((uint64_t)vidc.hcycle + 8u) * 1000000000u * hz_den + line_fraction;
    uint64_t delay = numerator / hz_num;
    line_fraction = numerator % hz_num;
    arc_rpc_event_after(ARC_RPC_EVENT_VIDEO, delay ? delay : 1u);
}
uint8_t *dirtybuffer = dirty;
void initvideo(void)
{
    memset(&vidc, 0, sizeof(vidc)); memset(dirty, 0xff, sizeof(dirty));
    raster = 0; line_fraction = 0; control_written = false;
    arc_rpc_event_cancel(ARC_RPC_EVENT_VIDEO);
}
void closevideo(void) { arc_rpc_event_cancel(ARC_RPC_EVENT_VIDEO); }
void arc_rpc_video_event(void)
{
    raster = (raster + 1u) % (vidc.vcycle + 2u);
    if (raster == vidc.vdsr + 1u) iomd_flyback(0);
    if (raster == vidc.vder + 1u) iomd_flyback(1);
    video_arm_line();
}
int vidc_get_xsize(void) { return vidc.hder >= vidc.hdsr ? (int)(vidc.hder - vidc.hdsr) : 0; }
int vidc_get_ysize(void) { return vidc.vder >= vidc.vdsr ? (int)(vidc.vder - vidc.vdsr) : 0; }
void vidc_get_doublesize(int *x, int *y) { *x = *y = 0; }
void drawscr(void) { }

void
writevidc20(uint32_t val)
{
	int index;
	unsigned freq;

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

		/* Because either the Sound Frequency or Control register has
		   changed recalculate the sample frequency. */

		/* We only support 16 bit sound (serial sound mode) */
		if(vidc.b1 & 2) {
			val = (vidc.b0 & 0xff) + 2;
			/* Calculated frequency depends on selected clock: */
			if (vidc.b1 & 1) {
				freq = 250000u / val;
			} else {
				freq = 176400u / val;
			}
			sound_samplefreq_change((int) freq);
		}
		break;

	case 0xc: /* External Register */
        vidc.external = val;
		/* Used to control things such as the Sync polarity
		   and the 'external port', no need to emulate. */
		break;

	case 0xd: /* Frequency Synthesizer Register */
        vidc.synth = val;
        line_fraction = 0; video_arm_line();
		/* Used to set the frequency of the pixel clock.
		   No need to emulate */
		break;

	case 0xe: /* Control Register */
		/* These bits should not be set */
		if ((val & 0x0ff00000) != 0) {
			return;
		}

		vidc.control = val; control_written = true;
        line_fraction = 0; video_arm_line();
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
