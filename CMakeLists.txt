# SPDX-License-Identifier: GPL-2.0-only
cmake_minimum_required(VERSION 3.20)
project(ArcDroid VERSION 0.1.0 LANGUAGES C)

include(CTest)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

option(ARCDROID_RPC_CROSSCHECK "Build test-only RPC canonical field cross-check reporting" OFF)
option(ARCDROID_RPC_PROFILE "Build test-only RPC interpreter profiling" OFF)
option(ARCDROID_SANITIZERS "Enable host AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(ARCDROID_SANITIZERS)
  if(CMAKE_CROSSCOMPILING OR NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    message(FATAL_ERROR "ARCDROID_SANITIZERS requires a native GCC or Clang build")
  endif()
  add_library(arcdroid_sanitizer_compile INTERFACE)
  target_compile_options(arcdroid_sanitizer_compile INTERFACE
    -fsanitize=address,undefined -fno-omit-frame-pointer)
  add_library(arcdroid_sanitizer_link INTERFACE)
  target_link_options(arcdroid_sanitizer_link INTERFACE -fsanitize=address,undefined)
endif()

foreach(cmos_name IN ITEMS arthur riscos2 riscos3_old riscos3_new)
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/engine/arculator/cmos/${cmos_name}/cmos.bin"
       cmos_${cmos_name}_hex HEX LIMIT 256)
  string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," cmos_${cmos_name}_bytes
         "${cmos_${cmos_name}_hex}")
