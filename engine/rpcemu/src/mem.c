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

/* ArcDroid modification (2026-10-06): owned configuration-sized memory,
 * checked translations and in-object pointers; no desktop allocations. */
/* Memory handling */
#include "rpc_memory.h"
#include "rpc_observer.h"
#include "rpc_mem_timing.h"
/* ArcDroid modification (2026-10-07): charge successful CPU attempts only;
 * physical descriptor reads and DMA do not enter the CPU timing ledger. */

#include "rpcemu.h"
#include "vidc20.h"
#include "mem.h"
#include "iomd.h"
#include "ide.h"
#include "arm.h"
#include "cp15.h"
#include "superio.h"
#include "podules.h"
#include "fdc.h"

/* References -
   Acorn Risc PC - Technical Reference Manual
*/

uint32_t *ram00 = NULL; /**< Word pointer to SIMM 0 Bank 0 of physical RAM */
uint32_t *ram01 = NULL; /**< Word pointer to SIMM 0 Bank 1 of physical RAM */
uint32_t *ram1  = NULL; /**< Word pointer to SIMM 1 of physical RAM */
uint32_t *rom   = NULL; /**< Word pointer to ROM */
uint32_t *vram  = NULL; /**< Word pointer to Video RAM */

int mmu = 0;     /**< Bool of whether the MMU is enabled */
int memmode = 0; /**< Bool of whether ARM is in a privileged mode */

uint32_t mem_rammask; /**< Mask used for SIMM Bank 0/1 to handle the repeating address space */
uint32_t mem_vrammask; /**< Mask used for VRAM to handle the repeating address space */

static uint8_t *ramb00 = NULL; /**< Byte pointer to SIMM 0 Bank 0 of physical RAM */
static uint8_t *ramb01 = NULL; /**< Byte pointer to SIMM 0 Bank 1 of physical RAM */
static uint8_t *ramb1  = NULL; /**< Byte pointer to SIMM 1 of physical RAM */
uint8_t *romb = NULL;          /**< Byte pointer to ROM */
static uint8_t *vramb  = NULL; /**< Byte pointer to Video RAM */

static uint32_t phys_space_mask;

void clearmemcache(void) { /* All translations are owned by CP15. */ }

bool arc_rpc_memory_load_rom(const uint8_t *bytes, size_t size)
{
    if (!rom || !bytes || (size != 2u * 1024u * 1024u &&
        size != 4u * 1024u * 1024u && size != ROMSIZE)) return false;
    for (size_t offset = 0; offset < ROMSIZE; offset += size)
        memcpy(romb + offset, bytes, size);
    return true;
}

void arc_rpc_memory_close(void)
{
    free(rom); free(vram); free(ram00); free(ram01); free(ram1);
    rom = vram = ram00 = ram01 = ram1 = NULL;
    romb = ramb00 = ramb01 = ramb1 = vramb = NULL;
    mem_rammask = mem_vrammask = phys_space_mask = 0;
    mmu = memmode = 0;
}

/* Ownership is recorded immediately after each allocation. The caller must
 * have a live fault boundary. Cleanup also handles any partially built state. */
void mem_init(void)
{
    if (config.mem_size != 64 || (config.vram_size != 0 && config.vram_size != 2))
        fatal("Unsupported memory configuration");
    if (rom || ram00 || ram01 || vram || ram1)
        fatal("Memory already owned");
    rom = arc_rpc_alloc(ROMSIZE);
    if (!rom) fatal("ROM allocation failed");
    romb = (uint8_t *)rom;
    ram00 = arc_rpc_alloc(32u * 1024u * 1024u);
    if (!ram00) fatal("RAM bank 0 allocation failed");
    ramb00 = (uint8_t *)ram00;
    ram01 = arc_rpc_alloc(32u * 1024u * 1024u);
    if (!ram01) fatal("RAM bank 1 allocation failed");
    ramb01 = (uint8_t *)ram01;
    if (config.vram_size) {
        vram = arc_rpc_alloc(2u * 1024u * 1024u);
        if (!vram) fatal("VRAM allocation failed");
        vramb = (uint8_t *)vram;
    }
    mem_reset(config.mem_size, config.vram_size);
}

