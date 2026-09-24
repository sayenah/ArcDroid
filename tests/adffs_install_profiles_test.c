/* Invented identities for install-path tests. GPL-2.0-only. */
#include "adffs.h"

const ArcInstallProfile *arc_install_profiles(size_t *count) {
  static const ArcInstallProfile profiles[] = {
      {123u, 4u, 1u,
       "03c03d87fa8eab79516540c4b226e7b0d0db06b736bd9855133d48e35d199c15",
       1u,
       {{"first", "Fixture first",
         {{1u, 592u, "45aaf4056148ca11eb98f5ed81818a7be4ff5ff9"},
          {2u, 592u, "a11b6926cbcec2dba059ae94a959e2a02f468929"}},
         2u, "!Fixture", 2u, "DATA", "TRACKS", true, false, 1600u},
        {"second", "Fixture second",
         {{1u, 592u, "45aaf4056148ca11eb98f5ed81818a7be4ff5ff9"},
          {3u, 592u, "9c09986f90fc30374e0c8bfb64158d8e5aa75f04"}},
         2u, "!Fixture", 3u, "DATA", "TRACKS", true, false, 1600u}},
       2u}};
  if (count) *count = sizeof(profiles) / sizeof(profiles[0]);
  return profiles;
}
