/* ArcDroid modification (2026-10-06): remove image loaders; use the
 * ArcDroid typed empty-drive interface and bounded controller responses.
 * 2026-10-07: SMC control/result interrupt acknowledgement, per-drive
 * seek status and empty-drive disc-change/index signals.
 * GPL-2.0-only additions; original RPCEmu notice preserved below. */
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

/* NEC 765/Intel 82077 Floppy drive emulation, on RPC/A7000 a part of the
   SMC 37C665GT PC style Super IO chip */
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "rpcemu.h"
#include "rpc_timing.h"
#include "fdc.h"
#include "vidc20.h"
#include "iomd.h"
#include "ide.h"
#include "arm.h"
#include "rpc_floppy.h"

/* FDC commands */
enum {
	FD_CMD_SPECIFY			= 0x03,
	FD_CMD_SENSE_DRIVE_STATUS	= 0x04,
	FD_CMD_RECALIBRATE		= 0x07,
	FD_CMD_SENSE_INTERRUPT_STATUS	= 0x08,
	FD_CMD_READ_ID_FM		= 0x0a,
	FD_CMD_DUMPREG			= 0x0e,
	FD_CMD_SEEK			= 0x0f,
	FD_CMD_CONFIGURE		= 0x13,
	FD_CMD_WRITE_DATA_MFM		= 0x45,
	FD_CMD_READ_DATA_MFM		= 0x46,
	FD_CMD_READ_ID_MFM		= 0x4a,
	FD_CMD_FORMAT_TRACK_MFM		= 0x4d,
	FD_CMD_VERIFY_DATA_MFM		= 0x56,
};

static void fdcsend(uint8_t val);

static struct
{
	uint8_t dor;
	int reset;
	uint8_t status;
	int incommand;
	uint8_t command;
	uint8_t st0;
	uint8_t st1;
	uint8_t st2;
	uint8_t st3;
	int track;
	uint8_t cylinders[4], physical[4];
	uint8_t pending_st0[4], pending_pcn[4], pending;
	int sector;
	int side;
	uint8_t data;
	int params;
	int curparam;
	uint8_t parameters[10];
	uint8_t dmadat;
	int rate;
	int oldpos;
	uint8_t results[16];
	uint8_t result_rp, result_wp; /* Bounded ring indices, never lifetime counters. */
	int drive;
	int density;
	int tc;
	int data_ready;
	int written;
	int in_read;
} fdc;

int fdccallback = 0;
int motoron = 0;

static inline void
fdc_irq_raise(void)
{
	iomd.irqb.status |= IOMD_IRQB_FLOPPY;
	updateirqs();
}

static inline void
fdc_irq_lower(void)
{
	iomd.irqb.status &= ~IOMD_IRQB_FLOPPY;
	updateirqs();
}

static inline void
fdc_dma_raise(void)
{
	iomd.fiq.status |= IOMD_FIQ_FLOPPY_DMA_REQUEST;
	updateirqs();
}

static inline void
fdc_dma_lower(void)
{
	iomd.fiq.status &= ~IOMD_FIQ_FLOPPY_DMA_REQUEST;
	updateirqs();
}

void
fdc_reset(void)
{
	arc_rpc_response(ARC_RPC_EVENT_FDC, &fdccallback, 0);
	motoron = 0;
	memset(&fdc, 0, sizeof(fdc));
	fdc.status = 0x80;
	fdc.density = 1;
	fdc_irq_lower();
	fdc_dma_lower();
}

void
fdc_init(void)
{
	fdc_reset();
}

/* A synchronous no-media result ends the command before a DMA request.
 * Physical cylinder, requested CHRN and density remain explicit at the edge. */
static int
fdc_request(ArcRpcFloppyOperation operation)
{
	ArcRpcFloppyRequest request = {
		operation, (unsigned)fdc.drive, fdc.physical[fdc.drive],
		(unsigned)fdc.side, fdc.parameters[1], fdc.parameters[2],
		fdc.parameters[3], fdc.parameters[4], (unsigned)fdc.density
	};
	if (arc_rpc_floppy_request(&request) == ARC_RPC_FLOPPY_NO_MEDIA) {
		fdc_notfound();
		return 0;
	}
	return 1;
}