/* ArcDroid modification (2026-10-13, session-preparation-r3b2b):
 * suspended construction owns allocations immediately, never executes guest
 * code, and produces exactly mem_init + memory_load_rom's final bytes. */
bool arc_rpc_memory_prepare(const uint8_t *bytes,size_t size,unsigned *phase,size_t *at)
{
    switch(*phase){
    case 0:
        rom=arc_rpc_alloc(ROMSIZE);if(!rom)fatal("ROM allocation failed");romb=(uint8_t *)rom;break;
    case 1:
        ram00=arc_rpc_alloc(32u*1024u*1024u);if(!ram00)fatal("RAM bank 0 allocation failed");ramb00=(uint8_t *)ram00;break;
    case 2:
        ram01=arc_rpc_alloc(32u*1024u*1024u);if(!ram01)fatal("RAM bank 1 allocation failed");ramb01=(uint8_t *)ram01;break;
    case 3:
        if(config.vram_size){vram=arc_rpc_alloc(2u*1024u*1024u);if(!vram)fatal("VRAM allocation failed");vramb=(uint8_t *)vram;}break;
    case 4:case 5:case 6:{
        uint8_t *data=*phase==4?ramb00:*phase==5?ramb01:vramb;
        size_t total=*phase==6?(config.vram_size?2u*1024u*1024u:0):32u*1024u*1024u;
        size_t take=total-*at;if(take>1048576)take=1048576;
        if(take)memset(data+*at,0,take);*at+=take;
        if(*at<total)return false;
        *at=0;break;
    }
    case 7:{
        size_t offset=*at%size,take=32768;
        if(take>size-offset)take=size-offset;
        if(take>ROMSIZE-*at)take=ROMSIZE-*at;
        memcpy(romb+*at,bytes+offset,take);*at+=take;
        if(*at<ROMSIZE)return false;
        *at=0;mem_rammask=32u*1024u*1024u-1u;
        mem_vrammask=config.vram_size?2u*1024u*1024u-1u:0;
        phys_space_mask=0x1fffffffu;break;
    }
    default:return true;
    }
    ++*phase;return false;
}

void mem_reset(uint32_t ramsize, uint32_t vram_size)
{
    if (ramsize != 64 || vram_size != config.vram_size || !rom ||
        !ram00 || !ram01 || (vram_size && !vram))
        fatal("Invalid owned memory reset");
    mem_rammask = 32u * 1024u * 1024u - 1u;
    mem_vrammask = vram_size ? 2u * 1024u * 1024u - 1u : 0u;
    phys_space_mask = 0x1fffffffu;
    memset(ram00, 0, 32u * 1024u * 1024u);
    memset(ram01, 0, 32u * 1024u * 1024u);
    if (vram) memset(vram, 0, 2u * 1024u * 1024u);
}

/* A pointer into an actual allocation, aligned to a 1 KiB translation unit.
 * Never subtract a guest address from a host pointer. NULL means no RAM/ROM. */
const uint32_t *arc_rpc_phys_page(uint32_t addr)
{
    addr &= phys_space_mask & ~0x3ffu;
    switch (addr & 0x1f000000u) {
    case 0x00000000: return rom + ((addr & 0x7fffffu) >> 2);
    case 0x02000000: return vram ? vram + ((addr & mem_vrammask) >> 2) : NULL;
    case 0x10000000: case 0x11000000: case 0x12000000: case 0x13000000:
        return ram00 + ((addr & mem_rammask) >> 2);
    case 0x14000000: case 0x15000000: case 0x16000000: case 0x17000000:
        return ram01 + ((addr & mem_rammask) >> 2);
    default: return NULL;
    }
}

/**
 * Read a 32-bit word from a physical address.
 *
 * @param addr Physical address
 * @return 32-bit word read from given physical address
 */
