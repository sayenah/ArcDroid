# SPDX-License-Identifier: GPL-2.0-only
include_guard(GLOBAL)
if(NOT BUILD_TESTING OR (CMAKE_CROSSCOMPILING AND NOT WIN32) OR
    NOT TARGET rpc_runner OR NOT TARGET arcdroid_rpcplan_libretro)
  return()
endif()
set(RPC_LOCKSTEP_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/rpc-lockstep")
set(RPC_LOCKSTEP_PY "${CMAKE_CURRENT_SOURCE_DIR}/tests")
if(TARGET arcdroid_rpclockstep_libretro)
  add_executable(rpc_lockstep_emitter tests/rpc_lockstep_emitter.c)
  target_include_directories(rpc_lockstep_emitter PRIVATE engine engine/rpc engine/rpcemu/src libretro/include core)
  target_compile_definitions(rpc_lockstep_emitter PRIVATE ARCDROID_TEST_FAULTS=1 RETRO_IMPORT_SYMBOLS=1)
  target_link_libraries(rpc_lockstep_emitter PRIVATE arcdroid_core ${CMAKE_DL_LIBS})
  if(WIN32)
    target_link_options(rpc_lockstep_emitter PRIVATE -static-libgcc)
  endif()
  if(ARCDROID_SANITIZERS)
    target_link_libraries(rpc_lockstep_emitter PRIVATE arcdroid_sanitizer_compile arcdroid_sanitizer_link)
  endif()
  add_executable(rpc_lockstep_fields tests/rpc_lockstep_fields.c)
  target_include_directories(rpc_lockstep_fields PRIVATE core libretro/include engine/rpc "${RPC_LOCKSTEP_TABLE_DIR}")
  target_link_libraries(rpc_lockstep_fields PRIVATE arcdroid_core)
  add_dependencies(rpc_lockstep_fields rpc-lockstep-tables)
  if(WIN32)
    target_link_options(rpc_lockstep_fields PRIVATE -static-libgcc)
  endif()
  if(ARCDROID_SANITIZERS)
    target_link_libraries(rpc_lockstep_fields PRIVATE arcdroid_sanitizer_compile arcdroid_sanitizer_link)
  endif()
  add_test(NAME rpc-lockstep-v1 COMMAND "${Python3_EXECUTABLE}" -B "${RPC_LOCKSTEP_PY}/rpc_lockstep_v1_test.py")
  set_tests_properties(rpc-lockstep-v1 PROPERTIES TIMEOUT 120)
  add_test(NAME rpc-lockstep-driver-inputs COMMAND "${Python3_EXECUTABLE}" -B "${RPC_LOCKSTEP_PY}/rpc_lockstep_driver_test.py")
  set_tests_properties(rpc-lockstep-driver-inputs PROPERTIES TIMEOUT 60)
  set(RPC_LOCKSTEP_REFUSING_CORES $<TARGET_FILE:arcdroid_libretro_test>
    $<TARGET_FILE:arcdroid_rpcplan_libretro> $<TARGET_FILE:arcdroid_rpcmeasure_libretro>)
  if(TARGET arcdroid_session_census_libretro)
    list(APPEND RPC_LOCKSTEP_REFUSING_CORES $<TARGET_FILE:arcdroid_session_census_libretro>)
  endif()
  add_test(NAME rpc-lockstep-api-refusal COMMAND "${Python3_EXECUTABLE}" -B
    "${RPC_LOCKSTEP_PY}/rpc_lockstep_api_test.py" $<TARGET_FILE:rpc_lockstep_emitter> $<TARGET_FILE:rpc_runner>
    $<TARGET_FILE:arcdroid_libretro> ${RPC_LOCKSTEP_REFUSING_CORES})
  set_tests_properties(rpc-lockstep-api-refusal PROPERTIES TIMEOUT 120)
  add_test(NAME rpc-lockstep-generated-fields COMMAND "${Python3_EXECUTABLE}" -B
    "${RPC_LOCKSTEP_PY}/rpc_lockstep_fields_test.py" $<TARGET_FILE:rpc_lockstep_fields>)
  set_tests_properties(rpc-lockstep-generated-fields PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
  find_program(RPC_LOCKSTEP_READOBJ NAMES llvm-readobj llvm-readobj-21 llvm-readobj-20 llvm-readobj-19 llvm-readobj-18
    HINTS /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin REQUIRED)
  add_test(NAME rpc-lockstep-member-layout COMMAND "${Python3_EXECUTABLE}" -B
    "${RPC_LOCKSTEP_PY}/rpc_lockstep_layout_test.py" "${CMAKE_CURRENT_BINARY_DIR}" "${RPC_LOCKSTEP_READOBJ}")
  set_tests_properties(rpc-lockstep-member-layout PROPERTIES TIMEOUT 240 RUN_SERIAL TRUE)
  add_test(NAME rpc-lockstep-codegen-guards COMMAND "${Python3_EXECUTABLE}" -B
    "${RPC_LOCKSTEP_PY}/rpc_lockstep_codegen_test.py" "${CMAKE_C_COMPILER}" "${RPC_LOCKSTEP_TABLE_DIR}")
  set_tests_properties(rpc-lockstep-codegen-guards PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
  if(ARCDROID_RPC_PROFILE)
    add_test(NAME rpc-lockstep-profile-counts COMMAND "${Python3_EXECUTABLE}" -B
      "${RPC_LOCKSTEP_PY}/rpc_lockstep_profile_test.py" $<TARGET_FILE:rpc_runner>
      $<TARGET_FILE:arcdroid_rpcplan_libretro> $<TARGET_FILE:arcdroid_rpclockstep_libretro>
      "${RPC_LOCKSTEP_DIR}")
    set_tests_properties(rpc-lockstep-profile-counts PROPERTIES TIMEOUT 180)
    foreach(model RANGE 0 3)
      add_test(NAME rpc-lockstep-site-counts-${model} COMMAND "${Python3_EXECUTABLE}" -B
        "${RPC_LOCKSTEP_PY}/rpc_lockstep_site_counts_test.py" $<TARGET_FILE:rpc_runner>
        $<TARGET_FILE:arcdroid_rpcplan_libretro> $<TARGET_FILE:arcdroid_rpclockstep_libretro>
        "${RPC_LOCKSTEP_DIR}" --model ${model})
      set_tests_properties(rpc-lockstep-site-counts-${model} PROPERTIES TIMEOUT 1200)
    endforeach()
  endif()
  if(NOT CMAKE_CROSSCOMPILING)
    add_test(NAME rpc-lockstep-projection-arithmetic COMMAND "${Python3_EXECUTABLE}" -B
      "${RPC_LOCKSTEP_PY}/rpc_lockstep_arithmetic_test.py" "${CMAKE_C_COMPILER}")
    set_tests_properties(rpc-lockstep-projection-arithmetic PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
  endif()
  if(CMAKE_GENERATOR STREQUAL "Ninja" AND NOT ARCDROID_SANITIZERS)
    add_test(NAME rpc-lockstep-emitter-mutants COMMAND "${Python3_EXECUTABLE}" -B
      "${RPC_LOCKSTEP_PY}/rpc_lockstep_emitter_mutants.py" "${CMAKE_CURRENT_BINARY_DIR}"
      $<TARGET_FILE:rpc_lockstep_emitter> $<TARGET_FILE:arcdroid_rpclockstep_libretro>
      "${RPC_LOCKSTEP_DIR}" --matrix "${CMAKE_CURRENT_BINARY_DIR}/rpc-lockstep-emitter-matrix.json")
    set_tests_properties(rpc-lockstep-emitter-mutants PROPERTIES TIMEOUT 600 RUN_SERIAL TRUE)
  endif()
  if(CMAKE_GENERATOR STREQUAL "Ninja")
    add_test(NAME rpc-lockstep-char-encoding COMMAND "${Python3_EXECUTABLE}" -B
      "${RPC_LOCKSTEP_PY}/rpc_lockstep_char_test.py" "${CMAKE_CURRENT_BINARY_DIR}"
      $<TARGET_FILE:rpc_lockstep_emitter> $<TARGET_FILE:arcdroid_rpclockstep_libretro>)
    set_tests_properties(rpc-lockstep-char-encoding PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)
  endif()
  foreach(section IN ITEMS points projection fatal audit observers lifecycle sink encoding media allocations reach)
    add_test(NAME rpc-lockstep-emitter-${section} COMMAND "${Python3_EXECUTABLE}" -B
      "${RPC_LOCKSTEP_PY}/rpc_lockstep_emitter_test.py" $<TARGET_FILE:rpc_lockstep_emitter>
      $<TARGET_FILE:arcdroid_rpclockstep_libretro> "${RPC_LOCKSTEP_DIR}" --section ${section})
    set_tests_properties(rpc-lockstep-emitter-${section} PROPERTIES TIMEOUT 180)
  endforeach()
  set_tests_properties(rpc-lockstep-emitter-reach PROPERTIES TIMEOUT 600)
  # Whole authored programs, bounded from the separate no-emitter oracle.
  foreach(model RANGE 0 3)
    foreach(row IN ITEMS g-1 g-2 g-3 g-4 g-6 g-8 g-9 g-10-e11 g-14 va-key ac-m2 ac-h2 fatal-mode prefetch-fiq walk-mmio fuzz-0000000000000001)
      add_test(NAME rpc-lockstep-consistency-${row}-${model} COMMAND "${Python3_EXECUTABLE}" -B
        "${RPC_LOCKSTEP_PY}/rpc_lockstep_consistency_test.py" $<TARGET_FILE:rpc_lockstep_emitter>
        $<TARGET_FILE:arcdroid_rpcplan_libretro> $<TARGET_FILE:arcdroid_rpclockstep_libretro>
        "${RPC_LOCKSTEP_DIR}" --model ${model} --only ${row}-${model} --audit)
      set_tests_properties(rpc-lockstep-consistency-${row}-${model} PROPERTIES TIMEOUT 1200)
    endforeach()
  endforeach()
  # Keep each existing golden as a separately bounded CTest item. No committed
  # vector is regenerated; runner checks additionally compare complete stdout.
  set(RPC_LOCKSTEP_GOLDENS rpc-cpu-trace rpc-cmos-trace rpc-plan-devices)
  foreach(model RANGE 0 3)
    list(APPEND RPC_LOCKSTEP_GOLDENS rpc-video-trace-${model} rpc-video-paths-${model}
      rpc-d2-guest-${model} rpc-plan-matrix-${model} rpc-plan-av-${model})
  endforeach()
  list(APPEND RPC_LOCKSTEP_GOLDENS rpc-factory-capture-2 rpc-factory-capture-3)
  foreach(density IN ITEMS baseline every-instruction full-audit)
    foreach(golden IN LISTS RPC_LOCKSTEP_GOLDENS)
      add_test(NAME rpc-lockstep-golden-${density}-${golden} COMMAND "${Python3_EXECUTABLE}" -B
        "${RPC_LOCKSTEP_PY}/rpc_lockstep_goldens_test.py" "${CMAKE_CURRENT_BINARY_DIR}"
        $<TARGET_FILE:arcdroid_rpcplan_libretro> $<TARGET_FILE:arcdroid_rpclockstep_libretro>
        ${density} --only ${golden})
      set_tests_properties(rpc-lockstep-golden-${density}-${golden} PROPERTIES TIMEOUT 6060)
    endforeach()
  endforeach()
endif()
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
set_tests_properties(rpc-lockstep-compare PROPERTIES TIMEOUT 180)
set_tests_properties(rpc-lockstep-compare-pipes PROPERTIES TIMEOUT 240)
set_tests_properties(rpc-lockstep-compare-mutants PROPERTIES TIMEOUT 1260 RUN_SERIAL TRUE)
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
  set_tests_properties(rpc-lockstep-sensitivity-variants PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)
endif()
foreach(model RANGE 0 3)
  add_test(NAME rpc-lockstep-corpus-${model} COMMAND "${Python3_EXECUTABLE}" -B
    "${RPC_LOCKSTEP_PY}/rpc_lockstep_corpus_test.py" $<TARGET_FILE:rpc_runner>
    $<TARGET_FILE:arcdroid_rpcplan_libretro> "${RPC_LOCKSTEP_DIR}" --model ${model})
  set_tests_properties(rpc-lockstep-corpus-${model} PROPERTIES TIMEOUT 120)
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
  set_tests_properties(rpc-profile-lockstep-coverage PROPERTIES TIMEOUT 180)
endif()

# W4 / Run F, CI 38995124335: compare 60.96 s, pipes 73.31 s,
# Windows corpus up to 37 s, sanitizer corpus up to 82 s, generator 26.12 s,
# timing 21.98 s and boundaries 17.62 s. All limits are whole minutes and
# at least twice these observations (also pipes 116.46 s and sanitizer
# profile coverage 171.65 s on the Run F host).
# Retain the larger sanitizer allowances
# (local Debug corpus worst 176.35 s). A timeout is a censored observation:
# the old mutant workload exceeded 600 s and Windows sensitivity exceeded
# 60 s. Allow the next whole minute above twice those limits (1260/180 s),
# pending completed CI timings. Sanitizer profile coverage also exceeded
# 60 s in CI; its completed local observation above requires 360 s.
if(ARCDROID_SANITIZERS)
  foreach(model RANGE 0 3)
    set_tests_properties(rpc-lockstep-corpus-${model} PROPERTIES TIMEOUT 360)
  endforeach()
  set_tests_properties(rpc-lockstep-generator-mutants PROPERTIES TIMEOUT 120)
  if(ARCDROID_RPC_PROFILE AND TARGET rpc-profile-tests)
    set_tests_properties(rpc-profile-lockstep-coverage PROPERTIES TIMEOUT 360)
  endif()
endif()
