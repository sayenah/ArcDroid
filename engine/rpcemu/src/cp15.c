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

/* ArcDroid modification (2026-10-06): permission-tagged bounded TLB,
 * complete reset, physical descriptor reads and in-object instruction pointers. */
/* System coprocessor + MMU emulation*/
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rpcemu.h"
#include "arm.h"
#include "cp15.h"
#include "mem.h"
#include "rpc_memory.h"

bool arc_rpc_cpu_idle;
int dcache = 0; /* Data cache on StrongARM, unified cache pre-StrongARM */

#define TLBCACHESIZE 256

/* 1 KiB tags preserve small-page subpage permissions. Descriptors and
 * physical base stay cached until an architected invalidation. Privilege,
 * access direction and DACR/S/R are checked on every hit, including fetch. */
typedef struct {
    uint32_t tag, physical;
    uint8_t domain, ap, page, valid;
} ArcRpcTlbEntry;
static ArcRpcTlbEntry tlb[TLBCACHESIZE];
uint32_t tlbs, flushes;

static struct cp15 {
	uint32_t ctrl;				/**< Control register */
	uint32_t translation_table;		/**< Translation Table Base register */
	uint32_t domain_access_control;		/**< Domain Access Control register */
	uint32_t fault_status;			/**< Fault Status register */
	uint32_t fault_address;			/**< Fault Address register */

	CPUModel cpu_model;			/**< CPU model emulated */
} cp15;

static int icache = 0;

/* The bits of the processor's internal coprocessor (MMU) control register */
#define CP15_CTRL_MMU			(1 << 0)
#define CP15_CTRL_ALIGNMENT_FAULT	(1 << 1)
#define CP15_CTRL_CACHE			(1 << 2)  /* Data cache only on SA */
#define CP15_CTRL_WRITE_BUFFER		(1 << 3)
#define CP15_CTRL_PROG32		(1 << 4)  /* Always enabled in SA */
#define CP15_CTRL_DATA32		(1 << 5)  /* Always enabled in SA */
#define CP15_CTRL_ABORT_TIMING		(1 << 6)  /* Always enabled in 710 & SA */
#define CP15_CTRL_BIG_ENDIAN		(1 << 7)
#define CP15_CTRL_SYSTEM		(1 << 8)
#define CP15_CTRL_ROM			(1 << 9)  /* 710 & SA */
#define CP15_CTRL_ICACHE		(1 << 12) /* SA only */

/* Fault Status codes stored in FSR[0:3] when a fault occurs */
#define CP15_FAULT_TRANSLATION_SECTION	0x5
#define CP15_FAULT_TRANSLATION_PAGE	0x7
#define CP15_FAULT_DOMAIN_SECTION	0x9
#define CP15_FAULT_DOMAIN_PAGE		0xb
#define CP15_FAULT_PERMISSION_SECTION	0xd
#define CP15_FAULT_PERMISSION_PAGE	0xf

static void cp15_tlb_flush(void) { memset(tlb, 0, sizeof(tlb)); }

void cp15_tlb_invalidate_physical(uint32_t addr)
{
    /* No translated writable-pointer cache remains. */
    NOT_USED(addr);
}

/**
 * Called on program startup and emulated machine reset to
 * prepare the cp15 module
 *
 * @param cpu_model Model of CPU (and associated mmu/cp15) being emulated
 */
void
cp15_reset(CPUModel cpu_model)
{
	memset(&cp15, 0, sizeof(cp15));
	arc_rpc_cpu_idle = false;
	cp15.cpu_model = cpu_model;
	switch (cpu_model) {
	case CPUModel_ARM610:
		cp15.ctrl = 0;
		break;
	case CPUModel_ARM710:
	case CPUModel_ARM7500:
	case CPUModel_ARM7500FE:
		cp15.ctrl = CP15_CTRL_ABORT_TIMING;
		break;
	case CPUModel_SA110:
	case CPUModel_ARM810:
		cp15.ctrl = CP15_CTRL_ABORT_TIMING | CP15_CTRL_DATA32 | CP15_CTRL_PROG32;
		break;
	}
	dcache = 0;
	icache = 0;
	mmu = 0;
	prog32 = (cp15.ctrl & CP15_CTRL_PROG32) != 0;

    cp15_tlb_flush();
    tlbs = flushes = 0;

}

/**
 * Called on program startup to prepare the cp15 module
 */
void
cp15_init(void)
{
}

static void cp15_tlb_flush_all(void)
{
    cp15_tlb_flush();
    pccache = 0xffffffffu;
    flushes++;
}