uint32_t
mem_phys_read32(uint32_t addr)
{
	addr &= phys_space_mask;

	switch (addr & (phys_space_mask & 0xff000000)) { /* Select in 16MB chunks */
	case 0x00000000: /* ROM */
		return rom[(addr & 0x7fffff) >> 2];

	case 0x02000000: /* VRAM */
		if (mem_vrammask == 0)
			return 0xffffffff;
		return vram[(addr & mem_vrammask) >> 2];

	case 0x03000000: /* IO */
		if ((addr & 0xc00000) == 0) {
			/* 03000000 - 033fffff */
			uint32_t bank = (addr >> 16) & 7;

			switch (bank) {
			case 0:
				/* IOMD Registers */
				return iomd_read(addr);
			case 1:
			case 2:
				if ((addr == 0x3310000) && (machine.iomd_type == IOMDType_IOMD))
					return iomd_mouse_buttons_read();
				if (addr >= 0x3010000 && addr < 0x3012000) {
					/* SuperIO */
					if ((addr & 0xffc) == 0x7c0) {
						return readidew();
					}
					return superio_read(addr);
				}
				break;
			case 4:
				/* Podule space 0, 1, 2, 3 */
				return podules_read16((addr >> 14) & 3, PODULE_IO_TYPE_IOC, addr & 0x3fff);
			case 7:
				/* Podule space 4, 5, 6, 7 */
				return podules_read16(((addr >> 14) & 3) + 4, PODULE_IO_TYPE_IOC, addr & 0x3fff);
			}
		}
		if ((machine.model == Model_Phoebe) && (addr & 0xcffffc) == 0x8007c0) {
			return readidew();
		}
		break;

	case 0x08000000: /* EASI space */
	case 0x09000000:
	case 0x0a000000:
	case 0x0b000000:
	case 0x0c000000:
	case 0x0d000000:
	case 0x0e000000:
	case 0x0f000000:
		return podules_read32((addr >> 24) & 7, PODULE_IO_TYPE_EASI, addr & 0xffffff);

	case 0x10000000: /* SIMM 0 bank 0 */
	case 0x11000000:
	case 0x12000000:
	case 0x13000000:
		return ram00[(addr & mem_rammask) >> 2];

	case 0x14000000: /* SIMM 0 bank 1 */
	case 0x15000000:
	case 0x16000000:
	case 0x17000000:
		return ram01[(addr & mem_rammask) >> 2];

	case 0x18000000: /* SIMM 1 bank 0 */
	case 0x19000000:
	case 0x1a000000:
	case 0x1b000000:
	case 0x1c000000: /* SIMM 1 bank 1 */
	case 0x1d000000:
	case 0x1e000000:
	case 0x1f000000:
		if (ram1 != NULL) {
			return ram1[(addr & 0x7ffffff) >> 2];
		}
	}
	return 0;
}

/**
 * Read a byte from a physical address.
 *
 * @param addr Physical address
 * @return Byte read from given physical address
 */
