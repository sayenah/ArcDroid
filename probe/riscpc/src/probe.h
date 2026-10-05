/* ArcDroid disposable probe, GPL-2.0-only. */
#ifndef PROBE_H
#define PROBE_H
#include <stdint.h>
#include <stddef.h>
#include <setjmp.h>
#include "rpcemu.h"
#include "arm.h"
#include "mem.h"
extern jmp_buf probe_escape;
extern int probe_can_escape, probe_failed, probe_idle;
extern char probe_error[256];
extern uint64_t probe_now, probe_instructions, probe_audio_hash;
extern int probe_batch_instructions;
extern unsigned probe_mips, probe_audio_count;
extern int16_t probe_audio[16384*2];
extern uint64_t probe_underruns, probe_overruns;
extern uint64_t probe_sound_due;
extern uint32_t probe_sound_rate;
extern FILE *probe_register_log;
void probe_register_trace(unsigned device,unsigned reg,uint32_t value);
void probe_register_trace_reset(void);
uint64_t probe_wall_ns(void);
uint64_t probe_hash(uint64_t seed, const void *data, size_t n);
void probe_machine_init(Model m, unsigned mips);
void probe_machine_reset(void);
void probe_machine_close(void);
void probe_advance(uint64_t target);
void probe_sound_reset(void);
void probe_sound_step(void);
void probe_sound_flush(uint64_t target);
int probe_sound_enabled(void);
uint64_t probe_video_period(void);
extern uint64_t probe_video_due, probe_video_frames, probe_video_last_period;
void probe_video_start(void);
void probe_video_step(void);
void probe_video_dma_write(unsigned reg);
uint64_t probe_video_blank_ns(void);
size_t probe_video_pitch(void);
uint64_t probe_video_hash(void);
const uint32_t *probe_video_pixels(unsigned *w, unsigned *h);
void probe_authored_rom(void);
void probe_workload_rom(void);
/* Cold state comes only from a guest factory reset, never a bundled dump. */
extern int probe_cmos_factory;
void probe_cmos_configure(const char *path, const char *sha1, int factory);
void probe_cmos_snapshot(uint8_t out[256]);
extern FILE *probe_boot_log;
extern FILE *probe_mode_log;
void probe_observe_swi(uint32_t number);
void probe_mode_reset(void);
extern uint64_t probe_keyboard_due;
void probe_keyboard_step(void);
#endif
