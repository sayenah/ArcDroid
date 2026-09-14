#ifndef ARCDROID_HOSTFS_STUB_H
#define ARCDROID_HOSTFS_STUB_H
#include <stdint.h>
#define ARCEM_SWI_HOSTFS 0x56ac1u
typedef struct { uint32_t *Reg; } ARMul_State;
void hostfs(ARMul_State *state);
#endif