static uint32_t
mem_phys_read8(uint32_t addr)
{
	addr &= phys_space_mask;

	switch (addr & (phys_space_mask & 0xff000000)) { /* Select in 16MB chunks */
	case 0x00000000: /* ROM */
#ifdef _RPCEMU_BIG_ENDIAN
		addr ^= 3;
#endif
		return romb[addr & 0x7fffff];

	case 0x02000000: /* VRAM */
		if (mem_vrammask == 0)
			return 0xff;
#ifdef _RPCEMU_BIG_ENDIAN
		addr ^= 3;
#endif
		return vramb[addr & mem_vrammask];

	case 0x03000000: /* IO */
		if ((addr & 0xc00000) == 0) {
			/* 03000000 - 033fffff */
			uint32_t bank = (addr >> 16) & 7;

			switch (bank) {
			case 0:
				/* IOMD Registers */
				return iomd_read(addr);
			case 1:
			case 2:
				if ((addr == 0x3310000) && (machine.iomd_type == IOMDType_IOMD))
					return iomd_mouse_buttons_read();
				if (addr >= 0x3012000 && addr <= 0x302a000)
					return fdc_dma_read(addr);
				if ((addr & 0xfff400) == 0x02b000) {
					/* Network podule */
					return 0xffffffff;
				}
				if (addr >= 0x3010000 && addr < 0x3012000) {
					/* SuperIO */
					return superio_read(addr);
				}
				break;
			case 4:
				/* Podule space 0, 1, 2, 3 */
				return podules_read8((addr >> 14) & 3, PODULE_IO_TYPE_IOC, addr & 0x3fff);
			case 7:
				/* Podule space 4, 5, 6, 7 */
				return podules_read8(((addr >> 14) & 3) + 4, PODULE_IO_TYPE_IOC, addr & 0x3fff);
			}
		}
		if ((machine.model == Model_Phoebe) && (addr & 0xcff000) == 0x800000) {
			return readide((addr >> 2) & 0x3ff);
		}
		break;

	case 0x08000000: /* EASI space */
	case 0x09000000:
	case 0x0a000000:
	case 0x0b000000:
	case 0x0c000000:
	case 0x0d000000:
	case 0x0e000000:
	case 0x0f000000:
		return podules_read8((addr >> 24) & 7, PODULE_IO_TYPE_EASI, addr & 0xffffff);

	case 0x10000000: /* SIMM 0 bank 0 */
	case 0x11000000:
	case 0x12000000:
	case 0x13000000:
#ifdef _RPCEMU_BIG_ENDIAN
		addr ^= 3;
#endif
		return ramb00[addr & mem_rammask];

	case 0x14000000: /* SIMM 0 bank 1 */
	case 0x15000000:
	case 0x16000000:
	case 0x17000000:
#ifdef _RPCEMU_BIG_ENDIAN
		addr ^= 3;
#endif
		return ramb01[addr & mem_rammask];

	case 0x18000000: /* SIMM 1 bank 0 */
	case 0x19000000:
	case 0x1a000000:
	case 0x1b000000:
	case 0x1c000000: /* SIMM 1 bank 1 */
	case 0x1d000000:
	case 0x1e000000:
	case 0x1f000000:
		if (ramb1 != NULL) {
#ifdef _RPCEMU_BIG_ENDIAN
			addr ^= 3;
#endif
			return ramb1[addr & 0x7ffffff];
		}
	}
	return 0xff;
}

/**
 * Write a 32-bit word to a physical address.
 *
 * @param addr Physical address
 * @param val  32-bit word to write
 */
static void
mem_phys_write32(uint32_t addr, uint32_t val)
{
	addr &= phys_space_mask;
    if (addr == 0x03310000u && machine.iomd_type == IOMDType_IOMD) {
        iomd_mouse_buttons_write(val);
        return;
    }


	switch (addr & (phys_space_mask & 0xff000000)) { /* Select in 16MB chunks */
	case 0x02000000: /* VRAM */
		if (mem_vrammask == 0)
			return;
		vram[(addr & mem_vrammask) >> 2] = val;
		dirtybuffer[(addr & mem_vrammask) >> 12] = 1;
		break;

	case 0x03000000: /* IO */
		if ((addr & 0xc00000) == 0) {
			uint32_t bank = (addr >> 16) & 7;

			switch (bank) {
			case 0:
				iomd_write(addr, val);
				return;
			case 1:
			case 2:
				if (addr >= 0x3010000 && addr < 0x3012000) {
					/* SuperIO */
					if ((addr & 0xffc) == 0x7c0) {
						writeidew(val);
						return;
					}
					superio_write(addr, val);
					return;
				}
				if ((addr & 0xfff0000) == 0x33a0000) {
					/* Econet? */
					return;
				}
				break;
			case 4:
				/* Podule space 0, 1, 2, 3 */
				podules_write16((addr >> 14) & 3, PODULE_IO_TYPE_IOC, addr & 0x3fff, val >> 16);
				break;
			case 7:
				/* Podule space 4, 5, 6, 7 */
				podules_write16(((addr >> 14) & 3) + 4, PODULE_IO_TYPE_IOC, addr & 0x3fff, val >> 16);
				break;
			}
		}
		if ((addr & 0xc00000) == 0x400000) {
			/* VIDC20 */
			writevidc20(val);
			return;
		}
		if ((machine.model == Model_Phoebe) && (addr & 0xcffffc) == 0x8007c0) {
			writeidew(val);
			return;
		}
		break;

	case 0x08000000: /* EASI space */
	case 0x09000000:
	case 0x0a000000:
	case 0x0b000000:
	case 0x0c000000:
	case 0x0d000000:
	case 0x0e000000:
	case 0x0f000000:
		podules_write32((addr >> 24) & 7, PODULE_IO_TYPE_EASI, addr & 0xffffff, val);
		return;

	case 0x10000000: /* SIMM 0 bank 0 */
	case 0x11000000:
	case 0x12000000:
	case 0x13000000:
		ram00[(addr & mem_rammask) >> 2] = val;
		/* In 0MB VRAM modes allow up to 4MB of writes to DRAM video data to update the dirty buffer */
		if ((mem_vrammask == 0) && ((addr & 0xffc00000) == (iomd.vidstart & 0xffc00000))) {
			dirtybuffer[(addr & 0x3fffffu) >> 12] = 1;
		}
		return;

	case 0x14000000: /* SIMM 0 bank 1 */
	case 0x15000000:
	case 0x16000000:
	case 0x17000000:
		ram01[(addr & mem_rammask) >> 2] = val;
		return;

	case 0x18000000: /* SIMM 1 bank 0 */
	case 0x19000000:
	case 0x1a000000:
	case 0x1b000000:
	case 0x1c000000: /* SIMM 1 bank 1 */
	case 0x1d000000:
	case 0x1e000000:
	case 0x1f000000:
		if (ram1 != NULL) {
			ram1[(addr & 0x7ffffff) >> 2] = val;
		}
		return;
	}
}

