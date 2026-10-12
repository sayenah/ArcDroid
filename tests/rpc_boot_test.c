/* Public authored-package process and VFS fault harness. GPL-2.0-only. */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#endif
#include "arc_io.h"
#include "rpc_boot.h"
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif
#define vfs_open base_open
#define vfs_close base_close
#define vfs_size base_size
#define vfs_seek base_seek
#define vfs_tell base_tell
#define vfs_read base_read
#define vfs_write base_write
#define vfs_flush base_flush
#define vfs_stat base_stat
#define vfs_remove base_remove
#define vfs_rename base_rename
#include "../tools/rpc_runner_vfs.h"
#undef vfs_open
#undef vfs_close
#undef vfs_size
#undef vfs_seek
#undef vfs_tell
#undef vfs_read
#undef vfs_write
#undef vfs_flush
#undef vfs_stat
#undef vfs_remove
#undef vfs_rename
static unsigned current_phase, points, cut, package_opens;
static const char *mode;
static bool point(void) {
  if (current_phase < ARC_RPC_BOOT_FORMAT)
    return true;
  ++points;
  if (points != cut)
    return true;
  if ((!strcmp(mode, "kill") || !strcmp(mode, "checkpoint-kill"))) {
#ifdef _WIN32
    TerminateProcess(GetCurrentProcess(), 77);
#else
    raise(SIGKILL);
#endif
    abort();
  }
  return strcmp(mode, "fault") != 0 && strcmp(mode, "checkpoint-fault") != 0;
}
static struct retro_vfs_file_handle *observed_open(const char *p, unsigned m,
                                                   unsigned h) {
  if (strstr(p, "/riscpc/boot/"))
    ++package_opens;
  if (!point())
    return NULL;
  struct retro_vfs_file_handle *f = base_open(p, m, h);
  if (f && !strcmp(mode, "swap") && strstr(p, "/riscpc/boot/"))
    assert(setvbuf(f->file, NULL, _IONBF, 0) == 0);
  if (!point() && f) {
    base_close(f);
    return NULL;
  }
  return f;
}
static int observed_close(struct retro_vfs_file_handle *f) {
  bool before = point();
  int result = base_close(f);
  bool after = point();
  return before && after ? result : -1;
}
#define WRAP(name, type, params, args, invalid)                                \
  static type observed_##name params {                                         \
    if (!point())                                                              \
      return invalid;                                                          \
    type r = base_##name args;                                                 \
    return point() ? r : invalid;                                              \
  }
WRAP(size, int64_t, (struct retro_vfs_file_handle * f), (f), -1)
WRAP(tell, int64_t, (struct retro_vfs_file_handle * f), (f), -1)
WRAP(seek, int64_t, (struct retro_vfs_file_handle * f, int64_t n, int o),
     (f, n, o), -1)
WRAP(read, int64_t, (struct retro_vfs_file_handle * f, void *p, uint64_t n),
     (f, p, n), -1)
