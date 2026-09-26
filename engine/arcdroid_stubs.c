/* Stubs for hardware deliberately excluded from Slice 1. GPL-2.0-only. */
#include "82c711.h"
#include "82c711_fdc.h"
#include "bmu.h"
#include "debugger.h"
#include "eterna.h"
#include "hostfs.h"
#include "ide.h"
#include "podules.h"
#include "st506.h"

int debugon;
int st506_present;
void debugger_do(void) {}
void debug_out(char *s) { (void)s; }
void debug_trap(int trap, uint32_t opcode) { (void)trap; (void)opcode; }
void debug_writememb(uint32_t address, uint8_t value) { (void)address; (void)value; }
void debug_writememl(uint32_t address, uint32_t value) { (void)address; (void)value; }
uint8_t readeterna(uint32_t address) { (void)address; return 0xffu; }
void writeeterna(uint32_t address, uint32_t value) { (void)address; (void)value; }
uint8_t podule_memc_read_b(int slot, uint32_t address) { (void)slot; (void)address; return 0xffu; }
uint32_t podule_memc_read_w(int slot, uint32_t address) { (void)slot; (void)address; return 0xffffffffu; }
void podule_memc_write_b(int slot, uint32_t address, uint8_t value) { (void)slot; (void)address; (void)value; }
void podule_memc_write_w(int slot, uint32_t address, uint32_t value) { (void)slot; (void)address; (void)value; }
uint8_t podule_read_b(int slot, uint32_t address) { (void)slot; (void)address; return 0xffu; }
uint32_t podule_read_w(int slot, uint32_t address) { (void)slot; (void)address; return 0xffffffffu; }
void podule_write_b(int slot, uint32_t address, uint8_t value) { (void)slot; (void)address; (void)value; }
void podule_write_w(int slot, uint32_t address, uint32_t value) { (void)slot; (void)address; (void)value; }
uint8_t podule_irq_state(void) { return 0u; }
uint8_t podule_read_backplane_mask(void) { return 0u; }
void podule_write_backplane_mask(uint8_t value) { (void)value; }
uint8_t st506_internal_readb(uint32_t address) { (void)address; return 0xffu; }
uint32_t st506_internal_readl(uint32_t address) { (void)address; return 0xffffffffu; }
void st506_internal_writeb(uint32_t address, uint8_t value) { (void)address; (void)value; }
void st506_internal_writel(uint32_t address, uint32_t value) { (void)address; (void)value; }
void hostfs(ARMul_State *state) { (void)state; }
uint8_t bmu_read(int address) { (void)address; return 0xffu; }
void bmu_write(int address, uint8_t value) { (void)address; (void)value; }
