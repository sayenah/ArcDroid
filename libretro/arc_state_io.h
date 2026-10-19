/* ArcDroid rollbackable media effects. GPL-2.0-only. */
#ifndef ARC_STATE_IO_H
#define ARC_STATE_IO_H
#include "libretro.h"
#include "arc_state.h"
#include "arcdroid_core.h"
struct retro_vfs_interface *arc_state_io_attach(struct retro_vfs_interface *vfs);
void arc_state_io_clear(void);
void arc_state_io_set_commit_observer(void (*observer)(const char *, const void *, size_t));
void arc_state_io_speculate(bool speculative);
bool arc_state_io_speculative(void);
bool arc_state_io_publish(void);
void arc_state_io_snapshot(ArcState *state);
void arc_state_io_root(const char *save_directory);
void arc_state_io_binding(unsigned index, const char *path);
bool arc_state_io_store(const char *path, const char *identity);
void arc_state_io_frontiers(ArcState *state);
void arc_state_io_forget_stores(void);
bool arc_state_io_pending(void);
size_t arc_state_io_pending_bound(void);
bool arc_state_io_recover_original(const char *path);
bool arc_state_io_original(const char *path, ArcDigest *identity);

#endif
