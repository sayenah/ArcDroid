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
#include "rpc_observer.h"
/* ArcDroid modification (2026-10-14, profiling-6b-p0i): scoped fetch sampling. */
#include "rpc_profile.h"
#include "rpc_mem_timing.h"
/* ArcDroid modification (2026-10-07): retain descriptor B/C and connect
 * permission-checked fetch, cache maintenance and SA clock-switch timing. */
/* ArcDroid modification (2026-10-07, R2a run D): share the checked TLB
 * result with fetch timing, avoiding a second hash lookup without omitting
 * current domain, privilege or subpage permission checks. */

bool arc_rpc_cpu_idle;
int dcache = 0; /* Data cache on StrongARM, unified cache pre-StrongARM */

#define TLBCACHESIZE 256

/* 1 KiB tags preserve small-page subpage permissions. Descriptors and
 * physical base stay cached until an architected invalidation. Privilege,
 * access direction and DACR/S/R are checked on every hit, including fetch. */
typedef struct {
    const uint32_t *code;
    uint32_t tag, physical;
    uint8_t domain, ap, page, valid, attributes;
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

static unsigned arc_rpc_attributes;
unsigned arc_rpc_cp15_attributes(void) { return mmu ? arc_rpc_attributes : 0; }
static void cp15_tlb_flush(void) { memset(tlb, 0, sizeof(tlb)); arc_rpc_mem_permission_change(); }

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
    arc_rpc_mem_control(cp15.ctrl);

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
    ARC_RPC_PROFILE(side_exit());
	const uint32_t crn = RN;
	const uint32_t crm = RM;
	const uint32_t opc2 = (opcode >> 5) & 7;
#if defined(ARCDROID_RPC_PROFILE)
    if(crn==7 && (cp15.cpu_model!=CPUModel_SA110 ||
        (opc2==0 && (crm==5 || crm==6 || crm==7)) ||
        (opc2==1 && (crm==6 || crm==10)) || (opc2==4 && crm==10)))
        arc_rpc_profile_maintenance();
#endif
    arc_rpc_mem_maintenance(crn, opc2, crm, val);

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
        arc_rpc_mem_control(val);
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
				arc_rpc_mem_clock_switch(true);
				return;
			}
			if (opc2 == 2 && crm == 2) { arc_rpc_mem_clock_switch(false); return; }
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
    ARC_RPC_PROFILE(side_exit());
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
static inline ArcRpcTlbEntry *checked_translation(uint32_t addr, int rw, int prefetch)
{
    /* Mix section and subpage bits: identity ROM/RAM/MMIO and page aliases
     * should not evict one another simply because their low offsets match. */
    uint32_t tag = addr >> 10;
    ArcRpcTlbEntry *entry = &tlb[(tag ^ (tag >> 10) ^ (tag >> 18)) & (TLBCACHESIZE - 1)];
    /* ArcDroid modification (2026-10-16, 6b-1a0): test-only reference
     * opportunities distinguish raw tags, walks and successful permissions. */
#if defined(ARCDROID_TEST_FAULTS)
    bool arc_rpc_tag_hit=entry->valid && entry->tag==(addr>>10);
    unsigned arc_rpc_counter_base=prefetch?ARC_RPC_COUNT_tlb_fetch_walk:ARC_RPC_COUNT_tlb_data_walk;
    arc_rpc_exercise_add(arc_rpc_counter_base+(arc_rpc_tag_hit?1:0),1);
#endif
    uint32_t domain = 0, fault_code;
    if (!entry->valid || entry->tag != (addr >> 10)) {
        uint32_t fld = mem_phys_read32(cp15.translation_table | ((addr >> 18) & 0x3ffcu));
        domain = (fld >> 5) & 15;
        uint32_t physical, ap, attributes = fld & 12;
        int page = (fld & 3) == 1;
        tlbs++;
        if (page) {
            uint32_t sld = mem_phys_read32((fld & 0xfffffc00u) | ((addr >> 10) & 0x3fcu));
            unsigned shift;
            attributes = sld & 12;
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
        *entry = (ArcRpcTlbEntry){arc_rpc_phys_page(physical), addr >> 10, physical & ~0x3ffu,
                                  (uint8_t)domain, (uint8_t)ap, (uint8_t)page, 1, (uint8_t)attributes};
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
#if defined(ARCDROID_TEST_FAULTS)
    if(arc_rpc_tag_hit && !(arm.event&0x40))arc_rpc_exercise_add(arc_rpc_counter_base+2,1);
#endif
    arc_rpc_attributes = entry->attributes;
    return entry;

do_fault:
    arm.event |= 0x40;
    if (!prefetch) {
        cp15.fault_address = addr;
        cp15.fault_status = (domain << 4) | fault_code;
    }
    return NULL;
}

uint32_t translateaddress2(uint32_t addr, int rw, int prefetch)
{
    ArcRpcTlbEntry *entry = checked_translation(addr, rw, prefetch);
    return entry ? entry->physical | (addr & 0x3ffu) : 0;
}

#if defined(ARCDROID_RPC_PROFILE)
#undef getpccache
#define getpccache arc_rpc_getpccache_profile_body
#endif
const uint32_t *getpccache(uint32_t addr)
{
    ArcRpcTlbEntry *entry = mmu ? checked_translation(addr, 0, 1) : NULL;
    if (arm.event & 0x40) {
        arm.event &= ~0x40u;
        return NULL;
    }
    uint32_t physical = entry ? entry->physical | (addr & 0x3ffu) : addr;
    const uint32_t *code = entry ? entry->code : arc_rpc_phys_page(physical);
#if defined(ARCDROID_RPC_PROFILE)
    int previous=arc_rpc_profile_enter(ARC_RPC_COST_TIMING);
#endif
    arc_rpc_mem_fetch(addr, physical, arc_rpc_cp15_attributes(), code != NULL);
#if defined(ARCDROID_RPC_PROFILE)
    arc_rpc_profile_leave(previous);
#endif
    return code;
}
#if defined(ARCDROID_RPC_PROFILE)
#undef getpccache
#define getpccache arc_rpc_getpccache

const uint32_t *
getpccache(uint32_t addr)
{

    int previous=arc_rpc_profile_enter(ARC_RPC_COST_FETCH);
    const uint32_t * result = arc_rpc_getpccache_profile_body(addr);
    arc_rpc_profile_leave(previous);
    return result;
}
#endif

/* ArcDroid modification (2026-10-13, observation-r3c1): pure inspection
 * honours stale tagged entries but never installs a translation or touches
 * fault/attribute/timing state. Descriptor reads have no device dispatch. */
static bool inspect_descriptor(uint32_t pa,uint32_t *out)
{
    if(pa&3)return false;
    uint32_t value=0;
    for(unsigned i=0;i<4;++i){uint8_t byte;if(!arc_rpc_inspect_physical(pa+i,&byte))return false;
        value|=(uint32_t)byte<<(8*i);}
    *out=value;return true;
}
bool arc_rpc_inspect_translate(uint32_t va,bool privileged,uint32_t *physical)
{
    if(!physical)return false;
    if(!mmu){*physical=va;return true;}
    uint32_t tag=va>>10;
    const ArcRpcTlbEntry *cached=&tlb[(tag^(tag>>10)^(tag>>18))&255];
    ArcRpcTlbEntry local={0};
    if(!cached->valid || cached->tag!=tag){
        uint32_t first,second,pa,ap,domain;
        if(!inspect_descriptor(cp15.translation_table|((va>>18)&0x3ffcu),&first))return false;
        domain=(first>>5)&15;
        if((first&3)==2){pa=(first&0xfff00000u)|(va&0xfffffu);ap=(first>>10)&3;}
        else if((first&3)==1){
            if(!inspect_descriptor((first&0xfffffc00u)|((va>>10)&0x3fcu),&second))return false;
            unsigned shift;
            if((second&3)==1){pa=(second&0xffff0000u)|(va&0xffffu);shift=4+((va>>13)&6);}
            else if((second&3)==2){pa=(second&0xfffff000u)|(va&0xfffu);shift=4+((va>>9)&6);}
            else return false;
            ap=(second>>shift)&3;
        }else return false;
        local.physical=pa&~0x3ffu;local.domain=(uint8_t)domain;local.ap=(uint8_t)ap;cached=&local;
    }
    uint32_t access=cp15_domain_access(cached->domain);
    if(access==0 || access==2)return false;
    if(access==1){
        unsigned ap=cached->ap;
        if(ap==0){unsigned sr=(cp15.ctrl>>8)&3;
            if(sr==0 || sr==3 || (sr==1 && !privileged))return false;
        }else if(ap==1 && !privileged)return false;
    }
    *physical=cached->physical|(va&0x3ffu);return true;
}
#if defined(ARCDROID_TEST_FAULTS)
size_t arc_rpc_inspector_snapshot(void *out,size_t capacity)
{
    size_t n=sizeof(cp15)+sizeof(tlb)+sizeof(arc_rpc_attributes)+sizeof(tlbs)+sizeof(flushes);
    if(!out || capacity<n)return n;
    uint8_t *p=out;
    memcpy(p,&cp15,sizeof(cp15));p+=sizeof(cp15);
    memcpy(p,tlb,sizeof(tlb));p+=sizeof(tlb);
    memcpy(p,&arc_rpc_attributes,sizeof(arc_rpc_attributes));p+=sizeof(arc_rpc_attributes);
    memcpy(p,&tlbs,sizeof(tlbs));p+=sizeof(tlbs);memcpy(p,&flushes,sizeof(flushes));return n;
}
#endif