/**
 * Perform a MCR to Co-processor 15.
 *
 * @param opcode Opcode of instruction being emulated
 * @param val    Value from ARM register
 */
void
cp15_write(uint32_t opcode, uint32_t val)
{
	const uint32_t crn = RN;
	const uint32_t crm = RM;
	const uint32_t opc2 = (opcode >> 5) & 7;

	switch (crn) {
	case 1: /* Control */
		if (!icache && (val & CP15_CTRL_ICACHE)) {
			resetcodeblocks();
		}

		/* Are any of the MMU, ROM or System bits changing? */
		if (((cp15.ctrl ^ val) & (CP15_CTRL_MMU | CP15_CTRL_ROM | CP15_CTRL_SYSTEM)) != 0) {
			cp15_tlb_flush_all();
			resetcodeblocks();
		}

		cp15.ctrl = val;
		dcache = val & CP15_CTRL_CACHE;
		icache = val & CP15_CTRL_ICACHE;
		mmu = val & CP15_CTRL_MMU;
		prog32 = val & CP15_CTRL_PROG32;

		if (!prog32 && (arm.mode & 0x10)) {
			updatemode(arm.mode & 0xf);
		}
		return;

	case 2: /* Translation Table Base */
		cp15.translation_table = val & ~0x3fffu;
		cp15_tlb_flush_all();
		resetcodeblocks();
		return;

	case 3: /* Domain Access Control */
		if (val != cp15.domain_access_control) {
			cp15.domain_access_control = val;
			cp15_tlb_flush_all();
			resetcodeblocks();
		}
		return;

	case 5:
	case 6:
		switch (cp15.cpu_model) {
		/* ARMv3 Architecture */
		case CPUModel_ARM610:
		case CPUModel_ARM710:
		case CPUModel_ARM7500:
		case CPUModel_ARM7500FE:
			switch (crn) {
			case 5: /* TLB Flush */
				cp15_tlb_flush_all();
				break;

			case 6: /* TLB Purge */
				cp15_tlb_flush_all();
				break;
			}
			resetcodeblocks();
			return;

		/* ARMv4 Architecture */
		case CPUModel_SA110:
		case CPUModel_ARM810:
			switch (crn) {
			case 5: /* Fault Status Register */
				cp15.fault_status = val;
				return;

			case 6: /* Fault Address Register */
				cp15.fault_address = val;
				return;
			}
			break;

		default:
			fatal("Unsupported CPU model");
		}
		break;

	case 7: /* Flush Cache */
		if ((crm & 1) && (opc2 == 0)) {
			resetcodeblocks();
		}
		pccache = 0xffffffff;
		return;

	case 8: /* TLB Operations (ARMv4) */
		if (cp15.cpu_model == CPUModel_SA110 || cp15.cpu_model == CPUModel_ARM810) {
			if (opc2 == 0) {
				/* TLB Flush */
				cp15_tlb_flush_all();
			} else {
				/* TLB Purge */
				cp15_tlb_flush_all();
			}
			if (crm & 1) {
				resetcodeblocks();
			}
			return;
		}
		break;

	case 15:
		if (cp15.cpu_model == CPUModel_SA110) {
			/* Test, Clock and Idle control: SA-110 manual table 7. */
            if (opc2 == 2 && crm == 8) { arc_rpc_cpu_idle = true; return; }
			if (opc2 == 2 && crm == 1) {
				/* Enable clock switching - no need to implement */
				return;
			}
		}
		break;
	}

	UNIMPLEMENTED("CP15 Write", "Register %u, opcode %08x", crn, opcode);
}

/**
 * Perform a MRC from Co-processor 15.
 *
 * @param opcode Opcode of instruction being emulated
 * @return Value to ARM register
 */
uint32_t
cp15_read(uint32_t opcode)
{
	const uint32_t crn = RN;

	switch (crn) {
	case 0: /* ID */
		switch (cp15.cpu_model) {
		case CPUModel_ARM7500:   return 0x41027100;
		case CPUModel_ARM7500FE: return 0x41077100;
		case CPUModel_ARM610:    return 0x41560610;
		case CPUModel_ARM710:    return 0x41007100;
		case CPUModel_ARM810:    return 0x41018100;
		case CPUModel_SA110:     return 0x4401a102;
		}
		break;
	case 1: /* Control */
		return cp15.ctrl;
	case 2: /* Translation Table Base */
		return cp15.translation_table;
	case 3: /* Domain Access Control */
		return cp15.domain_access_control;
	case 5: /* Fault Status */
		return cp15.fault_status;
	case 6: /* Fault Address */
		return cp15.fault_address;
	default:
		UNIMPLEMENTED("CP15 Read", "Unknown register %u, opcode %08x", crn, opcode);
	}
	/* Unimplemented register reads are rejected by the interpreter. */
	return 0;
}

