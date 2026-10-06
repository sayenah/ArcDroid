/* Slice 30 synthetic policy, lifecycle, route and set evidence. GPL-2.0-only. */
   if (jfd_tab_step || jfd_tab_route || jfd_tab_set)
   {
      bool (*drive)(unsigned, bool *, char[41]) =
         dlsym(module, "arc_engine_test_drive_media");
      bool (*stamp)(ArcBootObservation *) =
         dlsym(module, "arc_libretro_test_boot_stamp");
      const char *(*identity)(void) = dlsym(module, "arc_libretro_test_content_id");
      const char *(*staged)(unsigned) = dlsym(module, "arc_libretro_test_staged_path");
      bool (*guest_reset)(char *, size_t) = dlsym(module, "arc_engine_reset");
      assert(drive && stamp && identity && staged && guest_reset);
      option_launch = "off"; option_jfd_boot = "native";
      option_hard_disc = "off";
      option_machine = jfd_tab_step ? argv[6] : "A3000";
      option_cpu = !strcmp(option_machine, "A3020") ? "ARM250" : "ARM2";
      option_jfd_tab = jfd_tab_step ? argv[7] : jfd_tab_route ? argv[7] : "off";
      option_writes = jfd_tab_step ? argv[8] :
         jfd_tab_route && argc == 10 ? argv[9] : "shadow";
      if (jfd_tab_route)
      {
         void (*accept)(bool) = dlsym(module, "arc_libretro_test_adffs_accept_test_os");
         assert(accept); accept(true);
         option_launch = "auto"; option_jfd_boot = argv[6];
         char directory[4096], package[4096];
         snprintf(directory, sizeof(directory), "%s/arcdroid/adffs", system_path);
         make_directory(directory);
         snprintf(package, sizeof(package), "%s/synthetic.zip", directory);
         copy_file(argv[8], package);
      }
      bool protected = true;
      phase = PHASE_LOAD; assert(retro_load_game(&game));
      assert(drive(0u, &protected, NULL));
      char frozen[41]; snprintf(frozen, sizeof(frozen), "%s", identity());
      uint64_t staged_hash = hash_file(staged(0u));
      if (jfd_tab_route)
      {
         char initial_route[512];
         snprintf(initial_route, sizeof(initial_route), "%s", last_jfd_boot_log);
         phase = PHASE_RUN; retro_reset();
         assert(!strcmp(initial_route, last_jfd_boot_log));
         printf("tab-route: %s", last_jfd_boot_log);
         printf("tab-hidden-notices=%u tab-adffs-notices=%u\n",
                jfd_tab_hidden_notices, jfd_tab_adffs_notices);
      }
      else if (jfd_tab_set)
      {
         char digest[41];
         bool (*media_digest)(char[41]) = dlsym(module, "arc_libretro_test_boot_media_digest");
         assert(media_digest && disk_control.get_num_images() == 3u &&
                media_digest(digest));
         struct retro_game_info appended = {argv[6], NULL, 0u, NULL};
         assert(disk_control.set_eject_state(true) && disk_control.add_image_index() &&
                disk_control.replace_image_index(3u, &appended));
         struct retro_game_info requested = {argv[7], NULL, 0u, NULL};
         assert(disk_control.add_image_index() &&
                disk_control.replace_image_index(4u, &requested));
         int (*candidate)(const char *, unsigned) =
            dlsym(module, "arc_libretro_test_disc_candidate");
         bool (*root_name)(unsigned, uint8_t) =
            dlsym(module, "arc_libretro_test_disc_jfd_root_name");
         assert(candidate && root_name &&
                candidate("$.!Demo.!Run", ARC_DISC_CATALOGUE) == 4 &&
                root_name(4u, 0u) &&
                candidate("$.!Demo.!Run", ARC_DISC_CATALOGUE) == -1 &&
                root_name(4u, '!') &&
                candidate("$.!Demo.!Run", ARC_DISC_CATALOGUE) == 4);
         option_jfd_tab = "as-imaged"; variable_update = true;
         for (unsigned i = 1u; i < 5u; ++i)
         {
            assert(disk_control.set_image_index(i) &&
                   disk_control.set_eject_state(false) &&
                   drive(0u, &protected, NULL) && !protected && media_digest(digest));
            assert(disk_control.set_eject_state(true));
         }
         phase = PHASE_RUN; retro_reset();
         assert(disk_control.get_eject_state() &&
                disk_control.set_eject_state(false) && drive(0u, &protected, NULL) && protected);
         option_jfd_tab = "off"; retro_reset();
         assert(drive(0u, &protected, NULL) && !protected && !strcmp(frozen, identity()));
      }
      else if (strcmp(argv[10], "raw"))
      {
         printf("tab-writes: %s", last_jfd_writes_log);
         uint32_t expected = (uint32_t)strtoul(argv[9], NULL, 16);
         bool should_protect = strstr(last_jfd_writes_log, "state=protected") != NULL;
         assert(protected == should_protect);
         ArcBootObservation initial = {0};
         assert(stamp(&initial) && initial.jfd_write_policy ==
                (unsigned)!strcmp(option_jfd_tab, "off"));
         if (!strcmp(argv[10], "sampling"))
         {
            option_jfd_tab = "off"; variable_update = true;
            phase = PHASE_RUN; retro_run();
            assert(strstr(last_message, "change will apply after reset or reload") &&
                   drive(0u, &protected, NULL) && protected);
            unsigned notices = total_osd_calls;
            variable_update = true; retro_run();
            assert(total_osd_calls == notices);
            char error[128] = {0};
            assert(guest_reset(error, sizeof(error)) &&
                   drive(0u, &protected, NULL) && protected);
            retro_reset();
            assert(drive(0u, &protected, NULL) && !protected &&
                   strstr(last_jfd_writes_log, "reason=tab-off"));
            ArcBootObservation changed = {0};
            assert(stamp(&changed) && changed.jfd_write_policy == 1u &&
                   !arc_boot_observation_equal(&initial, &changed) &&
                   !strcmp(initial.media, changed.media));
            expected = 0x006a0000u;
         }
         bool fault_case = strstr(argv[10], "fault") != NULL;
         if (!strcmp(argv[10], "write-fault")) fault_write_substring = ".tmp";
         if (!strcmp(argv[10], "readback-fault")) fault_read_substring = ".tmp";
         if (!strcmp(argv[10], "rename-fault")) fault_rename_substring = "/arcdroid/jfd/";
         phase = PHASE_RUN;
         unsigned steady = 0u;
         for (unsigned f = 0u; f < 900u && steady < 8u; ++f)
         {
            retro_run();
            if (fault_case)
               steady = jfd_failed_status_calls &&
                  (last_video_pixel == 0x00006a00u || last_video_pixel == 0x0000006au) ?
                  steady + 1u : 0u;
            else steady = last_video_pixel == expected ? steady + 1u : 0u;
         }
         if (steady != 8u)
            fprintf(stderr, "tab guest: got %08x expected %08x\n", last_video_pixel, expected);
         assert(steady == 8u);
         if (!strcmp(argv[10], "probe-veto"))
         {
            uint8_t (*probe)(unsigned, unsigned) =
               dlsym(module, "arc_libretro_test_jfd_probe_mask");
            bool (*map)(unsigned, uint8_t *, bool, uint64_t *) =
               dlsym(module, "arc_libretro_test_jfd_map");
            uint8_t bytes[1024]; uint64_t generation = 99u;
            assert(probe && map && probe(0u, 0u) == (1u << 3u) &&
                   map(0u, bytes, false, &generation) && !generation &&
                   !jfd_failed_status_calls && jfd_refusal_log_lines == 1u &&
                   strstr(last_jfd_refusal_log, "reason=probed-unformatted"));
            printf("tab-veto: %s", last_jfd_refusal_log);
         }
         if (fault_case)
         {
            bool (*map)(unsigned, uint8_t *, bool, uint64_t *) =
               dlsym(module, "arc_libretro_test_jfd_map");
            uint8_t bytes[1024]; uint64_t generation = 99u;
            bool (*commit)(unsigned, size_t, uint8_t) =
               dlsym(module, "arc_libretro_test_jfd_commit");
            assert(map && commit && !commit(0u, 0u, 0x7cu) &&
                   map(0u, bytes, false, &generation) && !generation &&
                   jfd_failed_status_calls == 1u &&
                   !fault_write_substring && !fault_read_substring && !fault_rename_substring);
         }
         if (!strcmp(argv[10], "hide-restore"))
         {
            ArcDigest digest = digest_file(game.path);
            char slots[2][4096]; uint64_t hashes[2];
            for (unsigned i = 0u; i < 2u; ++i)
            {
               snprintf(slots[i], sizeof(slots[i]), "%s/arcdroid/jfd/%s.%c",
                        save_path, digest.sha1, i ? 'b' : 'a');
               hashes[i] = file_exists(slots[i]) ? hash_file(slots[i]) : 0u;
            }
            assert(hashes[0] || hashes[1]);
            option_jfd_tab = "as-imaged"; retro_reset();
            assert(drive(0u, &protected, NULL) && protected &&
                   jfd_tab_hidden_notices == 1u);
            unsigned notices = total_osd_calls;
            assert(disk_control.set_eject_state(true) && disk_control.set_eject_state(false));
            retro_reset(); assert(total_osd_calls == notices);
            for (unsigned i = 0u; i < 2u; ++i)
               assert((file_exists(slots[i]) ? hash_file(slots[i]) : 0u) == hashes[i]);
            option_jfd_tab = "off"; retro_reset();
            assert(drive(0u, &protected, NULL) && !protected);
            last_video_pixel = 0u; steady = 0u;
            for (unsigned f = 0u; f < 900u && steady < 8u; ++f)
            { retro_run(); steady = last_video_pixel == 0x00001500u ? steady + 1u : 0u; }
            assert(steady == 8u);
         }
         assert(!strcmp(frozen, identity()) && hash_file(staged(0u)) == staged_hash);
      }
      if (jfd_tab_step && !strcmp(argv[10], "raw"))
      {
         bool initial_protection = protected;
         option_jfd_tab = !strcmp(option_jfd_tab, "off") ? "as-imaged" : "off";
         phase = PHASE_RUN; retro_reset();
         assert(drive(0u, &protected, NULL) && protected == initial_protection &&
                !strcmp(frozen, identity()) && hash_file(staged(0u)) == staged_hash);
      }
      retro_unload_game(); retro_deinit();
      assert(hash_file(game.path) == source_hash && !open_files && !open_dirs &&
             dlclose(module) == 0);
      puts("jfd-tab-harness: passed");
      return 0;
   }