/**
 * Write a byte to a physical address.
 *
 * @param addr Physical address
 * @param val  Byte to write
 */
static void
mem_phys_write8(uint32_t addr, uint8_t val)
{
	addr &= phys_space_mask;
    if (addr == 0x03310000u && machine.iomd_type == IOMDType_IOMD) {
        iomd_mouse_buttons_write(val);
        return;
    }


	switch (addr & (phys_space_mask & 0xff000000)) { /* Select in 16MB chunks */
	case 0x02000000: /* VRAM */
		if (mem_vrammask == 0)
			return;
#ifdef _RPCEMU_BIG_ENDIAN
		addr ^= 3;
#endif
		vramb[addr & mem_vrammask] = val;
		dirtybuffer[(addr & mem_vrammask) >> 12] = 1;
		return;

	case 0x03000000: /* IO */
		if ((addr & 0xc00000) == 0) {
			uint32_t bank = (addr >> 16) & 7;

			switch (bank) {
			case 0:
				iomd_write(addr, val);
				return;
			case 1:
			case 2:
				if (addr == 0x3310000)
					return;
				if ((addr & 0xfc0000) == 0x240000)
					return;
				if (addr >= 0x3012000 && addr <= 0x302a000) {
					fdc_dma_write(addr, val);
					return;
				}
				if (addr >= 0x3010000 && addr < 0x3012000) {
					/* SuperIO */
					superio_write(addr, val);
					return;
				}
				if ((addr & 0xfff0000) == 0x33a0000) {
					/* Econet? */
					return;
				}
				break;
			case 4:
				/* Podule space 0, 1, 2, 3 */
				podules_write8((addr >> 14) & 3, PODULE_IO_TYPE_IOC, addr & 0x3fff, val);
				break;
			case 7:
				/* Podule space 4, 5, 6, 7 */
				podules_write8(((addr >> 14) & 3) + 4, PODULE_IO_TYPE_IOC, addr & 0x3fff, val);
				break;
			}
		}
		if ((machine.model == Model_Phoebe) && (addr & 0xcff000) == 0x800000) {
			writeide((addr >> 2) & 0x3ff, val);
			return;
		}
		break;

	case 0x08000000: /* EASI space */
	case 0x09000000:
	case 0x0a000000:
	case 0x0b000000:
	case 0x0c000000:
	case 0x0d000000:
	case 0x0e000000:
	case 0x0f000000:
		podules_write8((addr >> 24) & 7, PODULE_IO_TYPE_EASI, addr & 0xffffff, val);
		return;

	case 0x10000000: /* SIMM 0 bank 0 */
	case 0x11000000:
	case 0x12000000:
	case 0x13000000:
#ifdef _RPCEMU_BIG_ENDIAN
		addr ^= 3;
#endif
		ramb00[addr & mem_rammask] = val;
		/* In 0MB VRAM modes allow up to 4MB of writes to DRAM video data to update the dirty buffer */
		if ((mem_vrammask == 0) && ((addr & 0xffc00000) == (iomd.vidstart & 0xffc00000))) {
			dirtybuffer[(addr & 0x3fffffu) >> 12] = 1;
		}
		return;

	case 0x14000000: /* SIMM 0 bank 1 */
	case 0x15000000:
	case 0x16000000:
	case 0x17000000:
#ifdef _RPCEMU_BIG_ENDIAN
		addr ^= 3;
#endif
		ramb01[addr & mem_rammask] = val;
		return;

	case 0x18000000: /* SIMM 1 bank 0 */
	case 0x19000000:
	case 0x1a000000:
	case 0x1b000000:
	case 0x1c000000: /* SIMM 1 bank 1 */
	case 0x1d000000:
	case 0x1e000000:
	case 0x1f000000:
		if (ramb1 != NULL) {
#ifdef _RPCEMU_BIG_ENDIAN
			addr ^= 3;
#endif
			ramb1[addr & 0x7ffffff] = val;
		}
		return;
	}
}

