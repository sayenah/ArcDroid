/* Bounded R3a diagnostic-only frontend VFS scan. GPL-2.0-only. */
#include "rpc_inventory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
   struct retro_vfs_interface *vfs;
   const ArcRpcFirmwareFingerprint *table;
   size_t count, bytes;
   unsigned entries;
   ArcRpcFirmwareInventory result;
} Scan;
static void scan_file(Scan *s, const char *path)
{
   struct retro_vfs_file_handle *h = s->vfs->open(path, RETRO_VFS_FILE_ACCESS_READ,
      RETRO_VFS_FILE_ACCESS_HINT_NONE);
   if (!h) { s->result.vfs_failure = true; return; }
   int64_t length = s->vfs->size(h);
   if (length < 0) s->result.vfs_failure = true;
   bool wanted = false;
   for (size_t i=0; i<s->count; ++i)
      if (length >= 0 && (uint64_t)length == s->table[i].size) wanted = true;
   void *data = NULL;
   if (wanted && (uint64_t)length <= 4194304u) {
      if ((uint64_t)length > ARC_RPC_SCAN_BYTES - s->bytes) s->result.bounded = true;
      else {
         s->bytes += (size_t)length;
         data = malloc(length ? (size_t)length : 1u);
         if (!data) s->result.resource_failure = true;
         else if (s->vfs->read(h, data, (uint64_t)length) != length)
            s->result.vfs_failure = true;
         else {
            ArcDigest d = arc_digest(data, (size_t)length);
            const ArcRpcFirmwareFingerprint *f = arc_riscpc_firmware_recognize(&d, s->table, s->count);
            /* Close failures invalidate this file's evidence as well. */
            if (s->vfs->close(h) != 0) s->result.vfs_failure = true;
            else if (f) s->result.installed |= ARC_RPC_OS_BIT(f->os);
            free(data); return;
         }
      }
   } else if (wanted) s->result.bounded = true;
   free(data);
   if (s->vfs->close(h) != 0) s->result.vfs_failure = true;
}
static void scan_directory(Scan *s, const char *path, unsigned depth)
{
   struct retro_vfs_dir_handle *d = s->vfs->opendir(path, false);
   if (!d) { s->result.vfs_failure = true; return; }
   while (s->vfs->readdir(d)) {
      if (s->entries == ARC_RPC_SCAN_ENTRIES) { s->result.bounded = true; break; }
      ++s->entries;
      const char *name = s->vfs->dirent_get_name(d);
      if (!name) { s->result.vfs_failure = true; continue; }
      if (!name[0] || !strcmp(name,".") || !strcmp(name,"..")) continue;
      char child[4096];
      int n = snprintf(child, sizeof(child), "%s/%s", path, name);
      if (n<0 || (size_t)n>=sizeof(child)) { s->result.bounded = true; continue; }
      if (s->vfs->dirent_is_dir(d)) {
         if (depth) scan_directory(s,child,depth-1);
         else s->result.bounded = true;
      } else scan_file(s,child);
   }
   /* readdir's boolean cannot distinguish EOF from failure. Never invent an
    * unreadable-inventory failure from a silently truncated listing. */
   if (s->vfs->closedir(d) != 0) s->result.vfs_failure = true;
}
ArcRpcFirmwareInventory arc_riscpc_inventory_scan(struct retro_vfs_interface *vfs,
   const char *directory, const ArcRpcFirmwareFingerprint *table, size_t count)
{
   Scan s = {0}; s.vfs=vfs; s.table=table; s.count=count;
   if (!directory || !vfs || !vfs->open || !vfs->size || !vfs->read ||
       !vfs->close || !vfs->opendir || !vfs->readdir || !vfs->dirent_get_name ||
       !vfs->dirent_is_dir || !vfs->closedir) s.result.vfs_failure = true;
   else scan_directory(&s,directory,ARC_RPC_SCAN_DEPTH);
   return s.result;
}
