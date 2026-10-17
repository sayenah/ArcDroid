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

/* ArcDroid modification (2026-10-06): deterministic owned sound DMA and drain. */

/* ArcDroid deterministic VIDC20 serial sound. GPL-2.0-only.
 * DMA quadword/channel layout reviewed against RPCEmu 0.9.5 sound.c,
 * Copyright (C) 2005-2010 Sarah Walker, GPL-2.0-or-later. No host sound code.
 * Bounded quadword copies replace upstream's host-threaded buffer producer.
 * Rational clocks use ceiling deadlines; tied source precedes 48 kHz output.
 * Resampling is zero-order hold (no floating-point or host-rate feedback). */
#include "rpc_lockstep.h"
#include "rpc_audio.h"
#include "rpc_timing.h"
#include "rpc_memory.h"
#include "rpcemu.h"
#include "sound.h"
#include "iomd.h"
#include <string.h>

uint32_t soundaddr[4];
int soundinited, soundlatch, soundcount;
static int16_t arc_rpc_pcm[ARC_RPC_AUDIO_QUEUE * 2];
static size_t arc_rpc_read, arc_rpc_count;
static ArcRpcAudioCounts arc_rpc_counts;
static uint32_t arc_rpc_quad[4];
static unsigned arc_rpc_quad_pos, arc_rpc_frequency, arc_rpc_format;
static bool arc_rpc_enabled, arc_rpc_ready[2], arc_rpc_unsupported, arc_rpc_notice;
static int16_t arc_rpc_left, arc_rpc_right;
/* Increment a ceiling-rounded rational deadline, retaining the remainder. */
static uint64_t arc_rpc_source_due, arc_rpc_output_due;
static uint64_t arc_rpc_source_fraction, arc_rpc_output_fraction;
static uint64_t arc_rpc_tick(uint64_t *fraction, uint64_t numerator, unsigned denominator)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_rpc_audio));
    uint64_t old = *fraction;
    uint64_t total = old + numerator;
    *fraction = total % denominator;
    return total / denominator + (*fraction != 0) - (old != 0);
}
static void arc_rpc_sound_arm(void)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_rpc_audio));
    uint64_t due = arc_rpc_output_due;
    if (arc_rpc_enabled && arc_rpc_source_due < due) due = arc_rpc_source_due;
    arc_rpc_event_after(ARC_RPC_EVENT_SOUND, due - arc_rpc_time_ns());
}
static void arc_rpc_put(int16_t left, int16_t right)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_rpc_audio));
    if (arc_rpc_count == ARC_RPC_AUDIO_QUEUE) {
        arc_rpc_read = (arc_rpc_read + 1) % ARC_RPC_AUDIO_QUEUE;
        --arc_rpc_count; ++arc_rpc_counts.discarded;
    }
    size_t at = (arc_rpc_read + arc_rpc_count) % ARC_RPC_AUDIO_QUEUE;
    arc_rpc_pcm[2*at] = left; arc_rpc_pcm[2*at+1] = right;
    ++arc_rpc_count; ++arc_rpc_counts.generated;
}
void arc_rpc_sound_purge(void)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_rpc_audio));
    arc_rpc_counts.discarded += arc_rpc_count;
    arc_rpc_read = arc_rpc_count = 0;
    arc_rpc_left = arc_rpc_right = 0;
    arc_rpc_quad_pos = 4;
}
void arc_rpc_sound_reset(bool fresh)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_rpc_audio));
    arc_rpc_sound_purge();
    if (fresh) memset(&arc_rpc_counts, 0, sizeof(arc_rpc_counts));
    memset(soundaddr, 0, sizeof(soundaddr));
    memset(arc_rpc_ready, 0, sizeof(arc_rpc_ready));
    arc_rpc_enabled = arc_rpc_unsupported = arc_rpc_notice = false;
    arc_rpc_frequency = arc_rpc_format = 0;
    arc_rpc_source_fraction = arc_rpc_output_fraction = 0;
    soundinited = soundlatch = soundcount = 0;
    arc_rpc_output_due = arc_rpc_time_ns() + arc_rpc_tick(&arc_rpc_output_fraction, 1000000000u, 48000);
    arc_rpc_sound_arm();
}
static void arc_rpc_restart_source(void)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_rpc_audio));
    arc_rpc_source_fraction = 0;
    unsigned clock = (arc_rpc_format & 1) ? 250000 : 176400;
    arc_rpc_source_due = arc_rpc_time_ns() + arc_rpc_tick(&arc_rpc_source_fraction,
        UINT64_C(1000000000) * (arc_rpc_frequency + 2), clock);
}
void arc_rpc_sound_format(unsigned frequency, unsigned control)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_rpc_audio));
    bool changed = frequency != arc_rpc_frequency || control != arc_rpc_format;
    arc_rpc_frequency = frequency & 255; arc_rpc_format = control & 3;
    if (!(control & 2)) arc_rpc_left = arc_rpc_right = 0;
    if (changed) arc_rpc_restart_source();
    if (arc_rpc_enabled && !(control & 2) && !arc_rpc_unsupported) {
        arc_rpc_unsupported = true; arc_rpc_notice = true;
    }
    arc_rpc_sound_arm();
}
/* Legacy external signature retained; the VIDC hook uses the exact ratio. */
void sound_samplefreq_change(int frequency) {
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_rpc_audio)); soundlatch = frequency; }
static void arc_rpc_sound_irq(void)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_rpc_audio));
    if (iomd.sndstat & IOMD_DMA_STATUS_INTERRUPT) iomd.irqdma.status |= IOMD_IRQDMA_SOUND_0;
    else iomd.irqdma.status &= ~IOMD_IRQDMA_SOUND_0;
    updateirqs();
}
void arc_rpc_sound_pointer(unsigned index, uint32_t value)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_rpc_audio));
    soundaddr[index] = value;
    if (index & 1) {
        unsigned bank = index / 2;
        unsigned active = iomd.sndstat & IOMD_DMA_STATUS_BUFFER;
        /* IOMD FS figure 4.8a: idle OIA/OIB accepts the current bank,
         * retaining I to request the other bank. Busy IA/IB clears I only
         * when that other bank is programmed. Wrong-bank idle writes do not
         * arm a future buffer or clear the pending interrupt. */
        if (iomd.sndstat & IOMD_DMA_STATUS_OVERRUN) {
            if (bank == active) {
                arc_rpc_ready[bank] = true;
                iomd.sndstat &= ~IOMD_DMA_STATUS_OVERRUN;
            }
        } else {
            arc_rpc_ready[bank] = true;
            if (bank != active) iomd.sndstat &= ~IOMD_DMA_STATUS_INTERRUPT;
        }
        arc_rpc_sound_irq();
    }
}
void arc_rpc_sound_control(uint8_t value)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_rpc_audio));
    if (value & 0x80) {
        memset(arc_rpc_ready, 0, sizeof(arc_rpc_ready));
        arc_rpc_quad_pos = 4; arc_rpc_left = arc_rpc_right = 0;
        iomd.sndstat = IOMD_DMA_STATUS_INTERRUPT | IOMD_DMA_STATUS_OVERRUN;
        soundinited = 1; arc_rpc_sound_irq();
    }
    bool enabled = (value & 0x20) != 0;
    if (enabled != arc_rpc_enabled) {
        arc_rpc_enabled = enabled;
        arc_rpc_restart_source();
        if (!enabled) arc_rpc_left = arc_rpc_right = 0;
    }
    if (enabled && !(arc_rpc_format & 2) && !arc_rpc_unsupported) {
        arc_rpc_unsupported = true; arc_rpc_notice = true;
    }
    arc_rpc_sound_arm();
}
static void arc_rpc_dma_sample(void)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_rpc_audio));
    if (arc_rpc_quad_pos == 4) {
        unsigned bank = iomd.sndstat & IOMD_DMA_STATUS_BUFFER;
        if (!arc_rpc_ready[bank]) { arc_rpc_left = arc_rpc_right = 0; return; }
        uint32_t current = soundaddr[bank*2] & 0x1ffffff0u;
        const uint32_t *page = arc_rpc_phys_page(current);
        for (unsigned i = 0; i < 4; ++i)
            arc_rpc_quad[i] = page ? page[((current & 0x3ff)/4)+i] : 0;
        arc_rpc_quad_pos = 0; ++arc_rpc_counts.dma_blocks;
        uint32_t end = soundaddr[bank*2+1];
        soundaddr[bank*2] = (current & ~0xfffu) | ((current + 16) & 0xfff);
        if ((current & 0xff0) == (end & 0xff0)) {
            arc_rpc_ready[bank] = false;
            unsigned next = bank ^ 1;
            iomd.sndstat = next | IOMD_DMA_STATUS_INTERRUPT;
            if ((end & 0x80000000u) || !arc_rpc_ready[next]) {
                iomd.sndstat |= IOMD_DMA_STATUS_OVERRUN;
                if (end & 0x80000000u) arc_rpc_ready[next] = false;
            }
            arc_rpc_sound_irq();
        }
    }
    uint32_t word = arc_rpc_quad[arc_rpc_quad_pos++];
    ++arc_rpc_counts.dma_frames;
    if (arc_rpc_format & 2) {
        /* Defined signed conversion, independent of host endianness. */
        arc_rpc_left = (int16_t)((int32_t)(word & 65535) - ((word & 0x8000) ? 65536 : 0));
        arc_rpc_right = (int16_t)((int32_t)(word >> 16) - ((word & 0x80000000u) ? 65536 : 0));
    } else arc_rpc_left = arc_rpc_right = 0;
}
void arc_rpc_sound_event(void)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_rpc_audio));
    uint64_t now = arc_rpc_time_ns();
    if (arc_rpc_enabled && arc_rpc_source_due == now) {
        arc_rpc_dma_sample();
        arc_rpc_source_due += arc_rpc_tick(&arc_rpc_source_fraction,
            UINT64_C(1000000000) * (arc_rpc_frequency + 2), (arc_rpc_format & 1) ? 250000 : 176400);
    }
    if (arc_rpc_output_due == now) {
        arc_rpc_put(arc_rpc_left, arc_rpc_right);
        arc_rpc_output_due += arc_rpc_tick(&arc_rpc_output_fraction, 1000000000u, 48000);
    }
    arc_rpc_sound_arm();
}
size_t arc_rpc_audio_peek(int16_t *output, size_t capacity)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_rpc_audio));
    if (!output) return 0;
    size_t count = capacity < arc_rpc_count ? capacity : arc_rpc_count;
    for (size_t i = 0; i < count; ++i) {
        size_t at = (arc_rpc_read+i) % ARC_RPC_AUDIO_QUEUE;
        output[2*i] = arc_rpc_pcm[2*at]; output[2*i+1] = arc_rpc_pcm[2*at+1];
    }
    return count;
}
void arc_rpc_audio_commit(size_t offered, size_t accepted)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_rpc_audio));
    if (offered > arc_rpc_count) offered = arc_rpc_count;
    if (accepted > offered) accepted = offered;
    arc_rpc_counts.accepted += accepted;
    arc_rpc_counts.discarded += offered - accepted;
    arc_rpc_read = (arc_rpc_read + offered) % ARC_RPC_AUDIO_QUEUE;
    arc_rpc_count -= offered;
}
void arc_rpc_audio_counts(ArcRpcAudioCounts *out)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_rpc_audio)); if (out) { *out = arc_rpc_counts; out->queued = arc_rpc_count; } }
bool arc_rpc_sound_take_unsupported(void)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_rpc_audio)); bool result = arc_rpc_notice; arc_rpc_notice = false; return result; }
void arc_rpc_sound_fault_silence(size_t frames)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_rpc_audio)); if (frames > ARC_RPC_AUDIO_QUEUE) frames = ARC_RPC_AUDIO_QUEUE; for (size_t i=0;i<frames;++i) arc_rpc_put(0,0); }

#if defined(ARCDROID_RPC_LOCKSTEP)
ARC_RPC_LS_SERIALIZE_rpc_audio
void arc_rpc_lockstep_audio_output(void)
{
    arc_rpc_lockstep_output_word(arc_rpc_count);
    for(size_t i=0;i<arc_rpc_count;++i){size_t at=(arc_rpc_read+i)%ARC_RPC_AUDIO_QUEUE;
        arc_rpc_lockstep_output_word((uint64_t)(int64_t)arc_rpc_pcm[at*2]);
        arc_rpc_lockstep_output_word((uint64_t)(int64_t)arc_rpc_pcm[at*2+1]);}
}
#endif