void
fdc_write(uint32_t addr, uint32_t val)
{
	switch (addr) {
	case 0x3f2: /* Digital Output Register (DOR) */
		if ((val & 4) && !(fdc.dor & 4)) { /*Reset*/
			fdc.reset   = 1;
            fdc.status = 0; /* RQM remains low until reset completes. */
			arc_rpc_response(ARC_RPC_EVENT_FDC, &fdccallback, 500);
		}
		if (!(val & 4)) {
			/* Controller reset does not move the physical heads. */
			uint8_t physical[4];
			memcpy(physical, fdc.physical, sizeof(physical));
			fdc_reset(); /* Abort pending parameters/results/DMA. */
			fdc.status = 0; /* DOR holds reset until bit 2 is released. */
			memcpy(fdc.physical, physical, sizeof(physical));
		}
		motoron = val & 0xf0;
		fdc.dor = (uint8_t)val;
		break;

	case 0x3f4: /* Data Rate Select Register (DSR) */
        if (val & 0x80u) {
            /* SMC pp. 33/42: self-clearing core reset, DOR/CCR and physical
             * heads survive. DOR reset takes precedence over DSR reset. */
            uint8_t dor = fdc.dor, physical[4];
            int rate = fdc.rate, density = fdc.density, motors = motoron;
            memcpy(physical, fdc.physical, sizeof(physical));
            fdc_reset();
            memcpy(fdc.physical, physical, sizeof(physical));
            fdc.dor = dor; fdc.rate = rate; fdc.density = density; motoron = motors;
            fdc.status = 0;
            if (dor & 4u) {
                fdc.reset = 1;
                arc_rpc_response(ARC_RPC_EVENT_FDC, &fdccallback, 500);
            }
        }
		break;

	case 0x3f5: /* Data (FIFO) - Command */
        /* No FIFO access while resetting: reject without consuming or
         * cancelling the reset event, including DUMPREG and synchronous commands. */
        if (fdc.reset || !(fdc.status & 0x80u)) return;
		if (fdc.params) {
			fdc.parameters[fdc.curparam++] = val;

			if (fdc.curparam == fdc.params) {
				fdc.params = 0; /* Further FIFO writes cannot append parameters. */
				fdc.status &= ~0x80;
				fdc.tc = 0;
				fdc.data_ready = 0;
				fdc.in_read = 0;
				switch (fdc.command) {
				case FD_CMD_SPECIFY:
					fdc_callback();
					break;

				case FD_CMD_SENSE_DRIVE_STATUS:
					fdc_callback();
					break;

				case FD_CMD_RECALIBRATE:
					arc_rpc_response(ARC_RPC_EVENT_FDC, &fdccallback, 500);
					fdc.status |= 1;

					break;

				case FD_CMD_SEEK:
					arc_rpc_response(ARC_RPC_EVENT_FDC, &fdccallback, 500);
					fdc.status |= 1;

					break;

				case FD_CMD_CONFIGURE:
					fdc_callback();
					break;

				case FD_CMD_WRITE_DATA_MFM:
					fdc.st0        = fdc.parameters[0] & 7;
					fdc.st1        = 0;
					fdc.st2        = 0;
					fdc.track      = fdc.parameters[1];
					fdc.side       = fdc.parameters[2];
					fdc.sector     = fdc.parameters[3];
					fdc.drive      = fdc.st0 & 3;
					if (fdc_request(ARC_RPC_FLOPPY_WRITE))
						fdc_dma_raise();
					break;

				case FD_CMD_READ_DATA_MFM:
					fdc.in_read = 1;
				case FD_CMD_VERIFY_DATA_MFM:
					fdc.st0        = fdc.parameters[0] & 7;
					fdc.st1        = 0;
					fdc.st2        = 0;
					fdc.track      = fdc.parameters[1];
					fdc.side       = fdc.parameters[2];
					fdc.sector     = fdc.parameters[3];
					fdc.drive      = fdc.st0 & 3;
					(void)fdc_request(ARC_RPC_FLOPPY_READ);
					break;

				case FD_CMD_READ_ID_MFM:
					fdc.st0        = fdc.parameters[0] & 7;
					fdc.st1        = 0;
					fdc.st2        = 0;
					fdc.drive      = fdc.st0 & 3;
					fdc.side = (fdc.st0 >> 2) & 1;
					(void)fdc_request(ARC_RPC_FLOPPY_READ_ID);
					break;

				case FD_CMD_READ_ID_FM:
					arc_rpc_response(ARC_RPC_EVENT_FDC, &fdccallback, 4000);
					fdc.st0        = fdc.parameters[0] & 7;
					fdc.st1        = 0;
					fdc.st2        = 0;
					fdc.drive      = fdc.st0 & 3;
					break;

				case FD_CMD_FORMAT_TRACK_MFM:
					fdc.st0        = fdc.parameters[0] & 7;
					fdc.st1        = 0;
					fdc.st2        = 0;
					fdc.drive      = fdc.st0 & 3;
					fdc.side       = (fdc.st0 >> 2) & 1;
					(void)fdc_request(ARC_RPC_FLOPPY_FORMAT);
					break;

				default:
					fdcsend(0x80);
					fdc.incommand = fdc.params = 0;
					fdc.status = 0xd0;
				}
			}
			return;
		}

		/* Ignore FIFO writes while executing or returning a result. */
		if (fdc.incommand || fdc.result_rp != fdc.result_wp)
			return;
		fdc.incommand  = 1;
		fdc.command    = val;

		switch (fdc.command) {
		case FD_CMD_SPECIFY:
			fdc.params   = 2;
			fdc.curparam = 0;
			fdc.status   = 0x90;
			break;

		case FD_CMD_SENSE_DRIVE_STATUS:
			fdc.params   = 1;
			fdc.curparam = 0;
			fdc.status   = 0x90;
			break;

		case FD_CMD_RECALIBRATE:
			fdc.params   = 1;
			fdc.curparam = 0;
			fdc.status   = 0x90;
			break;

		case FD_CMD_SENSE_INTERRUPT_STATUS:
			fdc.status = 0x10;
			fdc_callback();
			break;

		case FD_CMD_DUMPREG: /* Used by Linux to identify FDC type. */
			fdc.st0       = 0x80;
			fdcsend(fdc.st0);
			fdc_irq_raise();
			fdc.incommand = 0;
			arc_rpc_response(ARC_RPC_EVENT_FDC, &fdccallback, 0);
			fdc.status    = 0xd0;
			break;

		case FD_CMD_SEEK:
			fdc.params   = 2;
			fdc.curparam = 0;
			fdc.status   = 0x90;
			break;

		case FD_CMD_CONFIGURE:
			fdc.params   = 3;
			fdc.curparam = 0;
			fdc.status   = 0x90;
			break;

		case FD_CMD_WRITE_DATA_MFM:
		case FD_CMD_READ_DATA_MFM:
		case FD_CMD_VERIFY_DATA_MFM:
			fdc.params   = 8;
			fdc.curparam = 0;
			fdc.status   = 0x90;
			break;

		case FD_CMD_READ_ID_FM:
		case FD_CMD_READ_ID_MFM:
			fdc.params   = 1;
			fdc.curparam = 0;
			fdc.status   = 0x90;
			break;

		case FD_CMD_FORMAT_TRACK_MFM:
			fdc.params   = 5;
			fdc.curparam = 0;
			fdc.status   = 0x80;
			break;

		default:
			/* Unsupported command: invalid-command result, no session fault. */
			fdcsend(0x80);
			fdc.incommand = 0;
			fdc.status = 0xd0;
		}
		break;

	case 0x3f7: /* Configuration Control Register (CCR) */
		fdc.rate = val & 3;
		switch (val & 3) {
		case 0:
			fdc.density = 2;
			break;
		case 1: case 2:
			fdc.density = 1;
			break;
		case 3:
			fdc.density = 3;
			break;
		}
		break;

	default:
		UNIMPLEMENTED("FDC write", "Unknown register 0x%03x", addr);
	}
}


