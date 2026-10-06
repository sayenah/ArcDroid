/* ArcDroid exact RPC audio boundary. GPL-2.0-only. */
#ifndef ARC_RPC_AUDIO_H
#define ARC_RPC_AUDIO_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define ARC_RPC_AUDIO_QUEUE 8192u
/* Frames are stereo pairs, never scalar samples. At every boundary:
 * generated = accepted + discarded + queued. DMA counts are independent.
 * peek does not retire frames; commit retires the whole offer, counting its
 * unaccepted suffix as discarded. No retries, host pacing or DMA backpressure.
 * Overflow drops the oldest frame. Reset/stop/fault purge and count the queue.
 * Counters survive reset/stop and start from zero on a fresh session. */
typedef struct {
    uint64_t generated, accepted, discarded, dma_frames, dma_blocks;
    size_t queued;
} ArcRpcAudioCounts;
size_t arc_rpc_audio_peek(int16_t *output, size_t capacity);
void arc_rpc_audio_commit(size_t offered, size_t accepted);
void arc_rpc_audio_counts(ArcRpcAudioCounts *out);
void arc_rpc_sound_reset(bool fresh);
void arc_rpc_sound_purge(void);
void arc_rpc_sound_event(void);
void arc_rpc_sound_control(uint8_t value);
void arc_rpc_sound_pointer(unsigned index, uint32_t value);
void arc_rpc_sound_format(unsigned frequency, unsigned control);
bool arc_rpc_sound_take_unsupported(void);
void arc_rpc_sound_fault_silence(size_t frames);
#endif
