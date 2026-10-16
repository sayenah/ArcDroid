# SPDX-License-Identifier: GPL-2.0-only
set(ARCDROID_SOURCE_REVISION "${ARC_IDENTITY_REVISION}")
# .git can be a worktree file. Do not discover a parent checkout for archives.
if(EXISTS "${ARC_IDENTITY_SOURCE}/.git")
  find_program(ARC_IDENTITY_GIT git REQUIRED)
  execute_process(COMMAND "${ARC_IDENTITY_GIT}" rev-parse --short=12 HEAD
    WORKING_DIRECTORY "${ARC_IDENTITY_SOURCE}" RESULT_VARIABLE revision_result
    OUTPUT_VARIABLE ARCDROID_SOURCE_REVISION OUTPUT_STRIP_TRAILING_WHITESPACE)
  execute_process(COMMAND "${ARC_IDENTITY_GIT}" status --porcelain --untracked-files=normal
    WORKING_DIRECTORY "${ARC_IDENTITY_SOURCE}" RESULT_VARIABLE status_result
    OUTPUT_VARIABLE dirty OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT revision_result EQUAL 0 OR NOT status_result EQUAL 0)
    message(FATAL_ERROR "Cannot obtain build source identity")
  endif()
  if(dirty)
    string(APPEND ARCDROID_SOURCE_REVISION "-dirty")
  endif()
endif()
if(NOT ARCDROID_SOURCE_REVISION)
  set(ARCDROID_SOURCE_REVISION "unknown")
endif()
if(NOT ARCDROID_SOURCE_REVISION MATCHES "^[A-Za-z0-9_.+-]+$")
  message(FATAL_ERROR "Invalid archive source revision")
endif()
configure_file("${ARC_IDENTITY_TEMPLATE}" "${ARC_IDENTITY_OUTPUT}" @ONLY)
