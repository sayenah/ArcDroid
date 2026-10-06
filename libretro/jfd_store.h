/* ArcDroid native JFD change store. GPL-2.0-only. */
#ifndef ARCDROID_JFD_STORE_H
#define ARCDROID_JFD_STORE_H
#include "jfd.h"
#include <stdbool.h>

/* False means storage could not be prepared; the image remains read-only. */
bool arc_jfd_store_load(ArcJfdImage *image, const char *save_directory,
                        ArcJfdWritePolicy policy, bool writes_enabled,
                        bool show_notice, unsigned image_number,
                        const char **reason);
void arc_jfd_store_notice(const char *message, bool warning);
void arc_jfd_store_log_commit(unsigned image_number, uint64_t generation,
                              size_t sector_index);
#endif
