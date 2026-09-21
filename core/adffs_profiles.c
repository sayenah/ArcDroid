/* Fingerprints for redistributable code are intentionally not present here:
 * these hashes only recognise a user-supplied package. GPL-2.0-only. */
#include "adffs.h"

const ArcAdffsProfile *arc_adffs_profiles(size_t *count) {
  static const ArcAdffsProfile profiles[] = {
      {"adffs-2.88-ioc",
       {"23256f9c73ea49a6c51d21844b2d07eaaa235bea41258b7a92e05ff488e77cae",
        "266617cf3f8c4f541730d9704dd9f390b71e00318e96a128f4b3dc609571b831",
        "2935eaa8bf7111ee726496eb2764e56206bdeed0cb220e50c4ed33c4acf9d849",
        "a73f88eac988c9057d0b46117279166f4c2cfda4e92574f385b80d57440a27fa",
        "6a012e8fc0e480c2b5cb88b98f4249303c88e7d2f32bfe7b05c8e1fc3e668e07",
        "96bb091eb6fedc96a32da876454f5e9f8aa53426d141bd20b4763a15c0f0efdc"},
       "ADFFS 2.88 (28 Mar 2026) IOC"}};
  if (count)
    *count = sizeof(profiles) / sizeof(profiles[0]);
  return profiles;
}
