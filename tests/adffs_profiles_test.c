/* Test core profiles: invented fixture bytes only. GPL-2.0-only. */
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
       "ADFFS 2.88 (28 Mar 2026) IOC"},
      {"synthetic-test",
       {"492e1b8f0c90a85bb551552b9b996a522c10e227ed18fbb5de497bea67869f40",
        "0af1fd58fc5fb926bfc0b40d67b94055d75b860fbb4573cc586dddc8d9bca3cc",
        "60711b41ee9917c08e484c9c66d96d4cfb635f69f58e79d8d2a09ac427045ef7",
        "d57a874a8972b23d32ade4ede379d79efc1d66722be2913cba009b8c9db20eab",
        "a6d00f0a57eda2085675ca3a0d551ee3e4e402cdb0faf4236baba040447fdffc",
        "7444e31a16fad6c3e7ce109546b473559159742ecfb522cb43e35c175bd38f0f"},
       "Synthetic 1.0"}};
  if (count)
    *count = sizeof(profiles) / sizeof(profiles[0]);
  return profiles;
}
