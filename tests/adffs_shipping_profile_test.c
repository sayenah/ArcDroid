#include "adffs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression)                                                       \
  do {                                                                          \
    if (!(expression)) {                                                        \
      fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__,        \
              #expression);                                                     \
      return EXIT_FAILURE;                                                      \
    }                                                                           \
  } while (0)
int main(void) {
  static const char *const expected[6] = {
      "23256f9c73ea49a6c51d21844b2d07eaaa235bea41258b7a92e05ff488e77cae",
      "266617cf3f8c4f541730d9704dd9f390b71e00318e96a128f4b3dc609571b831",
      "2935eaa8bf7111ee726496eb2764e56206bdeed0cb220e50c4ed33c4acf9d849",
      "a73f88eac988c9057d0b46117279166f4c2cfda4e92574f385b80d57440a27fa",
      "6a012e8fc0e480c2b5cb88b98f4249303c88e7d2f32bfe7b05c8e1fc3e668e07",
      "96bb091eb6fedc96a32da876454f5e9f8aa53426d141bd20b4763a15c0f0efdc"};
  size_t n = 0;
  const ArcAdffsProfile *p = arc_adffs_profiles(&n);
  CHECK(p && n == 1u && !strcmp(p[0].name, "adffs-2.88-ioc") &&
        !strcmp(p[0].version, "ADFFS 2.88 (28 Mar 2026) IOC"));
  for (unsigned i = 0; i < 6u; ++i)
    CHECK(!strcmp(p[0].sha256[i], expected[i]));
  return 0;
}
