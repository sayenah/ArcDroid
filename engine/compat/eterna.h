/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef ARCDROID_ETERNA_STUB_H
#define ARCDROID_ETERNA_STUB_H
#include <stdint.h>
uint8_t readeterna(uint32_t address);
void writeeterna(uint32_t address, uint32_t value);
#endif