uint8_t
fdc_read(uint32_t addr)
{
	switch (addr) {
	case 0x3f2: /* DOR readback */
		return fdc.dor;
	case 0x3f4: /* MSR has no interrupt acknowledgement side effect. */
		return fdc.status;

	case 0x3f5: { /* Data (FIFO) */
		if (fdc.result_rp == fdc.result_wp)
			return 0;
		/* A result-phase interrupt is acknowledged by the first FIFO read. */
		fdc_irq_lower();
		uint8_t data = fdc.results[fdc.result_rp & 0xf];

		if (fdc.result_rp != fdc.result_wp) {
			fdc.result_rp = (fdc.result_rp + 1) & 0xf;
		}

		if (fdc.result_rp == fdc.result_wp) {
			fdc.status = 0x80;
		} else {
			fdc.status |= 0xc0;
		}

		return data;
	}

	case 0x3f7: /* Empty media cannot clear DSKCHG by stepping. */
		return arc_rpc_floppy_signals(fdc.dor & 3,
			fdc.physical[fdc.dor & 3]).disc_changed ? 0x80 : 0;

	default:
		UNIMPLEMENTED("FDC read", "Unknown register 0x%03x", addr);
	}

	return 0;
}

static void
fdcsend(uint8_t val)
{
	uint8_t next = (fdc.result_wp + 1) & 0xf;
	if (next == fdc.result_rp)
		return; /* An accepted command produces at most seven result bytes. */
	fdc.results[fdc.result_wp & 0xf] = val;
	fdc.result_wp = next;
}

