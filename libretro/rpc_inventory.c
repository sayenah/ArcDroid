/* Bounded firmware inventory and verified locator; D-186. GPL-2.0-only. */
#include "rpc_inventory.h"
#include "arc_io.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
   const ArcRpcFirmwareFingerprint *table;
   size_t count, bytes;
   unsigned entries;
   ArcRpcFirmwareInventory result;
} Scan;

/* One bounded snapshot shared by recognition and locator revalidation. The
 * caller supplies the stat-confirmed length; changed length fails closed. */
static void *read_image(const char *path, size_t length, bool *resource_failure)
{
   void *data = malloc(length ? length : 1u);
   if (!data) { *resource_failure = true; return NULL; }
   ArcIoFile *h = arc_io_open_stream(path, false);
   if (!h) { free(data); return NULL; }
   bool ok = arc_io_file_length(h) == (int64_t)length &&
      arc_io_read_file(h, data, length) == length &&
      arc_io_file_length(h) == (int64_t)length;
   if (!arc_io_close_file(h)) ok = false;
   if (!ok) { free(data); return NULL; }
   return data;
}
static void scan_file(Scan *s, const char *path)
{
   size_t length;
   ArcIoMetadata metadata = arc_io_metadata(path, &length);
   if (metadata == ARC_IO_METADATA_UNKNOWN || metadata == ARC_IO_METADATA_DIRECTORY) {
      s->result.vfs_failure = true; return;
   }
   if (metadata != ARC_IO_METADATA_FILE) return;
   bool wanted = false;
   for (size_t i=0; i<s->count; ++i)
      if (length == s->table[i].size) wanted = true;
   if (!wanted) return;
   if (length > ARC_RPC_IMAGE_BYTES || length > ARC_RPC_SCAN_BYTES - s->bytes) {
      s->result.bounded = true; return;
   }
   /* Charge attempted bytes even on open/read/allocation failure. A failure
    * cannot make room for a later prefix-dependent recognition. */
   s->bytes += length;
   bool resource = false;
   void *data = read_image(path, length, &resource);
   if (!data) {
      if (resource) s->result.resource_failure = true;
      else s->result.vfs_failure = true;
      return;
   }
   ArcDigest digest = arc_digest(data, length);
   const ArcRpcFirmwareFingerprint *f = arc_riscpc_firmware_recognize(&digest, s->table, s->count);
   if (f) {
      ArcRpcFirmwareLocator *locator = &s->result.locator[f->os];
      if (!locator->path[0] || strcmp(path, locator->path) < 0) {
         strcpy(locator->path, path); locator->fingerprint = digest;
      }
      s->result.installed |= ARC_RPC_OS_BIT(f->os);
   }
   free(data);
}
static void scan_directory(Scan *s, const char *path, unsigned depth)
{
   struct retro_vfs_dir_handle *d = arc_io_open_directory(path);
   if (!d) { s->result.vfs_failure = true; return; }
   for (;;) {
      const char *name; bool regular; uint64_t operations = 0;
      int status = arc_io_read_directory(d, &name, &regular, &operations);
      if (!status) break;
      if (s->entries == ARC_RPC_SCAN_ENTRIES) { s->result.bounded = true; break; }
      ++s->entries;
      if (status < 0) { s->result.vfs_failure = true; continue; }
      if (!strcmp(name,".") || !strcmp(name,"..")) continue;
      /* Directory entries are names, not arbitrary paths. */
      if (strchr(name,'/')) { s->result.vfs_failure = true; continue; }
      char child[ARC_RPC_FIRMWARE_PATH];
      int n = snprintf(child, sizeof(child), "%s/%s", path, name);
      if (n<0 || (size_t)n>=sizeof(child)) { s->result.bounded = true; break; }
      if (!regular) {
         if (depth) scan_directory(s,child,depth-1);
         else s->result.bounded = true;
      } else scan_file(s,child);
      if (s->result.bounded) break;
   }
   /* Boolean readdir EOF cannot distinguish silent listing failure. */
   if (!arc_io_close_directory(d)) s->result.vfs_failure = true;
}
ArcRpcFirmwareInventory arc_riscpc_inventory_scan(const char *directory,
   const ArcRpcFirmwareFingerprint *table, size_t count)
{
   Scan s = {0}; s.table=table; s.count=count;
   s.result.state = ARC_RPC_INVENTORY_SCANNED;
   if (!directory || !directory[0] || (!table && count)) s.result.vfs_failure = true;
   else if (strlen(directory) >= ARC_RPC_FIRMWARE_PATH) s.result.bounded = true;
   else {
      ArcIoMetadata root = arc_io_metadata(directory, NULL);
      if (root == ARC_IO_METADATA_UNKNOWN || !arc_io_read_available()) s.result.vfs_failure = true;
      else if (root == ARC_IO_METADATA_DIRECTORY) scan_directory(&s,directory,ARC_RPC_SCAN_DEPTH);
   }
   if (s.result.bounded) {
      s.result.installed = 0;
      memset(s.result.locator, 0, sizeof(s.result.locator));
   }
   return s.result;
}
bool arc_riscpc_firmware_read(const ArcRpcFirmwareInventory *inventory,
   ArcRpcSelectionOs os, void **data, size_t *size)
{
   if (data) *data = NULL;
   if (size) *size = 0;
   if (!data || !size || !inventory || inventory->state != ARC_RPC_INVENTORY_SCANNED || inventory->bounded ||
       os < ARC_RPC_OS_350 || os > ARC_RPC_OS_371 ||
       !(inventory->installed & ARC_RPC_OS_BIT(os)) || !arc_io_read_available()) return false;
   const ArcRpcFirmwareLocator *locator = &inventory->locator[os];
   if (!locator->path[0] || !memchr(locator->path, 0, sizeof(locator->path)) ||
       locator->fingerprint.size > ARC_RPC_IMAGE_BYTES) return false;
   size_t length;
   if (arc_io_metadata(locator->path, &length) != ARC_IO_METADATA_FILE ||
       length != locator->fingerprint.size) return false;
   bool resource = false;
   void *buffer = read_image(locator->path, length, &resource);
   if (!buffer) return false;
   ArcDigest digest = arc_digest(buffer, length);
   if (digest.size != locator->fingerprint.size ||
       digest.crc32 != locator->fingerprint.crc32 ||
       strcmp(digest.sha1, locator->fingerprint.sha1)) { free(buffer); return false; }
   *data = buffer; *size = length; return true;
}
