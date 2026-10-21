# SPDX-License-Identifier: GPL-2.0-only
# PR labels leave the unfiltered push and nightly selections unchanged.
if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING AND NOT WIN32
    AND CMAKE_GENERATOR STREQUAL "Ninja"
    AND (ARCDROID_RPC_LOCKSTEP OR ARCDROID_SANITIZERS))
  if(ARCDROID_SANITIZERS)
    set(rpc_pr_suite sanitizers)
    set(rpc_pr_prefix rpc-sanitizer-pr-)
    set(rpc_pr_count 3)
  else()
    set(rpc_pr_suite host)
    set(rpc_pr_prefix rpc-host-pr-)
    set(rpc_pr_count 4)
  endif()
  add_test(NAME rpc-host-pr-shards COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/check-host-pr-shards.py"
    --build "${CMAKE_CURRENT_BINARY_DIR}" --suite ${rpc_pr_suite} --self-test)
  set_tests_properties(rpc-host-pr-shards PROPERTIES TIMEOUT 60)
  set(rpc_host_l1_variants default 0 1 2 3 4 poison poison3 poison4)
  get_property(rpc_host_tests DIRECTORY PROPERTY TESTS)
  foreach(test IN LISTS rpc_host_tests)
    get_test_property(${test} LABELS labels)
    if(NOT ARCDROID_SANITIZERS AND "rpc-lockstep-emitter-ci" IN_LIST labels)
      continue()
    endif()
    if(test MATCHES "^rpc-l1-corpus-(default|0|1|2|3|4|poison|poison3|poison4)$")
      list(FIND rpc_host_l1_variants "${CMAKE_MATCH_1}" index)
      math(EXPR shard "${index} % ${rpc_pr_count}")
    elseif(NOT ARCDROID_SANITIZERS AND
        (test STREQUAL "rpc-orchestration-mutants" OR test STREQUAL "rpc-session-census-leak-mutants"))
      # Measured serial work (239.380 s / 133.762 s) goes to the light runner.
      # This changes only placement, retaining their RUN_SERIAL properties.
      set(shard 2)
    else()
      # Stable placement covers new tests. Seven hex digits fit CMake's signed
      # arithmetic; no existing serial, dependency or fixture property changes.
      string(SHA256 digest "${test}")
      string(SUBSTRING "${digest}" 0 7 prefix)
      math(EXPR shard "0x${prefix} % ${rpc_pr_count}")
    endif()
    set_property(TEST ${test} APPEND PROPERTY LABELS ${rpc_pr_prefix}${shard})
  endforeach()
endif()