static void
fdc_end_command(void)
{
	fdc.status = 0xD0;
	fdc.incommand = 0;
	fdc.params = 0;
	fdc.curparam = 0;
	fdc.in_read = 0;
	fdc.data_ready = fdc.written = 0;
	arc_rpc_response(ARC_RPC_EVENT_FDC, &fdccallback, 0);
	fdc_dma_lower();
	fdc_irq_raise();
}

void
fdc_callback(void)
{
	arc_rpc_response(ARC_RPC_EVENT_FDC, &fdccallback, 0);
	if (fdc.reset) {
		fdc.reset     = 0;
		fdc.status    = 0x80;
		fdc.incommand = 0;
		fdc.st0       = 0xc0;
		/* Reset polling reports each of the four drive selects once. */
		fdc.pending = 0x0f;
		for (unsigned drive = 0; drive < 4; ++drive) {
			fdc.pending_st0[drive] = 0xc0 | drive;
			fdc.pending_pcn[drive] = fdc.cylinders[drive] = 0;
		}
		fdc.track     = 0;
		fdc.curparam  = 0;
		fdc.params    = 0;
		fdc_irq_raise();
		return;
	}
	if (!fdc.incommand)
		return;

	switch (fdc.command) {
	case FD_CMD_SPECIFY:
		fdc.incommand = 0;
		fdc.status    = 0x80;
		fdc.params    = 0;
		fdc.curparam  = 0;
		break;

	case FD_CMD_SENSE_DRIVE_STATUS: {
		unsigned drive = fdc.parameters[0] & 3;
		ArcRpcFloppySignals signals = arc_rpc_floppy_signals(drive, fdc.physical[drive]);
		/* SMC ST3 bits 3 and 5 are fixed high, not a media-ready signal. */
		fdc.st3 = (fdc.parameters[0] & 7) | 0x28;
		if (signals.track_zero) fdc.st3 |= 0x10;
		if (signals.write_protected) fdc.st3 |= 0x40;
		fdc.incommand = 0;
		fdcsend(fdc.st3);
		fdc.status = 0xd0;
		/* Sense Drive Status has no execution phase or completion IRQ. */
		break;
	}

	case FD_CMD_RECALIBRATE:
	case FD_CMD_SEEK: {
		unsigned drive = fdc.parameters[0] & 3;
		uint8_t target = fdc.command == FD_CMD_RECALIBRATE ? 0 : fdc.parameters[1];
		if (fdc.command == FD_CMD_RECALIBRATE) {
			fdc.physical[drive] = 0;
		} else {
			int position = fdc.physical[drive] + (int)target - fdc.cylinders[drive];
			fdc.physical[drive] = position < 0 ? 0 : position > 255 ? 255 : position;
		}
		fdc.cylinders[drive] = target;
		fdc.track = fdc.cylinders[drive];
		fdc.st0 = 0x20 | drive; /* Seek End; H is always zero for seek status. */
		if (fdc.command == FD_CMD_RECALIBRATE &&
		    !arc_rpc_floppy_signals(drive, 0).track_zero)
			fdc.st0 |= 0x50; /* Abnormal termination and equipment check. */
		fdc.pending_st0[drive] = fdc.st0;
		fdc.pending_pcn[drive] = fdc.cylinders[drive];
		fdc.pending |= 1u << drive;
		fdc.incommand = 0;
		fdc.status = 0x80;
		fdc_irq_raise();
		break;
	}

	case FD_CMD_SENSE_INTERRUPT_STATUS: {
		fdc_irq_lower();
		if (fdc.pending) {
			unsigned drive = 0;
			while (!(fdc.pending & (1u << drive))) ++drive;
			fdcsend(fdc.pending_st0[drive]);
			fdcsend(fdc.pending_pcn[drive]);
			fdc.pending &= ~(1u << drive);
		} else {
			fdcsend(0x80); /* No pending reset/seek: one invalid-status byte. */
		}
		fdc.status = 0xd0;
		fdc.incommand = 0;
		/* SIS consumes a cause; it must never generate another interrupt. */
		break;
	}

	case FD_CMD_CONFIGURE:
		fdc.incommand = 0;
		fdc.status    = 0x80;
		fdc.params    = 0;
		fdc.curparam  = 0;
		break;

	case FD_CMD_WRITE_DATA_MFM:
		fdc.sector++;
		if (fdc.sector > fdc.parameters[5])
			fdc.tc = 1;
		if (fdc.tc) {
			fdcsend(fdc.st0);
			fdcsend(fdc.st1);
			fdcsend(fdc.st2);
			fdcsend(fdc.track);
			fdcsend((fdc.parameters[0] & 4) ? 1 : 0);
			fdcsend(fdc.sector);
			fdcsend(fdc.parameters[4]);
			fdc.status = 0xD0;
			fdc_irq_raise();
			fdc.incommand = 0;
			fdc.params    = 0;
			fdc.curparam  = 0;
			arc_rpc_response(ARC_RPC_EVENT_FDC, &fdccallback, 0);
		} else {
			if (fdc_request(ARC_RPC_FLOPPY_WRITE))
				fdc_dma_raise();
		}
		break;

	case FD_CMD_READ_DATA_MFM:
		fdc.sector++;
		if (fdc.sector > fdc.parameters[5])
			fdc.tc = 1;
		if (fdc.tc) {
			fdcsend(fdc.st0);
			fdcsend(fdc.st1);
			fdcsend(fdc.st2);
			fdcsend(fdc.track);
			fdcsend((fdc.parameters[0] & 4) ? 1 : 0);
			fdcsend(fdc.sector);
			fdcsend(fdc.parameters[4]);
			fdc.status = 0xD0;
			fdc_irq_raise();
			fdc.incommand = 0;
			fdc.params    = 0;
			fdc.curparam  = 0;
			arc_rpc_response(ARC_RPC_EVENT_FDC, &fdccallback, 0);
		} else {
			(void)fdc_request(ARC_RPC_FLOPPY_READ);
		}
		break;

	case FD_CMD_READ_ID_FM:
		fdc_notfound();
		break;

	case FD_CMD_FORMAT_TRACK_MFM:
		fdcsend(fdc.st0);
		fdcsend(fdc.st1);
		fdcsend(fdc.st2);
		fdcsend(0);
		fdcsend(0);
		fdcsend(0);
		fdcsend(0);
		fdc.status = 0xD0;
		fdc_irq_raise();
		fdc.incommand = 0;
		fdc.params    = 0;
		fdc.curparam  = 0;
		break;

	case FD_CMD_VERIFY_DATA_MFM:
		/* Verified OK (amazing!), jump straight to the end */
		if (fdc.parameters[0] & 0x80 /* EC */) {
			fdc.sector = fdc.parameters[7];
		} else {
			fdc.sector = fdc.parameters[5];
		}
		fdcsend(fdc.st0);
		fdcsend(fdc.st1);
		fdcsend(fdc.st2);
		fdcsend(fdc.track);
		fdcsend((fdc.parameters[0] & 4) ? 1 : 0);
		fdcsend(fdc.sector);
		fdcsend(fdc.parameters[4]);
		fdc.status = 0xD0;
		fdc_irq_raise();
		fdc.incommand = 0;
		fdc.params    = 0;
		fdc.curparam  = 0;
		break;
	}
}

