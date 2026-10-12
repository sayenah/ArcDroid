/* R3b streaming ZIP reader. ArcDroid-authored GPL-2.0-only.
 * ZIP framing/validation adapted from core/zip.c; existing callers unchanged.
 */
#ifndef ARC_RPC_BOOT_ZIP_H
#define ARC_RPC_BOOT_ZIP_H
#include "arc_io.h"
#include "arcdroid_core.h"
#include "zlib.h"
#define ARC_BOOT_ZIP_ENTRIES 4096u
#define ARC_BOOT_INPUT_CAP 33554432u
#define ARC_BOOT_MEMBER_CAP 16777216u
#define ARC_BOOT_EXPANDED_CAP 100663296u
#define ARC_BOOT_QUANTUM 32768u

typedef struct {
  char name[256];
  uint32_t size, packed, crc, load, exec, attr, local, data, end;
  uint16_t flags, method;
  uint8_t hash[32];
  bool directory;
} ArcBootZipEntry;
typedef struct {
  ArcIoFile *file;
  ArcBootZipEntry entries[ARC_BOOT_ZIP_ENTRIES];
  unsigned count, indexed, extracting;
  uint32_t length, central, central_end, central_at, packed_at, output_at, crc;
  uint64_t io_bytes, operations, expanded, zbytes, zhigh;
  uint8_t scratch[65557], packed[ARC_BOOT_QUANTUM], output[ARC_BOOT_QUANTUM];
  uint8_t pins[ARC_BOOT_INPUT_CAP / ARC_BOOT_QUANTUM][32],
      verify[ARC_BOOT_QUANTUM];
  bool pinned;
  ArcSha256 hash;
  z_stream stream;
  bool inflating, extracting_active, ended;
} ArcBootZip;
/* Open/read ranges are exact and read-only; close failure is a refusal. */
bool arc_boot_zip_open(ArcBootZip *, const char *path, char *error,
                       size_t size);
bool arc_boot_zip_close(ArcBootZip *);
bool arc_boot_zip_read(ArcBootZip *, uint32_t at, void *out, size_t bytes);
void arc_boot_zip_pin(ArcBootZip *, uint32_t at, const void *data,
                      size_t bytes);
bool arc_boot_zip_directory(ArcBootZip *, char *error, size_t size);
bool arc_boot_zip_index_step(ArcBootZip *, char *error, size_t size);
bool arc_boot_zip_extract_begin(ArcBootZip *, unsigned entry);
bool arc_boot_zip_extract_step(ArcBootZip *, size_t *bytes, bool *done,
                               char *error, size_t size);
void arc_boot_zip_extract_end(ArcBootZip *);
#endif
