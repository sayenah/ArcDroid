/* Immutable header policy and unchanged store failure boundaries. GPL-2.0-only. */
static ArcJfdImage tab_fixture(const char *root, const char *kind, unsigned header)
{
   char path[4096], error[160];
   char suffix[16];
   if (header == 4u) snprintf(suffix, sizeof(suffix), "legacy");
   else snprintf(suffix, sizeof(suffix), "%u", header);
   snprintf(path, sizeof(path), "%s/jfd-tab/%s-%s.jfd", root, kind, suffix);
   FILE *file = fopen(path, "rb");
   assert(file && !fseek(file, 0, SEEK_END));
   long size = ftell(file); assert(size > 0 && !fseek(file, 0, SEEK_SET));
   uint8_t *bytes = malloc((size_t)size); assert(bytes);
   assert(fread(bytes, 1u, (size_t)size, file) == (size_t)size && !fclose(file));
   ArcJfdImage image;
   assert(arc_jfd_parse_owned(bytes, (size_t)size, &image, error, sizeof(error)));
   return image;
}
static void test_tab_store(const char *root)
{
   const char *kinds[] = {"captured", "created", "guard", "guard-fail", "guard-placement",
                          "short-full", "unschedulable", "nonmatching", "irregular-newmap-free"};
   uint8_t pattern[1024]; memset(pattern, 0x5a, sizeof(pattern));
   for (unsigned h = 0u; h < 5u; ++h)
      for (unsigned k = 0u; k < sizeof(kinds) / sizeof(kinds[0]); ++k)
         for (unsigned policy = 0u; policy < 2u; ++policy)
            for (unsigned writes = 0u; writes < 2u; ++writes)
            {
               clear_files();
               ArcJfdImage image = tab_fixture(root, kinds[k], h);
               char before[41], after[41];
               arc_sha1_hex(image.decoded, image.decoded_size, before);
               uint32_t flags = image.flags; bool extended = image.extended;
               bool header = h == 2u;
               bool effective = header || policy;
               assert(arc_jfd_write_authorized(&image, ARC_JFD_WRITE_AS_IMAGED) == header &&
                      arc_jfd_write_authorized(&image, (ArcJfdWritePolicy)policy) == effective);
               const char *reason = NULL;
               assert(arc_jfd_store_load(&image, "save", (ArcJfdWritePolicy)policy,
                                         writes != 0u, false, 1u, &reason));
               ArcJfdOverlay *o = image.overlay;
               assert(o && o->writable == (effective && writes && k < 3u));
               assert(!strcmp(reason, !effective ?
                  h == 4u ? "legacy-header" : (h & 1u) ? "wp-flag" : "no-flag" :
                  k >= 3u ? "layout-unsupported" : !writes ? "disk-writes-off" :
                  header ? "rw-flag" : "tab-off"));
               if (o->writable)
               {
                  assert(o->commit(&image, 1u, pattern) && o->generation == 1u);
                  if (k == 1u)
                     assert(o->commit_created(&image, 0u, 3u, pattern) && o->generation == 2u);
                  if (k == 2u)
                  {
                     uint8_t pending[1024];
                     memcpy(pending, image.decoded + image.sectors[0].data_offset, 1024u);
                     guard_pending(pending, 0u);
                     assert(o->guarded && !o->commit(&image, 0u, pending) &&
                            o->generation == 1u && !o->failed);
                  }
               }
               else assert(!files[0].exists && !files[1].exists && !files[2].exists);
               arc_sha1_hex(image.decoded, image.decoded_size, after);
               assert(!strcmp(before, after) && flags == image.flags && extended == image.extended);
               arc_jfd_free(&image);
            }
   for (unsigned h = 0u; h < 5u; ++h)
   {
      clear_files();
      uint8_t rejected[1024]; memset(rejected, 0x6c, sizeof(rejected));
      ArcJfdImage image = tab_fixture(root, "captured", h);
      const char *reason = NULL;
      assert(arc_jfd_store_load(&image, "save", ARC_JFD_WRITE_TAB_OFF,
                                true, false, 1u, &reason));
      assert(image.overlay->commit(&image, 1u, pattern) &&
             image.overlay->commit(&image, 1u, pattern));
      arc_jfd_free(&image);
      for (unsigned f = WRITE_FAULT; f <= RENAME_FAULT; ++f)
      {
         image = tab_fixture(root, "captured", h);
         assert(arc_jfd_store_load(&image, "save", ARC_JFD_WRITE_TAB_OFF,
                                   true, false, 1u, &reason));
         fault = f;
         assert(!image.overlay->commit(&image, 1u, rejected) &&
                image.overlay->failed && !image.overlay->writable &&
                image.overlay->generation == 2u && image.overlay->data[1][0] == 0x5au);
         arc_jfd_free(&image);
      }
      uint8_t *saved = malloc(files[1].size); assert(saved);
      size_t size = files[1].size; memcpy(saved, files[1].bytes, size);
      image = tab_fixture(root, "captured", h);
      unsigned notices_before = notices;
      assert(arc_jfd_store_load(&image, "save", ARC_JFD_WRITE_AS_IMAGED,
                                true, true, 1u, &reason));
      if (h != 2u)
         assert(image.overlay->hidden_store && !image.overlay->visible &&
                !image.overlay->generation && notices == notices_before + 1u);
      assert(files[1].size == size && !memcmp(saved, files[1].bytes, size));
      free(saved); arc_jfd_free(&image);
      image = tab_fixture(root, "captured", h);
      assert(arc_jfd_store_load(&image, "save", ARC_JFD_WRITE_TAB_OFF,
                                true, false, 1u, &reason) &&
             image.overlay->generation == 2u && image.overlay->data[1][0] == 0x5au &&
             image.overlay->commit(&image, 1u, pattern) && image.overlay->generation == 3u);
      arc_jfd_free(&image);
      /* Equal generations, malformed and unreadable slots stay protected. */
      unsigned newer = files[0].bytes[68] > files[1].bytes[68] ? 0u : 1u;
      unsigned older = 1u - newer;
      memcpy(files[newer].bytes + 68u, files[older].bytes + 68u, 8u);
      fix_checksum(&files[newer]);
      image = tab_fixture(root, "captured", h);
      assert(arc_jfd_store_load(&image, "save", ARC_JFD_WRITE_TAB_OFF,
                                true, false, 1u, &reason) &&
             image.overlay->damaged && !image.overlay->writable && !strcmp(reason, "overlay-invalid"));
      arc_jfd_free(&image);
      files[newer].bytes[0] ^= 1u;
      image = tab_fixture(root, "captured", h);
      assert(arc_jfd_store_load(&image, "save", ARC_JFD_WRITE_TAB_OFF,
                                true, false, 1u, &reason) && image.overlay->damaged);
      arc_jfd_free(&image);
      slot_unreadable = (int)older;
      image = tab_fixture(root, "captured", h);
      assert(arc_jfd_store_load(&image, "save", ARC_JFD_WRITE_TAB_OFF,
                                true, false, 1u, &reason) &&
             image.overlay->damaged && !image.overlay->writable);
      arc_jfd_free(&image); slot_unreadable = -1;
      image = tab_fixture(root, "captured", h);
      assert(!arc_jfd_store_load(&image, NULL, ARC_JFD_WRITE_TAB_OFF,
                                 true, false, 1u, &reason));
      arc_jfd_free(&image);
   }
   clear_files();
   puts("jfd-tab-store: policy matrix, immutable headers, guard, commit faults, hide/restore passed");
}
