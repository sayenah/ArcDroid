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

/* ArcDroid modification (2026-10-06): route every access through checked translation. */
#ifndef MEM_H
#define MEM_H

#include <stdint.h>

#include "rpcemu.h"

extern uint32_t mem_phys_read32(uint32_t addr);

extern uint32_t readmemfl(uint32_t addr);
extern uint32_t readmemfb(uint32_t addr);
extern void writememfb(uint32_t addr, uint8_t val);
extern void writememfl(uint32_t addr, uint32_t val);

extern void clearmemcache(void);
extern void mem_init(void);
extern void mem_reset(uint32_t ramsize, uint32_t vram_size);

//uint8_t pagedirty[0x1000];
#define HASH(l) (((l)>>2)&0x7FFF)

#define ROMSIZE (8*1024*1024)

extern uint32_t *ram00, *ram01, *ram1, *rom, *vram;
extern uint8_t *romb;

#define translateaddress(addr,rw,prefetch) translateaddress2(addr,rw,prefetch)
extern uint32_t translateaddress2(uint32_t addr, int rw, int prefetch);

extern int mmu,memmode;

extern void cacheclearpage(uint32_t a);

extern uint32_t mem_rammask;
extern uint32_t mem_vrammask;

/**
 * Read a 32-bit word from a virtual address.
 *
 * Performs direct access if possible.
 *
 * @param addr Virtual address
 * @return 32-bit word read from given virtual address
 */
static inline uint32_t
mem_read32(uint32_t addr)
{
	return readmemfl(addr);
}

/**
 * Read a byte from a virtual address.
 *
 * Performs direct access if possible.
 *
 * @param addr Virtual address
 * @return Byte read from given virtual address
 */
static inline uint32_t
mem_read8(uint32_t addr)
{
	return readmemfb(addr);
}

/**
 * Write a 32-bit word to a virtual address.
 *
 * Performs direct access if possible.
 *
 * @param addr Virtual address
 * @param val  32-bit word to write
 */
static inline void
mem_write32(uint32_t addr, uint32_t val)
{
	writememfl(addr, val);
}

/**
 * Write a byte to a virtual address.
 *
 * Performs direct access if possible.
 *
 * @param addr Virtual address
 * @param val  Byte to write
 */
static inline void
mem_write8(uint32_t addr, uint8_t val)
{
	writememfb(addr, val);
}

/**
 * Read a 32-bit word from a virtual address with User mode privileges.
 *
 * @param addr Virtual address
 * @return 32-bit word read from given virtual address
 */
static inline uint32_t
mem_user_read32(uint32_t addr)
{
	const int prev_memmode = memmode;
	uint32_t data;

	memmode = 0;
	data = mem_read32(addr);
	memmode = prev_memmode;

	return data;
}

/**
 * Read a byte from a virtual address with User mode privileges.
 *
 * @param addr Virtual address
 * @return Byte read from given virtual address
 */
static inline uint32_t
mem_user_read8(uint32_t addr)
{
	const int prev_memmode = memmode;
	uint32_t data;

	memmode = 0;
	data = mem_read8(addr);
	memmode = prev_memmode;

	return data;
}

/**
 * Write a 32-bit word to a virtual address with User mode privileges.
 *
 * @param addr Virtual address
 * @param val  32-bit word to write
 */
static inline void
mem_user_write32(uint32_t addr, uint32_t val)
{
	const int prev_memmode = memmode;

	memmode = 0;
	mem_write32(addr, val);
	memmode = prev_memmode;
}

/**
 * Write a byte to a virtual address with User mode privileges.
 *
 * @param addr Virtual address
 * @param val  Byte to write
 */
static inline void
mem_user_write8(uint32_t addr, uint8_t val)
{
	const int prev_memmode = memmode;

	memmode = 0;
	mem_write8(addr, val);
	memmode = prev_memmode;
}

#endif /* MEM_H */