static void fdc_overrun(void)
{

	fdcsend(0x40 | (fdc.side ? 4 : 0) | fdc.drive);
	fdcsend(0x10); /*Overrun*/
	fdcsend(0);
	fdcsend(fdc.track);
	fdcsend(fdc.side);
	fdcsend(fdc.sector);
	fdcsend(fdc.parameters[4]);
	fdc_end_command();
}

void fdc_data(uint8_t dat)
{
	if (fdc.tc || !fdc.in_read)
		return;

	if (fdc.data_ready) {
		fdc_overrun();
	} else {
		fdc.dmadat = dat;
		fdc.data_ready = 1;
		fdc_dma_raise();
	}
}

void fdc_finishread(void)
{
	arc_rpc_response(ARC_RPC_EVENT_FDC, &fdccallback, 25);
}

void fdc_notfound(void)
{
	fdcsend(0x40 | (fdc.side ? 4 : 0) | fdc.drive);
	fdcsend(5);
	fdcsend(0);
	fdcsend(0);
	fdcsend(0);
	fdcsend(0);
	fdcsend(0);
	fdc_end_command();
}

void fdc_datacrcerror(void)
{
	fdcsend(0x40 | (fdc.side ? 4 : 0) | fdc.drive);
	fdcsend(0x20); /*Data error*/
	fdcsend(0x20); /*Data error in data field*/
	fdcsend(fdc.track);
	fdcsend(fdc.side);
	fdcsend(fdc.sector);
	fdcsend(fdc.parameters[4]);
	fdc_end_command();
}

