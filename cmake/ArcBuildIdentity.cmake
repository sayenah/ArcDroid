# SPDX-License-Identifier: GPL-2.0-only
# All frontend targets share one build-time, write-if-different identity.
function(arc_build_identity_setup)
  set(ARCDROID_SOURCE_REVISION "" CACHE STRING "Exact source revision for git-archive builds")
  add_custom_target(arc_build_identity ALL
    COMMAND "${CMAKE_COMMAND}"
      "-DARC_IDENTITY_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}"
      "-DARC_IDENTITY_REVISION=${ARCDROID_SOURCE_REVISION}"
      "-DARC_IDENTITY_TEMPLATE=${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../libretro/arc_build_identity.h.in"
      "-DARC_IDENTITY_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/generated/arc_build_identity.h"
      -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/update-build-identity.cmake"
    BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/generated/arc_build_identity.h"
    VERBATIM)
endfunction()
