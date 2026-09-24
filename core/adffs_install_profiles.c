/* Exact-content, project-authored install recipe metadata. GPL-2.0-only. */
#include "adffs.h"

const ArcInstallProfile *arc_install_profiles(size_t *count) {
  static const ArcInstallProfile profiles[] = {
      {10140u, 2u, 1u,
       "0b1bf84fb117513d9715178be94fcec2d5b271095fae70d571d4ddec2dffd599",
       1u,
       {{"etype", "E-Type",
         {{1u, 445748u, "42b39361f1c4131222104d5a22b2ada69114344e"},
          {2u, 510152u, "31a5a3756d620327d08bad3b62048a98cab32826"}},
         2u, "!E-Type", 2u, "HIGH", "TRACKS", true, false, 1600u}},
       1u},
      {10609u, 4u, 1u,
       "744046fd32b92da2c47f651109b3420c462e90a5a45ccde617de2cfbfac01222",
       1u,
       {{"etype", "E-Type",
         {{1u, 428286u, "64f48cff5eeaddbbbfb04dcfb44e34db4b69642c"},
          {2u, 510152u, "6c953f23904a01606699dc2c14561fe1fbead464"}},
         2u, "!E-Type", 2u, "HIGH", "TRACKS", true, false, 1600u},
        {"xtra", "E-Type Xtra",
         {{1u, 428286u, "64f48cff5eeaddbbbfb04dcfb44e34db4b69642c"},
          {3u, 510152u, "89d6dfe9dd349de13e99ffffc215b4e5d3f1bf9e"}},
         2u, "!E-Type", 3u, "HIGH", "TRACKS", true, false, 1600u}},
       2u},
      {10587u, 1u, 1u,
       "9074d67cb21cafd219e5541f7f20e7c8b192230d5cc5e568d553d4b36dbac734",
       2u,
       {{"premier", "Premier Manager",
         {{1u, 910536u, "b23b57d1723cdd4c19df7f86ae7bd513b52d4ee7"}},
         1u, "!Premier", 0u, NULL, NULL, false, true, 2000u}},
       1u}};
  if (count) *count = sizeof(profiles) / sizeof(profiles[0]);
  return profiles;
}