endforeach()
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file(core/cmos_defaults_data.h.in generated/cmos_defaults_data.h @ONLY)
set(ARCDROID_SOURCE_REVISION "" CACHE STRING "Exact source revision for git-archive builds")
if(NOT ARCDROID_SOURCE_REVISION AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
  execute_process(COMMAND git rev-parse --short=12 HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE ARCDROID_SOURCE_REVISION OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif()
if(NOT ARCDROID_SOURCE_REVISION)
  set(ARCDROID_SOURCE_REVISION "unknown")
endif()
configure_file(libretro/arc_build_identity.h.in generated/arc_build_identity.h @ONLY)
set_source_files_properties(libretro/arcdroid_libretro.c PROPERTIES COMPILE_DEFINITIONS
  "ARC_BUILD_TARGET=\"$<TARGET_PROPERTY:NAME>\";ARC_BUILD_CONFIG=\"$<IF:$<BOOL:$<CONFIG>>,$<CONFIG>,unspecified>\";ARC_BUILD_SUBSTITUTIONS=\"$<TARGET_PROPERTY:ARCDROID_BUILD_SUBSTITUTIONS>\"")


add_library(arcdroid_zlib STATIC
  third_party/zlib/adler32.c
  third_party/zlib/crc32.c
  third_party/zlib/inffast.c
  third_party/zlib/inflate.c
  third_party/zlib/inftrees.c
  third_party/zlib/zutil.c
)
target_include_directories(arcdroid_zlib PUBLIC third_party/zlib)
if(ARCDROID_SANITIZERS)
  target_link_libraries(arcdroid_zlib PRIVATE arcdroid_sanitizer_compile)
endif()
target_compile_definitions(arcdroid_zlib PRIVATE Z_SOLO)
target_compile_options(arcdroid_zlib PRIVATE
  $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-fvisibility=hidden>)
set_target_properties(arcdroid_zlib PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(ARCDROID_CORE_SOURCES
  core/adffs.c
  core/install_disc.c
  core/rpc_boot_filecore.c
  core/hard_disc_scope.c
  core/hd_program.c
  core/zip.c
  core/zip_content.c
  core/machine.c
  core/rpc_selection.c
  core/hash.c
  core/input.c
  core/input_mapping.c
  core/osk.c
  core/launch.c
  core/launch_discovery.c
  core/jfd_layout.c
  core/jfd_file_data.c
  core/launch_disc.c
  core/launch_chooser.c
  core/boot_observation.c
  core/folder_set.c
  core/playlist.c
  core/jfd.c
  core/persistence.c
  core/cmos_defaults.c
)
add_library(arcdroid_core STATIC ${ARCDROID_CORE_SOURCES} core/adffs_profiles.c core/firmware_table.c)
target_include_directories(arcdroid_core PUBLIC core libretro/include "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(arcdroid_core PRIVATE arcdroid_zlib)
if(ARCDROID_SANITIZERS)
  target_link_libraries(arcdroid_core PRIVATE arcdroid_sanitizer_compile)
endif()
target_compile_options(arcdroid_core PRIVATE
  $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic;-Werror=implicit-function-declaration>
)
set_target_properties(arcdroid_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(ARCDROID_ARCULATOR_SOURCES
  engine/arculator_backend.c
  engine/arcdroid_stubs.c
  engine/arculator/src/82c711.c
  engine/arculator/src/ide.c
  engine/arculator/src/82c711_fdc.c
  engine/arculator/src/arm.c
  engine/arculator/src/cmos.c
  engine/arculator/src/cp15.c
  engine/arculator/src/disc.c
  engine/arculator/src/disc_adf.c
  engine/arculator/src/disc_jfd.c
  engine/arculator/src/ds2401.c
  engine/arculator/src/fpa.c
  engine/arculator/src/ioc.c
  engine/arculator/src/ioeb.c
  engine/arculator/src/joystick.c
  engine/arculator/src/keyboard.c
  engine/arculator/src/lc.c
  engine/arculator/src/mem.c
  engine/arculator/src/memc.c
  engine/arculator/src/printer.c
  engine/arculator/src/sound.c
  engine/arculator/src/timer.c
  engine/arculator/src/vidc.c
  engine/arculator/src/wd1770.c
  platform/arcdroid_input.c
  platform/arcdroid_joystick.c
  platform/arcdroid_sound.c
  platform/arcdroid_video.c
)

set(ARCDROID_RPC_SOURCES
  engine/rpc/rpc_observer.c engine/rpc/rpc_inspector.c
  engine/rpc/rpc_session.c engine/rpc/rpc_observers.c
  engine/rpc/rpc_mem_timing.c
  engine/rpc/rpc_audio.c
  engine/rpc/rpc_input.c
  engine/rpc/rpc_timing.c
  engine/rpc/rpc_scheduler.c
  engine/rpc/rpc_floppy.c
  engine/rpc/rpc_rtc.c
  engine/rpcemu/src/arm.c
  engine/rpcemu/src/arm_common.c
  engine/rpcemu/src/cp15.c
  engine/rpcemu/src/mem.c
  engine/rpcemu/src/cmos.c
  engine/rpcemu/src/iomd.c
  engine/rpcemu/src/keyboard.c
  engine/rpcemu/src/superio.c
  engine/rpcemu/src/i8042.c
  engine/rpcemu/src/vidc20.c
  engine/rpcemu/src/fdc.c
  engine/rpcemu/src/ide.c
)

set(ARCDROID_LIBRETRO_SOURCES
  engine/arcdroid_engine.c
  engine/arc_boot_watch.c
  engine/arc_launch_outcome.c
  engine/arc_error_box.c
  engine/arc_disc_request.c
  libretro/arc_io.c
  libretro/arc_lock.c
  libretro/arc_session.c
  libretro/rpc_inventory.c
  libretro/jfd_store.c
  libretro/install_store.c
  libretro/rpc_hd_store.c
  libretro/rpc_cmos_store.c
  libretro/rpc_preparation.c
  libretro/rpc_boot_zip.c
  libretro/rpc_boot.c
  libretro/arcdroid_libretro.c
)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(RPC_BOOT_ASSETS ${CMAKE_CURRENT_BINARY_DIR}/generated/rpc_boot_assets.c)
add_custom_command(OUTPUT ${RPC_BOOT_ASSETS}
  COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed-rpc-boot.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/riscpc-boot" ${RPC_BOOT_ASSETS}
  DEPENDS tools/embed-rpc-boot.py core/rpc_boot_assets.h
    assets/riscpc-boot/Multisync assets/riscpc-boot/BootEnv
    assets/riscpc-boot/BootRes assets/riscpc-boot/BootTasks
    assets/riscpc-boot/ArcTasks assets/riscpc-boot/Run assets/riscpc-boot/Boot
  VERBATIM)
target_sources(arcdroid_core PRIVATE ${RPC_BOOT_ASSETS})

set(ARCDROID_SYMBOL_CHECK_FLAGS)
if(ARCDROID_SANITIZERS)
  list(APPEND ARCDROID_SYMBOL_CHECK_FLAGS --sanitized)
endif()

function(arcdroid_add_libretro target shipping)
  set(core_target arcdroid_core)
  if(ARGC GREATER 2)
    set(core_target "${ARGV2}")
  endif()
  add_library(${target}_arculator OBJECT ${ARCDROID_ARCULATOR_SOURCES})
  target_include_directories(${target}_arculator PRIVATE
    engine engine/compat engine/arculator/src platform libretro libretro/include core)
  target_compile_definitions(${target}_arculator PRIVATE
    ARCDROID_VERSION="${PROJECT_VERSION}" RELEASE_BUILD=1)
  target_compile_options(${target}_arculator PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic;-Werror=implicit-function-declaration;-fno-common;-fno-strict-aliasing;-include;${CMAKE_CURRENT_SOURCE_DIR}/engine/arculator_hooks.h>)
  set_target_properties(${target}_arculator PROPERTIES POSITION_INDEPENDENT_CODE ON)
  # The final target may acquire test definitions after this function returns.
  target_compile_definitions(${target}_arculator PRIVATE
    $<TARGET_PROPERTY:${target},COMPILE_DEFINITIONS>)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(${target}_arculator PRIVATE arcdroid_sanitizer_compile)
  endif()
  if(shipping)
    target_compile_options(${target}_arculator PRIVATE
      $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-fvisibility=hidden>)
  endif()
  add_library(${target}_rpc OBJECT ${ARCDROID_RPC_SOURCES})
  target_include_directories(${target}_rpc PRIVATE engine/rpc engine/rpcemu/src
    engine core libretro libretro/include)
  target_compile_definitions(${target}_rpc PRIVATE RELEASE_BUILD=1
    $<TARGET_PROPERTY:${target},COMPILE_DEFINITIONS>)
  target_compile_options(${target}_rpc PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-include;${CMAKE_CURRENT_SOURCE_DIR}/engine/rpc/rpc_namespace.h;-fno-common;-fno-strict-aliasing;-Wall;-Wextra;-Werror=implicit-function-declaration>)
  set_target_properties(${target}_rpc PROPERTIES POSITION_INDEPENDENT_CODE ON)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(${target}_rpc PRIVATE arcdroid_sanitizer_compile)
  endif()
  if(shipping)
    target_compile_options(${target}_rpc PRIVATE
      $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-fvisibility=hidden>)
  endif()
  add_library(${target} SHARED ${ARCDROID_LIBRETRO_SOURCES}
    $<TARGET_OBJECTS:${target}_arculator> $<TARGET_OBJECTS:${target}_rpc>)
  set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/check-backend-symbols.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/engine/backend-symbols.json")
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tools/check-backend-symbols.py"
      --nm "${CMAKE_NM}"
      ${ARCDROID_SYMBOL_CHECK_FLAGS}
      --arculator "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/${target}_arculator.dir"
      --rpc "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/${target}_rpc.dir"
      --outside "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/${target}.dir"
      --outside "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/${core_target}.dir"
      --outside "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/arcdroid_zlib.dir"
    VERBATIM)
  target_include_directories(${target} PRIVATE
    engine platform libretro libretro/include core third_party/zlib)
  if(NOT shipping)
    target_sources(${target} PRIVATE tests/arc_test_plan.c tests/rpc_test_clock.c)
    target_include_directories(${target} PRIVATE tests)
  endif()
  target_compile_definitions(${target} PRIVATE
    ARCDROID_VERSION="${PROJECT_VERSION}"
    RELEASE_BUILD=1)
  target_compile_options(${target} PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic;-Werror=implicit-function-declaration;-fno-strict-aliasing>)
  set_target_properties(${target} PROPERTIES PREFIX "")
  target_link_libraries(${target} PRIVATE ${core_target})
  if(ARCDROID_SANITIZERS)
    target_link_libraries(${target} PRIVATE arcdroid_sanitizer_link)
  endif()

  if(shipping)
    target_compile_options(${target} PRIVATE
      $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-fvisibility=hidden>)
    if(NOT WIN32 AND NOT APPLE)
      set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/libretro/link.T")
    endif()
  endif()

  if(ANDROID)
    if(shipping)
      set_target_properties(${target} PROPERTIES OUTPUT_NAME arcdroid_libretro_android)
      target_link_options(${target} PRIVATE
        "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/libretro/link.T"
        "-Wl,-z,max-page-size=16384")
    endif()
    target_link_libraries(${target} PRIVATE m dl)
  elseif(WIN32)
    if(shipping)
      set_target_properties(${target} PROPERTIES OUTPUT_NAME arcdroid_libretro)
      target_sources(${target} PRIVATE libretro/arcdroid.def)
    else()
      # Internal probes loaded by the RPC timing/CMOS harnesses are not
      # dllexport functions. Export only these from non-installed test DLLs.
      target_sources(${target} PRIVATE tests/rpc_test_exports.def)
    endif()
    target_link_options(${target} PRIVATE -static-libgcc)
  elseif(APPLE)
    if(shipping)
      target_link_options(${target} PRIVATE
        "-Wl,-exported_symbols_list,${CMAKE_CURRENT_SOURCE_DIR}/libretro/arcdroid.exports")
    endif()
  else()
    if(shipping)
      target_link_options(${target} PRIVATE
        "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/libretro/link.T"
        "-Wl,-z,defs")
    endif()
    target_link_libraries(${target} PRIVATE m)
  endif()
endfunction()

# The installed/packaged core never contains test fault-injection hooks.
arcdroid_add_libretro(arcdroid_libretro TRUE)

# Register source/output pairs with the metadata guard, including installed
# shipping metadata and non-installed test metadata.
function(arcdroid_configure_core_info source output)
  configure_file("${source}" "${output}" COPYONLY)
  set_property(GLOBAL APPEND PROPERTY ARCDROID_CORE_INFO_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/${source}" "${CMAKE_CURRENT_BINARY_DIR}/${output}")
endfunction()
arcdroid_configure_core_info(arcdroid_libretro.info arcdroid_libretro.info)

install(TARGETS arcdroid_libretro DESTINATION .)
install(FILES arcdroid_libretro.info DESTINATION .)

option(ARCDROID_ANDROID_TEST_CORE "Build the non-installed Android test core" OFF)
option(ARCDROID_RPC_RUNNER "Build the non-installed R1 plan runner" ON)
if(ARCDROID_RPC_RUNNER AND ((BUILD_TESTING AND (NOT CMAKE_CROSSCOMPILING OR WIN32)) OR
                          (ANDROID AND ARCDROID_ANDROID_TEST_CORE)))
  add_executable(hash_accel_test tests/hash_accel_test.c core/hash.c tests/rpc_test_clock.c)
  target_include_directories(hash_accel_test PRIVATE core)
  target_compile_definitions(hash_accel_test PRIVATE ARCDROID_HASH_TEST=1)
  target_compile_options(hash_accel_test PRIVATE -UNDEBUG -Wall -Wextra)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(hash_accel_test PRIVATE arcdroid_sanitizer_compile arcdroid_sanitizer_link)
  endif()
  if(WIN32)
    target_link_options(hash_accel_test PRIVATE -static-libgcc)
  endif()
  if(NOT CMAKE_CROSSCOMPILING OR WIN32)
    add_test(NAME rpc-hash-identity COMMAND hash_accel_test)
    set_tests_properties(rpc-hash-identity PROPERTIES TIMEOUT 600)
  endif()
  add_executable(rpc_runner tools/rpc_runner.c tests/rpc_test_clock.c)
  target_include_directories(rpc_runner PRIVATE engine engine/rpc core libretro libretro/include tests)
  target_compile_definitions(rpc_runner PRIVATE ARCDROID_TEST_FAULTS=1 RETRO_IMPORT_SYMBOLS=1)
  if(ARCDROID_RPC_PROFILE)
    target_compile_definitions(rpc_runner PRIVATE ARCDROID_RPC_PROFILE=1)
    target_sources(rpc_runner PRIVATE tools/rpc_profile_json.c)
  endif()
  target_compile_options(rpc_runner PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG;-Wall;-Wextra;-Werror=implicit-function-declaration>)
  target_link_libraries(rpc_runner PRIVATE ${CMAKE_DL_LIBS})
  if(UNIX AND NOT APPLE)
    target_link_libraries(rpc_runner PRIVATE m)
  endif()
  if(WIN32)
    target_link_options(rpc_runner PRIVATE -static-libgcc)
  endif()
  if(ARCDROID_SANITIZERS)
    target_link_libraries(rpc_runner PRIVATE arcdroid_sanitizer_compile arcdroid_sanitizer_link)
  endif()
  set_property(TARGET rpc_runner APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/check-backend-symbols.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/engine/backend-symbols.json")
  add_custom_command(TARGET rpc_runner POST_BUILD
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tools/check-backend-symbols.py"
      --nm "${CMAKE_NM}" ${ARCDROID_SYMBOL_CHECK_FLAGS}
      --outside "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/rpc_runner.dir" VERBATIM)
endif()


if(BUILD_TESTING AND (NOT CMAKE_CROSSCOMPILING OR WIN32 OR ANDROID))
  # The process probe is also a standalone P10b Android arm64 executable. Neither
  # executable carries plan/fault definitions or changes any store caller.
  foreach(lock_target IN ITEMS rpc_lock_test rpc_lock_process)
    add_executable(${lock_target} tests/${lock_target}.c libretro/arc_lock.c libretro/arc_io.c)
    target_include_directories(${lock_target} PRIVATE libretro libretro/include)
    target_compile_options(${lock_target} PRIVATE -UNDEBUG -Wall -Wextra -Werror=implicit-function-declaration)
    if(WIN32)
      target_link_options(${lock_target} PRIVATE -static-libgcc)
    endif()
    if(ARCDROID_SANITIZERS)
      target_link_libraries(${lock_target} PRIVATE arcdroid_sanitizer_compile arcdroid_sanitizer_link)
    endif()
  endforeach()
  if(NOT CMAKE_CROSSCOMPILING OR WIN32)
    add_test(NAME rpc-lock-unit COMMAND rpc_lock_test)
    add_test(NAME rpc-lock-process COMMAND rpc_lock_process self-test "${CMAKE_CURRENT_BINARY_DIR}/rpc-lock-save")
    add_test(NAME rpc-lock-io-guard COMMAND "${Python3_EXECUTABLE}" -B
      "${CMAKE_CURRENT_SOURCE_DIR}/tools/check-shipping-lock-io.py" --self-test)
    add_test(NAME rpc-lock-dormancy COMMAND "${Python3_EXECUTABLE}" -B
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_lock_source_guard.py" "${CMAKE_CURRENT_SOURCE_DIR}")
    set_tests_properties(rpc-lock-io-guard PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    set_tests_properties(rpc-lock-unit rpc-lock-io-guard PROPERTIES TIMEOUT 30)
    set_tests_properties(rpc-lock-process PROPERTIES TIMEOUT 45)
  endif()
  if(NOT CMAKE_CROSSCOMPILING AND NOT WIN32)
    set(lock_probe_capability_flags)
    if(ARCDROID_SANITIZERS)
      list(APPEND lock_probe_capability_flags --sanitize)
    endif()
    add_test(NAME rpc-lock-probe-capabilities COMMAND "${Python3_EXECUTABLE}" -B
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_lock_probe_capabilities.py"
      "${CMAKE_C_COMPILER}" "${CMAKE_CURRENT_SOURCE_DIR}" ${lock_probe_capability_flags})
    set_tests_properties(rpc-lock-probe-capabilities PROPERTIES TIMEOUT 90)
    foreach(native_kind RANGE 0 2)
      add_executable(rpc_lock_native_${native_kind} tests/rpc_lock_native_test.c libretro/arc_io.c)
      target_include_directories(rpc_lock_native_${native_kind} PRIVATE tests/lock-shims libretro libretro/include)
      target_compile_definitions(rpc_lock_native_${native_kind} PRIVATE ARC_LOCK_NATIVE_KIND=${native_kind})
      target_compile_options(rpc_lock_native_${native_kind} PRIVATE -UNDEBUG -Wall -Wextra)
      if(ARCDROID_SANITIZERS)
        target_link_libraries(rpc_lock_native_${native_kind} PRIVATE arcdroid_sanitizer_compile arcdroid_sanitizer_link)
      endif()
      add_test(NAME rpc-lock-native-${native_kind} COMMAND rpc_lock_native_${native_kind})
      set_tests_properties(rpc-lock-native-${native_kind} PROPERTIES TIMEOUT 15)
    endforeach()
    if(NOT ARCDROID_SANITIZERS)
      add_test(NAME rpc-lock-mutants COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_lock_mutants.py" "${CMAKE_C_COMPILER}" "${CMAKE_CURRENT_SOURCE_DIR}")
      set_tests_properties(rpc-lock-mutants PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
    endif()
  endif()
endif()

if(ANDROID AND ARCDROID_ANDROID_TEST_CORE)
  add_library(arcdroid_core_test STATIC
    ${ARCDROID_CORE_SOURCES} ${RPC_BOOT_ASSETS} tests/adffs_profiles_test.c tests/firmware_table_test.c)
  target_include_directories(arcdroid_core_test PUBLIC
    core libretro/include "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_link_libraries(arcdroid_core_test PRIVATE arcdroid_zlib)
  target_compile_definitions(arcdroid_core_test PUBLIC ARCDROID_TEST_FAULTS=1)
  set_target_properties(arcdroid_core_test PROPERTIES POSITION_INDEPENDENT_CODE ON)
  arcdroid_add_libretro(arcdroid_libretro_test FALSE arcdroid_core_test)
  target_compile_definitions(arcdroid_libretro_test PRIVATE ARCDROID_TEST_FAULTS=1)
  target_sources(arcdroid_libretro_test PRIVATE tests/hd_product_seed.c)
  set_target_properties(arcdroid_libretro_test PROPERTIES OUTPUT_NAME arcdroid_libretro_test_android)
  target_link_options(arcdroid_libretro_test PRIVATE "-Wl,-z,defs" "-Wl,-z,max-page-size=16384")
endif()

if(BUILD_TESTING AND (NOT CMAKE_CROSSCOMPILING OR WIN32))
  add_test(NAME arculator-import-guard COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/check-arculator-import.py")
  add_test(NAME arculator-import-guard-self-test COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/arculator_import_guard_test.py")
  add_test(NAME public-text-guard COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/check-public-text.py")
  add_test(NAME public-text-guard-self-test COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/public_text_guard_test.py")
  add_test(NAME license-guard COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/check-licenses.py")
  add_test(NAME license-guard-self-test COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/license_guard_test.py")
  add_test(NAME input-source-retirement-self-test COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/input_source_retirement_test.py")
  add_test(NAME rpc-import-guard COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_import_test.py"
    "${CMAKE_CURRENT_SOURCE_DIR}")
  add_test(NAME rpc-include-closure COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/check-rpcemu-import.py"
    --compiler "${CMAKE_C_COMPILER}")
  add_test(NAME rpc-symbol-guard COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_symbols_test.py"
    "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_C_COMPILER}" "${CMAKE_NM}")
  add_test(NAME rpc-frontend-boundary-guard COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_boundary_test.py"
    "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_C_COMPILER}" "${CMAKE_NM}")
  add_executable(rpc_scheduler_test tests/rpc_scheduler_test.c
    engine/rpc/rpc_scheduler.c)
  target_include_directories(rpc_scheduler_test PRIVATE engine/rpc)
  target_compile_options(rpc_scheduler_test PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG;-Wall;-Wextra;-Wpedantic>)
  add_test(NAME rpc-scheduler COMMAND rpc_scheduler_test)
  add_executable(rpc_scheduler_batch_test tests/rpc_scheduler_batch_test.c engine/rpc/rpc_scheduler.c)
  target_include_directories(rpc_scheduler_batch_test PRIVATE engine/rpc)
  target_compile_options(rpc_scheduler_batch_test PRIVATE -UNDEBUG)
  add_test(NAME rpc-scheduler-batch COMMAND rpc_scheduler_batch_test)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(rpc_scheduler_batch_test PRIVATE arcdroid_sanitizer_compile arcdroid_sanitizer_link)
  endif()
  # A bounded CPU/memory component. Unadapted devices still cannot join cores.
  add_library(arcdroid_rpc_cpu OBJECT
    engine/rpc/rpc_observer.c engine/rpc/rpc_inspector.c
    engine/rpc/rpc_mem_timing.c
    engine/rpcemu/src/arm.c engine/rpcemu/src/arm_common.c
    engine/rpcemu/src/cp15.c engine/rpcemu/src/mem.c)
  target_include_directories(arcdroid_rpc_cpu PRIVATE engine/rpc engine/rpcemu/src)
  target_compile_definitions(arcdroid_rpc_cpu PRIVATE RELEASE_BUILD=1)
  target_compile_options(arcdroid_rpc_cpu PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-include;${CMAKE_CURRENT_SOURCE_DIR}/engine/rpc/rpc_namespace.h;-fno-common;-fno-strict-aliasing;-Wall;-Wextra;-Werror=implicit-function-declaration>)
  add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/rpc_authored_rom.h"
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_cpu_rom.py"
      "${CMAKE_CURRENT_BINARY_DIR}/generated/rpc_authored_rom.h"
    DEPENDS tests/rpc_cpu_rom.py VERBATIM)
  add_executable(rpc_cpu_test tests/rpc_cpu_test.c
    "${CMAKE_CURRENT_BINARY_DIR}/generated/rpc_authored_rom.h"
    $<TARGET_OBJECTS:arcdroid_rpc_cpu>)
  target_include_directories(rpc_cpu_test PRIVATE engine/rpc engine/rpcemu/src
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_compile_options(rpc_cpu_test PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG;-Wall;-Wextra;-Werror=implicit-function-declaration>)
  add_executable(rpc_observer_test tests/rpc_observer_test.c
    engine/rpc/rpc_observer.c engine/rpc/rpc_inspector.c
    engine/rpc/rpc_mem_timing.c engine/rpc/rpc_scheduler.c
    engine/rpcemu/src/arm.c engine/rpcemu/src/arm_common.c
    engine/rpcemu/src/cp15.c engine/rpcemu/src/mem.c
    "${CMAKE_CURRENT_BINARY_DIR}/generated/rpc_authored_rom.h")
  target_include_directories(rpc_observer_test PRIVATE engine/rpc engine/rpcemu/src
    libretro/include "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_compile_definitions(rpc_observer_test PRIVATE ARCDROID_TEST_FAULTS=1 ARCDROID_OBSERVATION_TEST=1 RELEASE_BUILD=1)
  target_compile_options(rpc_observer_test PRIVATE -UNDEBUG -Wall -Wextra -fno-strict-aliasing
    -include "${CMAKE_CURRENT_SOURCE_DIR}/engine/rpc/rpc_namespace.h")
  if(ARCDROID_SANITIZERS)
    target_link_libraries(rpc_observer_test PRIVATE arcdroid_sanitizer_compile arcdroid_sanitizer_link)
  endif()
  get_target_property(endpoint_cpu_sources rpc_observer_test SOURCES)
  list(REMOVE_ITEM endpoint_cpu_sources tests/rpc_observer_test.c)
  add_executable(rpc_endpoint_cpu_test tests/rpc_endpoint_cpu_test.c engine/rpc/rpc_profile.c ${endpoint_cpu_sources})
  target_include_directories(rpc_endpoint_cpu_test PRIVATE engine/rpc engine/rpcemu/src libretro/include "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_compile_definitions(rpc_endpoint_cpu_test PRIVATE ARCDROID_TEST_FAULTS=1 ARCDROID_RPC_PROFILE=1 ARCDROID_OBSERVATION_TEST=1 RELEASE_BUILD=1)
  target_compile_options(rpc_endpoint_cpu_test PRIVATE -UNDEBUG -Wall -Wextra -fno-strict-aliasing
    -include "${CMAKE_CURRENT_SOURCE_DIR}/engine/rpc/rpc_namespace.h")
  if(ARCDROID_SANITIZERS)
    target_link_libraries(rpc_endpoint_cpu_test PRIVATE arcdroid_sanitizer_compile arcdroid_sanitizer_link)
  endif()
  add_test(NAME rpc-endpoint-cpu COMMAND rpc_endpoint_cpu_test)
  add_executable(rpc_observers_test tests/rpc_observers_test.c
    engine/rpc/rpc_observers.c engine/arc_error_box.c engine/arc_disc_request.c
    engine/arc_launch_outcome.c engine/arc_boot_watch.c
    engine/rpc/rpc_observer.c engine/rpc/rpc_inspector.c
    engine/rpc/rpc_mem_timing.c engine/rpc/rpc_scheduler.c
    engine/rpcemu/src/arm.c engine/rpcemu/src/arm_common.c
    engine/rpcemu/src/cp15.c engine/rpcemu/src/mem.c
    "${CMAKE_CURRENT_BINARY_DIR}/generated/rpc_authored_rom.h")
  target_include_directories(rpc_observers_test PRIVATE engine engine/rpc engine/rpcemu/src
    core libretro libretro/include "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_compile_definitions(rpc_observers_test PRIVATE ARCDROID_TEST_FAULTS=1 ARCDROID_OBSERVATION_TEST=1 RELEASE_BUILD=1)
  target_compile_options(rpc_observers_test PRIVATE -UNDEBUG -Wall -Wextra -fno-strict-aliasing
    -include "${CMAKE_CURRENT_SOURCE_DIR}/engine/rpc/rpc_namespace.h")
  if(ARCDROID_SANITIZERS)
    target_link_libraries(rpc_observers_test PRIVATE arcdroid_sanitizer_compile arcdroid_sanitizer_link)
  endif()
  add_test(NAME rpc-observers COMMAND rpc_observers_test)
  set_tests_properties(rpc-observers PROPERTIES TIMEOUT 60)
  set(RPC_DISC_DIFFERENTIAL_PROFILE full)
  if(ARCDROID_SANITIZERS)
    set(RPC_DISC_DIFFERENTIAL_PROFILE sanitizer)
  endif()
  add_test(NAME rpc-observers-differential COMMAND rpc_observers_test --differential "${RPC_DISC_DIFFERENTIAL_PROFILE}")
  set_tests_properties(rpc-observers-differential PROPERTIES TIMEOUT 120)
  add_test(NAME rpc-observers-recovery COMMAND rpc_observers_test --recovery-positive)
  set_tests_properties(rpc-observers-recovery PROPERTIES TIMEOUT 60)
  add_test(NAME rpc-observers-qualification COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_observer_qualification_test.py"
    $<TARGET_FILE:rpc_observers_test> "${CMAKE_CURRENT_SOURCE_DIR}/docs/riscpc-observer-qualification.md")
  set_tests_properties(rpc-observers-qualification PROPERTIES TIMEOUT 60)
  if(NOT ARCDROID_SANITIZERS AND NOT CMAKE_CROSSCOMPILING AND CMAKE_GENERATOR STREQUAL "Ninja")
    foreach(shard 0 1)
      if(shard EQUAL 0)
        set(mutant_test rpc-observers-mutants)
      else()
        set(mutant_test rpc-observers-mutants-2)
      endif()
      add_test(NAME ${mutant_test} COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_observers_mutants.py"
        "${CMAKE_CURRENT_BINARY_DIR}" $<TARGET_FILE:rpc_observers_test> --policy-shard=${shard})
      set_tests_properties(${mutant_test} PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)
    endforeach()
  endif()
  add_test(NAME rpc-observation-primitives COMMAND rpc_observer_test)
  set_tests_properties(rpc-observation-primitives PROPERTIES TIMEOUT 60)
  if(CMAKE_GENERATOR STREQUAL "Ninja" AND NOT ARCDROID_SANITIZERS)
    add_test(NAME rpc-observation-mutants COMMAND "${Python3_EXECUTABLE}" -B
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_observer_mutants.py"
      "${CMAKE_CURRENT_BINARY_DIR}" $<TARGET_FILE:rpc_observer_test>)
    set_tests_properties(rpc-observation-mutants PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)
    if(WIN32)
      # Windows CI reached 130.34 s; allow twice that, rounded up to a minute.
      set_property(TEST rpc-observation-mutants PROPERTY TIMEOUT 300)
    endif()
  endif()
  add_test(NAME rpc-cpu-mmu-memory COMMAND rpc_cpu_test)
  add_test(NAME rpc-fetch-memo-rewalk COMMAND rpc_cpu_test --fetch-memo)
  add_library(arcdroid_rpc_cmos OBJECT engine/rpcemu/src/cmos.c engine/rpc/rpc_rtc.c)
  target_include_directories(arcdroid_rpc_cmos PRIVATE engine/rpc engine/rpcemu/src)
  target_compile_options(arcdroid_rpc_cmos PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-include;${CMAKE_CURRENT_SOURCE_DIR}/engine/rpc/rpc_namespace.h;-fno-common;-Wall;-Wextra;-Werror=implicit-function-declaration>)
  add_executable(rpc_cmos_test tests/rpc_cmos_test.c $<TARGET_OBJECTS:arcdroid_rpc_cmos>)
  target_include_directories(rpc_cmos_test PRIVATE engine/rpc engine/rpcemu/src)
  target_compile_options(rpc_cmos_test PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG;-Wall;-Wextra;-Werror=implicit-function-declaration>)
  add_test(NAME rpc-cmos-rtc COMMAND rpc_cmos_test)
  add_test(NAME rpc-cpu-trace COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_trace_test.py" "$<TARGET_FILE:rpc_cpu_test>"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/rpc-cpu.trace")
  add_test(NAME rpc-cmos-trace COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_trace_test.py" "$<TARGET_FILE:rpc_cmos_test>"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/rpc-cmos.trace")
  if(ARCDROID_SANITIZERS)
    target_link_libraries(arcdroid_rpc_cpu PRIVATE arcdroid_sanitizer_compile)
    target_link_libraries(rpc_cpu_test PRIVATE arcdroid_sanitizer_compile arcdroid_sanitizer_link)
    target_link_libraries(arcdroid_rpc_cmos PRIVATE arcdroid_sanitizer_compile)
    target_link_libraries(rpc_cmos_test PRIVATE arcdroid_sanitizer_compile arcdroid_sanitizer_link)
  endif()
  add_custom_command(TARGET rpc_cpu_test POST_BUILD
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tools/check-backend-symbols.py"
      --nm "${CMAKE_NM}" --rpc "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/arcdroid_rpc_cpu.dir"
      ${ARCDROID_SYMBOL_CHECK_FLAGS} VERBATIM)
  add_custom_command(TARGET rpc_cmos_test POST_BUILD
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tools/check-backend-symbols.py"
      --nm "${CMAKE_NM}" --rpc "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/arcdroid_rpc_cmos.dir"
      ${ARCDROID_SYMBOL_CHECK_FLAGS} VERBATIM)
  add_library(arcdroid_rpc_fdc OBJECT engine/rpcemu/src/fdc.c
    engine/rpc/rpc_floppy.c engine/rpc/rpc_timing.c engine/rpc/rpc_scheduler.c)
  target_include_directories(arcdroid_rpc_fdc PRIVATE engine/rpc engine/rpcemu/src engine core libretro libretro/include)
  target_compile_definitions(arcdroid_rpc_fdc PRIVATE RELEASE_BUILD=1)
  target_compile_options(arcdroid_rpc_fdc PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-include;${CMAKE_CURRENT_SOURCE_DIR}/engine/rpc/rpc_namespace.h;-fno-common;-fno-strict-aliasing>)
  add_executable(rpc_fdc_test tests/rpc_fdc_test.c libretro/arc_io.c $<TARGET_OBJECTS:arcdroid_rpc_fdc>)
  target_link_libraries(rpc_fdc_test PRIVATE arcdroid_core)
  target_include_directories(rpc_fdc_test PRIVATE engine/rpc engine/rpcemu/src engine core libretro libretro/include)
  target_compile_definitions(rpc_fdc_test PRIVATE RELEASE_BUILD=1)
  target_compile_options(rpc_fdc_test PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG;-Wall;-Wextra;-Wpedantic>)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(arcdroid_rpc_fdc PRIVATE arcdroid_sanitizer_compile)
    target_link_libraries(rpc_fdc_test PRIVATE arcdroid_sanitizer_compile arcdroid_sanitizer_link)
    target_link_libraries(rpc_scheduler_test PRIVATE arcdroid_sanitizer_compile arcdroid_sanitizer_link)
  endif()
  add_test(NAME rpc-fdc-empty COMMAND rpc_fdc_test)
  add_test(NAME rpc-fdc-media-change-seek COMMAND rpc_fdc_test --media-change)
  add_test(NAME rpc-fdc-pending-irq COMMAND rpc_fdc_test --pending-irq)
  add_custom_command(TARGET rpc_fdc_test POST_BUILD
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tools/check-backend-symbols.py"
      --nm "${CMAKE_NM}"
      --rpc "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/arcdroid_rpc_fdc.dir"
      ${ARCDROID_SYMBOL_CHECK_FLAGS}
    VERBATIM)
  add_executable(core_tests
    tests/core_tests.c
    tests/keytable_provenance.c)
  target_link_libraries(core_tests PRIVATE arcdroid_core)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(core_tests PRIVATE arcdroid_sanitizer_link)
  endif()
  target_compile_options(core_tests PRIVATE $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG>)
  add_executable(power_on_cmos_test tests/power_on_cmos_test.c)
  target_link_libraries(power_on_cmos_test PRIVATE arcdroid_core)
  target_compile_options(power_on_cmos_test PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG;-Wall;-Wextra;-Wpedantic>)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(power_on_cmos_test PRIVATE arcdroid_sanitizer_link)
  endif()
  add_test(NAME power-on-cmos COMMAND power_on_cmos_test)
  add_executable(boot_watch_test tests/boot_watch_test.c engine/arc_boot_watch.c)
  target_include_directories(boot_watch_test PRIVATE engine)
  target_link_libraries(boot_watch_test PRIVATE arcdroid_core)
  target_compile_options(boot_watch_test PRIVATE $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG>)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(boot_watch_test PRIVATE arcdroid_sanitizer_compile arcdroid_sanitizer_link)
  endif()
  add_test(NAME boot-watch COMMAND boot_watch_test
    "${CMAKE_CURRENT_BINARY_DIR}/tests/fixtures/generated")
  add_executable(launch_outcome_test tests/launch_outcome_test.c
    engine/arc_launch_outcome.c)
  target_include_directories(launch_outcome_test PRIVATE engine)
  target_compile_options(launch_outcome_test PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG;-Wall;-Wextra;-Wpedantic>)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(launch_outcome_test PRIVATE
      arcdroid_sanitizer_compile arcdroid_sanitizer_link)
  endif()
  add_test(NAME launch-outcome COMMAND launch_outcome_test)
  add_executable(error_box_test tests/error_box_test.c engine/arc_error_box.c)
  target_include_directories(error_box_test PRIVATE engine core)
  target_compile_options(error_box_test PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG;-Wall;-Wextra;-Wpedantic>)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(error_box_test PRIVATE
      arcdroid_sanitizer_compile arcdroid_sanitizer_link)
  endif()
  add_test(NAME error-box-unit COMMAND error_box_test)
  add_executable(disc_request_test tests/disc_request_test.c
    engine/arc_disc_request.c)
  target_include_directories(disc_request_test PRIVATE engine)
  target_compile_options(disc_request_test PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG;-Wall;-Wextra;-Wpedantic>)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(disc_request_test PRIVATE
      arcdroid_sanitizer_compile arcdroid_sanitizer_link)
  endif()
  add_test(NAME disc-request-unit COMMAND disc_request_test)
  add_executable(disc_resolver_test tests/disc_resolver_test.c)
  target_link_libraries(disc_resolver_test PRIVATE arcdroid_core)
  target_compile_options(disc_resolver_test PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG;-Wall;-Wextra;-Wpedantic>)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(disc_resolver_test PRIVATE arcdroid_sanitizer_link)
  endif()
  add_test(NAME disc-resolver-unit COMMAND disc_resolver_test
    "${CMAKE_CURRENT_BINARY_DIR}/tests/fixtures/generated")
  add_executable(adffs_shipping_profile_test
    tests/adffs_shipping_profile_test.c)
  target_link_libraries(adffs_shipping_profile_test PRIVATE arcdroid_core)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(adffs_shipping_profile_test PRIVATE arcdroid_sanitizer_link)
  endif()
  target_compile_options(adffs_shipping_profile_test PRIVATE $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG>)
  add_executable(install_disc_test tests/install_disc_test.c)
  target_link_libraries(install_disc_test PRIVATE arcdroid_core)
  target_compile_options(install_disc_test PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG>)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(install_disc_test PRIVATE arcdroid_sanitizer_link)
  endif()
  add_test(NAME hard-disc-formatter COMMAND install_disc_test)
  add_executable(hard_disc_scope_test tests/hard_disc_scope_test.c)
  target_link_libraries(hard_disc_scope_test PRIVATE arcdroid_core)
  target_compile_options(hard_disc_scope_test PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG;-Wall;-Wextra;-Wpedantic>)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(hard_disc_scope_test PRIVATE arcdroid_sanitizer_link)
  endif()
  add_test(NAME hard-disc-scope COMMAND hard_disc_scope_test)
  add_executable(hd_program_test tests/hd_program_test.c)
  target_link_libraries(hd_program_test PRIVATE arcdroid_core)
  target_compile_options(hd_program_test PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG;-Wall;-Wextra;-Wpedantic>)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(hd_program_test PRIVATE arcdroid_sanitizer_link)
  endif()
  add_test(NAME hard-disc-program COMMAND hd_program_test)
  add_executable(install_ide_test tests/install_ide_test.c
    engine/arculator/src/ide.c)
  target_include_directories(install_ide_test PRIVATE engine engine/arculator/src)
  target_compile_options(install_ide_test PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG;-Wall;-Wextra;-Wpedantic>)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(install_ide_test PRIVATE
      arcdroid_sanitizer_compile arcdroid_sanitizer_link)
  endif()
  add_test(NAME install-ide-registers COMMAND install_ide_test)
  add_executable(install_store_test tests/install_store_test.c
    libretro/install_store.c libretro/arc_io.c)
  target_include_directories(install_store_test PRIVATE core libretro libretro/include)
  target_link_libraries(install_store_test PRIVATE arcdroid_core)
  target_compile_options(install_store_test PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG;-Wall;-Wextra;-Wpedantic>)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(install_store_test PRIVATE
      arcdroid_sanitizer_compile arcdroid_sanitizer_link)
  endif()
  add_test(NAME native-hard-disc-store COMMAND install_store_test)
  set(FIXTURE_DIR "${CMAKE_CURRENT_BINARY_DIR}/tests/fixtures/generated")
  if(NOT WIN32)
    add_test(NAME launch-outcome-rom
      COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_launch_outcome.py"
        $<TARGET_FILE:libretro_harness> $<TARGET_FILE:arcdroid_libretro_test>
        "${FIXTURE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/launch-outcome-rom-data")
    add_test(NAME hd-product-process
      COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_hd_product.py"
        $<TARGET_FILE:libretro_harness> $<TARGET_FILE:arcdroid_libretro_test>
        "${FIXTURE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/hd-product-process-data")
    add_test(NAME hd-started-process
      COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_hd_started.py"
        $<TARGET_FILE:libretro_harness> $<TARGET_FILE:arcdroid_libretro_test>
        "${FIXTURE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/hd-started-process-data")
    add_test(NAME hd-chooser-process
      COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_hd_chooser.py"
        $<TARGET_FILE:libretro_harness> $<TARGET_FILE:arcdroid_libretro_test>
        "${FIXTURE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/hd-chooser-process-data")
    add_test(NAME adf-refusal-rom
      COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_adf_refusal.py"
        $<TARGET_FILE:libretro_harness> $<TARGET_FILE:arcdroid_libretro_test>
        "${FIXTURE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/adf-refusal-rom-data")
  endif()
  add_library(arcdroid_core_test STATIC
    ${ARCDROID_CORE_SOURCES} ${RPC_BOOT_ASSETS} tests/adffs_profiles_test.c tests/firmware_table_test.c)
  target_include_directories(arcdroid_core_test PUBLIC
    core libretro/include "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_link_libraries(arcdroid_core_test PRIVATE arcdroid_zlib)
  add_custom_command(
    OUTPUT "${FIXTURE_DIR}/arcdroid-native-hd-guest.rom"
    COMMAND "${Python3_EXECUTABLE}" -B
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/generate_native_hd_guest.py"
      "${FIXTURE_DIR}/arcdroid-native-hd-guest.rom"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/generate_native_hd_guest.py"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/generate_fixtures.py"
    VERBATIM)
  add_custom_target(native_hd_guest_fixture ALL DEPENDS
    "${FIXTURE_DIR}/arcdroid-native-hd-guest.rom")
  add_custom_command(
    OUTPUT "${FIXTURE_DIR}/arcdroid-native-hd-stress.rom"
    COMMAND "${Python3_EXECUTABLE}" -B
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/generate_native_hd_guest.py"
      "${FIXTURE_DIR}/arcdroid-native-hd-stress.rom" --stress
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/generate_native_hd_guest.py"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/generate_fixtures.py"
    VERBATIM)
  add_custom_target(native_hd_stress_fixture ALL DEPENDS
    "${FIXTURE_DIR}/arcdroid-native-hd-stress.rom")
  target_compile_definitions(arcdroid_core_test PUBLIC ARCDROID_TEST_FAULTS=1)
  target_compile_options(arcdroid_core_test PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic;-Werror=implicit-function-declaration>)
  set_target_properties(arcdroid_core_test PROPERTIES POSITION_INDEPENDENT_CODE ON)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(arcdroid_core_test PRIVATE arcdroid_sanitizer_compile)
  endif()
  add_executable(jfd_reader_tests
    tests/jfd_reader_tests.c engine/arculator/src/disc_jfd.c)
  target_include_directories(jfd_reader_tests PRIVATE
    core engine/compat engine/arculator/src)
  target_link_libraries(jfd_reader_tests PRIVATE arcdroid_core)
  target_compile_options(jfd_reader_tests PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG;-Wall;-Wextra;-Wpedantic>)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(jfd_reader_tests PRIVATE
      arcdroid_sanitizer_compile arcdroid_sanitizer_link)
  endif()
  add_test(NAME jfd-engine-reader COMMAND jfd_reader_tests)
  add_executable(cmos_device_tests tests/cmos_device_tests.c engine/arculator/src/cmos.c)
  target_include_directories(cmos_device_tests PRIVATE
    engine engine/compat engine/arculator/src)
  target_compile_options(cmos_device_tests PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG;-Wall;-Wextra;-Wpedantic>)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(cmos_device_tests PRIVATE
      arcdroid_sanitizer_compile arcdroid_sanitizer_link)
  endif()
  add_test(NAME cmos-device-bus COMMAND cmos_device_tests)
  add_executable(jfd_controller_tests
    tests/jfd_controller_tests.c
      engine/arculator/src/wd1770.c
    engine/arculator/src/82c711_fdc.c
    engine/arculator/src/disc.c
    engine/arculator/src/disc_adf.c
    engine/arculator/src/disc_jfd.c
    engine/arculator/src/timer.c)
  target_include_directories(jfd_controller_tests PRIVATE
    core engine engine/compat engine/arculator/src libretro libretro/include)
  target_link_libraries(jfd_controller_tests PRIVATE arcdroid_core)
  target_compile_options(jfd_controller_tests PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG;-Wall;-Wextra;-Wpedantic>)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(jfd_controller_tests PRIVATE
      arcdroid_sanitizer_compile arcdroid_sanitizer_link)
  endif()
  add_test(NAME shipping-io-guard
    COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tools/check-shipping-io.sh" --self-test)
  add_test(NAME asset-guard-self-test
    COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_assets_guard.sh"
      "${CMAKE_CURRENT_SOURCE_DIR}/tools/check-assets.sh")
  set_tests_properties(shipping-io-guard PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    ENVIRONMENT "ARCDROID_SHIPPING_IO_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}")

  set(ADFFS_FIXTURES
    "${FIXTURE_DIR}/adffs/synthetic.zip"
    "${FIXTURE_DIR}/adffs/script-changed.zip"
    "${FIXTURE_DIR}/adffs/stored.zip"
    "${FIXTURE_DIR}/adffs/descriptor.zip"
    "${FIXTURE_DIR}/adffs/reordered.zip"
    "${FIXTURE_DIR}/adffs/untested.zip"
    "${FIXTURE_DIR}/adffs/truncated.zip"
    "${FIXTURE_DIR}/adffs/bad-crc.zip"
    "${FIXTURE_DIR}/adffs/encrypted.zip"
    "${FIXTURE_DIR}/adffs/zip64.zip"
    "${FIXTURE_DIR}/adffs/zip64-extra.zip"
    "${FIXTURE_DIR}/adffs/descriptor-mismatch.zip"
    "${FIXTURE_DIR}/adffs/local-mismatch.zip"
    "${FIXTURE_DIR}/adffs/extra-mismatch.zip"
    "${FIXTURE_DIR}/adffs/central-size-mismatch.zip"
    "${FIXTURE_DIR}/adffs/inflated-lie.zip"
    "${FIXTURE_DIR}/adffs/missing-arc0.zip"
    "${FIXTURE_DIR}/adffs/duplicate.zip"
    "${FIXTURE_DIR}/adffs/case-collision.zip"
    "${FIXTURE_DIR}/adffs/unsafe-name.zip"
    "${FIXTURE_DIR}/adffs/absolute-name.zip"
    "${FIXTURE_DIR}/adffs/backslash-name.zip"
    "${FIXTURE_DIR}/adffs/entry-limit.zip"
    "${FIXTURE_DIR}/adffs/oversize.zip"
    "${FIXTURE_DIR}/adffs/used-total.zip"
    "${FIXTURE_DIR}/adffs/excluded-trackers.zip"
    "${FIXTURE_DIR}/adffs/version-control.zip"
    "${FIXTURE_DIR}/adffs/version-quote.zip"
    "${FIXTURE_DIR}/adffs/version-backslash.zip"
    "${FIXTURE_DIR}/adffs/missing-obey.zip"
    "${FIXTURE_DIR}/adffs/missing-runtypes.zip"
    "${FIXTURE_DIR}/adffs/missing-fixobey.zip"
    "${FIXTURE_DIR}/adffs/invalid-obey-name.zip"
    "${FIXTURE_DIR}/adffs/eligible.jfd"
    "${FIXTURE_DIR}/adffs/embedded-only.jfd"
    "${FIXTURE_DIR}/adffs/no-script.jfd"
    "${FIXTURE_DIR}/adffs/game-only.jfd"
    "${FIXTURE_DIR}/adffs/disc-two.jfd"
    "${FIXTURE_DIR}/adffs/count-two.jfd"
    "${FIXTURE_DIR}/adffs/missing-resource.jfd"
    "${FIXTURE_DIR}/adffs/comment-reference.jfd"
    "${FIXTURE_DIR}/adffs/closure-too-large.jfd"
    "${FIXTURE_DIR}/adffs/capacity.jfd"
    "${FIXTURE_DIR}/adffs/too-large.jfd"
    "${FIXTURE_DIR}/adffs/decoded-limit.jfd"
    "${FIXTURE_DIR}/adffs/decoded-too-large.jfd"
    "${FIXTURE_DIR}/adffs/multi.m3u"
    "${FIXTURE_DIR}/adffs/writable-install.jfd"
    "${FIXTURE_DIR}/adffs/writable-os16-17.jfd"
    "${FIXTURE_DIR}/adffs/writable-os17-22.jfd"
    "${FIXTURE_DIR}/adffs/writable-os18.jfd"
    "${FIXTURE_DIR}/adffs/writable-os22.jfd")
  list(APPEND ADFFS_FIXTURES
    "${FIXTURE_DIR}/adffs/install-1.jfd"
    "${FIXTURE_DIR}/adffs/install-2.jfd"
    "${FIXTURE_DIR}/adffs/install-3.jfd"
    "${FIXTURE_DIR}/adffs/install.m3u"
    "${FIXTURE_DIR}/adffs/install-first.m3u"
    "${FIXTURE_DIR}/adffs/install.zip"
    "${FIXTURE_DIR}/adffs/Install.jfd/Install.jfd"
    "${FIXTURE_DIR}/adffs/Install.jfd/Install 2.jfd"
    "${FIXTURE_DIR}/adffs/Install.jfd/Install 3.jfd"
    "${FIXTURE_DIR}/adffs/eligible-hd.jfd")
  list(APPEND ADFFS_FIXTURES
    "${FIXTURE_DIR}/adffs/eligible-hd-rw.jfd"
    "${FIXTURE_DIR}/adffs/install-single-hd.jfd")
  set(JFD_TAB_FIXTURES "${FIXTURE_DIR}/jfd-tab/mixed.m3u")
  foreach(tab_kind IN ITEMS captured created guard guard-fail guard-placement
      route route-os route-layout request set probe short-full unschedulable
      nonmatching irregular-newmap-free)
    foreach(tab_header IN ITEMS 0 1 2 3 legacy)
      list(APPEND JFD_TAB_FIXTURES "${FIXTURE_DIR}/jfd-tab/${tab_kind}-${tab_header}.jfd")
    endforeach()
  endforeach()
  add_custom_command(
    OUTPUT ${JFD_TAB_FIXTURES} "${FIXTURE_DIR}/arcdroid-test.rom" "${FIXTURE_DIR}/arcdroid-test.adf"
      "${FIXTURE_DIR}/arcdroid-disc-request-a.rom"
      "${FIXTURE_DIR}/arcdroid-disc-request-b.rom"
      "${FIXTURE_DIR}/arcdroid-disc-request-b-found.rom"
      "${FIXTURE_DIR}/arcdroid-disc-request-b-found-between.rom"
      "${FIXTURE_DIR}/arcdroid-disc-request-b-back.rom"
      "${FIXTURE_DIR}/arcdroid-disc-request-b-alternate.rom"
      "${FIXTURE_DIR}/disc-request/Set.m3u"
      "${FIXTURE_DIR}/disc-request/SetShort.m3u"
      "${FIXTURE_DIR}/disc-request/SetJfd.m3u"
      "${FIXTURE_DIR}/disc-request/SetPerf.m3u"
      "${FIXTURE_DIR}/disc-request/SetHigh.m3u"
      "${FIXTURE_DIR}/disc-request/AUnknown.m3u"
      "${FIXTURE_DIR}/disc-request/Set32Jfd.m3u"
      "${FIXTURE_DIR}/disc-request/One.adf"
      "${FIXTURE_DIR}/disc-request/Two.adf"
      "${FIXTURE_DIR}/disc-request/TwoShort.adf"
      "${FIXTURE_DIR}/disc-request/TwoJfd.jfd"
      "${FIXTURE_DIR}/disc-request/Neutral00.jfd"
      "${FIXTURE_DIR}/disc-request/Neutral01.jfd"
      "${FIXTURE_DIR}/disc-request/HighF.adf"
      "${FIXTURE_DIR}/disc-request/UnknownName.adf"
      "${FIXTURE_DIR}/arcdroid-power-on-early-swi.rom"
      "${FIXTURE_DIR}/arcdroid-power-on-early-xswi.rom"
      "${FIXTURE_DIR}/arcdroid-power-on-late-kart.rom"
      "${FIXTURE_DIR}/arcdroid-power-on-cmos-guest.rom"
      "${FIXTURE_DIR}/arcdroid-power-on-boot-prompt.rom"
      "${FIXTURE_DIR}/arcdroid-power-on-boot-cli-then-prompt.rom"
      "${FIXTURE_DIR}/arcdroid-boot-prompt.rom"
      "${FIXTURE_DIR}/arcdroid-boot-prompt-write.rom"
      "${FIXTURE_DIR}/arcdroid-boot-cli.rom"
      "${FIXTURE_DIR}/arcdroid-boot-cli-confirm.rom"
      "${FIXTURE_DIR}/arcdroid-boot-horizon-edge.rom"
      "${FIXTURE_DIR}/arcdroid-boot-cli-then-prompt.rom"
      "${FIXTURE_DIR}/arcdroid-boot-desktop.rom"
      "${FIXTURE_DIR}/arcdroid-boot-two-phase.rom"
      "${FIXTURE_DIR}/arcdroid-boot-struct-one-to-several.rom"
      "${FIXTURE_DIR}/arcdroid-boot-struct-one-to-none.rom"
      "${FIXTURE_DIR}/arcdroid-boot-struct-none-to-one.rom"
      "${FIXTURE_DIR}/arcdroid-boot-struct-one-to-renamed.rom"
      "${FIXTURE_DIR}/arcdroid-boot-struct-sibling.rom"
      "${FIXTURE_DIR}/arcdroid-chooser-test.rom"
      "${FIXTURE_DIR}/arcdroid-chooser-d-test.rom"
      "${FIXTURE_DIR}/arcdroid-unsupported-launch-test.rom"
      "${FIXTURE_DIR}/arcdroid-demo.adl"
      "${FIXTURE_DIR}/arcdroid-application-l.adl"
      "${FIXTURE_DIR}/arcdroid-chooser-l.adl"
      "${FIXTURE_DIR}/arcdroid-g1-chooser-l.adl"
      "${FIXTURE_DIR}/arcdroid-nested-chooser-l.adl"
      "${FIXTURE_DIR}/arcdroid-extended-chooser-d.adf"
      "${FIXTURE_DIR}/launch-family/boot-3-l.adl"
      "${FIXTURE_DIR}/launch-family/checkpoint-nested-literal.adf"
      "${FIXTURE_DIR}/launch-family/checkpoint-nested-extended.adf"
      "${FIXTURE_DIR}/launch-family/single-l.adl"
      "${FIXTURE_DIR}/launch-family/ambiguous-l.adl"
      "${FIXTURE_DIR}/launch-family/chooser-resources-d.adf"
      "${FIXTURE_DIR}/launch-family/chooser-resource-pair-d.adf"
      "${FIXTURE_DIR}/launch-family/chooser-incomplete-d.adf"
      "${FIXTURE_DIR}/launch-family/checkpoint-a-guest.adf"
      "${FIXTURE_DIR}/launch-family/checkpoint-a-top-bit.adf"
      "${FIXTURE_DIR}/launch-family/checkpoint-a-hard-space.adf"
      "${FIXTURE_DIR}/launch-family/checkpoint-a-greater-than.adf"
      "${FIXTURE_DIR}/launch-family/sibling-pipe-l.adl"
      "${FIXTURE_DIR}/launch-family/duplicate-siblings-l.adl"
      "${FIXTURE_DIR}/launch-family/boot-struct-one-to-several.adf"
      "${FIXTURE_DIR}/launch-family/boot-struct-one-to-none.adf"
      "${FIXTURE_DIR}/launch-family/boot-struct-none-to-one.adf"
      "${FIXTURE_DIR}/launch-family/boot-struct-one-to-several.jfd"
      "${FIXTURE_DIR}/launch-family/boot-struct-one-to-renamed.adf"
      "${FIXTURE_DIR}/launch-family/boot-struct-sibling.adf"
      "${FIXTURE_DIR}/launch-family/boot-known-one-launchable.adf"
      "${FIXTURE_DIR}/arcdroid-media-test.rom"
      "${FIXTURE_DIR}/arcdroid-short-tail-wd.rom"
      "${FIXTURE_DIR}/arcdroid-jfd-save-test.rom"
      "${FIXTURE_DIR}/arcdroid-jfd-newio-save-test.rom"
      "${FIXTURE_DIR}/arcdroid-jfd-create-test.rom"
      "${FIXTURE_DIR}/arcdroid-jfd-newio-create-test.rom"
      "${FIXTURE_DIR}/arcdroid-jfd-merge-test.rom"
      "${FIXTURE_DIR}/arcdroid-jfd-newio-merge-test.rom"
      "${FIXTURE_DIR}/arcdroid-jfd-merge-readback-test.rom"
      "${FIXTURE_DIR}/arcdroid-jfd-newio-merge-readback-test.rom"
      "${FIXTURE_DIR}/arcdroid-jfd-merge-missing-test.rom"
      "${FIXTURE_DIR}/arcdroid-jfd-newio-merge-missing-test.rom"
      "${FIXTURE_DIR}/arcdroid-jfd-guard-u.rom"
      "${FIXTURE_DIR}/arcdroid-jfd-guard-tail.rom"
      "${FIXTURE_DIR}/arcdroid-jfd-guard-invalid.rom"
      "${FIXTURE_DIR}/arcdroid-jfd-guard-size.rom"
      "${FIXTURE_DIR}/arcdroid-jfd-guard-data.rom"
      "${FIXTURE_DIR}/arcdroid-jfd-newio-guard-u.rom"
      "${FIXTURE_DIR}/arcdroid-jfd-newio-guard-tail.rom"
      "${FIXTURE_DIR}/arcdroid-jfd-newio-guard-invalid.rom"
      "${FIXTURE_DIR}/arcdroid-jfd-newio-guard-size.rom"
      "${FIXTURE_DIR}/arcdroid-jfd-newio-guard-data.rom"
      "${FIXTURE_DIR}/jfd-creation/irregular-newmap-free-sequence.jfd"
      "${FIXTURE_DIR}/jfd-file-data/guard-terminal.jfd"
      "${FIXTURE_DIR}/jfd-allocation/guard-route.jfd"
      "${FIXTURE_DIR}/jfd-allocation/guard-crossing.jfd"
      "${FIXTURE_DIR}/jfd-allocation/guard-physical.jfd"
      "${FIXTURE_DIR}/jfd-allocation/guard-one-tail.jfd"
      "${FIXTURE_DIR}/jfd-allocation/guard-split-tail.jfd"
      "${FIXTURE_DIR}/jfd-allocation/guard-extra.jfd"
      "${FIXTURE_DIR}/jfd-allocation/guard-u-allocated.jfd"
      "${FIXTURE_DIR}/jfd-allocation/guard-occupied.jfd"
      "${FIXTURE_DIR}/jfd-allocation/guard-occupied-bad-crc.jfd"
      "${FIXTURE_DIR}/jfd-allocation/guard-captured-density.jfd"
      "${FIXTURE_DIR}/jfd-allocation/guard-profile-admission-fail.jfd"
      "${FIXTURE_DIR}/jfd-allocation/guard-extra-odd.jfd"
      "${FIXTURE_DIR}/jfd-allocation/guard-extra-even.jfd"
      "${FIXTURE_DIR}/jfd-allocation/guard-allocated-profile-fail.jfd"
      "${FIXTURE_DIR}/jfd-allocation/guard-tail-allocated.jfd"
      "${FIXTURE_DIR}/jfd-allocation/guard-partial-u.jfd"
      "${FIXTURE_DIR}/jfd-allocation/guard-sparse-crossing.jfd"
      "${FIXTURE_DIR}/jfd-allocation/guard-profile-fail.jfd"
      "${FIXTURE_DIR}/jfd-allocation/guard-placement-fail.jfd"
      "${FIXTURE_DIR}/jfd-allocation/guard-merge.jfd"
      "${FIXTURE_DIR}/arcdroid-jfd-allocation-veto-test.rom"
      "${FIXTURE_DIR}/arcdroid-jfd-allocation-claim-test.rom"
      "${FIXTURE_DIR}/arcdroid-jfd-allocation-read-test.rom"
      "${FIXTURE_DIR}/arcdroid-jfd-allocation-probe-write-test.rom"
      "${FIXTURE_DIR}/arcdroid-jfd-newio-allocation-veto-test.rom"
      "${FIXTURE_DIR}/arcdroid-jfd-newio-allocation-claim-test.rom"
      "${FIXTURE_DIR}/arcdroid-jfd-newio-allocation-read-test.rom"
      "${FIXTURE_DIR}/arcdroid-jfd-newio-allocation-probe-write-test.rom"
      "${FIXTURE_DIR}/arcdroid-jfd-directory-benign.rom"
      "${FIXTURE_DIR}/arcdroid-jfd-directory-rename.rom"
      "${FIXTURE_DIR}/arcdroid-media-disc1.adf"
      "${FIXTURE_DIR}/arcdroid-media-disc2.adf"
      "${FIXTURE_DIR}/arcdroid-media.m3u"
      "${FIXTURE_DIR}/Qaventra.adf/Qaventra.adf"
      "${FIXTURE_DIR}/Qaventra.adf/Qaventra 2.adf"
      "${FIXTURE_DIR}/Qaventra.adf/Qaventra 3.adl"
      "${FIXTURE_DIR}/Qaventra.adf/Qaventra 4.jfd"
      "${FIXTURE_DIR}/Qaventra.adf/Qaventra 5.adl"
      "${FIXTURE_DIR}/Qaventra.adf/Qaventra 9.adf"
      "${FIXTURE_DIR}/Qaventra.adf/Qaventra 10.JFD"
      "${FIXTURE_DIR}/Qaventra.adf/.Qaventra 1.adf"
      "${FIXTURE_DIR}/Qaventra.adf/Qaventra.m3u"
      "${FIXTURE_DIR}/Qaventra.adf/Nested.adf/Nested.adf"
      "${FIXTURE_DIR}/Virelai.adf/Virelai.adf"
      "${FIXTURE_DIR}/Virelai.adf/Virelai 2.adf"
      "${FIXTURE_DIR}/arcdroid-test.jfd"
      "${FIXTURE_DIR}/jfd-writes/rw-d.jfd"
      "${FIXTURE_DIR}/jfd-writes/rw-create-d.jfd"
      "${FIXTURE_DIR}/jfd-writes/rw-identification-d.jfd"
      "${FIXTURE_DIR}/jfd-writes/wp-create-d.jfd"
      "${FIXTURE_DIR}/jfd-writes/rw-merge-d.jfd"
      "${FIXTURE_DIR}/jfd-writes/wp-merge-d.jfd"
      "${FIXTURE_DIR}/jfd-writes/rw-newio-d.jfd"
      "${FIXTURE_DIR}/jfd-writes/wp-newio-d.jfd"
      "${FIXTURE_DIR}/jfd-writes/launch-unchanged-d.jfd"
      "${FIXTURE_DIR}/jfd-writes/launch-renamed-d.jfd"
      "${FIXTURE_DIR}/jfd-writes/missing-one-d.jfd"
      "${FIXTURE_DIR}/jfd-creation/partial-untimed.jfd"
      "${FIXTURE_DIR}/jfd-creation/launch-full.jfd"
      "${FIXTURE_DIR}/jfd-creation/launch-partial.jfd"
      "${FIXTURE_DIR}/jfd-creation/partial-edge.jfd"
      "${FIXTURE_DIR}/jfd-creation/partial-timed.jfd"
      "${FIXTURE_DIR}/jfd-creation/timed-rounded.jfd"
      "${FIXTURE_DIR}/jfd-creation/boundary.jfd"
      "${FIXTURE_DIR}/jfd-creation/unschedulable.jfd"
      "${FIXTURE_DIR}/jfd-creation/short-full.jfd"
      "${FIXTURE_DIR}/jfd-creation/short-partial.jfd"
      "${FIXTURE_DIR}/jfd-creation/nonmatching.jfd"
      "${FIXTURE_DIR}/jfd-creation/irregular-free.jfd"
      "${FIXTURE_DIR}/jfd-creation/irregular-free-sequence.jfd"
      "${FIXTURE_DIR}/jfd-creation/irregular-newmap-free.jfd"
      "${FIXTURE_DIR}/jfd-creation/irregular-allocated.jfd"
      "${FIXTURE_DIR}/jfd-creation/irregular-all-ids.jfd"
      "${FIXTURE_DIR}/jfd-creation/irregular-id-only-free.jfd"
      "${FIXTURE_DIR}/jfd-creation/irregular-id-only-allocated.jfd"
      "${FIXTURE_DIR}/jfd-creation/irregular-bad-crc-free.jfd"
      "${FIXTURE_DIR}/jfd-creation/irregular-bad-crc-allocated.jfd"
      "${FIXTURE_DIR}/jfd-creation/half-vote.jfd"
      "${FIXTURE_DIR}/jfd-writes/adffs-rw.jfd"
      "${FIXTURE_DIR}/jfd-writes/wp-d.jfd"
      "${FIXTURE_DIR}/jfd-writes/SaveSet.jfd/SaveSet 2.jfd"
      "${FIXTURE_DIR}/jfd-writes/neither-d.jfd"
      "${FIXTURE_DIR}/jfd-writes/legacy-d.jfd"
      "${FIXTURE_DIR}/jfd-writes/rw-d-gzip.jfd"
      "${FIXTURE_DIR}/jfd-writes/rw-d.zip"
      "${FIXTURE_DIR}/jfd-writes/mixed.m3u"
      "${FIXTURE_DIR}/arcdroid-test-gzip.jfd"
      "${FIXTURE_DIR}/arcdroid-test-two-member.jfd"
      "${FIXTURE_DIR}/arcdroid-test-three-member.jfd"
      "${FIXTURE_DIR}/jfd-identification/sparse-d-timed.jfd"
      "${FIXTURE_DIR}/jfd-identification/sparse-d-partial-first.jfd"
      "${FIXTURE_DIR}/jfd-identification/sparse-d-untimed.jfd"
      "${FIXTURE_DIR}/jfd-identification/sparse-d-unplaceable.jfd"
      "${FIXTURE_DIR}/jfd-identification/sparse-d-outside.jfd"
      "${FIXTURE_DIR}/jfd-identification/sparse-d-coincident.jfd"
      "${FIXTURE_DIR}/jfd-identification/sparse-d-clamped-interior.jfd"
      "${FIXTURE_DIR}/jfd-identification/sparse-d-clamped-crossing.jfd"
      "${FIXTURE_DIR}/jfd-identification/sparse-d-clamped-exterior.jfd"
      "${FIXTURE_DIR}/jfd-identification/sparse-d-clamped-bad-exterior.jfd"
      "${FIXTURE_DIR}/jfd-identification/complete-d.jfd"
      "${FIXTURE_DIR}/jfd-identification/legacy-l.jfd"
      "${FIXTURE_DIR}/jfd-identification/sparse-e-free.jfd"
      "${FIXTURE_DIR}/jfd-identification/sparse-e-spare-bit.jfd"
      "${FIXTURE_DIR}/jfd-identification/sparse-e-allocated.jfd"
      "${FIXTURE_DIR}/jfd-identification/sparse-e-copy-allocated.jfd"
      "${FIXTURE_DIR}/jfd-identification/sparse-e-backup.jfd"
      "${FIXTURE_DIR}/jfd-identification/sparse-e-conflict.jfd"
      "${FIXTURE_DIR}/jfd-identification/sparse-e-bad-link.jfd"
      "${FIXTURE_DIR}/jfd-identification/sparse-f-timed.jfd"
      "${FIXTURE_DIR}/jfd-identification/sparse-f-untimed.jfd"
      "${FIXTURE_DIR}/jfd-identification/sparse-f-mixed-basis.jfd"
      "${FIXTURE_DIR}/jfd-identification/sparse-f-zero-tail.jfd"
      "${FIXTURE_DIR}/jfd-identification/sparse-f-tail-id-only.jfd"
      "${FIXTURE_DIR}/jfd-identification/sparse-f-tail-stop-only.jfd"
      "${FIXTURE_DIR}/jfd-identification/sparse-f-tail-other-id.jfd"
      "${FIXTURE_DIR}/jfd-identification/sparse-f-tail-two-fragments.jfd"
      "${FIXTURE_DIR}/jfd-identification/sparse-f-tail-early-stop.jfd"
      "${FIXTURE_DIR}/jfd-identification/sparse-f-tail-free-link.jfd"
      "${FIXTURE_DIR}/jfd-identification/sparse-f-tail-stray-bit.jfd"
      "${FIXTURE_DIR}/jfd-identification/sparse-f-one-zone.jfd"
      "${FIXTURE_DIR}/jfd-identification/sparse-f-one-zone-stray-tail.jfd"
      "${FIXTURE_DIR}/jfd-identification/sparse-f-map-backup.jfd"
      "${FIXTURE_DIR}/jfd-identification/sparse-f-map-primary.jfd"
      "${FIXTURE_DIR}/jfd-identification/sparse-f-backup.jfd"
      "${FIXTURE_DIR}/jfd-identification/sparse-f-copy-disagree.jfd"
      "${FIXTURE_DIR}/jfd-identification/sparse-f-copy-free-prefix.jfd"
      "${FIXTURE_DIR}/jfd-identification/sparse-f-partial-system.jfd"
      "${FIXTURE_DIR}/jfd-identification/sparse-f-root-boundary.jfd"
      "${FIXTURE_DIR}/jfd-identification/sparse-f-root-second.jfd"
      "${FIXTURE_DIR}/jfd-identification/sparse-f-root-unresolved.jfd"
      "${FIXTURE_DIR}/jfd-identification/sparse-f-boot-omitted.jfd"
      "${FIXTURE_DIR}/jfd-identification/sparse-f-mixed.jfd"
      "${FIXTURE_DIR}/jfd-identification/sparse-f-allocated.jfd"
      "${FIXTURE_DIR}/jfd-identification/sparse-f-conflict.jfd"
      "${FIXTURE_DIR}/jfd-identification/sparse-f-boot-conflict.jfd"
      "${FIXTURE_DIR}/jfd-identification/sparse-f-bad-cross.jfd"
      "${FIXTURE_DIR}/jfd-identification/sparse-f-past-end.jfd"
      "${FIXTURE_DIR}/zip-content/single.zip"
      ${ADFFS_FIXTURES}
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/generate_fixtures.py" "${FIXTURE_DIR}"
    DEPENDS tests/generate_fixtures.py VERBATIM)
  add_custom_target(test-fixtures ALL DEPENDS ${JFD_TAB_FIXTURES}
    "${FIXTURE_DIR}/arcdroid-test.rom" "${FIXTURE_DIR}/arcdroid-test.adf"
    "${FIXTURE_DIR}/arcdroid-disc-request-a.rom"
    "${FIXTURE_DIR}/arcdroid-disc-request-b.rom"
    "${FIXTURE_DIR}/arcdroid-disc-request-b-found.rom"
    "${FIXTURE_DIR}/arcdroid-disc-request-b-found-between.rom"
    "${FIXTURE_DIR}/arcdroid-disc-request-b-back.rom"
    "${FIXTURE_DIR}/arcdroid-disc-request-b-alternate.rom"
    "${FIXTURE_DIR}/disc-request/Set.m3u"
    "${FIXTURE_DIR}/disc-request/SetShort.m3u"
    "${FIXTURE_DIR}/disc-request/SetJfd.m3u"
    "${FIXTURE_DIR}/disc-request/SetPerf.m3u"
    "${FIXTURE_DIR}/disc-request/SetHigh.m3u"
    "${FIXTURE_DIR}/disc-request/AUnknown.m3u"
    "${FIXTURE_DIR}/disc-request/Set32Jfd.m3u"
    "${FIXTURE_DIR}/disc-request/TwoJfd.jfd"
    "${FIXTURE_DIR}/disc-request/Neutral00.jfd"
    "${FIXTURE_DIR}/disc-request/Neutral01.jfd"
    "${FIXTURE_DIR}/disc-request/HighF.adf"
    "${FIXTURE_DIR}/disc-request/UnknownName.adf"
    "${FIXTURE_DIR}/arcdroid-power-on-early-swi.rom"
    "${FIXTURE_DIR}/arcdroid-power-on-early-xswi.rom"
    "${FIXTURE_DIR}/arcdroid-power-on-late-kart.rom"
    "${FIXTURE_DIR}/arcdroid-power-on-cmos-guest.rom"
    "${FIXTURE_DIR}/arcdroid-power-on-boot-prompt.rom"
    "${FIXTURE_DIR}/arcdroid-power-on-boot-cli-then-prompt.rom"
    "${FIXTURE_DIR}/arcdroid-boot-prompt.rom"
    "${FIXTURE_DIR}/arcdroid-boot-prompt-write.rom"
    "${FIXTURE_DIR}/arcdroid-boot-cli.rom"
    "${FIXTURE_DIR}/arcdroid-boot-cli-confirm.rom"
    "${FIXTURE_DIR}/arcdroid-boot-horizon-edge.rom"
    "${FIXTURE_DIR}/arcdroid-boot-cli-then-prompt.rom"
    "${FIXTURE_DIR}/arcdroid-boot-desktop.rom"
    "${FIXTURE_DIR}/arcdroid-boot-two-phase.rom"
    "${FIXTURE_DIR}/arcdroid-boot-struct-one-to-several.rom"
    "${FIXTURE_DIR}/arcdroid-boot-struct-one-to-none.rom"
    "${FIXTURE_DIR}/arcdroid-boot-struct-none-to-one.rom"
    "${FIXTURE_DIR}/arcdroid-boot-struct-one-to-renamed.rom"
    "${FIXTURE_DIR}/arcdroid-boot-struct-sibling.rom"
    "${FIXTURE_DIR}/arcdroid-chooser-test.rom"
    "${FIXTURE_DIR}/arcdroid-chooser-d-test.rom"
    "${FIXTURE_DIR}/arcdroid-unsupported-launch-test.rom"
    "${FIXTURE_DIR}/arcdroid-demo.adl"
    "${FIXTURE_DIR}/arcdroid-application-l.adl"
    "${FIXTURE_DIR}/arcdroid-chooser-l.adl"
    "${FIXTURE_DIR}/arcdroid-g1-chooser-l.adl"
    "${FIXTURE_DIR}/arcdroid-nested-chooser-l.adl"
    "${FIXTURE_DIR}/arcdroid-extended-chooser-d.adf"
    "${FIXTURE_DIR}/launch-family/boot-3-l.adl"
    "${FIXTURE_DIR}/launch-family/checkpoint-nested-literal.adf"
    "${FIXTURE_DIR}/launch-family/checkpoint-nested-extended.adf"
    "${FIXTURE_DIR}/launch-family/single-l.adl"
    "${FIXTURE_DIR}/launch-family/ambiguous-l.adl"
    "${FIXTURE_DIR}/launch-family/chooser-resources-d.adf"
    "${FIXTURE_DIR}/launch-family/chooser-resource-pair-d.adf"
    "${FIXTURE_DIR}/launch-family/chooser-incomplete-d.adf"
    "${FIXTURE_DIR}/launch-family/checkpoint-a-guest.adf"
    "${FIXTURE_DIR}/launch-family/checkpoint-a-top-bit.adf"
    "${FIXTURE_DIR}/launch-family/checkpoint-a-hard-space.adf"
    "${FIXTURE_DIR}/launch-family/checkpoint-a-greater-than.adf"
    "${FIXTURE_DIR}/launch-family/sibling-pipe-l.adl"
    "${FIXTURE_DIR}/launch-family/duplicate-siblings-l.adl"
    "${FIXTURE_DIR}/launch-family/boot-struct-one-to-several.adf"
    "${FIXTURE_DIR}/launch-family/boot-struct-one-to-none.adf"
    "${FIXTURE_DIR}/launch-family/boot-struct-none-to-one.adf"
    "${FIXTURE_DIR}/launch-family/boot-struct-one-to-several.jfd"
    "${FIXTURE_DIR}/launch-family/boot-struct-one-to-renamed.adf"
    "${FIXTURE_DIR}/launch-family/boot-struct-sibling.adf"
    "${FIXTURE_DIR}/launch-family/boot-known-one-launchable.adf"
    "${FIXTURE_DIR}/arcdroid-media-test.rom"
    "${FIXTURE_DIR}/arcdroid-short-tail-wd.rom"
    "${FIXTURE_DIR}/arcdroid-jfd-save-test.rom"
    "${FIXTURE_DIR}/arcdroid-jfd-newio-save-test.rom"
    "${FIXTURE_DIR}/arcdroid-jfd-create-test.rom"
    "${FIXTURE_DIR}/arcdroid-jfd-newio-create-test.rom"
    "${FIXTURE_DIR}/arcdroid-jfd-directory-benign.rom"
    "${FIXTURE_DIR}/arcdroid-jfd-directory-rename.rom"
    "${FIXTURE_DIR}/arcdroid-media-disc1.adf"
    "${FIXTURE_DIR}/arcdroid-media-disc2.adf"
    "${FIXTURE_DIR}/arcdroid-media.m3u"
    "${FIXTURE_DIR}/Qaventra.adf/Qaventra.adf"
    "${FIXTURE_DIR}/Qaventra.adf/Qaventra 2.adf"
    "${FIXTURE_DIR}/Qaventra.adf/Qaventra 3.adl"
    "${FIXTURE_DIR}/Qaventra.adf/Qaventra 4.jfd"
    "${FIXTURE_DIR}/Qaventra.adf/Qaventra 5.adl"
    "${FIXTURE_DIR}/Qaventra.adf/Qaventra 9.adf"
    "${FIXTURE_DIR}/Qaventra.adf/Qaventra 10.JFD"
    "${FIXTURE_DIR}/Qaventra.adf/.Qaventra 1.adf"
    "${FIXTURE_DIR}/Qaventra.adf/Qaventra.m3u"
    "${FIXTURE_DIR}/Qaventra.adf/Nested.adf/Nested.adf"
    "${FIXTURE_DIR}/Virelai.adf/Virelai.adf"
    "${FIXTURE_DIR}/Virelai.adf/Virelai 2.adf"
    "${FIXTURE_DIR}/arcdroid-test.jfd"
    "${FIXTURE_DIR}/jfd-writes/rw-d.jfd"
    "${FIXTURE_DIR}/jfd-writes/rw-create-d.jfd"
    "${FIXTURE_DIR}/jfd-writes/wp-create-d.jfd"
    "${FIXTURE_DIR}/jfd-writes/rw-merge-d.jfd"
    "${FIXTURE_DIR}/jfd-writes/wp-merge-d.jfd"
    "${FIXTURE_DIR}/jfd-writes/rw-newio-d.jfd"
    "${FIXTURE_DIR}/jfd-writes/wp-newio-d.jfd"
    "${FIXTURE_DIR}/jfd-writes/launch-unchanged-d.jfd"
    "${FIXTURE_DIR}/jfd-writes/launch-renamed-d.jfd"
    "${FIXTURE_DIR}/jfd-writes/missing-one-d.jfd"
    "${FIXTURE_DIR}/jfd-creation/partial-untimed.jfd"
    "${FIXTURE_DIR}/jfd-creation/launch-full.jfd"
    "${FIXTURE_DIR}/jfd-creation/launch-partial.jfd"
    "${FIXTURE_DIR}/jfd-creation/partial-edge.jfd"
    "${FIXTURE_DIR}/jfd-creation/partial-timed.jfd"
    "${FIXTURE_DIR}/jfd-creation/timed-rounded.jfd"
    "${FIXTURE_DIR}/jfd-creation/boundary.jfd"
    "${FIXTURE_DIR}/jfd-creation/unschedulable.jfd"
    "${FIXTURE_DIR}/jfd-creation/short-full.jfd"
    "${FIXTURE_DIR}/jfd-creation/short-partial.jfd"
    "${FIXTURE_DIR}/jfd-creation/nonmatching.jfd"
    "${FIXTURE_DIR}/jfd-creation/irregular-free.jfd"
    "${FIXTURE_DIR}/jfd-creation/irregular-free-sequence.jfd"
    "${FIXTURE_DIR}/jfd-creation/irregular-newmap-free.jfd"
    "${FIXTURE_DIR}/jfd-creation/irregular-allocated.jfd"
    "${FIXTURE_DIR}/jfd-creation/irregular-all-ids.jfd"
    "${FIXTURE_DIR}/jfd-creation/irregular-id-only-free.jfd"
    "${FIXTURE_DIR}/jfd-creation/irregular-id-only-allocated.jfd"
    "${FIXTURE_DIR}/jfd-creation/irregular-bad-crc-free.jfd"
    "${FIXTURE_DIR}/jfd-creation/irregular-bad-crc-allocated.jfd"
    "${FIXTURE_DIR}/jfd-creation/half-vote.jfd"
    "${FIXTURE_DIR}/jfd-writes/adffs-rw.jfd"
    "${FIXTURE_DIR}/jfd-writes/wp-d.jfd"
    "${FIXTURE_DIR}/jfd-writes/SaveSet.jfd/SaveSet 2.jfd"
    "${FIXTURE_DIR}/jfd-writes/neither-d.jfd"
    "${FIXTURE_DIR}/jfd-writes/legacy-d.jfd"
    "${FIXTURE_DIR}/jfd-writes/rw-d-gzip.jfd"
    "${FIXTURE_DIR}/jfd-writes/rw-d.zip"
    "${FIXTURE_DIR}/jfd-writes/mixed.m3u"
    "${FIXTURE_DIR}/arcdroid-test-gzip.jfd"
    "${FIXTURE_DIR}/arcdroid-test-two-member.jfd"
    "${FIXTURE_DIR}/arcdroid-test-three-member.jfd"
    "${FIXTURE_DIR}/zip-content/single.zip"
    ${ADFFS_FIXTURES})

  add_dependencies(core_tests test-fixtures)
  add_dependencies(boot_watch_test test-fixtures)
  add_executable(boot_observation_test tests/boot_observation_test.c)
  target_link_libraries(boot_observation_test PRIVATE arcdroid_core_test)
  target_compile_options(boot_observation_test PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG;-Wall;-Wextra;-Wpedantic>)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(boot_observation_test PRIVATE arcdroid_sanitizer_link)
  endif()
  add_test(NAME boot-observation COMMAND boot_observation_test)
  add_executable(adffs_tests tests/adffs_tests.c)
  target_link_libraries(adffs_tests PRIVATE arcdroid_core_test)
  target_compile_options(adffs_tests PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG;-Wall;-Wextra;-Wpedantic>)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(adffs_tests PRIVATE arcdroid_sanitizer_link)
  endif()
  add_dependencies(adffs_tests test-fixtures)
  add_test(NAME adffs-package-media COMMAND adffs_tests "${FIXTURE_DIR}")
  add_executable(zip_content_tests tests/zip_content_tests.c)
  target_link_libraries(zip_content_tests PRIVATE arcdroid_core_test)
  target_compile_options(zip_content_tests PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG;-Wall;-Wextra;-Wpedantic>)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(zip_content_tests PRIVATE arcdroid_sanitizer_link)
  endif()
  add_dependencies(zip_content_tests test-fixtures)
  add_test(NAME zip-content COMMAND zip_content_tests "${FIXTURE_DIR}")
  add_test(NAME adffs-shipping-profile COMMAND adffs_shipping_profile_test)
  add_executable(rpc_selection_test tests/rpc_selection_test.c)
  target_link_libraries(rpc_selection_test PRIVATE arcdroid_core)
  target_compile_options(rpc_selection_test PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG;-Wall;-Wextra;-Wpedantic>)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(rpc_selection_test PRIVATE arcdroid_sanitizer_link)
  endif()
  add_test(NAME rpc-selection-matrix COMMAND rpc_selection_test)
  add_executable(rpc_inventory_test tests/rpc_inventory_test.c tests/rpc_inventory_scan.c libretro/arc_io.c)
  target_include_directories(rpc_inventory_test PRIVATE libretro)
  target_link_libraries(rpc_inventory_test PRIVATE arcdroid_core)
  target_compile_options(rpc_inventory_test PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG;-Wall;-Wextra;-Wpedantic>)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(rpc_inventory_test PRIVATE arcdroid_sanitizer_compile arcdroid_sanitizer_link)
  endif()
  add_test(NAME rpc-firmware-inventory COMMAND rpc_inventory_test)
  add_executable(rpc_selection_load_test tests/rpc_selection_load_test.c)
  target_include_directories(rpc_selection_load_test PRIVATE core libretro/include)
  target_link_libraries(rpc_selection_load_test PRIVATE arcdroid_core ${CMAKE_DL_LIBS})
  target_compile_options(rpc_selection_load_test PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG;-Wall;-Wextra;-Wpedantic>)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(rpc_selection_load_test PRIVATE arcdroid_sanitizer_compile arcdroid_sanitizer_link)
  endif()
  add_dependencies(rpc_selection_load_test test-fixtures)
  add_test(NAME rpc-selection-load-preparation
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_selection_load_test.py"
      $<TARGET_FILE:rpc_selection_load_test> $<TARGET_FILE:arcdroid_libretro_test>
      "${FIXTURE_DIR}/arcdroid-test.rom" "${CMAKE_CURRENT_BINARY_DIR}/rpc-selection-load-data")
  # Reuse configured Ninja flags (including sanitizer and Windows link flags),
  # rebuilding only copied sources into temporary binaries, never build objects.
  if(CMAKE_GENERATOR STREQUAL "Ninja")
    foreach(mode IN ITEMS policy inventory integration)
      if(mode STREQUAL "policy")
        set(rpc_mutation_target rpc_selection_test)
      elseif(mode STREQUAL "inventory")
        set(rpc_mutation_target rpc_inventory_test)
      else()
        set(rpc_mutation_target arcdroid_libretro_test)
      endif()
      add_test(NAME rpc-selection-mutations-${mode}
        COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_selection_mutants.py"
          --build "${CMAKE_CURRENT_BINARY_DIR}" --mode ${mode}
          --target $<TARGET_FILE:${rpc_mutation_target}>
          --load $<TARGET_FILE:rpc_selection_load_test> --rom "${FIXTURE_DIR}/arcdroid-test.rom")
      set_tests_properties(rpc-selection-mutations-${mode} PROPERTIES TIMEOUT 600)
    endforeach()
  endif()
  add_executable(arc_io_tests tests/arc_io_tests.c libretro/arc_io.c)
  target_include_directories(arc_io_tests PRIVATE libretro libretro/include)
  target_compile_options(arc_io_tests PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG;-Wall;-Wextra;-Wpedantic>)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(arc_io_tests PRIVATE
      arcdroid_sanitizer_compile arcdroid_sanitizer_link)
  endif()
  add_test(NAME arc-io-write-guards COMMAND arc_io_tests)
  add_executable(jfd_store_tests
    tests/jfd_store_tests.c libretro/arc_io.c libretro/jfd_store.c)
  target_include_directories(jfd_store_tests PRIVATE
    core libretro libretro/include)
  target_link_libraries(jfd_store_tests PRIVATE arcdroid_core)
  target_compile_options(jfd_store_tests PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG;-Wall;-Wextra;-Wpedantic>)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(jfd_store_tests PRIVATE
      arcdroid_sanitizer_compile arcdroid_sanitizer_link)
  endif()
  add_dependencies(jfd_store_tests test-fixtures)
  add_test(NAME jfd-guard-wd COMMAND jfd_controller_tests "${FIXTURE_DIR}" --guard-wd)
  add_test(NAME jfd-guard-newio COMMAND jfd_controller_tests "${FIXTURE_DIR}" --guard-newio)
  add_test(NAME jfd-guard-store COMMAND jfd_store_tests "${FIXTURE_DIR}" guard)
  add_test(NAME jfd-tab-store COMMAND jfd_store_tests "${FIXTURE_DIR}" tab)
  add_test(NAME jfd-store COMMAND jfd_store_tests "${FIXTURE_DIR}")
  add_test(NAME jfd-file-data-store COMMAND jfd_store_tests "${FIXTURE_DIR}" file-data)
  add_test(NAME native-writable-layout COMMAND jfd_store_tests
    "${FIXTURE_DIR}" native-writable)
  add_test(NAME jfd-gate-short COMMAND jfd_store_tests "${FIXTURE_DIR}" short)
  add_test(NAME jfd-gate-irregular COMMAND jfd_store_tests "${FIXTURE_DIR}" irregular)
  add_test(NAME jfd-gate-timing COMMAND jfd_store_tests "${FIXTURE_DIR}" timing)
  add_dependencies(jfd_controller_tests test-fixtures)
  add_executable(jfd_identification_tests tests/jfd_identification_tests.c)
  target_include_directories(jfd_identification_tests PRIVATE core)
  target_link_libraries(jfd_identification_tests PRIVATE arcdroid_core)
  target_compile_options(jfd_identification_tests PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG;-Wall;-Wextra;-Wpedantic>)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(jfd_identification_tests PRIVATE
      arcdroid_sanitizer_compile arcdroid_sanitizer_link)
  endif()
  add_dependencies(jfd_identification_tests test-fixtures)
  add_test(NAME jfd-identification COMMAND jfd_identification_tests "${FIXTURE_DIR}")
  add_executable(jfd_file_data_tests tests/jfd_file_data_tests.c)
  target_include_directories(jfd_file_data_tests PRIVATE core)
  target_link_libraries(jfd_file_data_tests PRIVATE arcdroid_core)
  target_compile_options(jfd_file_data_tests PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG;-Wall;-Wextra;-Wpedantic>)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(jfd_file_data_tests PRIVATE
      arcdroid_sanitizer_compile arcdroid_sanitizer_link)
  endif()
  add_dependencies(jfd_file_data_tests test-fixtures)
  add_test(NAME jfd-file-data COMMAND jfd_file_data_tests "${FIXTURE_DIR}")
  add_test(NAME jfd-file-data-newio COMMAND jfd_controller_tests
    "${FIXTURE_DIR}" --file-data-newio)
  add_executable(jfd_quarantine_tests tests/jfd_quarantine_tests.c)
  target_include_directories(jfd_quarantine_tests PRIVATE core)
  target_link_libraries(jfd_quarantine_tests PRIVATE arcdroid_core)
  target_compile_options(jfd_quarantine_tests PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG;-Wall;-Wextra;-Wpedantic>)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(jfd_quarantine_tests PRIVATE
      arcdroid_sanitizer_compile arcdroid_sanitizer_link)
  endif()
  add_dependencies(jfd_quarantine_tests test-fixtures)
  add_test(NAME jfd-quarantine COMMAND jfd_quarantine_tests "${FIXTURE_DIR}")
  add_test(NAME core-services COMMAND core_tests "${FIXTURE_DIR}")
  add_test(NAME launch-ramdisc-unit COMMAND core_tests "${FIXTURE_DIR}" --launch-ramdisc)
  add_test(NAME short-tail-unit COMMAND core_tests "${FIXTURE_DIR}" --short-tail)
  add_test(NAME short-raw-wd COMMAND jfd_controller_tests "${FIXTURE_DIR}" --short-raw-wd)
  add_test(NAME short-raw-newio COMMAND jfd_controller_tests "${FIXTURE_DIR}" --short-raw-newio)
  add_test(NAME jfd-controller-golden COMMAND jfd_controller_tests
    "${FIXTURE_DIR}" --wd1770)
  add_test(NAME newio-controller-golden COMMAND jfd_controller_tests
    "${FIXTURE_DIR}" --82c711)
  set_tests_properties(jfd-controller-golden PROPERTIES TIMEOUT 300)
  set_tests_properties(newio-controller-golden PROPERTIES TIMEOUT 300)
  set_tests_properties(core-services PROPERTIES TIMEOUT 300)

  if(WIN32)
    # Native RetroArch launch probes use generated test ROMs;
    # test-only OS gating and probes remain outside the shipping DLL.
    arcdroid_add_libretro(arcdroid_libretro_test FALSE arcdroid_core_test)
    target_compile_definitions(arcdroid_libretro_test PRIVATE ARCDROID_TEST_FAULTS=1)
    target_sources(arcdroid_libretro_test PRIVATE tests/hd_product_seed.c)
    set_target_properties(arcdroid_libretro_test PROPERTIES OUTPUT_NAME arcdroid_libretro_test)
    add_executable(windows_load_harness tests/windows_load_harness.c)
    target_include_directories(windows_load_harness PRIVATE libretro/include)
    target_compile_options(windows_load_harness PRIVATE $<$<C_COMPILER_ID:GNU,Clang>:-UNDEBUG>)
    add_dependencies(windows_load_harness arcdroid_libretro arcdroid_libretro_test test-fixtures)
    add_test(NAME windows-load-harness COMMAND windows_load_harness
      $<TARGET_FILE:arcdroid_libretro>
      "${FIXTURE_DIR}/arcdroid-test.rom"
      "${FIXTURE_DIR}/arcdroid-test.adf"
      "${CMAKE_CURRENT_BINARY_DIR}/windows-shipping-harness-data")
    foreach(tab_core IN ITEMS shipping test)
      if(tab_core STREQUAL "shipping")
        set(tab_core_target arcdroid_libretro)
      else()
        set(tab_core_target arcdroid_libretro_test)
      endif()
      add_test(NAME windows-jfd-tab-options-${tab_core} COMMAND windows_load_harness
        $<TARGET_FILE:${tab_core_target}>
        "${FIXTURE_DIR}/arcdroid-test.rom"
        "${FIXTURE_DIR}/arcdroid-test.adf"
        "${CMAKE_CURRENT_BINARY_DIR}/windows-jfd-tab-options-${tab_core}-data")
    endforeach()
    foreach(tab_case IN ITEMS load-reset guest-write persistence hide-restore)
      add_test(NAME windows-jfd-tab-${tab_case} COMMAND windows_load_harness
        $<TARGET_FILE:arcdroid_libretro_test>
        "${FIXTURE_DIR}/arcdroid-jfd-save-test.rom"
        "${FIXTURE_DIR}/jfd-tab/captured-3.jfd"
        "${CMAKE_CURRENT_BINARY_DIR}/windows-jfd-tab-${tab_case}-data"
        --jfd-tab-${tab_case})
    endforeach()
    add_test(NAME windows-test-core-load-harness COMMAND windows_load_harness
      $<TARGET_FILE:arcdroid_libretro_test>
      "${FIXTURE_DIR}/arcdroid-test.rom"
      "${FIXTURE_DIR}/arcdroid-test.adf"
      "${CMAKE_CURRENT_BINARY_DIR}/windows-harness-data"
      "${FIXTURE_DIR}/adffs/synthetic.zip"
      "${FIXTURE_DIR}/adffs/eligible.jfd")
  else()
    # Fault injection and engine test probes live only in this non-installed
    # host test core; release/RetroArch artifacts use arcdroid_libretro above.
    arcdroid_add_libretro(arcdroid_libretro_test FALSE arcdroid_core_test)
    target_compile_definitions(arcdroid_libretro_test PRIVATE ARCDROID_TEST_FAULTS=1)
    target_sources(arcdroid_libretro_test PRIVATE tests/hd_product_seed.c)
    set_target_properties(arcdroid_libretro_test PROPERTIES OUTPUT_NAME arcdroid_libretro_test)
    add_executable(libretro_harness tests/libretro_harness.c)
    target_include_directories(libretro_harness PRIVATE libretro libretro/include)
    target_include_directories(libretro_harness PRIVATE core engine "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_link_libraries(libretro_harness PRIVATE arcdroid_core ${CMAKE_DL_LIBS})
    if(ARCDROID_SANITIZERS)
      target_link_libraries(libretro_harness PRIVATE
        arcdroid_sanitizer_compile arcdroid_sanitizer_link)
    endif()
    target_compile_options(libretro_harness PRIVATE $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG>)
    add_test(NAME input-source-retirement COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_input_source_retirement.py"
      "${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME libretro-parser-self-test COMMAND libretro_harness --self-test-parser)
    add_dependencies(libretro_harness test-fixtures arcdroid_libretro_test)
    add_test(NAME short-tail-guest COMMAND libretro_harness
      $<TARGET_FILE:arcdroid_libretro_test>
      "${FIXTURE_DIR}/arcdroid-short-tail-wd.rom"
      "${FIXTURE_DIR}/short-family/800k-d-one-sector.adf"
      "${CMAKE_CURRENT_BINARY_DIR}/short-tail-guest-data"
      --short-tail-guest)
    add_test(NAME short-tail-newio-guest COMMAND libretro_harness
      $<TARGET_FILE:arcdroid_libretro_test>
      "${FIXTURE_DIR}/arcdroid-short-tail-wd.rom"
      "${FIXTURE_DIR}/short-family/800k-d-one-sector.adf"
      "${CMAKE_CURRENT_BINARY_DIR}/short-tail-newio-guest-data"
      --short-tail-newio-guest)
    add_test(NAME short-tail-payload-guest COMMAND libretro_harness
      $<TARGET_FILE:arcdroid_libretro_test>
      "${FIXTURE_DIR}/arcdroid-short-tail-wd.rom"
      "${FIXTURE_DIR}/short-family/discovery-payload-tail.adf"
      "${CMAKE_CURRENT_BINARY_DIR}/short-tail-payload-guest-data"
      --short-tail-guest)
    add_test(NAME short-tail-discovery COMMAND libretro_harness
      $<TARGET_FILE:arcdroid_libretro_test>
      "${FIXTURE_DIR}/arcdroid-test.rom"
      "${FIXTURE_DIR}/short-family/discovery-directory-tail.adf"
      "${CMAKE_CURRENT_BINARY_DIR}/short-tail-discovery-data"
      --short-tail-discovery
      "${FIXTURE_DIR}/short-family/discovery-directory-full.adf")
    add_test(NAME native-hd-guest-process COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/native_hd_guest_process_test.py"
      $<TARGET_FILE:libretro_harness>
      $<TARGET_FILE:arcdroid_libretro_test>
      "${FIXTURE_DIR}/arcdroid-native-hd-guest.rom"
      "${FIXTURE_DIR}/hd-family/valid-f.adf"
      "${FIXTURE_DIR}/arcdroid-test.adf")
    set_tests_properties(native-hd-guest-process PROPERTIES TIMEOUT 900)
    add_test(NAME native-hd-stress-process COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/native_hd_stress_process_test.py"
      $<TARGET_FILE:libretro_harness>
      $<TARGET_FILE:arcdroid_libretro_test>
      "${FIXTURE_DIR}/arcdroid-native-hd-guest.rom"
      "${FIXTURE_DIR}/arcdroid-native-hd-stress.rom"
      "${FIXTURE_DIR}/hd-family/valid-f.adf")
    set_tests_properties(native-hd-stress-process PROPERTIES TIMEOUT 900)
    add_test(NAME helper-stress-harness COMMAND libretro_harness
      $<TARGET_FILE:arcdroid_libretro_test>
      "${FIXTURE_DIR}/arcdroid-test.rom"
      "${FIXTURE_DIR}/arcdroid-test.adf"
      "${CMAKE_CURRENT_BINARY_DIR}/helper-stress-data"
      "${FIXTURE_DIR}/arcdroid-media-test.rom"
      "${FIXTURE_DIR}/arcdroid-media.m3u"
      "${FIXTURE_DIR}/arcdroid-media-disc1.adf"
      "${FIXTURE_DIR}/arcdroid-media-disc2.adf"
      "${FIXTURE_DIR}/arcdroid-test.jfd"
      "${FIXTURE_DIR}/arcdroid-test-two-member.jfd"
      "${FIXTURE_DIR}"
      "${FIXTURE_DIR}/arcdroid-application-l.adl"
      "${FIXTURE_DIR}/adffs/synthetic.zip"
      "${FIXTURE_DIR}/adffs/eligible.jfd"
      "${FIXTURE_DIR}/zip-content" --helper-stress)
    add_test(NAME left-stick-unit COMMAND core_tests "${FIXTURE_DIR}" --left-stick)
    foreach(stick_case IN ITEMS probe a3010 error chooser)
      set(stick_rom "${FIXTURE_DIR}/arcdroid-test.rom")
      set(stick_content "${FIXTURE_DIR}/arcdroid-test.adf")
      if(stick_case STREQUAL "error")
        set(stick_rom "${FIXTURE_DIR}/arcdroid-error-box.rom")
      elseif(stick_case STREQUAL "chooser")
        set(stick_rom "${FIXTURE_DIR}/arcdroid-chooser-test.rom")
        set(stick_content "${FIXTURE_DIR}/arcdroid-chooser-l.adl")
      endif()
      add_test(NAME left-stick-${stick_case}-harness COMMAND libretro_harness
        $<TARGET_FILE:arcdroid_libretro_test> "${stick_rom}" "${stick_content}"
        "${CMAKE_CURRENT_BINARY_DIR}/left-stick-${stick_case}-data"
        --left-stick-${stick_case})
    endforeach()
    add_test(NAME left-stick-equivalence-process COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/input_trace_test.py"
      $<TARGET_FILE:libretro_harness> $<TARGET_FILE:arcdroid_libretro_test>
      "${FIXTURE_DIR}")
    add_test(NAME left-stick-input-trace COMMAND libretro_harness
      $<TARGET_FILE:arcdroid_libretro_test>
      "${FIXTURE_DIR}/arcdroid-test.rom" "${FIXTURE_DIR}/arcdroid-test.adf"
      "${CMAKE_CURRENT_BINARY_DIR}/left-stick-trace-data"
      --input-trace "${CMAKE_CURRENT_BINARY_DIR}/left-stick-input.trace")
    add_test(NAME input-layout-harness COMMAND libretro_harness
      $<TARGET_FILE:arcdroid_libretro_test>
      "${FIXTURE_DIR}/arcdroid-test.rom"
      "${FIXTURE_DIR}/arcdroid-test.adf"
      "${CMAKE_CURRENT_BINARY_DIR}/input-layout-data"
      --input-layout-probe)
    add_test(NAME error-box-rom COMMAND libretro_harness
      $<TARGET_FILE:arcdroid_libretro_test>
      "${FIXTURE_DIR}/arcdroid-error-box.rom"
      "${FIXTURE_DIR}/arcdroid-test.adf"
      "${CMAKE_CURRENT_BINARY_DIR}/error-box-rom-data"
      --error-box-probe)
    add_test(NAME error-box-x-rom COMMAND libretro_harness
      $<TARGET_FILE:arcdroid_libretro_test>
      "${FIXTURE_DIR}/arcdroid-error-box-x.rom"
      "${FIXTURE_DIR}/arcdroid-test.adf"
      "${CMAKE_CURRENT_BINARY_DIR}/error-box-x-rom-data"
      --error-box-probe)
    foreach(signal IN ITEMS a b)
      add_test(NAME disc-request-${signal}-rom COMMAND libretro_harness
        $<TARGET_FILE:arcdroid_libretro_test>
        "${FIXTURE_DIR}/arcdroid-disc-request-${signal}.rom"
        "${FIXTURE_DIR}/disc-request/Set.m3u"
        "${CMAKE_CURRENT_BINARY_DIR}/disc-request-${signal}-data"
        --disc-request-${signal})
      foreach(mode IN ITEMS hint off)
        add_test(NAME disc-request-${signal}-${mode}-rom COMMAND libretro_harness
          $<TARGET_FILE:arcdroid_libretro_test>
          "${FIXTURE_DIR}/arcdroid-disc-request-${signal}.rom"
          "${FIXTURE_DIR}/disc-request/Set.m3u"
          "${CMAKE_CURRENT_BINARY_DIR}/disc-request-${signal}-${mode}-data"
          --disc-request-${signal}-${mode})
      endforeach()
    endforeach()
    foreach(scenario IN ITEMS a1 a-chain a-back b-back b-found b-found-between b-alternate b-single b-distinct b-wimp b-two-contexts b-block b-otherdisc
                              b-dir-load b-cat-dir b-d6 b-108d3 b-error-branch b-nonx-error b-conflict b-open-file-dir)
      set(request_playlist "${FIXTURE_DIR}/disc-request/Set.m3u")
      if(scenario STREQUAL "a-chain")
        set(request_playlist "${FIXTURE_DIR}/disc-request/Chain.m3u")
      elseif(scenario STREQUAL "b-distinct")
        set(request_playlist "${FIXTURE_DIR}/disc-request/Distinct.m3u")
      endif()
      add_test(NAME disc-request-${scenario}-rom COMMAND libretro_harness
        $<TARGET_FILE:arcdroid_libretro_test>
        "${FIXTURE_DIR}/arcdroid-disc-request-${scenario}.rom"
        "${request_playlist}"
        "${CMAKE_CURRENT_BINARY_DIR}/disc-request-${scenario}-data"
        --disc-request-${scenario})
    endforeach()
    add_test(NAME disc-request-b-duplicate-rom COMMAND libretro_harness
      $<TARGET_FILE:arcdroid_libretro_test>
      "${FIXTURE_DIR}/arcdroid-disc-request-b.rom"
      "${FIXTURE_DIR}/disc-request/Chain.m3u"
      "${CMAKE_CURRENT_BINARY_DIR}/disc-request-b-duplicate-data"
      --disc-request-b-duplicate)
    add_test(NAME disc-request-b-unknown-rom COMMAND libretro_harness
      $<TARGET_FILE:arcdroid_libretro_test>
      "${FIXTURE_DIR}/arcdroid-disc-request-b.rom"
      "${FIXTURE_DIR}/disc-request/Unknown.m3u"
      "${CMAKE_CURRENT_BINARY_DIR}/disc-request-b-unknown-data"
      --disc-request-b-unknown)
    add_test(NAME disc-request-a-unknown-rom COMMAND libretro_harness
      $<TARGET_FILE:arcdroid_libretro_test>
      "${FIXTURE_DIR}/arcdroid-disc-request-a.rom"
      "${FIXTURE_DIR}/disc-request/AUnknown.m3u"
      "${CMAKE_CURRENT_BINARY_DIR}/disc-request-a-unknown-data"
      --disc-request-a-unknown)
    add_test(NAME disc-request-b-32-rom COMMAND libretro_harness
      $<TARGET_FILE:arcdroid_libretro_test>
      "${FIXTURE_DIR}/arcdroid-disc-request-b.rom"
      "${FIXTURE_DIR}/disc-request/Set32.m3u"
      "${CMAKE_CURRENT_BINARY_DIR}/disc-request-b-32-data"
      --disc-request-b-32)
    add_test(NAME disc-request-effective-process COMMAND libretro_harness
      $<TARGET_FILE:arcdroid_libretro_test>
      "${FIXTURE_DIR}/arcdroid-disc-request-b.rom"
      "${FIXTURE_DIR}/disc-request/Set.m3u"
      "${CMAKE_CURRENT_BINARY_DIR}/disc-request-effective-data"
      --disc-request-effective)
    foreach(case IN ITEMS effective bound)
      set(jfd_playlist "${FIXTURE_DIR}/disc-request/SetJfd.m3u")
      if(case STREQUAL "bound")
        set(jfd_playlist "${FIXTURE_DIR}/disc-request/Set32Jfd.m3u")
      endif()
      add_test(NAME disc-request-${case}-jfd-process COMMAND libretro_harness
        $<TARGET_FILE:arcdroid_libretro_test>
        "${FIXTURE_DIR}/arcdroid-disc-request-b.rom"
        "${jfd_playlist}"
        "${CMAKE_CURRENT_BINARY_DIR}/disc-request-${case}-jfd-data"
        --disc-request-${case}-jfd)
    endforeach()
    add_test(NAME disc-request-perf-process COMMAND libretro_harness
      $<TARGET_FILE:arcdroid_libretro_test>
      "${FIXTURE_DIR}/arcdroid-disc-request-b.rom"
      "${FIXTURE_DIR}/disc-request/SetPerf.m3u"
      "${CMAKE_CURRENT_BINARY_DIR}/disc-request-perf-data"
      --disc-request-perf)
    add_test(NAME disc-request-idle-trace COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_disc_idle_trace.py"
      $<TARGET_FILE:libretro_harness> $<TARGET_FILE:arcdroid_libretro_test>
      "${FIXTURE_DIR}/arcdroid-test.rom"
      "${FIXTURE_DIR}/arcdroid-test.adf"
      "${CMAKE_CURRENT_BINARY_DIR}/disc-request-idle-trace-data")
    add_test(NAME disc-request-b-found-perf-rom COMMAND libretro_harness
      $<TARGET_FILE:arcdroid_libretro_test>
      "${FIXTURE_DIR}/arcdroid-disc-request-b-found.rom"
      "${FIXTURE_DIR}/disc-request/SetPerf.m3u"
      "${CMAKE_CURRENT_BINARY_DIR}/disc-request-b-found-perf-data"
      --disc-request-b-found-perf)
    add_test(NAME disc-request-b-log-limit-rom COMMAND libretro_harness
      $<TARGET_FILE:arcdroid_libretro_test>
      "${FIXTURE_DIR}/arcdroid-disc-request-b.rom"
      "${FIXTURE_DIR}/disc-request/Set.m3u"
      "${CMAKE_CURRENT_BINARY_DIR}/disc-request-b-log-limit-data"
      --disc-request-b-log-limit)
    add_test(NAME disc-request-b-found-between-fail-rom COMMAND libretro_harness
      $<TARGET_FILE:arcdroid_libretro_test>
      "${FIXTURE_DIR}/arcdroid-disc-request-b-found-between.rom"
      "${FIXTURE_DIR}/disc-request/Set.m3u"
      "${CMAKE_CURRENT_BINARY_DIR}/disc-request-b-found-between-fail-data"
      --disc-request-b-found-between-fail)
    add_test(NAME disc-request-path-cache-32-process COMMAND libretro_harness
      $<TARGET_FILE:arcdroid_libretro_test>
      "${FIXTURE_DIR}/arcdroid-disc-request-b.rom"
      "${FIXTURE_DIR}/disc-request/Set32Jfd.m3u"
      "${CMAKE_CURRENT_BINARY_DIR}/disc-request-path-cache-32-data"
      --disc-request-path-cache-32)
    add_test(NAME disc-request-b-first-log-limit-rom COMMAND libretro_harness
      $<TARGET_FILE:arcdroid_libretro_test>
      "${FIXTURE_DIR}/arcdroid-disc-request-b.rom"
      "${FIXTURE_DIR}/disc-request/Set.m3u"
      "${CMAKE_CURRENT_BINARY_DIR}/disc-request-b-first-log-limit-data"
      --disc-request-b-first-log-limit)
    add_test(NAME disc-request-b-busy-retry-rom COMMAND libretro_harness
      $<TARGET_FILE:arcdroid_libretro_test>
      "${FIXTURE_DIR}/arcdroid-disc-request-b.rom"
      "${FIXTURE_DIR}/disc-request/Set.m3u"
      "${CMAKE_CURRENT_BINARY_DIR}/disc-request-b-busy-retry-data"
      --disc-request-b-busy-retry)
    add_test(NAME disc-request-commit-checks COMMAND libretro_harness
      $<TARGET_FILE:arcdroid_libretro_test>
      "${FIXTURE_DIR}/arcdroid-disc-request-b.rom"
      "${FIXTURE_DIR}/disc-request/Set.m3u"
      "${CMAKE_CURRENT_BINARY_DIR}/disc-request-commit-checks-data"
      --disc-request-commit-checks)
    foreach(preflight IN ITEMS unreadable store machine)
      set(preflight_playlist "${FIXTURE_DIR}/disc-request/Set.m3u")
      if(preflight STREQUAL "store")
        set(preflight_playlist "${FIXTURE_DIR}/disc-request/SetJfd.m3u")
      elseif(preflight STREQUAL "machine")
        set(preflight_playlist "${FIXTURE_DIR}/disc-request/SetHigh.m3u")
      endif()
      add_test(NAME disc-request-preflight-real-${preflight} COMMAND libretro_harness
        $<TARGET_FILE:arcdroid_libretro_test>
        "${FIXTURE_DIR}/arcdroid-disc-request-b.rom"
        "${preflight_playlist}"
        "${CMAKE_CURRENT_BINARY_DIR}/disc-request-preflight-real-${preflight}-data"
        --disc-request-preflight-real-${preflight})
    endforeach()
    foreach(scenario IN ITEMS a-manual-reset a-manual-cancel a-box-ready
                              a-outcome-cancel b-preflight-fail b-insert-fail b-adffs b-newio)
      set(request_rom "a")
      if(scenario MATCHES "^b-")
        set(request_rom "b")
      endif()
      add_test(NAME disc-request-${scenario}-rom COMMAND libretro_harness
        $<TARGET_FILE:arcdroid_libretro_test>
        "${FIXTURE_DIR}/arcdroid-disc-request-${request_rom}.rom"
        "${FIXTURE_DIR}/disc-request/Set.m3u"
        "${CMAKE_CURRENT_BINARY_DIR}/disc-request-${scenario}-data"
        --disc-request-${scenario})
    endforeach()
    foreach(action IN ITEMS eject index insert)
      add_test(NAME disc-request-b-action-${action}-rom COMMAND libretro_harness
        $<TARGET_FILE:arcdroid_libretro_test>
        "${FIXTURE_DIR}/arcdroid-disc-request-b.rom"
        "${FIXTURE_DIR}/disc-request/Set.m3u"
        "${CMAKE_CURRENT_BINARY_DIR}/disc-request-b-action-${action}-data"
        --disc-request-b-action-${action})
    endforeach()
    foreach(mode IN ITEMS missing zero)
      add_test(NAME disc-request-a-short-${mode}-rom COMMAND libretro_harness
        $<TARGET_FILE:arcdroid_libretro_test>
        "${FIXTURE_DIR}/arcdroid-disc-request-a.rom"
        "${FIXTURE_DIR}/disc-request/SetShort.m3u"
        "${CMAKE_CURRENT_BINARY_DIR}/disc-request-a-short-${mode}-data"
        --disc-request-a-short-${mode})
    endforeach()
    foreach(arbitration_case IN ITEMS buffer mouse after)
      set(arbitration_rom_suffix "")
      if(NOT arbitration_case STREQUAL "buffer")
        set(arbitration_rom_suffix "-${arbitration_case}")
      endif()
      add_test(NAME error-box-arbitration-${arbitration_case} COMMAND libretro_harness
        $<TARGET_FILE:arcdroid_libretro_test>
        "${FIXTURE_DIR}/arcdroid-error-box-arbitration${arbitration_rom_suffix}.rom"
        "${FIXTURE_DIR}/arcdroid-test.adf"
        "${CMAKE_CURRENT_BINARY_DIR}/error-box-arbitration-${arbitration_case}-data"
        --error-box-arbitration-${arbitration_case})
    endforeach()
    add_test(NAME rpc-arculator-reset-poweron COMMAND "${Python3_EXECUTABLE}" -B
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/arculator_reset_test.py"
      $<TARGET_FILE:libretro_harness> $<TARGET_FILE:arcdroid_libretro_test>
      "${FIXTURE_DIR}/arcdroid-test.rom" "${FIXTURE_DIR}/arcdroid-test.adf")
    add_test(NAME libretro-harness COMMAND libretro_harness
      $<TARGET_FILE:arcdroid_libretro_test>
      "${FIXTURE_DIR}/arcdroid-test.rom"
      "${FIXTURE_DIR}/arcdroid-test.adf"
      "${CMAKE_CURRENT_BINARY_DIR}/harness-data"
      "${FIXTURE_DIR}/arcdroid-media-test.rom"
      "${FIXTURE_DIR}/arcdroid-media.m3u"
      "${FIXTURE_DIR}/arcdroid-media-disc1.adf"
      "${FIXTURE_DIR}/arcdroid-media-disc2.adf"
      "${FIXTURE_DIR}/arcdroid-test.jfd"
      "${FIXTURE_DIR}/arcdroid-test-two-member.jfd"
      "${FIXTURE_DIR}"
      "${FIXTURE_DIR}/arcdroid-application-l.adl"
      "${FIXTURE_DIR}/adffs/synthetic.zip"
      "${FIXTURE_DIR}/adffs/eligible.jfd"
      "${FIXTURE_DIR}/zip-content" --cached-frame-probe)
    add_test(NAME power-on-shift-harness COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_power_on_shift.py"
      $<TARGET_FILE:libretro_harness> $<TARGET_FILE:arcdroid_libretro_test>
      "${FIXTURE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/power-on-shift-data"
      "${FIXTURE_DIR}/adffs/eligible.jfd" "${FIXTURE_DIR}/adffs/synthetic.zip")
    set_tests_properties(power-on-shift-harness PROPERTIES TIMEOUT 900)
    add_test(NAME cmos-guest-exerciser COMMAND libretro_harness
      $<TARGET_FILE:arcdroid_libretro_test>
      "${FIXTURE_DIR}/arcdroid-cmos-exerciser.rom"
      "${FIXTURE_DIR}/arcdroid-test.adf"
      "${CMAKE_CURRENT_BINARY_DIR}/cmos-guest-data" --cmos-exerciser)
    add_test(NAME power-on-cmos-guest COMMAND "${CMAKE_COMMAND}" -E env
      ARCDROID_TEST_POWER_ON_SHIFT=1 $<TARGET_FILE:libretro_harness>
      $<TARGET_FILE:arcdroid_libretro_test>
      "${FIXTURE_DIR}/arcdroid-power-on-cmos-guest.rom"
      "${FIXTURE_DIR}/arcdroid-test.adf"
      "${CMAKE_CURRENT_BINARY_DIR}/power-on-cmos-guest-data"
      --power-on-cmos-guest "${FIXTURE_DIR}/arcdroid-test.rom")
    foreach(tab_case IN ITEMS guest routing set evidence)
      add_test(NAME jfd-tab-${tab_case}-process COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/jfd_tab_process_test.py"
        $<TARGET_FILE:libretro_harness> $<TARGET_FILE:arcdroid_libretro_test>
        "${FIXTURE_DIR}" ${tab_case})
      set_tests_properties(jfd-tab-${tab_case}-process PROPERTIES TIMEOUT 1800)
    endforeach()
    add_test(NAME jfd-writes-libretro-harness COMMAND libretro_harness
      $<TARGET_FILE:arcdroid_libretro_test>
      "${FIXTURE_DIR}/arcdroid-jfd-save-test.rom"
      "${FIXTURE_DIR}/jfd-writes/rw-d.jfd"
      "${CMAKE_CURRENT_BINARY_DIR}/jfd-writes-harness-data"
      --jfd-writes "${FIXTURE_DIR}/jfd-writes")
    add_test(NAME jfd-file-data-process COMMAND libretro_harness
      $<TARGET_FILE:arcdroid_libretro_test>
      "${FIXTURE_DIR}/arcdroid-test.rom"
      "${FIXTURE_DIR}/jfd-file-data/d-terminal.jfd"
      "${CMAKE_CURRENT_BINARY_DIR}/jfd-file-data-process-data"
      --jfd-file-data-step)
    add_test(NAME jfd-file-data-set-process COMMAND libretro_harness
      $<TARGET_FILE:arcdroid_libretro_test>
      "${FIXTURE_DIR}/arcdroid-test.rom"
      "${FIXTURE_DIR}/jfd-file-data/mixed.m3u"
      "${CMAKE_CURRENT_BINARY_DIR}/jfd-file-data-set-data"
      --jfd-file-data-set)
    add_test(NAME jfd-file-data-unavailable-process COMMAND libretro_harness
      $<TARGET_FILE:arcdroid_libretro_test>
      "${FIXTURE_DIR}/arcdroid-test.rom"
      "${FIXTURE_DIR}/jfd-file-data/d-terminal-rw.jfd"
      "${CMAKE_CURRENT_BINARY_DIR}/jfd-file-data-unavailable-data"
      --jfd-file-data-unavailable)
    add_test(NAME jfd-file-data-reasons-process COMMAND libretro_harness
      $<TARGET_FILE:arcdroid_libretro_test>
      "${FIXTURE_DIR}/arcdroid-test.rom"
      "${FIXTURE_DIR}/jfd-file-data/d-terminal.jfd"
      "${CMAKE_CURRENT_BINARY_DIR}/jfd-file-data-reasons-data"
      --jfd-file-data-reasons "${FIXTURE_DIR}/jfd-file-data")
    add_test(NAME jfd-file-data-recovery-process COMMAND libretro_harness
      $<TARGET_FILE:arcdroid_libretro_test>
      "${FIXTURE_DIR}/arcdroid-boot-prompt.rom"
      "${FIXTURE_DIR}/jfd-file-data/d-recovery.jfd"
      "${CMAKE_CURRENT_BINARY_DIR}/jfd-file-data-recovery-data"
      --jfd-file-data-recovery)
    add_test(NAME jfd-file-data-gzip-process COMMAND libretro_harness
      $<TARGET_FILE:arcdroid_libretro_test>
      "${FIXTURE_DIR}/arcdroid-test.rom"
      "${FIXTURE_DIR}/jfd-file-data/d-terminal-gzip.jfd"
      "${CMAKE_CURRENT_BINARY_DIR}/jfd-file-data-gzip-data"
      --jfd-file-data-step)
    add_test(NAME jfd-file-data-zip-process COMMAND libretro_harness
      $<TARGET_FILE:arcdroid_libretro_test>
      "${FIXTURE_DIR}/arcdroid-test.rom"
      "${FIXTURE_DIR}/jfd-file-data/d-terminal.zip"
      "${CMAKE_CURRENT_BINARY_DIR}/jfd-file-data-zip-data"
      --jfd-file-data-step)
    add_test(NAME jfd-quarantine-wd-guest COMMAND libretro_harness
      $<TARGET_FILE:arcdroid_libretro_test>
      "${FIXTURE_DIR}/arcdroid-jfd-quarantine.rom"
      "${FIXTURE_DIR}/jfd-family/quarantine-guest.jfd"
      "${CMAKE_CURRENT_BINARY_DIR}/jfd-quarantine-wd-data"
      --jfd-quarantine-wd)
    add_test(NAME jfd-quarantine-newio-guest COMMAND libretro_harness
      $<TARGET_FILE:arcdroid_libretro_test>
      "${FIXTURE_DIR}/arcdroid-jfd-quarantine.rom"
      "${FIXTURE_DIR}/jfd-family/quarantine-guest.jfd"
      "${CMAKE_CURRENT_BINARY_DIR}/jfd-quarantine-newio-data"
      --jfd-quarantine-newio)
    add_test(NAME jfd-quarantine-zip-guest COMMAND libretro_harness
      $<TARGET_FILE:arcdroid_libretro_test>
      "${FIXTURE_DIR}/arcdroid-jfd-quarantine.rom"
      "${FIXTURE_DIR}/jfd-family/quarantine-guest.zip"
      "${CMAKE_CURRENT_BINARY_DIR}/jfd-quarantine-zip-data"
      --jfd-quarantine-wd)
    add_test(NAME jfd-quarantine-density COMMAND libretro_harness
      $<TARGET_FILE:arcdroid_libretro_test>
      "${FIXTURE_DIR}/arcdroid-test.rom"
      "${FIXTURE_DIR}/jfd-family/quarantine-quad.jfd"
      "${CMAKE_CURRENT_BINARY_DIR}/jfd-quarantine-density-data"
      --jfd-quarantine-density
      "${FIXTURE_DIR}/jfd-family/quarantine-quad-usable.jfd")
    add_test(NAME jfd-quarantine-adffs COMMAND libretro_harness
      $<TARGET_FILE:arcdroid_libretro_test>
      "${FIXTURE_DIR}/arcdroid-test.rom"
      "${FIXTURE_DIR}/adffs/quarantine-adffs.jfd"
      "${CMAKE_CURRENT_BINARY_DIR}/jfd-quarantine-adffs-data"
      --jfd-quarantine-adffs
      "${FIXTURE_DIR}/adffs/synthetic.zip")
    add_test(NAME jfd-creation-process COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/jfd_creation_process_test.py"
      $<TARGET_FILE:libretro_harness>
      $<TARGET_FILE:arcdroid_libretro_test> "${FIXTURE_DIR}")
    add_test(NAME jfd-merge-process COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/jfd_merge_process_test.py"
      $<TARGET_FILE:libretro_harness>
      $<TARGET_FILE:arcdroid_libretro_test> "${FIXTURE_DIR}")
    add_test(NAME jfd-guard-process COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/jfd_guard_process_test.py"
      $<TARGET_FILE:libretro_harness> $<TARGET_FILE:arcdroid_libretro_test> "${FIXTURE_DIR}")
    add_test(NAME jfd-allocation-process COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/jfd_allocation_process_test.py"
      $<TARGET_FILE:libretro_harness>
      $<TARGET_FILE:arcdroid_libretro_test> "${FIXTURE_DIR}")
    add_test(NAME jfd-identification-process COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/jfd_identification_process_test.py"
      $<TARGET_FILE:libretro_harness>
      $<TARGET_FILE:arcdroid_libretro_test> "${FIXTURE_DIR}")
    add_test(NAME newio-libretro-harness COMMAND libretro_harness
      $<TARGET_FILE:arcdroid_libretro_test>
      "${FIXTURE_DIR}/arcdroid-test.rom"
      "${FIXTURE_DIR}/arcdroid-test.adf"
      "${CMAKE_CURRENT_BINARY_DIR}/newio-harness-data"
      --newio-smoke
      "${FIXTURE_DIR}/arcdroid-media.m3u"
      "${FIXTURE_DIR}/arcdroid-media-disc1.adf"
      "${FIXTURE_DIR}/arcdroid-media-disc2.adf"
      "${FIXTURE_DIR}/arcdroid-application-l.adl"
      "${FIXTURE_DIR}/hd-family/valid-f.adf" --cached-frame-probe)
    add_test(NAME mouse-script-harness COMMAND libretro_harness
      $<TARGET_FILE:arcdroid_libretro_test>
      "${FIXTURE_DIR}/arcdroid-test.rom"
      "${FIXTURE_DIR}/arcdroid-test.adf"
      "${CMAKE_CURRENT_BINARY_DIR}/mouse-script-data"
      --real-boot --osk-script "${CMAKE_CURRENT_SOURCE_DIR}/tests/mouse-script.txt")
    foreach(case_name IN ITEMS prompt cli cli-then-prompt desktop)
      if(case_name STREQUAL "prompt" OR case_name STREQUAL "cli-then-prompt")
        set(boot_expected helper)
      elseif(case_name STREQUAL "cli")
        set(boot_expected expired)
      else()
        set(boot_expected disabled)
      endif()
      add_test(NAME boot-watch-${case_name}-harness
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_boot_watch.py"
          $<TARGET_FILE:libretro_harness> $<TARGET_FILE:arcdroid_libretro_test>
          "${FIXTURE_DIR}/arcdroid-boot-${case_name}.rom"
          "${FIXTURE_DIR}/launch-family/boot-file-3-l.adl"
          "${CMAKE_CURRENT_BINARY_DIR}/boot-watch-${case_name}-data" "${boot_expected}")
    endforeach()
    add_test(NAME boot-watch-write-harness
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_boot_watch.py"
        $<TARGET_FILE:libretro_harness> $<TARGET_FILE:arcdroid_libretro_test>
        "${FIXTURE_DIR}/arcdroid-boot-prompt-write.rom"
        "${FIXTURE_DIR}/launch-family/boot-file-3-l.adl"
        "${CMAKE_CURRENT_BINARY_DIR}/boot-watch-write-data" write)
    add_test(NAME boot-watch-chooser-harness
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_boot_watch.py"
        $<TARGET_FILE:libretro_harness> $<TARGET_FILE:arcdroid_libretro_test>
        "${FIXTURE_DIR}/arcdroid-boot-prompt.rom"
        "${FIXTURE_DIR}/launch-family/boot-matrix-3-file-multiple.adf"
        "${CMAKE_CURRENT_BINARY_DIR}/boot-watch-chooser-data" chooser)
    add_test(NAME boot-watch-none-harness
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_boot_watch.py"
        $<TARGET_FILE:libretro_harness> $<TARGET_FILE:arcdroid_libretro_test>
        "${FIXTURE_DIR}/arcdroid-boot-prompt.rom"
        "${FIXTURE_DIR}/launch-family/boot-matrix-3-file-zero.adf"
        "${CMAKE_CURRENT_BINARY_DIR}/boot-watch-none-data" none)
    add_test(NAME boot-watch-publish-fail-harness
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_boot_watch.py"
        $<TARGET_FILE:libretro_harness> $<TARGET_FILE:arcdroid_libretro_test>
        "${FIXTURE_DIR}/arcdroid-boot-prompt.rom"
        "${FIXTURE_DIR}/launch-family/boot-file-3-l.adl"
        "${CMAKE_CURRENT_BINARY_DIR}/boot-watch-publish-fail-data" publish-fail)
    add_test(NAME boot-watch-publish-fail-chooser-harness
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_boot_watch.py"
        $<TARGET_FILE:libretro_harness> $<TARGET_FILE:arcdroid_libretro_test>
        "${FIXTURE_DIR}/arcdroid-boot-prompt.rom"
        "${FIXTURE_DIR}/launch-family/boot-matrix-3-file-multiple.adf"
        "${CMAKE_CURRENT_BINARY_DIR}/boot-watch-publish-fail-chooser-data"
        publish-fail-chooser)
    add_test(NAME boot-watch-input-harness
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_boot_watch.py"
        $<TARGET_FILE:libretro_harness> $<TARGET_FILE:arcdroid_libretro_test>
        "${FIXTURE_DIR}/arcdroid-boot-prompt.rom"
        "${FIXTURE_DIR}/launch-family/boot-file-3-l.adl"
        "${CMAKE_CURRENT_BINARY_DIR}/boot-watch-input-data" input-cancel)
    add_test(NAME power-on-input-cancel-harness
      COMMAND "${CMAKE_COMMAND}" -E env ARCDROID_TEST_POWER_ON_SHIFT=1
        "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_boot_watch.py"
        $<TARGET_FILE:libretro_harness> $<TARGET_FILE:arcdroid_libretro_test>
        "${FIXTURE_DIR}/arcdroid-boot-prompt.rom"
        "${FIXTURE_DIR}/launch-family/boot-file-3-l.adl"
        "${CMAKE_CURRENT_BINARY_DIR}/power-on-input-cancel-data" shift-cancel)
    foreach(launch_setting IN ITEMS off explicit)
      add_test(NAME boot-watch-${launch_setting}-harness
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_boot_watch.py"
          $<TARGET_FILE:libretro_harness> $<TARGET_FILE:arcdroid_libretro_test>
          "${FIXTURE_DIR}/arcdroid-boot-prompt.rom"
          "${FIXTURE_DIR}/launch-family/boot-file-3-l.adl"
          "${CMAKE_CURRENT_BINARY_DIR}/boot-watch-${launch_setting}-data"
          ${launch_setting})
    endforeach()
    add_test(NAME boot-watch-reset-harness
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_boot_watch.py"
        $<TARGET_FILE:libretro_harness> $<TARGET_FILE:arcdroid_libretro_test>
        "${FIXTURE_DIR}/arcdroid-boot-prompt.rom"
        "${FIXTURE_DIR}/launch-family/boot-file-3-l.adl"
          "${CMAKE_CURRENT_BINARY_DIR}/boot-watch-reset-data" reset)
    add_test(NAME boot-observation-harness
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_boot_observation.py"
        "$<TARGET_FILE:libretro_harness>" "$<TARGET_FILE:arcdroid_libretro_test>"
        "${FIXTURE_DIR}/arcdroid-boot-prompt.rom"
        "${FIXTURE_DIR}/arcdroid-boot-cli-then-prompt.rom"
        "${FIXTURE_DIR}/launch-family/boot-file-3-l.adl"
        "${CMAKE_CURRENT_BINARY_DIR}/boot-observation-data")
    add_test(NAME boot-observation-hd
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_boot_observation_hd.py"
        "$<TARGET_FILE:libretro_harness>" "$<TARGET_FILE:arcdroid_libretro_test>"
        "${FIXTURE_DIR}/arcdroid-boot-prompt.rom"
        "${FIXTURE_DIR}/launch-family/boot-file-3-l.adl")
    add_test(NAME power-on-recovery-harness
      COMMAND "${CMAKE_COMMAND}" -E env ARCDROID_TEST_POWER_ON_SHIFT=1
        "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_boot_observation.py"
        "$<TARGET_FILE:libretro_harness>" "$<TARGET_FILE:arcdroid_libretro_test>"
        "${FIXTURE_DIR}/arcdroid-power-on-boot-prompt.rom"
        "${FIXTURE_DIR}/arcdroid-power-on-boot-cli-then-prompt.rom"
        "${FIXTURE_DIR}/launch-family/boot-file-3-l.adl"
        "${CMAKE_CURRENT_BINARY_DIR}/power-on-recovery-data")
    add_test(NAME launch-ramdisc-harness
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_launch_ramdisc.py"
        "$<TARGET_FILE:libretro_harness>" "$<TARGET_FILE:arcdroid_libretro_test>"
        "${FIXTURE_DIR}/arcdroid-boot-prompt.rom"
        "${FIXTURE_DIR}/launch-family/boot-file-3-l.adl"
        "${FIXTURE_DIR}/launch-family/boot-matrix-3-file-multiple.adf"
        "${FIXTURE_DIR}/launch-family/boot-matrix-0-directory-multiple.adf"
        "${FIXTURE_DIR}/launch-family/boot-matrix-0-file-one.adf"
        "${CMAKE_CURRENT_BINARY_DIR}/launch-ramdisc-data")
    add_test(NAME boot-observation-two-phase-harness
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_boot_two_phase.py"
        "$<TARGET_FILE:libretro_harness>" "$<TARGET_FILE:arcdroid_libretro_test>"
        "${FIXTURE_DIR}/arcdroid-boot-prompt.rom"
        "${FIXTURE_DIR}/arcdroid-boot-two-phase.rom"
        "${FIXTURE_DIR}/launch-family/boot-file-3-l.adl"
        "${CMAKE_CURRENT_BINARY_DIR}/boot-observation-two-phase-data")
    add_test(NAME boot-observation-chooser-harness
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_boot_observation_chooser.py"
        "$<TARGET_FILE:libretro_harness>" "$<TARGET_FILE:arcdroid_libretro_test>"
        "${FIXTURE_DIR}/arcdroid-boot-prompt.rom"
        "${FIXTURE_DIR}/launch-family/boot-matrix-3-file-multiple.adf"
        "${CMAKE_CURRENT_BINARY_DIR}/boot-observation-chooser-data")
    add_test(NAME power-on-chooser-harness
      COMMAND "${CMAKE_COMMAND}" -E env ARCDROID_TEST_POWER_ON_SHIFT=1
        "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_boot_observation_chooser.py"
        "$<TARGET_FILE:libretro_harness>" "$<TARGET_FILE:arcdroid_libretro_test>"
        "${FIXTURE_DIR}/arcdroid-power-on-boot-prompt.rom"
        "${FIXTURE_DIR}/launch-family/boot-matrix-3-file-multiple.adf"
        "${CMAKE_CURRENT_BINARY_DIR}/power-on-chooser-data")
    add_test(NAME boot-observation-jfd-harness
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_boot_observation_jfd.py"
        "$<TARGET_FILE:libretro_harness>" "$<TARGET_FILE:arcdroid_libretro_test>"
        "${FIXTURE_DIR}/arcdroid-boot-prompt.rom"
        "${FIXTURE_DIR}/arcdroid-boot-struct-one-to-several.rom"
        "${FIXTURE_DIR}/launch-family/boot-struct-one-to-several.jfd"
        "${CMAKE_CURRENT_BINARY_DIR}/boot-observation-jfd-data")
    foreach(structural IN ITEMS one-to-several one-to-none none-to-one one-to-renamed sibling)
      if(structural STREQUAL "one-to-several" OR structural STREQUAL "sibling")
        set(structural_action chooser)
      elseif(structural STREQUAL "one-to-none")
        set(structural_action none)
      else()
        set(structural_action helper)
      endif()
      add_test(NAME boot-watch-structure-${structural}-harness
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_boot_structural.py"
          "$<TARGET_FILE:libretro_harness>" "$<TARGET_FILE:arcdroid_libretro_test>"
          "${FIXTURE_DIR}/arcdroid-boot-struct-${structural}.rom"
          "${FIXTURE_DIR}/launch-family/boot-struct-${structural}.adf"
          "${CMAKE_CURRENT_BINARY_DIR}/boot-watch-structure-${structural}-data"
          "${structural_action}")
    endforeach()
    add_test(NAME boot-watch-structure-jfd-harness
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_boot_structural.py"
        "$<TARGET_FILE:libretro_harness>" "$<TARGET_FILE:arcdroid_libretro_test>"
        "${FIXTURE_DIR}/arcdroid-boot-struct-one-to-several.rom"
        "${FIXTURE_DIR}/launch-family/boot-struct-one-to-several.jfd"
        "${CMAKE_CURRENT_BINARY_DIR}/boot-watch-structure-jfd-data" chooser)
    add_test(NAME boot-watch-known-one-launchable-harness
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_boot_watch.py"
        "$<TARGET_FILE:libretro_harness>" "$<TARGET_FILE:arcdroid_libretro_test>"
        "${FIXTURE_DIR}/arcdroid-boot-prompt.rom"
        "${FIXTURE_DIR}/launch-family/boot-known-one-launchable.adf"
        "${CMAKE_CURRENT_BINARY_DIR}/boot-watch-known-one-launchable-data"
        known-one-launchable)
    add_test(NAME boot-watch-adffs-route-harness
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_boot_routes.py"
        "$<TARGET_FILE:libretro_harness>" "$<TARGET_FILE:arcdroid_libretro_test>"
        "${FIXTURE_DIR}/arcdroid-boot-prompt.rom"
        "${FIXTURE_DIR}/adffs/eligible.jfd"
        "${FIXTURE_DIR}/adffs/synthetic.zip"
        "${CMAKE_CURRENT_BINARY_DIR}/boot-watch-adffs-route-data" adffs)
    add_test(NAME boot-watch-confirmation-frame-harness
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_boot_timing.py"
        "$<TARGET_FILE:libretro_harness>" "$<TARGET_FILE:arcdroid_libretro_test>"
        "${FIXTURE_DIR}/arcdroid-boot-cli-confirm.rom"
        "${FIXTURE_DIR}/launch-family/boot-file-3-l.adl"
        "${CMAKE_CURRENT_BINARY_DIR}/boot-watch-confirmation-frame-data" cancel)
    add_test(NAME boot-watch-horizon-edge-harness
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_boot_timing.py"
        "$<TARGET_FILE:libretro_harness>" "$<TARGET_FILE:arcdroid_libretro_test>"
        "${FIXTURE_DIR}/arcdroid-boot-horizon-edge.rom"
        "${FIXTURE_DIR}/launch-family/boot-file-3-l.adl"
        "${CMAKE_CURRENT_BINARY_DIR}/boot-watch-horizon-edge-data" horizon)
    foreach(chooser_action IN ITEMS pick continue)
      add_test(NAME boot-watch-chooser-${chooser_action}-harness
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_boot_watch.py"
          $<TARGET_FILE:libretro_harness> $<TARGET_FILE:arcdroid_libretro_test>
          "${FIXTURE_DIR}/arcdroid-boot-prompt.rom"
          "${FIXTURE_DIR}/launch-family/boot-matrix-3-file-multiple.adf"
          "${CMAKE_CURRENT_BINARY_DIR}/boot-watch-chooser-${chooser_action}-data"
          chooser-${chooser_action})
    endforeach()
    foreach(chooser_action IN ITEMS reset unload reset-fail unload-fail)
      add_test(NAME boot-watch-chooser-${chooser_action}-harness
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_boot_watch.py"
          $<TARGET_FILE:libretro_harness> $<TARGET_FILE:arcdroid_libretro_test>
          "${FIXTURE_DIR}/arcdroid-boot-prompt.rom"
          "${FIXTURE_DIR}/launch-family/boot-matrix-3-file-multiple.adf"
          "${CMAKE_CURRENT_BINARY_DIR}/boot-watch-chooser-${chooser_action}-data"
          chooser-${chooser_action})
    endforeach()
    if(ARCDROID_SANITIZERS)
      # R1 instruments Arculator engine objects too, slowing the full harness.
      set_tests_properties(libretro-harness PROPERTIES TIMEOUT 900)
    else()
      # Lifecycle/media/save stress takes nearly 300 s on loaded hosts. Allow
      # scheduling variance without changing the workload or correctness checks.
      set_tests_properties(libretro-harness PROPERTIES TIMEOUT 900)
    endif()
    set_tests_properties(newio-libretro-harness PROPERTIES TIMEOUT 300)
  endif()

  add_executable(firmware_probe tools/firmware_probe.c)
  target_link_libraries(firmware_probe PRIVATE arcdroid_core)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(firmware_probe PRIVATE arcdroid_sanitizer_link)
  endif()
  add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/rpc_timing_rom.h"
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_timing_rom.py"
      "${CMAKE_CURRENT_BINARY_DIR}/generated/rpc_timing_rom.h"
    DEPENDS tests/rpc_timing_rom.py tests/rpc_timing_oracle.py tests/rpc_hot_dma_rom.py
      tests/rpc_video_rom.py tests/rpc_raster_timeline.py tests/rpc_authored_timeline.py VERBATIM)
  add_executable(rpc_timing_test tests/rpc_timing_test.c
    "${CMAKE_CURRENT_BINARY_DIR}/generated/rpc_timing_rom.h")
  add_executable(rpc_mem_timing_unit tests/rpc_mem_timing_unit.c
    engine/rpc/rpc_mem_timing.c engine/rpc/rpc_scheduler.c)
  target_include_directories(rpc_mem_timing_unit PRIVATE engine/rpc engine/rpcemu/src)
  target_compile_definitions(rpc_mem_timing_unit PRIVATE RELEASE_BUILD=1 ARCDROID_TEST_FAULTS=1)
  target_compile_options(rpc_mem_timing_unit PRIVATE -UNDEBUG
    -include ${CMAKE_CURRENT_SOURCE_DIR}/engine/rpc/rpc_namespace.h)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(rpc_mem_timing_unit PRIVATE arcdroid_sanitizer_compile arcdroid_sanitizer_link)
  endif()
  add_executable(rpc_endpoint_test tests/rpc_endpoint_test.c engine/rpc/rpc_scheduler.c)
  target_include_directories(rpc_endpoint_test PRIVATE engine/rpc engine/rpcemu/src)
  target_compile_definitions(rpc_endpoint_test PRIVATE RELEASE_BUILD=1 ARCDROID_TEST_FAULTS=1)
  target_compile_options(rpc_endpoint_test PRIVATE -UNDEBUG -fno-strict-aliasing
    -include ${CMAKE_CURRENT_SOURCE_DIR}/engine/rpc/rpc_namespace.h)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(rpc_endpoint_test PRIVATE arcdroid_sanitizer_compile arcdroid_sanitizer_link)
  endif()
  add_test(NAME rpc-endpoint-directed COMMAND rpc_endpoint_test)
  add_executable(rpc_counter_profile_test tests/rpc_endpoint_test.c engine/rpc/rpc_scheduler.c engine/rpc/rpc_profile.c)
  target_include_directories(rpc_counter_profile_test PRIVATE engine/rpc engine/rpcemu/src libretro/include)
  target_compile_definitions(rpc_counter_profile_test PRIVATE RELEASE_BUILD=1 ARCDROID_TEST_FAULTS=1 ARCDROID_RPC_PROFILE=1)
  target_compile_options(rpc_counter_profile_test PRIVATE -UNDEBUG -fno-strict-aliasing
    -include ${CMAKE_CURRENT_SOURCE_DIR}/engine/rpc/rpc_namespace.h)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(rpc_counter_profile_test PRIVATE arcdroid_sanitizer_compile arcdroid_sanitizer_link)
  endif()
  add_test(NAME rpc-counter-profile-directed COMMAND rpc_counter_profile_test)
  if(CMAKE_GENERATOR STREQUAL "Ninja" AND NOT ARCDROID_SANITIZERS)
    add_test(NAME rpc-measurement-mutants COMMAND "${Python3_EXECUTABLE}" -B
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_measure_mutants.py" "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(rpc-measurement-mutants PROPERTIES TIMEOUT 900 RUN_SERIAL TRUE)
  endif()

  add_test(NAME rpc-memory-service-boundaries COMMAND rpc_mem_timing_unit)
  add_test(NAME rpc-fetch-memo-eligibility COMMAND rpc_mem_timing_unit --fetch-memo)
  add_test(NAME rpc-memory-hot-video-boundaries COMMAND rpc_mem_timing_unit --hot-video)
  add_test(NAME rpc-qa-command-parsing COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_ninja_commands_test.py")
  if(WIN32)
    add_test(NAME rpc-windows-test-exports COMMAND "${Python3_EXECUTABLE}" -B
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_windows_exports_test.py" "${CMAKE_OBJDUMP}"
      "${CMAKE_CURRENT_SOURCE_DIR}" $<TARGET_FILE:arcdroid_libretro_test>
      $<TARGET_FILE:arcdroid_rpcplan_libretro> $<TARGET_FILE:arcdroid_rpcmeasure_libretro>)
  endif()
  # Mutation kills are checked by the ordinary host/Windows builds. Sanitizer
  # builds retain the unmutated regression tests, without recompiling mutants.
  if(CMAKE_GENERATOR STREQUAL "Ninja" AND NOT ARCDROID_SANITIZERS)
    add_test(NAME rpc-qa-mutations COMMAND "${Python3_EXECUTABLE}" -B
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_qa_mutations.py"
      --build "${CMAKE_CURRENT_BINARY_DIR}" --core $<TARGET_FILE:arcdroid_libretro_test>
      --timing $<TARGET_FILE:rpc_timing_test> --unit $<TARGET_FILE:rpc_mem_timing_unit>
      --video $<TARGET_FILE:rpc_video_test> --store $<TARGET_FILE:rpc_cmos_store_test>
      --fdc $<TARGET_FILE:rpc_fdc_test> --hd $<TARGET_FILE:rpc_hd_store_test>
      --ide $<TARGET_FILE:rpc_ide_test> --runner $<TARGET_FILE:rpc_runner>
      --plan $<TARGET_FILE:arcdroid_rpcplan_libretro> --cpu $<TARGET_FILE:rpc_cpu_test>)
    set_tests_properties(rpc-qa-mutations PROPERTIES TIMEOUT 600 RUN_SERIAL TRUE)
    if(WIN32)
      # Windows CI reached 414 s; allow twice that, rounded up to a minute.
      set_property(TEST rpc-qa-mutations PROPERTY TIMEOUT 840)
    endif()
  endif()
  foreach(rate IN ITEMS 60 99)
    foreach(pacing IN ITEMS 0 1)
      add_test(NAME rpc-memory-long-${rate}-${pacing} COMMAND rpc_mem_timing_unit ${rate} ${pacing})
      set_tests_properties(rpc-memory-long-${rate}-${pacing} PROPERTIES TIMEOUT 1800)
    endforeach()
  endforeach()
  target_include_directories(rpc_timing_test PRIVATE engine engine/rpc core libretro/include
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_compile_definitions(rpc_timing_test PRIVATE ARCDROID_TEST_FAULTS=1 RETRO_IMPORT_SYMBOLS=1)
  target_compile_options(rpc_timing_test PRIVATE -UNDEBUG)
  target_link_libraries(rpc_timing_test PRIVATE ${CMAKE_DL_LIBS})
  if(ARCDROID_SANITIZERS)
    target_link_libraries(rpc_timing_test PRIVATE arcdroid_sanitizer_compile arcdroid_sanitizer_link)
  endif()
  foreach(model RANGE 0 3)
    add_test(NAME rpc-memory-timing-${model} COMMAND rpc_timing_test
      $<TARGET_FILE:arcdroid_libretro_test> ${model})
  endforeach()
  add_test(NAME rpc-endpoint-late-arm COMMAND rpc_timing_test
    $<TARGET_FILE:arcdroid_libretro_test> --late-arm)
  set_tests_properties(rpc-endpoint-late-arm PROPERTIES TIMEOUT 1800)
  add_test(NAME rpc-test-loop-guard COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/rpc_test_loop_compare.py" --self-test)
  add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/rpc_session_rom.h"
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_session_rom.py"
      "${CMAKE_CURRENT_BINARY_DIR}/generated/rpc_session_rom.h"
    DEPENDS tests/rpc_session_rom.py VERBATIM)
  add_executable(rpc_session_test tests/rpc_session_test.c
    "${CMAKE_CURRENT_BINARY_DIR}/generated/rpc_session_rom.h")
  target_include_directories(rpc_session_test PRIVATE engine engine/rpc core
    libretro/include "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_compile_definitions(rpc_session_test PRIVATE ARCDROID_TEST_FAULTS=1 RETRO_IMPORT_SYMBOLS=1)
  target_compile_options(rpc_session_test PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG;-Wall;-Wextra;-Werror=implicit-function-declaration>)
  target_link_libraries(rpc_session_test PRIVATE arcdroid_core ${CMAKE_DL_LIBS})
  add_dependencies(rpc_session_test arcdroid_libretro_test test-fixtures)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(rpc_session_test PRIVATE arcdroid_sanitizer_compile arcdroid_sanitizer_link)
  endif()
  foreach(model RANGE 0 3)
    add_test(NAME rpc-session-guest-${model} COMMAND rpc_session_test
      $<TARGET_FILE:arcdroid_libretro_test> --guest ${model})
  endforeach()
  add_executable(rpc_observer_lifecycle_test tests/rpc_observer_lifecycle_test.c
    "${CMAKE_CURRENT_BINARY_DIR}/generated/rpc_session_rom.h")
  target_include_directories(rpc_observer_lifecycle_test PRIVATE engine engine/rpc core
    libretro/include "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_compile_definitions(rpc_observer_lifecycle_test PRIVATE ARCDROID_TEST_FAULTS=1 RETRO_IMPORT_SYMBOLS=1)
  target_compile_options(rpc_observer_lifecycle_test PRIVATE -UNDEBUG -Wall -Wextra)
  target_link_libraries(rpc_observer_lifecycle_test PRIVATE arcdroid_core ${CMAKE_DL_LIBS})
  add_dependencies(rpc_observer_lifecycle_test arcdroid_libretro_test)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(rpc_observer_lifecycle_test PRIVATE arcdroid_sanitizer_compile arcdroid_sanitizer_link)
  endif()
  add_executable(rpc_observers_frontend_test tests/rpc_observers_frontend_test.c
    "${CMAKE_CURRENT_BINARY_DIR}/generated/rpc_session_rom.h")
  target_include_directories(rpc_observers_frontend_test PRIVATE engine engine/rpc core libretro/include
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_compile_definitions(rpc_observers_frontend_test PRIVATE ARCDROID_TEST_FAULTS=1 RETRO_IMPORT_SYMBOLS=1)
  target_compile_options(rpc_observers_frontend_test PRIVATE -UNDEBUG -Wall -Wextra)
  target_link_libraries(rpc_observers_frontend_test PRIVATE arcdroid_core ${CMAKE_DL_LIBS})
  add_dependencies(rpc_observers_frontend_test arcdroid_rpcplan_libretro test-fixtures)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(rpc_observers_frontend_test PRIVATE arcdroid_sanitizer_compile arcdroid_sanitizer_link)
  endif()
  if(ARCDROID_SANITIZERS)
    # Preserve every case but bound individual Linux sanitizer test times.
    foreach(group error disc-0 disc-1 extended)
      if(group STREQUAL "error")
        set(frontend_test rpc-observers-frontend)
      else()
        set(frontend_test rpc-observers-frontend-${group})
      endif()
      add_test(NAME ${frontend_test} COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_observers_frontend_test.py"
        $<TARGET_FILE:rpc_observers_frontend_test> $<TARGET_FILE:arcdroid_rpcplan_libretro> "${FIXTURE_DIR}" sanitizer-${group})
      set_tests_properties(${frontend_test} PROPERTIES TIMEOUT 300)
    endforeach()
  else()
    add_test(NAME rpc-observers-frontend COMMAND "${Python3_EXECUTABLE}" -B
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_observers_frontend_test.py"
      $<TARGET_FILE:rpc_observers_frontend_test> $<TARGET_FILE:arcdroid_rpcplan_libretro> "${FIXTURE_DIR}" full)
    set_tests_properties(rpc-observers-frontend PROPERTIES TIMEOUT 300)
  endif()
  if(NOT ARCDROID_SANITIZERS AND NOT CMAKE_CROSSCOMPILING AND CMAKE_GENERATOR STREQUAL "Ninja")
    # Keep each native Windows mutant matrix comfortably inside its timeout.
    # All shards validate the full baseline and every focused test profile.
    foreach(shard 0 1 2 3)
      if(shard EQUAL 0)
        set(mutant_test rpc-observers-frontend-mutants)
      else()
        math(EXPR shard_number "${shard} + 1")
        set(mutant_test rpc-observers-frontend-mutants-${shard_number})
      endif()
      add_test(NAME ${mutant_test} COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_observers_mutants.py"
        "${CMAKE_CURRENT_BINARY_DIR}" $<TARGET_FILE:rpc_observers_frontend_test> $<TARGET_FILE:arcdroid_rpcplan_libretro> ${shard})
      set_tests_properties(${mutant_test} PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)
    endforeach()
  endif()
  add_test(NAME rpc-observation-lifecycle COMMAND rpc_observer_lifecycle_test $<TARGET_FILE:arcdroid_libretro_test>)
  set_tests_properties(rpc-observation-lifecycle PROPERTIES TIMEOUT 60)
  add_test(NAME rpc-session-lifecycle COMMAND rpc_session_test
    $<TARGET_FILE:arcdroid_libretro_test> --lifecycle)
  add_executable(rpc_session_hygiene_test tests/rpc_session_hygiene_test.c)
  target_include_directories(rpc_session_hygiene_test PRIVATE engine engine/rpc core libretro libretro/include)
  target_compile_definitions(rpc_session_hygiene_test PRIVATE ARCDROID_TEST_FAULTS=1 RETRO_IMPORT_SYMBOLS=1)
  target_compile_options(rpc_session_hygiene_test PRIVATE -UNDEBUG -Wall -Wextra)
  target_link_libraries(rpc_session_hygiene_test PRIVATE ${CMAKE_DL_LIBS})
  set(RPC_SESSION_HYGIENE_PROFILE full)
  if(ARCDROID_SANITIZERS)
    set(RPC_SESSION_HYGIENE_PROFILE sanitizer)
    target_compile_definitions(rpc_session_hygiene_test PRIVATE ARCDROID_SESSION_SANITIZER_PROFILE=1)
    target_link_libraries(rpc_session_hygiene_test PRIVATE arcdroid_sanitizer_compile arcdroid_sanitizer_link)
  endif()
  add_test(NAME rpc-session-frontend-switch COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_session_hygiene_test.py"
    $<TARGET_FILE:rpc_session_hygiene_test> $<TARGET_FILE:arcdroid_rpcplan_libretro> "${FIXTURE_DIR}" "${RPC_SESSION_HYGIENE_PROFILE}")
  set_tests_properties(rpc-session-frontend-switch PROPERTIES TIMEOUT 900 RUN_SERIAL TRUE)
  set(ARCDROID_EXHAUSTION_SOURCES ${ARCDROID_LIBRETRO_SOURCES})
  list(REMOVE_ITEM ARCDROID_EXHAUSTION_SOURCES libretro/arc_session.c)
  add_executable(rpc_session_exhaustion_test tests/rpc_session_exhaustion_test.c
    ${ARCDROID_EXHAUSTION_SOURCES}
    $<TARGET_OBJECTS:arcdroid_libretro_arculator> $<TARGET_OBJECTS:arcdroid_libretro_rpc>)
  target_include_directories(rpc_session_exhaustion_test PRIVATE engine core libretro platform libretro/include third_party/zlib)
  target_compile_definitions(rpc_session_exhaustion_test PRIVATE RELEASE_BUILD=1)
  target_compile_options(rpc_session_exhaustion_test PRIVATE -UNDEBUG -Wall -Wextra -fno-strict-aliasing)
  target_link_libraries(rpc_session_exhaustion_test PRIVATE arcdroid_core)
  if(UNIX)
    target_link_libraries(rpc_session_exhaustion_test PRIVATE m)
  endif()
  if(ARCDROID_SANITIZERS)
    target_link_libraries(rpc_session_exhaustion_test PRIVATE arcdroid_sanitizer_compile arcdroid_sanitizer_link)
  endif()
  set_property(TARGET rpc_session_exhaustion_test PROPERTY ARCDROID_BUILD_SUBSTITUTIONS "session-id-exhaustion")
  add_test(NAME rpc-session-exhaustion COMMAND rpc_session_exhaustion_test)
  add_executable(rpc_session_id_test tests/rpc_session_id_test.c libretro/arc_session.c)
  target_include_directories(rpc_session_id_test PRIVATE libretro)
  target_compile_options(rpc_session_id_test PRIVATE -UNDEBUG -Wall -Wextra)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(rpc_session_id_test PRIVATE arcdroid_sanitizer_compile arcdroid_sanitizer_link)
  endif()
  add_test(NAME rpc-session-ids COMMAND rpc_session_id_test)
  add_test(NAME rpc-session-source-guard COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_session_source_guard.py" "${CMAKE_CURRENT_SOURCE_DIR}")
  if(NOT ARCDROID_SANITIZERS AND NOT CMAKE_CROSSCOMPILING AND CMAKE_GENERATOR STREQUAL "Ninja")
    add_test(NAME rpc-session-hygiene-mutants COMMAND "${Python3_EXECUTABLE}" -B
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_session_mutants.py"
      "${CMAKE_CURRENT_BINARY_DIR}" $<TARGET_FILE:rpc_session_hygiene_test>
      $<TARGET_FILE:arcdroid_rpcplan_libretro> "${FIXTURE_DIR}"
      $<TARGET_FILE:rpc_session_test> $<TARGET_FILE:rpc_video_test> $<TARGET_FILE:rpc_observers_frontend_test>)
    set_tests_properties(rpc-session-hygiene-mutants PROPERTIES TIMEOUT 900 RUN_SERIAL TRUE)
  endif()
  add_test(NAME rpc-session-switch COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_session_switch_test.py"
    $<TARGET_FILE:rpc_session_test> $<TARGET_FILE:arcdroid_libretro_test>
    "${FIXTURE_DIR}/arcdroid-test.rom")
  add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/rpc_video_rom.h"
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_video_rom.py"
      "${CMAKE_CURRENT_BINARY_DIR}/generated/rpc_video_rom.h"
    DEPENDS tests/rpc_video_rom.py tests/rpc_raster_timeline.py tests/rpc_authored_timeline.py VERBATIM)
  add_executable(rpc_video_test tests/rpc_video_test.c
    "${CMAKE_CURRENT_BINARY_DIR}/generated/rpc_video_rom.h"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/rpc_video_paths.h")
  add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/rpc_video_paths.h"
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_video_paths.py"
      --header "${CMAKE_CURRENT_BINARY_DIR}/generated/rpc_video_paths.h"
    DEPENDS tests/rpc_video_paths.py tests/rpc_video_rom.py VERBATIM)
  target_include_directories(rpc_video_test PRIVATE engine engine/rpc core
    libretro/include "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_compile_definitions(rpc_video_test PRIVATE ARCDROID_TEST_FAULTS=1 RETRO_IMPORT_SYMBOLS=1)
  target_compile_options(rpc_video_test PRIVATE $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG;-Wall;-Wextra>)
  target_link_libraries(rpc_video_test PRIVATE arcdroid_core ${CMAKE_DL_LIBS})
  add_dependencies(rpc_video_test arcdroid_libretro_test)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(rpc_video_test PRIVATE arcdroid_sanitizer_compile arcdroid_sanitizer_link)
  endif()
  # Included by native Windows CTest and CI's rpc-.* sanitizer selection.
  foreach(model RANGE 0 3)
    add_test(NAME rpc-video-paths-${model} COMMAND "${Python3_EXECUTABLE}" -B
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_video_trace_test.py" $<TARGET_FILE:rpc_video_test>
      $<TARGET_FILE:arcdroid_libretro_test> ${model}
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/rpc-video-paths-${model}.trace" --paths)
    add_test(NAME rpc-video-pixel-clock-${model} COMMAND rpc_video_test
      $<TARGET_FILE:arcdroid_libretro_test> --pixel-clock ${model})
    add_test(NAME rpc-video-extreme-${model} COMMAND rpc_video_test
      $<TARGET_FILE:arcdroid_libretro_test> --extreme ${model})
    add_test(NAME rpc-fault-silence-${model} COMMAND rpc_video_test
      $<TARGET_FILE:arcdroid_libretro_test> --fault-silence ${model})
    add_test(NAME rpc-video-scroll-${model} COMMAND rpc_video_test
      $<TARGET_FILE:arcdroid_libretro_test> --scroll ${model})
    add_test(NAME rpc-video-guest-${model} COMMAND rpc_video_test
      $<TARGET_FILE:arcdroid_libretro_test> --guest ${model})
    add_test(NAME rpc-video-trace-${model} COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_video_trace_test.py" $<TARGET_FILE:rpc_video_test>
      $<TARGET_FILE:arcdroid_libretro_test> ${model}
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/rpc-video-${model}.trace")
  endforeach()
  foreach(model RANGE 2 3)
    add_test(NAME rpc-video-contention-${model} COMMAND rpc_video_test
      $<TARGET_FILE:arcdroid_libretro_test> --contention ${model})
  endforeach()
  add_test(NAME rpc-video-frontend COMMAND rpc_video_test
    $<TARGET_FILE:arcdroid_libretro_test> --frontend 2)

  # D2 tests are in native Windows CTest and the rpc-.* sanitizer selection.
  add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/rpc_d2_rom.h"
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_d2_rom.py"
      "${CMAKE_CURRENT_BINARY_DIR}/generated/rpc_d2_rom.h"
    DEPENDS tests/rpc_d2_rom.py VERBATIM)
  add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/rpc_d2_expected.h"
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_d2_expectations.py"
      --header "${CMAKE_CURRENT_BINARY_DIR}/generated/rpc_d2_expected.h"
    DEPENDS tests/rpc_d2_expectations.py tests/rpc_d2_goldens.py tests/rpc_d2_rom.py tests/rpc_authored_timeline.py VERBATIM)
  add_executable(rpc_d2_test tests/rpc_d2_test.c
    "${CMAKE_CURRENT_BINARY_DIR}/generated/rpc_d2_rom.h"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/rpc_d2_expected.h")
  target_include_directories(rpc_d2_test PRIVATE engine engine/rpc core libretro/include
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_compile_definitions(rpc_d2_test PRIVATE ARCDROID_TEST_FAULTS=1 RETRO_IMPORT_SYMBOLS=1)
  target_compile_options(rpc_d2_test PRIVATE $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG;-Wall;-Wextra>)
  target_link_libraries(rpc_d2_test PRIVATE arcdroid_core ${CMAKE_DL_LIBS})
  add_dependencies(rpc_d2_test arcdroid_libretro_test)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(rpc_d2_test PRIVATE arcdroid_sanitizer_compile arcdroid_sanitizer_link)
  endif()
  foreach(model RANGE 0 3)
    add_test(NAME rpc-d2-guest-${model} COMMAND "${Python3_EXECUTABLE}" -B
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_d2_expectations.py"
      $<TARGET_FILE:rpc_d2_test> $<TARGET_FILE:arcdroid_libretro_test> --guest ${model}
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/rpc-d2-${model}.trace")
  endforeach()
  foreach(model RANGE 2 3)
    add_test(NAME rpc-factory-capture-${model} COMMAND "${Python3_EXECUTABLE}" -B
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_d2_expectations.py"
      $<TARGET_FILE:rpc_d2_test> $<TARGET_FILE:arcdroid_libretro_test> --factory ${model}
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/rpc-factory-${model}.trace")
  endforeach()

  add_test(NAME rpc-shipping-route COMMAND rpc_session_test
    $<TARGET_FILE:arcdroid_libretro> --shipping)

  if(TARGET rpc_runner)
  set(RPC_PLAN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/rpc-plans")
  add_custom_command(OUTPUT "${RPC_PLAN_DIR}/matrix-0.arcplan" "${RPC_PLAN_DIR}/ac8.arcplan" "${RPC_PLAN_DIR}/hot.arcplan"
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_plan_fixtures.py" "${RPC_PLAN_DIR}"
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tools/rpc_ac8_workload.py" "${RPC_PLAN_DIR}"
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tools/rpc_hot_workload.py" "${RPC_PLAN_DIR}"
    DEPENDS tests/rpc_plan_fixtures.py tests/rpc_plan_matrix_rom.py tests/rpc_session_rom.py
      tests/rpc_video_rom.py tests/rpc_video_paths.py tests/rpc_d2_rom.py tools/rpc_ac8_workload.py tools/rpc_hot_workload.py tests/rpc_timing_rom.py tests/rpc_timing_oracle.py VERBATIM)
  add_custom_target(rpc-plan-fixtures ALL DEPENDS "${RPC_PLAN_DIR}/matrix-0.arcplan" "${RPC_PLAN_DIR}/ac8.arcplan" "${RPC_PLAN_DIR}/hot.arcplan")
  add_dependencies(rpc_runner rpc-plan-fixtures)
  foreach(mode IN ITEMS admission matrix-0 matrix-1 matrix-2 matrix-3 av-0 av-1 av-2 av-3)
    add_test(NAME rpc-plan-${mode} COMMAND "${Python3_EXECUTABLE}" -B
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_plan_test.py" $<TARGET_FILE:rpc_runner>
      $<TARGET_FILE:arcdroid_rpcplan_libretro> $<TARGET_FILE:arcdroid_libretro> "${RPC_PLAN_DIR}" ${mode})
  endforeach()
  add_test(NAME rpc-keyboard-held-reset COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_keyboard_reset_test.py"
    $<TARGET_FILE:rpc_runner> $<TARGET_FILE:arcdroid_rpcplan_libretro>)
  set(RPC_FLOPPY_DIFFERENTIAL_ARGS)
  if(TARGET libretro_harness)
    list(APPEND RPC_FLOPPY_DIFFERENTIAL_ARGS $<TARGET_FILE:libretro_harness> $<TARGET_FILE:arcdroid_libretro_test>)
  endif()
  add_test(NAME rpc-floppy-guest COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_floppy_guest_test.py"
    $<TARGET_FILE:rpc_runner> $<TARGET_FILE:arcdroid_rpcplan_libretro> $<TARGET_FILE:arcdroid_libretro>
    "${FIXTURE_DIR}" ${RPC_FLOPPY_DIFFERENTIAL_ARGS})
  set_tests_properties(rpc-floppy-guest PROPERTIES TIMEOUT 1800)
  add_test(NAME rpc-fdc-boot-probe COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_fdc_boot_test.py"
    $<TARGET_FILE:rpc_runner> $<TARGET_FILE:arcdroid_rpcplan_libretro>)
  add_test(NAME rpc-cmos-reset-gate COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_cmos_gate_test.py"
    $<TARGET_FILE:rpc_runner> $<TARGET_FILE:arcdroid_rpcplan_libretro>)
  add_test(NAME rpc-ac8-workload COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_ac8_test.py"
    $<TARGET_FILE:rpc_runner> $<TARGET_FILE:arcdroid_rpcplan_libretro> "${RPC_PLAN_DIR}")
  add_test(NAME rpc-hot-throughput COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_hot_test.py"
    $<TARGET_FILE:rpc_runner> $<TARGET_FILE:arcdroid_rpcplan_libretro> "${RPC_PLAN_DIR}")
  set_tests_properties(rpc-hot-throughput rpc-memory-timing-2 PROPERTIES TIMEOUT 1800)
  set(RPC_OBSERVATION_PROFILE full)
  if(ARCDROID_SANITIZERS)
    set(RPC_OBSERVATION_PROFILE sanitizer)
  endif()
  add_test(NAME rpc-preparation-trace COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_preparation_trace_test.py"
    $<TARGET_FILE:rpc_runner> $<TARGET_FILE:arcdroid_rpcplan_libretro>)
  set_tests_properties(rpc-preparation-trace PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
  add_test(NAME rpc-observation-plan COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_observation_plan_test.py"
    $<TARGET_FILE:rpc_runner> $<TARGET_FILE:arcdroid_rpcplan_libretro>
    $<TARGET_FILE:arcdroid_libretro> "${RPC_PLAN_DIR}" "${RPC_OBSERVATION_PROFILE}")
  set_tests_properties(rpc-observation-plan PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)

  add_test(NAME rpc-runner-dump COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_runner_dump_test.py"
    $<TARGET_FILE:rpc_runner> $<TARGET_FILE:arcdroid_rpcplan_libretro> "${RPC_PLAN_DIR}" "${FIXTURE_DIR}")
  set_tests_properties(rpc-runner-dump PROPERTIES TIMEOUT 180)
  add_test(NAME rpc-runner-timing-window COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_runner_timing_test.py"
    $<TARGET_FILE:rpc_runner> $<TARGET_FILE:arcdroid_rpcplan_libretro> "${RPC_PLAN_DIR}")
  foreach(mode IN ITEMS devices lifecycle factory)
    add_test(NAME rpc-plan-${mode} COMMAND "${Python3_EXECUTABLE}" -B
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_plan_lifecycle_test.py"
      $<TARGET_FILE:rpc_runner> $<TARGET_FILE:arcdroid_rpcplan_libretro> "${RPC_PLAN_DIR}" ${mode})
  endforeach()
  endif()

endif()

# A plan-enabled test core has its own metadata/content contract. The existing
# fault-test core keeps the exact interface asserted by unchanged harnesses.
if(TARGET arcdroid_libretro_test)
  arcdroid_add_libretro(arcdroid_rpcplan_libretro FALSE arcdroid_core_test)
  target_compile_definitions(arcdroid_rpcplan_libretro PRIVATE ARCDROID_TEST_FAULTS=1 ARCDROID_TEST_PLANS=1)
  target_sources(arcdroid_rpcplan_libretro PRIVATE tests/hd_product_seed.c)
  arcdroid_add_libretro(arcdroid_rpcmeasure_libretro FALSE arcdroid_core_test)
  target_compile_definitions(arcdroid_rpcmeasure_libretro PRIVATE ARCDROID_TEST_FAULTS=1
    ARCDROID_TEST_API=1 ARCDROID_TEST_PLANS=1 ARCDROID_RPC_MEASUREMENT=1)
  target_sources(arcdroid_rpcmeasure_libretro PRIVATE tests/hd_product_seed.c)
  add_test(NAME rpc-measurement-core COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_measure_core_test.py" $<TARGET_FILE:rpc_runner>
    $<TARGET_FILE:arcdroid_rpcplan_libretro> $<TARGET_FILE:arcdroid_rpcmeasure_libretro>)
  set_tests_properties(rpc-measurement-core PROPERTIES TIMEOUT 240)
  if(TARGET rpc_observers_frontend_test)
    foreach(observer_core IN ITEMS plan measure)
      add_test(NAME rpc-observers-off-${observer_core} COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_observers_frontend_test.py"
        $<TARGET_FILE:rpc_observers_frontend_test> $<TARGET_FILE:arcdroid_rpc${observer_core}_libretro> "${FIXTURE_DIR}" off)
      set_tests_properties(rpc-observers-off-${observer_core} PROPERTIES TIMEOUT 120)
    endforeach()
  endif()
  add_test(NAME rpc-endpoint-plan-prefix COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_endpoint_plan_test.py" $<TARGET_FILE:rpc_runner>
    $<TARGET_FILE:arcdroid_rpcplan_libretro> "${RPC_PLAN_DIR}")
  set_tests_properties(rpc-endpoint-plan-prefix PROPERTIES TIMEOUT 600)


  foreach(measure_target IN ITEMS arcdroid_rpcmeasure_libretro arcdroid_rpcmeasure_libretro_rpc arcdroid_rpcmeasure_libretro_arculator)
    target_compile_options(${measure_target} PRIVATE $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-fvisibility=hidden>)
  endforeach()
  if(ANDROID)
    set_target_properties(arcdroid_rpcmeasure_libretro PROPERTIES OUTPUT_NAME arcdroid_rpcmeasure_libretro_android)
    target_link_options(arcdroid_rpcmeasure_libretro PRIVATE "-Wl,-z,defs" "-Wl,-z,max-page-size=16384")
  endif()

  if(ARCDROID_RPC_CROSSCHECK)
    target_compile_definitions(arcdroid_rpcplan_libretro_rpc PRIVATE ARCDROID_RPC_CROSSCHECK=1)
    target_sources(arcdroid_rpcplan_libretro_rpc PRIVATE engine/rpc/rpc_crosscheck.c)
    add_executable(rpc_crosscheck_test tests/rpc_crosscheck_test.c engine/rpc/rpc_crosscheck.c)
    target_include_directories(rpc_crosscheck_test PRIVATE engine/rpc libretro/include)
    target_compile_definitions(rpc_crosscheck_test PRIVATE ARCDROID_TEST_FAULTS=1 ARCDROID_RPC_CROSSCHECK=1)
    target_compile_options(rpc_crosscheck_test PRIVATE -UNDEBUG)
    if(ARCDROID_SANITIZERS)
      target_link_libraries(rpc_crosscheck_test PRIVATE arcdroid_sanitizer_compile arcdroid_sanitizer_link)
    endif()
    add_test(NAME rpc-crosscheck-selftest COMMAND "${Python3_EXECUTABLE}" -B
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_crosscheck_test.py" $<TARGET_FILE:rpc_crosscheck_test>)
  endif()
  if(ARCDROID_RPC_PROFILE)
    target_compile_definitions(arcdroid_rpcplan_libretro_rpc PRIVATE ARCDROID_RPC_PROFILE=1)
    target_sources(arcdroid_rpcplan_libretro_rpc PRIVATE engine/rpc/rpc_profile.c)
  endif()
  if(WIN32)
    # Keep matching source locations for the native RetroArch exception
    # locator, without adding debug information to any shipping target.
    foreach(plan_target IN ITEMS arcdroid_rpcplan_libretro
        arcdroid_rpcplan_libretro_arculator arcdroid_rpcplan_libretro_rpc)
      target_compile_options(${plan_target} PRIVATE -g)
    endforeach()
    target_sources(arcdroid_rpcplan_libretro PRIVATE tests/rpc_crash_locator.c)
    target_sources(arcdroid_rpcmeasure_libretro PRIVATE tests/rpc_crash_locator.c)
    add_executable(rpc_crash_locator_test tests/rpc_crash_locator_test.c tests/rpc_crash_locator.c)
    target_include_directories(rpc_crash_locator_test PRIVATE tests libretro/include)
    target_compile_options(rpc_crash_locator_test PRIVATE -UNDEBUG -Wall -Wextra)
    add_test(NAME rpc-windows-crash-locator COMMAND rpc_crash_locator_test
      "${CMAKE_CURRENT_BINARY_DIR}/rpc-crash-locator-data")
  endif()
  arcdroid_configure_core_info(tests/arcdroid_rpcplan_libretro.info arcdroid_rpcplan_libretro.info)
  if(ANDROID)
    set_target_properties(arcdroid_rpcplan_libretro PROPERTIES OUTPUT_NAME arcdroid_rpcplan_libretro_android)
    target_link_options(arcdroid_rpcplan_libretro PRIVATE "-Wl,-z,defs" "-Wl,-z,max-page-size=16384")
  endif()
endif()

# Non-shipping driver for the R3d-2 private Mac/Android VFS cost legs.
if(BUILD_TESTING OR ARCDROID_ANDROID_TEST_CORE)
  add_executable(rpc_firmware_probe tests/rpc_firmware_probe.c libretro/rpc_inventory.c libretro/arc_io.c)
  target_include_directories(rpc_firmware_probe PRIVATE libretro)
  target_link_libraries(rpc_firmware_probe PRIVATE arcdroid_core)
  target_compile_options(rpc_firmware_probe PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG;-Wall;-Wextra;-Wpedantic>)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(rpc_firmware_probe PRIVATE arcdroid_sanitizer_compile arcdroid_sanitizer_link)
  endif()
endif()

if(BUILD_TESTING)
  get_property(arc_core_info_files GLOBAL PROPERTY ARCDROID_CORE_INFO_FILES)
  add_test(NAME rpc-core-info COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/core_info_test.py"
    "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}" ${arc_core_info_files})
  get_property(arc_tests DIRECTORY PROPERTY TESTS)
  set_property(TEST ${arc_tests} APPEND PROPERTY ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
endif()

if(BUILD_TESTING AND (NOT CMAKE_CROSSCOMPILING OR WIN32))
  add_executable(rpc_cmos_store_test tests/rpc_cmos_store_test.c libretro/rpc_cmos_store.c libretro/arc_io.c core/hash.c)
  target_include_directories(rpc_cmos_store_test PRIVATE libretro libretro/include core)
  target_compile_options(rpc_cmos_store_test PRIVATE -UNDEBUG -Wall -Wextra)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(rpc_cmos_store_test PRIVATE arcdroid_sanitizer_compile arcdroid_sanitizer_link)
  endif()
  add_test(NAME rpc-cmos-record-store COMMAND rpc_cmos_store_test)
endif()
if(BUILD_TESTING AND (NOT CMAKE_CROSSCOMPILING OR WIN32))
  add_executable(rpc_cmos_plan_test tests/rpc_cmos_plan_test.c)
  target_include_directories(rpc_cmos_plan_test PRIVATE engine engine/rpc core libretro/include)
  target_compile_definitions(rpc_cmos_plan_test PRIVATE ARCDROID_TEST_FAULTS=1 RETRO_IMPORT_SYMBOLS=1)
  target_compile_options(rpc_cmos_plan_test PRIVATE -UNDEBUG -Wall -Wextra)
  target_link_libraries(rpc_cmos_plan_test PRIVATE ${CMAKE_DL_LIBS})
  if(ARCDROID_SANITIZERS)
    target_link_libraries(rpc_cmos_plan_test PRIVATE arcdroid_sanitizer_compile arcdroid_sanitizer_link)
  endif()
  set(RPC_CMOS_PLAN_PROFILE full)
  add_test(NAME rpc-hd-notices COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_hd_notices_test.py"
    $<TARGET_FILE:rpc_cmos_plan_test> $<TARGET_FILE:arcdroid_rpcplan_libretro>)
  set_tests_properties(rpc-hd-notices PROPERTIES TIMEOUT 180)
  if(ARCDROID_SANITIZERS)
    set(RPC_CMOS_PLAN_PROFILE sanitizer)
  endif()
  foreach(model RANGE 0 3)
    add_test(NAME rpc-cmos-seed-${model} COMMAND "${Python3_EXECUTABLE}" -B
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_cmos_seed_plan_test.py" $<TARGET_FILE:rpc_cmos_plan_test>
      $<TARGET_FILE:arcdroid_rpcplan_libretro> ${model})
    set_tests_properties(rpc-cmos-seed-${model} PROPERTIES TIMEOUT 180)
    foreach(case full ro350 empty scatter incomplete late reset nodelete writer delayed badchecksum fallback terminalfallback)
      add_test(NAME rpc-cmos-plan-${model}-${case} COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_cmos_plan_test.py" $<TARGET_FILE:rpc_cmos_plan_test>
        $<TARGET_FILE:arcdroid_rpcplan_libretro> $<TARGET_FILE:rpc_runner>
        $<TARGET_FILE:arcdroid_libretro> "${FIXTURE_DIR}" ${model} ${case} ${RPC_CMOS_PLAN_PROFILE})
      set_tests_properties(rpc-cmos-plan-${model}-${case} PROPERTIES TIMEOUT 900)
    endforeach()
  endforeach()
endif()

if(BUILD_TESTING AND (NOT CMAKE_CROSSCOMPILING OR WIN32))
  # Same shipping sources/objects, with neither plan nor fault definitions.
  add_executable(rpc_orchestration_test tests/rpc_orchestration_test.c
    ${ARCDROID_LIBRETRO_SOURCES}
    $<TARGET_OBJECTS:arcdroid_libretro_arculator> $<TARGET_OBJECTS:arcdroid_libretro_rpc>)
  target_include_directories(rpc_orchestration_test PRIVATE core engine platform libretro
    libretro/include third_party/zlib tests)
  target_compile_definitions(rpc_orchestration_test PRIVATE RELEASE_BUILD=1)
  target_compile_options(rpc_orchestration_test PRIVATE -UNDEBUG -Wall -Wextra -fno-strict-aliasing)
  target_link_libraries(rpc_orchestration_test PRIVATE arcdroid_core)
  if(UNIX)
    target_link_libraries(rpc_orchestration_test PRIVATE m)
  endif()
  if(ARCDROID_SANITIZERS)
    target_link_libraries(rpc_orchestration_test PRIVATE arcdroid_sanitizer_compile arcdroid_sanitizer_link)
  endif()
  set(RPC_ORCHESTRATION_PROFILE full)
  if(ARCDROID_SANITIZERS)
    set(RPC_ORCHESTRATION_PROFILE sanitizer)
  endif()
  add_test(NAME rpc-session-orchestration COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_orchestration_test.py"
    $<TARGET_FILE:rpc_orchestration_test> "${CMAKE_CURRENT_BINARY_DIR}/tests/fixtures/generated"
    ${RPC_ORCHESTRATION_PROFILE})
  set_tests_properties(rpc-session-orchestration PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
  if(NOT ARCDROID_SANITIZERS)
    if(WIN32)
      set(RPC_ORCHESTRATION_MUTANT_PROFILE mutant-idle)
    else()
      set(RPC_ORCHESTRATION_MUTANT_PROFILE mutant)
    endif()
    add_test(NAME rpc-orchestration-mutants COMMAND "${Python3_EXECUTABLE}" -B
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_orchestration_mutants.py"
      "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}" "${FIXTURE_DIR}" ${RPC_ORCHESTRATION_MUTANT_PROFILE})
    set_tests_properties(rpc-orchestration-mutants PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
    if(WIN32)
      # Windows CI timed out at 300.16 s; twice that rounds up to 660 s.
      set_property(TEST rpc-orchestration-mutants PROPERTY TIMEOUT 660)
    endif()
  endif()
  add_executable(rpc_preparation_test tests/rpc_preparation_test.c libretro/rpc_hd_store.c libretro/install_store.c libretro/arc_io.c)
  target_include_directories(rpc_preparation_test PRIVATE core engine libretro libretro/include)
  target_link_libraries(rpc_preparation_test PRIVATE arcdroid_core)
  target_compile_options(rpc_preparation_test PRIVATE -UNDEBUG -Wall -Wextra)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(rpc_preparation_test PRIVATE arcdroid_sanitizer_compile arcdroid_sanitizer_link)
  endif()
  add_test(NAME rpc-preparation-store COMMAND rpc_preparation_test)
  set_tests_properties(rpc-preparation-store PROPERTIES TIMEOUT 120)
  if(NOT ARCDROID_SANITIZERS)
    add_test(NAME rpc-preparation-mutants COMMAND "${Python3_EXECUTABLE}" -B
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_preparation_mutants.py" "${CMAKE_C_COMPILER}"
      $<TARGET_FILE:arcdroid_core> "${CMAKE_CURRENT_SOURCE_DIR}")
    set_tests_properties(rpc-preparation-mutants PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
  endif()
  add_executable(rpc_hd_store_test tests/rpc_hd_store_test.c libretro/rpc_hd_store.c
    libretro/install_store.c libretro/arc_io.c)
  target_include_directories(rpc_hd_store_test PRIVATE core engine libretro libretro/include)
  target_link_libraries(rpc_hd_store_test PRIVATE arcdroid_core)
  target_compile_definitions(rpc_hd_store_test PRIVATE ARCDROID_HD_TEST_ALLOC=1)
  target_compile_options(rpc_hd_store_test PRIVATE -UNDEBUG -Wall -Wextra)
  add_test(NAME rpc-hd-handle-cleanup COMMAND rpc_hd_store_test handles)
  add_test(NAME rpc-hd-maintenance-steps COMMAND rpc_hd_store_test steps)
  add_executable(rpc_ide_test tests/rpc_ide_test.c engine/rpcemu/src/ide.c
    engine/rpc/rpc_scheduler.c engine/rpc/rpc_timing.c)
  target_include_directories(rpc_ide_test PRIVATE engine/rpc engine/rpcemu/src engine core libretro/include)
  target_compile_options(rpc_ide_test PRIVATE -UNDEBUG -Wall -Wextra
    -include ${CMAKE_CURRENT_SOURCE_DIR}/engine/rpc/rpc_namespace.h)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(rpc_hd_store_test PRIVATE arcdroid_sanitizer_compile arcdroid_sanitizer_link)
    target_link_libraries(rpc_ide_test PRIVATE arcdroid_sanitizer_compile arcdroid_sanitizer_link)
  endif()
  add_executable(rpc_hd_process tests/rpc_hd_process.c libretro/rpc_hd_store.c
    libretro/install_store.c libretro/arc_io.c)
  target_include_directories(rpc_hd_process PRIVATE core engine libretro libretro/include)
  target_link_libraries(rpc_hd_process PRIVATE arcdroid_core)
  target_compile_definitions(rpc_hd_process PRIVATE ARCDROID_HD_TEST_POINTS=1)
  target_compile_options(rpc_hd_process PRIVATE -UNDEBUG -Wall -Wextra)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(rpc_hd_process PRIVATE arcdroid_sanitizer_compile arcdroid_sanitizer_link)
  endif()
  set(RPC_HD_PROCESS_PROFILE full)
  set(RPC_HD_PROCESS_TIMEOUT 3600)
  if(ARCDROID_SANITIZERS)
    set(RPC_HD_PROCESS_PROFILE sanitizer)
    set(RPC_HD_PROCESS_TIMEOUT 900)
  endif()
  add_test(NAME rpc-hd-store-process COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_hd_process_test.py" $<TARGET_FILE:rpc_hd_process>
    --profile "${RPC_HD_PROCESS_PROFILE}")
  set_tests_properties(rpc-hd-store-process PROPERTIES TIMEOUT "${RPC_HD_PROCESS_TIMEOUT}" RUN_SERIAL TRUE)
  add_executable(rpc_boot_test tests/rpc_boot_test.c libretro/rpc_boot.c
    libretro/rpc_boot_zip.c libretro/rpc_hd_store.c libretro/arc_io.c libretro/install_store.c)
  target_include_directories(rpc_boot_test PRIVATE core engine libretro libretro/include third_party/zlib)
  target_link_libraries(rpc_boot_test PRIVATE arcdroid_core arcdroid_zlib)
  target_compile_options(rpc_boot_test PRIVATE -UNDEBUG -Wall -Wextra)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(rpc_boot_test PRIVATE arcdroid_sanitizer_compile arcdroid_sanitizer_link)
  endif()
  add_test(NAME rpc-boot-packages COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_boot_packages_test.py" $<TARGET_FILE:rpc_boot_test>)
  set(RPC_BOOT_FAULTS_PROFILE full)
  set(RPC_BOOT_FAULTS_TIMEOUT 3600)
  if(ARCDROID_SANITIZERS)
    set(RPC_BOOT_FAULTS_PROFILE sanitizer)
    set(RPC_BOOT_FAULTS_TIMEOUT 900)
  elseif(WIN32)
    # Windows CI reached 3355 s; allow twice that, rounded up to a minute.
    set(RPC_BOOT_FAULTS_TIMEOUT 6720)
  endif()
  add_test(NAME rpc-boot-budget COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_boot_budget_test.py" $<TARGET_FILE:rpc_boot_test>
    "${RPC_BOOT_FAULTS_PROFILE}")
  set_tests_properties(rpc-boot-budget PROPERTIES TIMEOUT 600)
  add_test(NAME rpc-boot-faults COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_boot_faults_test.py" $<TARGET_FILE:rpc_boot_test>
    --profile "${RPC_BOOT_FAULTS_PROFILE}")
  # R2c run Q precedent: ordinary builds kill all writer mutants; sanitizers
  # retain the unmutated package regressions and every fault boundary class.
  if(NOT ARCDROID_SANITIZERS)
    add_test(NAME rpc-boot-mutants COMMAND "${Python3_EXECUTABLE}" -B
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_boot_mutants.py" "${CMAKE_C_COMPILER}"
      $<TARGET_FILE:arcdroid_core> $<TARGET_FILE:arcdroid_zlib>
      "${CMAKE_CURRENT_SOURCE_DIR}")
    set_tests_properties(rpc-boot-mutants PROPERTIES TIMEOUT 1800 RUN_SERIAL TRUE)
  endif()
  set_tests_properties(rpc-boot-packages PROPERTIES TIMEOUT 600)
  set_tests_properties(rpc-boot-faults PROPERTIES TIMEOUT "${RPC_BOOT_FAULTS_TIMEOUT}" RUN_SERIAL TRUE)
  add_executable(rpc_hd_formatter_test tests/rpc_hd_formatter_test.c)
  target_include_directories(rpc_hd_formatter_test PRIVATE core)
  target_link_libraries(rpc_hd_formatter_test PRIVATE arcdroid_core)
  target_compile_options(rpc_hd_formatter_test PRIVATE -UNDEBUG)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(rpc_hd_formatter_test PRIVATE arcdroid_sanitizer_compile arcdroid_sanitizer_link)
  endif()
  add_test(NAME rpc-hd-formatter COMMAND rpc_hd_formatter_test)
  add_test(NAME rpc-filecore-check COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_filecore_check_test.py"
    $<TARGET_FILE:rpc_hd_formatter_test>)
  add_test(NAME rpc-filecore-check-mutants COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_filecore_mutants.py"
    $<TARGET_FILE:rpc_hd_formatter_test>)
  add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/rpc_ide_rom.h"
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_ide_rom.py"
      "${CMAKE_CURRENT_BINARY_DIR}/generated/rpc_ide_rom.h"
    DEPENDS tests/rpc_ide_rom.py tests/rpc_session_rom.py VERBATIM)
  add_executable(rpc_ide_guest_test tests/rpc_ide_guest_test.c libretro/rpc_hd_store.c
    libretro/install_store.c libretro/arc_io.c "${CMAKE_CURRENT_BINARY_DIR}/generated/rpc_ide_rom.h")
  target_include_directories(rpc_ide_guest_test PRIVATE core engine engine/rpc libretro
    libretro/include "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_compile_definitions(rpc_ide_guest_test PRIVATE ARCDROID_TEST_FAULTS=1 RETRO_IMPORT_SYMBOLS=1)
  target_compile_options(rpc_ide_guest_test PRIVATE -UNDEBUG)
  target_link_libraries(rpc_ide_guest_test PRIVATE arcdroid_core ${CMAKE_DL_LIBS})
  if(ARCDROID_SANITIZERS)
    target_link_libraries(rpc_ide_guest_test PRIVATE arcdroid_sanitizer_compile arcdroid_sanitizer_link)
  endif()
  add_test(NAME rpc-ide-guest COMMAND rpc_ide_guest_test $<TARGET_FILE:arcdroid_libretro_test>)
  set(RPC_HD_RETROARCH_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/rpc-hd-retroarch")
  add_custom_command(OUTPUT "${RPC_HD_RETROARCH_DIR}/hd.arcplan"
    BYPRODUCTS "${RPC_HD_RETROARCH_DIR}/hd.rom" "${RPC_HD_RETROARCH_DIR}/floppy.adf"
      "${RPC_HD_RETROARCH_DIR}/disk.script"
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_hd_retroarch_rom.py"
      "${RPC_HD_RETROARCH_DIR}"
    DEPENDS tests/rpc_hd_retroarch_rom.py tests/rpc_video_rom.py tests/generate_fixtures.py VERBATIM)
  add_custom_target(rpc-hd-retroarch-fixtures ALL DEPENDS "${RPC_HD_RETROARCH_DIR}/hd.arcplan")
  add_dependencies(rpc_runner rpc-hd-retroarch-fixtures)
  add_test(NAME rpc-hd-retroarch-guest COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_hd_retroarch_test.py"
    $<TARGET_FILE:rpc_runner> $<TARGET_FILE:arcdroid_rpcplan_libretro> "${RPC_HD_RETROARCH_DIR}")
  set_tests_properties(rpc-hd-retroarch-guest PROPERTIES TIMEOUT 600)
  set(RPC_HD_COST_PROFILE)
  if(ARCDROID_SANITIZERS)
    list(APPEND RPC_HD_COST_PROFILE --sanitizer)
  endif()
  add_test(NAME rpc-hd-cost-report COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_hd_cost_test.py"
    $<TARGET_FILE:rpc_runner> $<TARGET_FILE:arcdroid_rpcplan_libretro> "${RPC_HD_RETROARCH_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/check-rpc-hd-cost.py" ${RPC_HD_COST_PROFILE})
  set_tests_properties(rpc-hd-cost-report PROPERTIES TIMEOUT 600)
  add_test(NAME rpc-boot-plan COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_boot_plan_test.py"
    $<TARGET_FILE:rpc_cmos_plan_test> $<TARGET_FILE:arcdroid_rpcplan_libretro>
    $<TARGET_FILE:rpc_runner> $<TARGET_FILE:arcdroid_libretro>
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_ide_rom.py")
  set_tests_properties(rpc-boot-plan PROPERTIES TIMEOUT 600)
  add_test(NAME rpc-hd-plan COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_hd_plan_test.py" $<TARGET_FILE:rpc_cmos_plan_test>
    $<TARGET_FILE:arcdroid_rpcplan_libretro> $<TARGET_FILE:rpc_runner>
    $<TARGET_FILE:arcdroid_libretro> "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_ide_rom.py")
  add_test(NAME rpc-hd-frame-cost COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_hd_plan_test.py" $<TARGET_FILE:rpc_cmos_plan_test>
    $<TARGET_FILE:arcdroid_rpcplan_libretro> $<TARGET_FILE:rpc_runner>
    $<TARGET_FILE:arcdroid_libretro> "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_ide_rom.py" frame)
  add_test(NAME rpc-hd-store COMMAND rpc_hd_store_test)
  add_test(NAME rpc-hd-reconstruction COMMAND rpc_hd_store_test reconstruction)
  add_test(NAME rpc-hd-qa-integrity COMMAND rpc_hd_store_test qa-integrity)
  add_test(NAME rpc-hd-import-roundtrip COMMAND rpc_hd_store_test import-roundtrip)
  add_test(NAME rpc-hd-vfs-conformance COMMAND rpc_hd_store_test vfs)
  add_executable(rpc_vfs_test tests/rpc_vfs_test.c)
  target_include_directories(rpc_vfs_test PRIVATE libretro/include)
  target_compile_options(rpc_vfs_test PRIVATE -UNDEBUG -Wall -Wextra)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(rpc_vfs_test PRIVATE arcdroid_sanitizer_compile arcdroid_sanitizer_link)
  endif()
  add_test(NAME rpc-vfs-conformance COMMAND rpc_vfs_test
    "${CMAKE_CURRENT_BINARY_DIR}/rpc-vfs-conformance-data")
  add_test(NAME rpc-hd-population COMMAND rpc_hd_store_test population)
  set(RPC_HD_MEMORY_FAULTS faults)
  set(RPC_HD_MEMORY_TIMEOUT 3600)
  if(ARCDROID_SANITIZERS)
    set(RPC_HD_MEMORY_FAULTS faults-sanitizer)
    set(RPC_HD_MEMORY_TIMEOUT 900)
  endif()
  add_test(NAME rpc-hd-store-faults COMMAND rpc_hd_store_test "${RPC_HD_MEMORY_FAULTS}")
  set_tests_properties(rpc-hd-store-faults PROPERTIES TIMEOUT "${RPC_HD_MEMORY_TIMEOUT}")
  set(RPC_HD_BOUNDS_PROFILE full)
  if(ARCDROID_SANITIZERS)
    set(RPC_HD_BOUNDS_PROFILE sanitizer)
  endif()
  add_test(NAME rpc-hd-store-bounds COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_hd_bounds_test.py" $<TARGET_FILE:rpc_hd_store_test>
    --profile "${RPC_HD_BOUNDS_PROFILE}")
  set_tests_properties(rpc-hd-store-bounds PROPERTIES TIMEOUT 3600 RUN_SERIAL TRUE)
  add_test(NAME rpc-ide-registers COMMAND rpc_ide_test)
endif()

# D-157: sanitizer CI includes rpc-.*; use the real imported CPU objects.
if(BUILD_TESTING AND NOT ANDROID)
  add_executable(rpc_smlal_test tests/rpc_smlal_test.c $<TARGET_OBJECTS:arcdroid_rpc_cpu>)
  target_include_directories(rpc_smlal_test PRIVATE engine/rpc engine/rpcemu/src "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_compile_options(rpc_smlal_test PRIVATE -UNDEBUG -Wall -Wextra)
  add_dependencies(rpc_smlal_test rpc_cpu_test)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(rpc_smlal_test PRIVATE arcdroid_sanitizer_compile arcdroid_sanitizer_link)
  endif()
  add_test(NAME rpc-cpu-smlal COMMAND rpc_smlal_test)
endif()

# D-155 part (a): independently committable repetition before the fix.
if(BUILD_TESTING AND NOT ANDROID)
  foreach(kind IN ITEMS cpu cmos session)
    add_executable(rpc_jump_repeat_${kind} tests/rpc_jump_repeat_${kind}.c)
    target_compile_options(rpc_jump_repeat_${kind} PRIVATE -UNDEBUG -Wall -Wextra)
    target_include_directories(rpc_jump_repeat_${kind} PRIVATE engine engine/rpc
      engine/rpcemu/src core libretro/include "${CMAKE_CURRENT_BINARY_DIR}/generated")
    if(kind STREQUAL "session")
      target_compile_definitions(rpc_jump_repeat_${kind} PRIVATE ARCDROID_TEST_FAULTS=1 RETRO_IMPORT_SYMBOLS=1)
      if(ARCDROID_SANITIZERS)
        target_compile_definitions(rpc_jump_repeat_${kind} PRIVATE ARCDROID_SANITIZER_PROFILE=1)
      endif()
      target_link_libraries(rpc_jump_repeat_${kind} PRIVATE arcdroid_core ${CMAKE_DL_LIBS})
      add_dependencies(rpc_jump_repeat_${kind} arcdroid_libretro_test rpc_session_test test-fixtures)
      add_test(NAME rpc-windows-jump-repeat-session COMMAND rpc_jump_repeat_${kind}
        $<TARGET_FILE:arcdroid_libretro_test> "${FIXTURE_DIR}/arcdroid-test.rom")
    else()
      target_sources(rpc_jump_repeat_${kind} PRIVATE $<TARGET_OBJECTS:arcdroid_rpc_${kind}>)
      add_dependencies(rpc_jump_repeat_${kind} rpc_${kind}_test)
      add_test(NAME rpc-windows-jump-repeat-${kind} COMMAND rpc_jump_repeat_${kind})
    endif()
    if(ARCDROID_SANITIZERS)
      target_link_libraries(rpc_jump_repeat_${kind} PRIVATE arcdroid_sanitizer_compile arcdroid_sanitizer_link)
    endif()
    set_tests_properties(rpc-windows-jump-repeat-${kind} PROPERTIES TIMEOUT 600)
  endforeach()
endif()

# Capture first-chance faults before main in every non-shipping RPC executable.
# Keep Release optimization/assertion settings: only add matching DWARF, so the
# RVA and module base in stderr map directly using addr2line on the CI artifact.
if(WIN32 AND BUILD_TESTING)
  add_executable(rpc_crash_probe tests/rpc_crash_probe.c)
  target_compile_options(rpc_crash_probe PRIVATE -UNDEBUG)
  add_test(NAME rpc-windows-crash-stderr COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_crash_stderr_test.py" $<TARGET_FILE:rpc_crash_probe>
    "${CMAKE_OBJDUMP}" "${CMAKE_ADDR2LINE}")
  get_property(rpc_diagnostic_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
  foreach(rpc_target IN LISTS rpc_diagnostic_targets)
    get_target_property(rpc_type ${rpc_target} TYPE)
    if(rpc_type STREQUAL "EXECUTABLE" AND
        (rpc_target MATCHES "^rpc_" OR rpc_target STREQUAL "hash_accel_test"))
      target_compile_options(${rpc_target} PRIVATE -g)
      # This lifecycle oracle owns its registrations explicitly.
      if(NOT rpc_target STREQUAL "rpc_crash_locator_test")
        target_sources(${rpc_target} PRIVATE tests/rpc_test_crash.c tests/rpc_crash_locator.c)
        target_include_directories(${rpc_target} PRIVATE tests libretro/include)
      endif()
    elseif(rpc_type STREQUAL "OBJECT_LIBRARY" AND rpc_target MATCHES "^arcdroid_rpc_(cpu|cmos|fdc)$")
      target_compile_options(${rpc_target} PRIVATE -g)
    endif()
  endforeach()
endif()

if(ARCDROID_RPC_PROFILE AND BUILD_TESTING AND (NOT CMAKE_CROSSCOMPILING OR WIN32))
  add_executable(rpc_profile_test tests/rpc_profile_test.c tests/rpc_test_clock.c
    engine/rpc/rpc_profile.c engine/rpc/rpc_observer.c engine/rpc/rpc_inspector.c
    engine/rpc/rpc_mem_timing.c engine/rpc/rpc_scheduler.c
    engine/rpcemu/src/arm.c engine/rpcemu/src/arm_common.c
    engine/rpcemu/src/cp15.c engine/rpcemu/src/mem.c
    "${CMAKE_CURRENT_BINARY_DIR}/generated/rpc_authored_rom.h")
  target_include_directories(rpc_profile_test PRIVATE engine/rpc engine/rpcemu/src
    libretro/include "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_compile_definitions(rpc_profile_test PRIVATE ARCDROID_RPC_PROFILE=1
    ARCDROID_TEST_FAULTS=1 ARCDROID_OBSERVATION_TEST=1 RELEASE_BUILD=1)
  target_compile_options(rpc_profile_test PRIVATE -UNDEBUG -Wall -Wextra -fno-strict-aliasing
    -include "${CMAKE_CURRENT_SOURCE_DIR}/engine/rpc/rpc_namespace.h")
  if(ARCDROID_SANITIZERS)
    target_link_libraries(rpc_profile_test PRIVATE arcdroid_sanitizer_compile arcdroid_sanitizer_link)
  endif()
  if(WIN32)
    target_link_options(rpc_profile_test PRIVATE -static-libgcc)
  endif()
  add_test(NAME rpc-profile-counts COMMAND rpc_profile_test)
  add_executable(rpc_profile_lifecycle_test tests/rpc_profile_lifecycle_test.c
    tests/rpc_test_clock.c tools/rpc_profile_json.c)
  target_include_directories(rpc_profile_lifecycle_test PRIVATE engine engine/rpc core libretro libretro/include tests)
  target_compile_definitions(rpc_profile_lifecycle_test PRIVATE ARCDROID_RPC_PROFILE=1 ARCDROID_TEST_FAULTS=1 RETRO_IMPORT_SYMBOLS=1)
  target_compile_options(rpc_profile_lifecycle_test PRIVATE -UNDEBUG -Wall -Wextra)
  target_link_libraries(rpc_profile_lifecycle_test PRIVATE ${CMAKE_DL_LIBS})
  if(UNIX AND NOT APPLE)
    target_link_libraries(rpc_profile_lifecycle_test PRIVATE m)
  endif()
  if(WIN32)
    target_link_options(rpc_profile_lifecycle_test PRIVATE -static-libgcc)
  endif()
  if(ARCDROID_SANITIZERS)
    target_link_libraries(rpc_profile_lifecycle_test PRIVATE arcdroid_sanitizer_compile arcdroid_sanitizer_link)
  endif()
  # Keep the build closure beside the tests; CI builds this single target.
  add_custom_target(rpc-profile-tests DEPENDS rpc_profile_test rpc_profile_lifecycle_test
    rpc_runner arcdroid_rpcplan_libretro arcdroid_libretro rpc-plan-fixtures)
  add_test(NAME rpc-profile-runner COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_profile_runner_test.py"
    $<TARGET_FILE:rpc_runner> $<TARGET_FILE:arcdroid_rpcplan_libretro>
    $<TARGET_FILE:arcdroid_libretro> "${RPC_PLAN_DIR}" $<TARGET_FILE:rpc_profile_lifecycle_test>)
  set_tests_properties(rpc-profile-counts rpc-profile-runner PROPERTIES TIMEOUT 120)
  if(CMAKE_GENERATOR STREQUAL "Ninja" AND NOT ARCDROID_SANITIZERS)
    add_test(NAME rpc-profile-mutants COMMAND "${Python3_EXECUTABLE}" -B
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_profile_mutants.py"
      "${CMAKE_CURRENT_BINARY_DIR}" $<TARGET_FILE:rpc_profile_test> $<TARGET_FILE:rpc_runner>
      $<TARGET_FILE:arcdroid_rpcplan_libretro> $<TARGET_FILE:arcdroid_libretro> "${RPC_PLAN_DIR}" $<TARGET_FILE:rpc_profile_lifecycle_test>)
    set_tests_properties(rpc-profile-mutants PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)
  endif()
endif()

# Authored 1a-0 probes are also runnable in the Android shell kit. CTest never
# attempts to execute these cross-built programs on the build host.
if(ANDROID AND ARCDROID_ANDROID_TEST_CORE)
  add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/rpc_timing_rom.h"
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_timing_rom.py"
      "${CMAKE_CURRENT_BINARY_DIR}/generated/rpc_timing_rom.h"
    DEPENDS tests/rpc_timing_rom.py tests/rpc_timing_oracle.py tests/rpc_hot_dma_rom.py
      tests/rpc_video_rom.py tests/rpc_raster_timeline.py tests/rpc_authored_timeline.py VERBATIM)
  add_executable(rpc_timing_test tests/rpc_timing_test.c "${CMAKE_CURRENT_BINARY_DIR}/generated/rpc_timing_rom.h")
  target_include_directories(rpc_timing_test PRIVATE engine engine/rpc core libretro/include "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_compile_definitions(rpc_timing_test PRIVATE ARCDROID_TEST_FAULTS=1 RETRO_IMPORT_SYMBOLS=1)
  target_compile_options(rpc_timing_test PRIVATE -UNDEBUG)
  target_link_libraries(rpc_timing_test PRIVATE dl)
  add_executable(rpc_endpoint_test tests/rpc_endpoint_test.c engine/rpc/rpc_scheduler.c)
  target_include_directories(rpc_endpoint_test PRIVATE engine/rpc engine/rpcemu/src)
  target_compile_definitions(rpc_endpoint_test PRIVATE RELEASE_BUILD=1 ARCDROID_TEST_FAULTS=1)
  target_compile_options(rpc_endpoint_test PRIVATE -UNDEBUG -fno-strict-aliasing
    -include ${CMAKE_CURRENT_SOURCE_DIR}/engine/rpc/rpc_namespace.h)
  add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/rpc_authored_rom.h"
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/rpc_cpu_rom.py"
      "${CMAKE_CURRENT_BINARY_DIR}/generated/rpc_authored_rom.h"
    DEPENDS tests/rpc_cpu_rom.py VERBATIM)
  add_executable(rpc_endpoint_cpu_test tests/rpc_endpoint_cpu_test.c engine/rpc/rpc_profile.c
    engine/rpc/rpc_observer.c engine/rpc/rpc_inspector.c engine/rpc/rpc_mem_timing.c engine/rpc/rpc_scheduler.c
    engine/rpcemu/src/arm.c engine/rpcemu/src/arm_common.c engine/rpcemu/src/cp15.c engine/rpcemu/src/mem.c
    "${CMAKE_CURRENT_BINARY_DIR}/generated/rpc_authored_rom.h")
  target_include_directories(rpc_endpoint_cpu_test PRIVATE engine/rpc engine/rpcemu/src libretro/include "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_compile_definitions(rpc_endpoint_cpu_test PRIVATE ARCDROID_TEST_FAULTS=1 ARCDROID_RPC_PROFILE=1 ARCDROID_OBSERVATION_TEST=1 RELEASE_BUILD=1)
  target_compile_options(rpc_endpoint_cpu_test PRIVATE -UNDEBUG -fno-strict-aliasing
    -include ${CMAKE_CURRENT_SOURCE_DIR}/engine/rpc/rpc_namespace.h)
endif()
