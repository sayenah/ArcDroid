/* ArcDroid line-sampled RPC video boundary. GPL-2.0-only. */
#ifndef ARC_RPC_VIDEO_H
#define ARC_RPC_VIDEO_H
#include <stdbool.h>
#include <stdint.h>
#define ARC_RPC_VIDEO_LIMIT 2048u
typedef struct {
    uint64_t fields, lines, pixels, bytes, wraps, transfers, flyback_rises, flyback_falls;
    unsigned raster, vidcur, bit_offset, width, height;
    double refresh_hz;
    bool flyback, clipped;
} ArcRpcVideoStatus;
void arc_rpc_video_status(ArcRpcVideoStatus *out);
const uint32_t *arc_rpc_video_pixels(void);
void arc_rpc_video_dma_write(unsigned reg);
uint32_t arc_rpc_video_current(void);
bool arc_rpc_video_take_clip(void);
/* Aggregated memory-bus occupancy at the sampling edge, not pixel cycles. */
void arc_rpc_video_bus(unsigned transfers, bool dram);
#endif