void fdc_headercrcerror(void)
{
	fdcsend(0x40 | (fdc.side ? 4 : 0) | fdc.drive);
	fdcsend(0x20); /*Data error*/
	fdcsend(0);
	fdcsend(fdc.track);
	fdcsend(fdc.side);
	fdcsend(fdc.sector);
	fdcsend(fdc.parameters[4]);
	fdc_end_command();
}

void fdc_writeprotect(void)
{
	fdcsend(0x40 | (fdc.side ? 4 : 0) | fdc.drive);
	fdcsend(0x02); /*Not writeable*/
	fdcsend(0);
	fdcsend(0);
	fdcsend(0);
	fdcsend(0);
	fdcsend(0);
	fdc_end_command();
}

int fdc_getdata(int last)
{
	uint8_t temp;

	if (!fdc.written && !fdc.tc)
		return -1;
	if (!last && !fdc.tc)
		fdc_dma_raise();
	fdc.written = 0;
	temp = fdc.dmadat;
	return temp;
}

void fdc_sectorid(uint8_t track, uint8_t side, uint8_t sector, uint8_t size)
{
	fdcsend((fdc.side ? 4 : 0) | fdc.drive);
	fdcsend(0);
	fdcsend(0);
	fdcsend(track);
	fdcsend(side);
	fdcsend(sector);
	fdcsend(size);
	fdc_end_command();
}

void fdc_indexpulse(void)
{
	unsigned drive = fdc.dor & 3;
	if (!(motoron & (0x10 << drive)) ||
	    !arc_rpc_floppy_signals(drive, fdc.physical[drive]).index) return;
	iomd.irqa.status |= IOMD_IRQA_FLOPPY_INDEX;
	updateirqs();
}

uint8_t
fdc_dma_read(uint32_t addr)
{
	fdc_dma_lower();
	fdc.data_ready = 0;
	if (addr == 0x302a000) {
		fdc.tc = 1;
		fdc.st0        = 0;
	}
	return fdc.dmadat;
}

void
fdc_dma_write(uint32_t addr, uint8_t val)
{
	fdc_dma_lower();
	if (addr == 0x302a000) {
		fdc.tc = 1;
		fdc.st0        = 0;
	}
	fdc.written = 1;
	fdc.dmadat = val;
}
