/* ArcDroid probe: electrically absent expansion and network
 * devices for the initial mechanism gate. GPL-2.0-only.
 * This file must be replaced before media acceptance; it never claims I/O success. */
#include "probe.h"
#include "podules.h"
#include "hostfs.h"
#include "ide.h"
#include "fdc.h"
#include "disc.h"

void hostfs(ARMul_State *s) { (void)s; fatal("HostFS excluded"); }
void runpoduletimers(int t) { (void)t; }
uint32_t podules_read32(int n,PoduleIoType t,uint32_t a) { (void)n;(void)t;(void)a;return 0xffffffff; }
uint16_t podules_read16(int n,PoduleIoType t,uint32_t a) {return (uint16_t)podules_read32(n,t,a);}
uint8_t podules_read8(int n,PoduleIoType t,uint32_t a) {return (uint8_t)podules_read32(n,t,a);}
void podules_write32(int n,PoduleIoType t,uint32_t a,uint32_t v) {(void)n;(void)t;(void)a;(void)v;}
void podules_write16(int n,PoduleIoType t,uint32_t a,uint16_t v) {podules_write32(n,t,a,v);}
void podules_write8(int n,PoduleIoType t,uint32_t a,uint8_t v) {podules_write32(n,t,a,v);}
void resetcodeblocks(void) {pccache=0xffffffff;}
void cacheclearpage(uint32_t a) {(void)a;pccache=0xffffffff;}
void initcodeblocks(void) {}
