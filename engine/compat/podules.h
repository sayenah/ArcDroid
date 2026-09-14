#ifndef ARCDROID_PODULE_STUB_H
#define ARCDROID_PODULE_STUB_H
#include <stdint.h>
uint8_t podule_memc_read_b(int slot, uint32_t address);
uint32_t podule_memc_read_w(int slot, uint32_t address);
void podule_memc_write_b(int slot, uint32_t address, uint8_t value);
void podule_memc_write_w(int slot, uint32_t address, uint32_t value);
uint8_t podule_read_b(int slot, uint32_t address);
uint32_t podule_read_w(int slot, uint32_t address);
void podule_write_b(int slot, uint32_t address, uint8_t value);
void podule_write_w(int slot, uint32_t address, uint32_t value);
uint8_t podule_irq_state(void);
uint8_t podule_read_backplane_mask(void);
void podule_write_backplane_mask(uint8_t value);
#endif
