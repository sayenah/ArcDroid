# SPDX-License-Identifier: GPL-2.0-only
include_guard(GLOBAL)
if(NOT BUILD_TESTING OR (CMAKE_CROSSCOMPILING AND NOT WIN32) OR
    NOT TARGET rpc_runner OR NOT TARGET arcdroid_rpcplan_libretro)
  return()
endif()
set(RPC_LOCKSTEP_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/rpc-lockstep")
set(RPC_LOCKSTEP_PY "${CMAKE_CURRENT_SOURCE_DIR}/tests")
add_custom_command(OUTPUT "${RPC_LOCKSTEP_DIR}/manifest.json"
  COMMAND "${Python3_EXECUTABLE}" -B "${RPC_LOCKSTEP_PY}/rpc_lockstep_fuzz.py" "${RPC_LOCKSTEP_DIR}"
  DEPENDS tests/rpc_lockstep_fuzz.py tests/rpc_lockstep_rom.py tests/rpc_lockstep_expect.py
    tests/rpc_lockstep_schedule.py tests/rpc_lockstep_seeds.txt
    tests/rpc_timing_rom.py tests/rpc_plan_matrix_rom.py tests/rpc_video_rom.py
  VERBATIM)
add_custom_target(rpc-lockstep-fixtures ALL DEPENDS "${RPC_LOCKSTEP_DIR}/manifest.json")
add_test(NAME rpc-lockstep-compare COMMAND "${Python3_EXECUTABLE}" -B
  "${RPC_LOCKSTEP_PY}/rpc_lockstep_compare_test.py")
add_test(NAME rpc-lockstep-compare-pipes COMMAND "${Python3_EXECUTABLE}" -B
  "${RPC_LOCKSTEP_PY}/rpc_lockstep_compare_test.py" --stress)
add_test(NAME rpc-lockstep-compare-mutants COMMAND "${Python3_EXECUTABLE}" -B
  "${RPC_LOCKSTEP_PY}/rpc_lockstep_compare_test.py" --mutants)
add_test(NAME rpc-lockstep-schedule COMMAND "${Python3_EXECUTABLE}" -B
  "${RPC_LOCKSTEP_PY}/rpc_lockstep_schedule_test.py")
add_test(NAME rpc-lockstep-determinism COMMAND "${Python3_EXECUTABLE}" -B
  "${RPC_LOCKSTEP_PY}/rpc_lockstep_determinism_test.py")
set_tests_properties(rpc-lockstep-compare PROPERTIES TIMEOUT 120)
set_tests_properties(rpc-lockstep-compare-pipes PROPERTIES TIMEOUT 180)
set_tests_properties(rpc-lockstep-compare-mutants PROPERTIES TIMEOUT 600 RUN_SERIAL TRUE)
set_tests_properties(rpc-lockstep-schedule PROPERTIES TIMEOUT 60)
set_tests_properties(rpc-lockstep-determinism PROPERTIES TIMEOUT 60)
add_executable(rpc_lockstep_interval tests/rpc_lockstep_interval.c)
target_include_directories(rpc_lockstep_interval PRIVATE engine engine/rpc engine/rpcemu/src libretro/include core)
target_compile_definitions(rpc_lockstep_interval PRIVATE ARCDROID_TEST_FAULTS=1 RETRO_IMPORT_SYMBOLS=1)
target_link_libraries(rpc_lockstep_interval PRIVATE ${CMAKE_DL_LIBS})
if(WIN32)
  target_link_options(rpc_lockstep_interval PRIVATE -static-libgcc)
endif()
if(ARCDROID_SANITIZERS)
  target_link_libraries(rpc_lockstep_interval PRIVATE arcdroid_sanitizer_compile arcdroid_sanitizer_link)
endif()
add_test(NAME rpc-lockstep-timing COMMAND "${Python3_EXECUTABLE}" -B
  "${RPC_LOCKSTEP_PY}/rpc_lockstep_timing_test.py" $<TARGET_FILE:rpc_lockstep_interval>
  $<TARGET_FILE:arcdroid_rpcplan_libretro>)
set_tests_properties(rpc-lockstep-timing PROPERTIES TIMEOUT 120)
add_executable(rpc_lockstep_boundary tests/rpc_lockstep_boundary.c)
target_include_directories(rpc_lockstep_boundary PRIVATE engine engine/rpc engine/rpcemu/src libretro/include core)
target_compile_definitions(rpc_lockstep_boundary PRIVATE ARCDROID_TEST_FAULTS=1 RETRO_IMPORT_SYMBOLS=1)
target_link_libraries(rpc_lockstep_boundary PRIVATE ${CMAKE_DL_LIBS})
if(WIN32)
  target_link_options(rpc_lockstep_boundary PRIVATE -static-libgcc)
endif()
if(ARCDROID_SANITIZERS)
  target_link_libraries(rpc_lockstep_boundary PRIVATE arcdroid_sanitizer_compile arcdroid_sanitizer_link)
endif()
add_test(NAME rpc-lockstep-boundaries COMMAND "${Python3_EXECUTABLE}" -B
  "${RPC_LOCKSTEP_PY}/rpc_lockstep_boundary_test.py" $<TARGET_FILE:rpc_lockstep_boundary>
  $<TARGET_FILE:arcdroid_rpcplan_libretro>)
set_tests_properties(rpc-lockstep-boundaries PROPERTIES TIMEOUT 120)
if(CMAKE_GENERATOR STREQUAL "Ninja" AND NOT ARCDROID_SANITIZERS)
  add_test(NAME rpc-lockstep-sensitivity-variants COMMAND "${Python3_EXECUTABLE}" -B
    "${RPC_LOCKSTEP_PY}/rpc_lockstep_mutants.py" "${CMAKE_CURRENT_BINARY_DIR}" $<TARGET_FILE:rpc_runner>
    $<TARGET_FILE:arcdroid_rpcplan_libretro> "${RPC_LOCKSTEP_DIR}"
    --matrix "${CMAKE_CURRENT_BINARY_DIR}/rpc-lockstep-sensitivity.json")
  set_tests_properties(rpc-lockstep-sensitivity-variants PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
endif()
foreach(model RANGE 0 3)
  add_test(NAME rpc-lockstep-corpus-${model} COMMAND "${Python3_EXECUTABLE}" -B
    "${RPC_LOCKSTEP_PY}/rpc_lockstep_corpus_test.py" $<TARGET_FILE:rpc_runner>
    $<TARGET_FILE:arcdroid_rpcplan_libretro> "${RPC_LOCKSTEP_DIR}" --model ${model})
  set_tests_properties(rpc-lockstep-corpus-${model} PROPERTIES TIMEOUT 60)
endforeach()
add_test(NAME rpc-lockstep-generator-mutants COMMAND "${Python3_EXECUTABLE}" -B
  "${RPC_LOCKSTEP_PY}/rpc_lockstep_corpus_test.py" $<TARGET_FILE:rpc_runner>
  $<TARGET_FILE:arcdroid_rpcplan_libretro> "${RPC_LOCKSTEP_DIR}" --model 2 --quick --generator-mutants)
set_tests_properties(rpc-lockstep-generator-mutants PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
# Run D R-1b: inventory classifies guest state, not sanitizer instrumentation
# (Clang registry/descriptors/assembler metadata; GCC init/term markers/labels).
# Coverage remains Linux GCC ELF in CI host/host-arm64, MinGW COFF in windows,
# and Mach-O locally. Keep ODR alias controls in the inventory tool.
if(CMAKE_NM AND NOT ARCDROID_SANITIZERS)
  set(rpc_lockstep_nm_args --nm "${CMAKE_NM}")
  if(APPLE)
    list(APPEND rpc_lockstep_nm_args --apple)
  endif()
  add_test(NAME rpc-lockstep-inventory COMMAND "${Python3_EXECUTABLE}" -B
    "${RPC_LOCKSTEP_PY}/rpc_lockstep_inventory.py" "${CMAKE_CURRENT_BINARY_DIR}" ${rpc_lockstep_nm_args}
    --probe-compiler "${CMAKE_C_COMPILER}"
    --write-inventory "${CMAKE_CURRENT_BINARY_DIR}/rpc-lockstep-inventory.json")
  set_tests_properties(rpc-lockstep-inventory PROPERTIES TIMEOUT 60)
endif()
if(ARCDROID_RPC_PROFILE AND TARGET rpc-profile-tests)
  add_dependencies(rpc-profile-tests rpc-lockstep-fixtures)
  add_test(NAME rpc-profile-lockstep-coverage COMMAND "${Python3_EXECUTABLE}" -B
    "${RPC_LOCKSTEP_PY}/rpc_lockstep_corpus_test.py" $<TARGET_FILE:rpc_runner>
    $<TARGET_FILE:arcdroid_rpcplan_libretro> "${RPC_LOCKSTEP_DIR}" --profile --quick)
  set_tests_properties(rpc-profile-lockstep-coverage PROPERTIES TIMEOUT 60)
endif()

# W4: Release corpus ~13 s; Debug sanitizer corpus worst 176.35 s.
# All limits are whole minutes. Sub-six-second tests use the one-minute floor.
if(ARCDROID_SANITIZERS)
  foreach(model RANGE 0 3)
    set_tests_properties(rpc-lockstep-corpus-${model} PROPERTIES TIMEOUT 360)
  endforeach()
  set_tests_properties(rpc-lockstep-generator-mutants PROPERTIES TIMEOUT 120)
endif()