/* Every data path, including LDRT/STRT, checks tagged permissions before
 * touching the physical bus. No privilege-dependent host-pointer cache. */
uint32_t readmemfl(uint32_t addr)
{
    uint32_t physical = mmu ? translateaddress(addr, 0, 0) : addr;
    if (arm.event & 0x40) return 0;
    arc_rpc_mem_access(addr, physical, 4, false, false, arc_rpc_cp15_attributes(),
                       arc_rpc_phys_page(physical) != NULL);
    return mem_phys_read32(physical);
}
uint32_t readmemfb(uint32_t addr)
{
    uint32_t physical = mmu ? translateaddress(addr, 0, 0) : addr;
    if (arm.event & 0x40) return 0;
    arc_rpc_mem_access(addr, physical, 1, false, false, arc_rpc_cp15_attributes(),
                       arc_rpc_phys_page(physical) != NULL);
    return mem_phys_read8(physical);
}
void writememfl(uint32_t addr, uint32_t val)
{
    uint32_t physical = mmu ? translateaddress(addr, 1, 0) : addr;
    if (arm.event & 0x40) return;
    arc_rpc_mem_access(addr, physical, 4, true, false, arc_rpc_cp15_attributes(),
                       arc_rpc_phys_page(physical) != NULL);
    mem_phys_write32(physical, val);
}
void writememfb(uint32_t addr, uint8_t val)
{
    uint32_t physical = mmu ? translateaddress(addr, 1, 0) : addr;
    if (arm.event & 0x40) return;
    arc_rpc_mem_access(addr, physical, 1, true, false, arc_rpc_cp15_attributes(),
                       arc_rpc_phys_page(physical) != NULL);
    mem_phys_write8(physical, val);
}

/* ArcDroid modification (2026-10-13, observation-r3c1): allocated backing
 * only, with the existing physical mirrors; no mem_phys_read/device calls. */
bool arc_rpc_inspect_physical(uint32_t address,uint8_t *out)
{
    if(!out)return false;
    uint32_t pa=address&phys_space_mask,offset;
    const uint32_t *base;
    switch(pa&0x1f000000u){
    case 0:base=rom;offset=pa&0x7fffffu;break;
    case 0x02000000:base=vram;offset=pa&mem_vrammask;break;
    case 0x10000000:case 0x11000000:case 0x12000000:case 0x13000000:
        base=ram00;offset=pa&mem_rammask;break;
    case 0x14000000:case 0x15000000:case 0x16000000:case 0x17000000:
        base=ram01;offset=pa&mem_rammask;break;
    default:return false;
    }
    if(!base)return false;
    *out=(uint8_t)(base[offset>>2]>>((offset&3)*8));return true;
}
