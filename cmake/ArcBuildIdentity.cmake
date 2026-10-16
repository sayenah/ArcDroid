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
  # Run after all targets (including subdirectories) have been declared. A new
  # target compiling an identity consumer inherits the ordering automatically.
  cmake_language(DEFER CALL arc_build_identity_wire "${CMAKE_CURRENT_SOURCE_DIR}")
endfunction()

function(arc_build_identity_targets directory result)
  get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
  get_property(subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
  foreach(subdirectory IN LISTS subdirectories)
    arc_build_identity_targets("${subdirectory}" child_targets)
    list(APPEND targets ${child_targets})
  endforeach()
  set("${result}" "${targets}" PARENT_SCOPE)
endfunction()

function(arc_build_identity_wire directory)
  arc_build_identity_targets("${directory}" targets)
  set(audit "")
  foreach(target IN LISTS targets)
    # Generate from final target properties, independently of which sources the
    # configure-time scan recognises. Expressions are evaluated per config.
    string(APPEND audit "${target}\n$<TARGET_PROPERTY:${target},SOURCE_DIR>\n$<JOIN:$<TARGET_GENEX_EVAL:${target},$<TARGET_PROPERTY:${target},SOURCES>>,\n>\n---dependencies---\n$<JOIN:$<TARGET_PROPERTY:${target},MANUALLY_ADDED_DEPENDENCIES>,\n>\n---end---\n")
    get_target_property(source_directory "${target}" SOURCE_DIR)
    get_target_property(sources "${target}" SOURCES)
    foreach(source IN LISTS sources)
      # Configure-time scanning covers literal sources. The CTest graph check
      # also checks evaluated generator-expression sources, which must acquire
      # the dependency explicitly if they cannot be resolved here.
      if(source MATCHES "\\$<")
        continue()
      endif()
      get_filename_component(source "${source}" ABSOLUTE BASE_DIR "${source_directory}")
      if(EXISTS "${source}" AND NOT IS_DIRECTORY "${source}")
        file(STRINGS "${source}" identity_include
          REGEX "^[ \t]*#[ \t]*include[ \t]*[<\"]arc_build_identity\\.h[>\"]")
        if(identity_include)
          add_dependencies("${target}" arc_build_identity)
          break()
        endif()
      endif()
    endforeach()
  endforeach()
  if(BUILD_TESTING)
    file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/generated/identity-targets-$<CONFIG>.txt"
      CONTENT "${audit}")
  endif()
endfunction()