static int64_t observed_write(struct retro_vfs_file_handle *f, const void *p,
                              uint64_t n) {
  bool before = point();
  int64_t result = base_write(f, p, before ? n : n / 2);
  return point() ? result : -1;
}
WRAP(flush, int, (struct retro_vfs_file_handle * f), (f), -1)
WRAP(stat, int32_t, (const char *p, int32_t *n), (p, n), 0)
WRAP(remove, int, (const char *p), (p), -1)
WRAP(rename, int, (const char *a, const char *b), (a, b), -1)
static void replace_package(const char *system, const char *name) {
  char from[1200], to[1200];
  snprintf(from, sizeof(from), "%s/arcdroid/riscpc/boot/%s", system, name);
  snprintf(to, sizeof(to), "%s/arcdroid/riscpc/boot/HardDisc4.zip", system);
  FILE *input = fopen(from, "rb"), *output = fopen(to, "wb");
  assert(input && output);
  uint8_t block[32768];
  size_t n;
  while ((n = fread(block, 1, sizeof(block), input)) != 0)
    assert(fwrite(block, 1, n, output) == n);
  assert(!ferror(input) && !fclose(input) && !fclose(output));
}
static const char *id = "0123456789012345678901234567890123456789";
int main(int argc, char **argv) {
  assert(argc == 7);
  const char *system = argv[1];
  assert(canonical(argv[2], save_directory));
  unsigned units = (unsigned)strtoul(argv[3], NULL, 10);
  mode = argv[4];
  cut = (unsigned)strtoul(argv[5], NULL, 10);
  unsigned special = (unsigned)strtoul(argv[6], NULL, 10);
  arc_io_set_vfs(&vfs);
  vfs.open = observed_open;
  vfs.close = observed_close;
  vfs.size = observed_size;
  vfs.tell = observed_tell;
  vfs.seek = observed_seek;
  vfs.read = observed_read;
  vfs.write = observed_write;
  vfs.flush = observed_flush;
  vfs.stat = observed_stat;
  vfs.remove = observed_remove;
  vfs.rename = observed_rename;
  char error[512] = {0};
  bool blank = !strcmp(mode, "legacy") || !strcmp(mode, "forge");
  ArcRpcHdStore *s =
      arc_riscpc_hd_open(save_directory, id, blank, error, sizeof(error));
  if (!s) {
    fprintf(stderr, "%s\n", error);
    return 3;
  }
  if (!strcmp(mode, "forge")) {
    for (unsigned n = 0; n < 100; ++n)
      arc_riscpc_hd_tick(s);
    uint8_t fake[512] = {0};
    memcpy(fake, "ARCRBOOT", 8);
    fake[8] = 1;
    memcpy(fake + 128, "ARCRHDM1", 8);
    fake[136] = 2;
    assert(arc_riscpc_hd_write(s, 200000, 1, fake));
    arc_riscpc_hd_close(s);
    s = arc_riscpc_hd_open(save_directory, id, false, error, sizeof(error));
    assert(s && !arc_riscpc_hd_receipt(s, NULL));
  }
  if (!strcmp(mode, "verify")) {
    ArcRpcHdStats a;
    arc_riscpc_hd_stats(s, &a);
    printf("{\"published\":%u,\"receipt\":%u}\n", a.published,
           arc_riscpc_hd_receipt(s, NULL));
    arc_riscpc_hd_close(s);
    vfs_assert_no_open_handles();
    return 0;
  }
  ArcRpcBoot *b = arc_riscpc_boot_begin(s, system, error, sizeof(error));
  assert(b);
  ArcRpcBootStats stats;
  bool steered = false, restored = false;
  ArcRpcBootState state;
  for (unsigned steps = 0;; ++steps) {
    assert(steps < 1000000);
    arc_riscpc_boot_stats(b, &stats);
    current_phase = stats.phase;
    if (!steered && current_phase == special && !strcmp(mode, "cancel")) {
      arc_riscpc_boot_cancel(b);
      steered = true;
    }
    if (!steered && current_phase == special && !strcmp(mode, "change")) {
      char path[1200];
      snprintf(path, sizeof(path), "%s/arcdroid/riscpc/boot/HardDisc4.zip",
               system);
      FILE *f = fopen(path, "r+b");
      assert(f);
      assert(fseek(f, 15, SEEK_SET) == 0);
      int value = fgetc(f);
      assert(value != EOF);
      assert(fseek(f, 15, SEEK_SET) == 0 && fputc(value ^ 1, f) != EOF &&
             !fclose(f));
      steered = true;
    }
    if (!strcmp(mode, "swap")) {
      if (!steered && current_phase == ARC_RPC_BOOT_INDEX) {
        replace_package(system, "Alternate.zip");
        steered = true;
      }
      if (!restored && steered && current_phase == ARC_RPC_BOOT_TREE) {
        replace_package(system, "Original.zip");
        restored = true;
      }
    }
    state = arc_riscpc_boot_step(b, units);
    if (state >= ARC_RPC_BOOT_DONE)
      break;
  }
  arc_riscpc_boot_stats(b, &stats);
  const char *notice = arc_riscpc_boot_notice(b);
  if (notice)
    fprintf(stderr, "%s\n", notice);
  assert(stats.memory_high <= 8388608 && stats.max_unit_io_bytes <= 4194304 &&
         stats.max_unit_operations <= 128);
  printf(
      "{\"phase\":%u,\"files\":%llu,\"bytes\":%llu,\"units\":%llu,\"io_bytes\":"
      "%llu,\"operations\":%llu,\"memory\":%llu,\"max_bytes\":%llu,\"max_ops\":"
      "%llu,\"allocated\":%llu,\"points\":%u,\"package_opens\":%u}\n",
      state, (unsigned long long)stats.files,
      (unsigned long long)stats.file_bytes, (unsigned long long)stats.units,
      (unsigned long long)stats.io_bytes, (unsigned long long)stats.operations,
      (unsigned long long)stats.memory_high,
      (unsigned long long)stats.max_unit_io_bytes,
      (unsigned long long)stats.max_unit_operations,
      (unsigned long long)stats.allocated_bytes, points, package_opens);
  arc_riscpc_boot_free(b);
  current_phase = ARC_RPC_BOOT_DONE;
  ArcRpcHdReceipt original_receipt;
  bool have_receipt = arc_riscpc_hd_receipt(s, &original_receipt);
  if (!strcmp(mode, "checkpoint") || !strcmp(mode, "checkpoint-fault") ||
      !strcmp(mode, "checkpoint-kill")) {
    uint8_t payload[131072];
    memset(payload, 0xa5, sizeof(payload));
    for (unsigned i = 0; i < 8; ++i)
      if (!arc_riscpc_hd_write(s, 200000 + i * 256, 256, payload))
        break;
  }
  for (unsigned n = 0; n < 4096; ++n) {
    ArcRpcHdStats a;
    arc_riscpc_hd_stats(s, &a);
    if (a.read_only || a.state == ARC_RPC_HD_IDLE)
      break;
    arc_riscpc_hd_tick(s);
  }
  if (have_receipt) {
    ArcRpcHdReceipt after;
    assert(arc_riscpc_hd_receipt(s, &after));
    assert(!memcmp(&after, &original_receipt, sizeof(after)));
  }
  printf("points=%u\n", points);
  arc_riscpc_hd_close(s);
  vfs_assert_no_open_handles();
  return state == ARC_RPC_BOOT_FAILED || state == ARC_RPC_BOOT_CANCELLED ? 2
                                                                         : 0;
}
