/* Bounded work for the captured-only JFD file-data proof. GPL-2.0-only. */
#ifndef ARC_JFD_FILE_DATA_LIMITS_H
#define ARC_JFD_FILE_DATA_LIMITS_H

#include <stdbool.h>

enum {
   ARC_JFD_FILE_DATA_MAX_DEPTH = 16u,
   ARC_JFD_FILE_DATA_MAX_DIRS = 512u,
   ARC_JFD_FILE_DATA_MAX_OBJECTS = 8192u,
   ARC_JFD_FILE_DATA_MAX_STEPS = 65536u
};

static inline bool arc_jfd_file_data_limits_ok(unsigned depth,
                                                unsigned dirs,
                                                unsigned objects,
                                                unsigned steps)
{
   return depth <= ARC_JFD_FILE_DATA_MAX_DEPTH &&
      dirs <= ARC_JFD_FILE_DATA_MAX_DIRS &&
      objects <= ARC_JFD_FILE_DATA_MAX_OBJECTS &&
      steps <= ARC_JFD_FILE_DATA_MAX_STEPS;
}

#endif
