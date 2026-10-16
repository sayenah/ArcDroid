/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef ARCDROID_ST506_STUB_H
#define ARCDROID_ST506_STUB_H
#include <stdint.h>
extern int st506_present;
uint8_t st506_internal_readb(uint32_t address);
uint32_t st506_internal_readl(uint32_t address);
void st506_internal_writeb(uint32_t address, uint8_t value);
void st506_internal_writel(uint32_t address, uint32_t value);
#endif