/**
 * @param ap       Access Permissions (from Descriptor)
 * @param is_write Non-zero if this is for write access
 * @return Non-zero if the access should be faulted
 */
static int
cp15_check_permissions(uint32_t ap, int is_write)
{
	switch (ap) {
	case 0:
		switch (cp15.ctrl & 0x300) {
		case 0x000: /* No access */
		case 0x300: /* Unpredictable */
			return 1;

		case 0x100: /* Supervisor read-only */
			return !memmode || is_write;

		case 0x200: /* Read-only */
			return is_write;
		}
		break;

	case 1: /* Supervisor read/write */
		return !memmode;

	case 2: /* Supervisor read/write, User read-only*/
		return !memmode && is_write;
	}
	/* Any access permitted */
	return 0;
}

/**
 * Return the value which encodes the access permitted for a Domain.
 *
 * @param domain Domain number
 * @return Access permitted by Domain
 */
static uint32_t
cp15_domain_access(uint32_t domain)
{
	uint32_t shift = (domain << 1); /* Shift needed to extract value for this Domain */

	return (cp15.domain_access_control >> shift) & 3;
}

/**
 * Translate a virtual address to a physical address.
 *
 * The access permissions are checked and an Abort may be generated.
 *
 * @param addr     Virtual address
 * @param rw       Bool of whether this is for write access
 * @param prefetch Bool of whether this is for instruction fetch
 * @return Translated physical address (if no Fault occurred)
 */
uint32_t translateaddress2(uint32_t addr, int rw, int prefetch)
{
    ArcRpcTlbEntry *entry = &tlb[(addr >> 10) & (TLBCACHESIZE - 1)];
    uint32_t domain = 0, fault_code;
    if (!entry->valid || entry->tag != (addr >> 10)) {
        uint32_t fld = mem_phys_read32(cp15.translation_table | ((addr >> 18) & 0x3ffcu));
        domain = (fld >> 5) & 15;
        uint32_t physical, ap;
        int page = (fld & 3) == 1;
        tlbs++;
        if (page) {
            uint32_t sld = mem_phys_read32((fld & 0xfffffc00u) | ((addr >> 10) & 0x3fcu));
            unsigned shift;
            if ((sld & 3) == 1) {
                physical = (sld & 0xffff0000u) | (addr & 0xffffu);
                shift = 4 + ((addr >> 13) & 6);
            } else if ((sld & 3) == 2) {
                physical = (sld & 0xfffff000u) | (addr & 0xfffu);
                shift = 4 + ((addr >> 9) & 6);
            } else {
                fault_code = CP15_FAULT_TRANSLATION_PAGE;
                goto do_fault;
            }
            ap = (sld >> shift) & 3;
        } else if ((fld & 3) == 2) {
            physical = (fld & 0xfff00000u) | (addr & 0xfffffu);
            ap = (fld >> 10) & 3;
        } else {
            /* First-level type 3 is reserved on these ARMv3/v4 models. */
            fault_code = CP15_FAULT_TRANSLATION_SECTION;
            goto do_fault;
        }
        *entry = (ArcRpcTlbEntry){addr >> 10, physical & ~0x3ffu,
                                  (uint8_t)domain, (uint8_t)ap, (uint8_t)page, 1};
    }
    domain = entry->domain;
    uint32_t access = cp15_domain_access(domain);
    if (access == 0 || access == 2) {
        fault_code = entry->page ? CP15_FAULT_DOMAIN_PAGE : CP15_FAULT_DOMAIN_SECTION;
        goto do_fault;
    }
    if (access == 1 && cp15_check_permissions(entry->ap, rw)) {
        fault_code = entry->page ? CP15_FAULT_PERMISSION_PAGE : CP15_FAULT_PERMISSION_SECTION;
        goto do_fault;
    }
    return entry->physical | (addr & 0x3ffu);

do_fault:
    arm.event |= 0x40;
    if (!prefetch) {
        cp15.fault_address = addr;
        cp15.fault_status = (domain << 4) | fault_code;
    }
    return 0;
}

const uint32_t *getpccache(uint32_t addr)
{
    uint32_t physical = mmu ? translateaddress(addr, 0, 1) : addr;
    if (arm.event & 0x40) {
        arm.event &= ~0x40u;
        return NULL;
    }
    /* Absent/external instruction bus: prefetch abort, no FAR/FSR update. */
    return arc_rpc_phys_page(physical);
}
