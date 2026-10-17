/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef ARCDROID_DEBUGGER_STUB_H
#define ARCDROID_DEBUGGER_STUB_H
#include <stdint.h>
enum { DEBUG_TRAP_PREF_ABORT, DEBUG_TRAP_DATA_ABORT, DEBUG_TRAP_ADDR_EXCEP,
       DEBUG_TRAP_UNDEF, DEBUG_TRAP_SWI };
extern int debugon;
void debugger_do(void);
void debug_out(char *s);
void debug_trap(int trap, uint32_t opcode);
void debug_writememb(uint32_t address, uint8_t value);
void debug_writememl(uint32_t address, uint32_t value);
#endif
