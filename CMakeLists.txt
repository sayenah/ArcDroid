cmake_minimum_required(VERSION 3.20)
project(ArcDroid VERSION 0.1.0 LANGUAGES C)

include(CTest)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

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
  core/hard_disc_scope.c
  core/hd_program.c
  core/zip.c
  core/zip_content.c
  core/machine.c
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

set(ARCDROID_LIBRETRO_SOURCES
  engine/arcdroid_engine.c
  engine/arc_boot_watch.c
  engine/arc_launch_outcome.c
  engine/arc_error_box.c
  engine/arc_disc_request.c
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
  libretro/arc_io.c
  libretro/jfd_store.c
  libretro/install_store.c
  libretro/arcdroid_libretro.c
  platform/arcdroid_input.c
  platform/arcdroid_joystick.c
  platform/arcdroid_sound.c
  platform/arcdroid_video.c
)

function(arcdroid_add_libretro target shipping)
  set(core_target arcdroid_core)
  if(ARGC GREATER 2)
    set(core_target "${ARGV2}")
  endif()
  add_library(${target} SHARED ${ARCDROID_LIBRETRO_SOURCES})
  target_include_directories(${target} PRIVATE
    engine engine/compat engine/arculator/src platform libretro libretro/include core)
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

configure_file(arcdroid_libretro.info arcdroid_libretro.info COPYONLY)

install(TARGETS arcdroid_libretro DESTINATION .)
install(FILES arcdroid_libretro.info DESTINATION .)

if(BUILD_TESTING AND (NOT CMAKE_CROSSCOMPILING OR WIN32))
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
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
  target_include_directories(install_ide_test PRIVATE engine/arculator/src)
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
    ${ARCDROID_CORE_SOURCES} tests/adffs_profiles_test.c tests/firmware_table_test.c)
  target_include_directories(arcdroid_core_test PUBLIC
    core libretro/include "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_link_libraries(arcdroid_core_test PRIVATE arcdroid_zlib)
  add_custom_command(
    OUTPUT "${FIXTURE_DIR}/arcdroid-native-hd-guest.rom"
    COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/generate_native_hd_guest.py"
      "${FIXTURE_DIR}/arcdroid-native-hd-guest.rom"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/generate_native_hd_guest.py"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/generate_fixtures.py"
    VERBATIM)
  add_custom_target(native_hd_guest_fixture ALL DEPENDS
    "${FIXTURE_DIR}/arcdroid-native-hd-guest.rom")
  add_custom_command(
    OUTPUT "${FIXTURE_DIR}/arcdroid-native-hd-stress.rom"
    COMMAND "${Python3_EXECUTABLE}"
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
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

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
  add_custom_command(
    OUTPUT "${FIXTURE_DIR}/arcdroid-test.rom" "${FIXTURE_DIR}/arcdroid-test.adf"
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
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/generate_fixtures.py" "${FIXTURE_DIR}"
    DEPENDS tests/generate_fixtures.py VERBATIM)
  add_custom_target(test-fixtures ALL DEPENDS
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
    set_tests_properties(libretro-harness PROPERTIES TIMEOUT 300)
    set_tests_properties(newio-libretro-harness PROPERTIES TIMEOUT 300)
  endif()

  add_executable(firmware_probe tools/firmware_probe.c)
  target_link_libraries(firmware_probe PRIVATE arcdroid_core)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(firmware_probe PRIVATE arcdroid_sanitizer_link)
  endif()
endif()
