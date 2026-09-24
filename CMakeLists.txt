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
  core/adffs_install.c
  core/zip.c
  core/zip_content.c
  core/machine.c
  core/hash.c
  core/input.c
  core/input_mapping.c
  core/input_profiles.c
  core/osk.c
  core/launch.c
  core/launch_discovery.c
  core/launch_disc.c
  core/launch_chooser.c
  core/folder_set.c
  core/playlist.c
  core/jfd.c
  core/persistence.c
  core/cmos_defaults.c
)
add_library(arcdroid_core STATIC ${ARCDROID_CORE_SOURCES} core/adffs_profiles.c core/adffs_install_profiles.c core/firmware_table.c)
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
  engine/arcdroid_stubs.c
  engine/arculator/src/82c711.c
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
  add_executable(adffs_shipping_profile_test
    tests/adffs_shipping_profile_test.c tests/adffs_install_profiles_test.c)
  target_link_libraries(adffs_shipping_profile_test PRIVATE arcdroid_core)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(adffs_shipping_profile_test PRIVATE arcdroid_sanitizer_link)
  endif()
  target_compile_options(adffs_shipping_profile_test PRIVATE $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG>)
  set(FIXTURE_DIR "${CMAKE_CURRENT_BINARY_DIR}/tests/fixtures/generated")
  add_library(arcdroid_core_test STATIC
    ${ARCDROID_CORE_SOURCES} tests/adffs_profiles_test.c tests/adffs_install_profiles_test.c tests/firmware_table_test.c)
  target_include_directories(arcdroid_core_test PUBLIC
    core libretro/include "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_link_libraries(arcdroid_core_test PRIVATE arcdroid_zlib)
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
  target_compile_options(jfd_reader_tests PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG;-Wall;-Wextra;-Wpedantic>)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(jfd_reader_tests PRIVATE
      arcdroid_sanitizer_compile arcdroid_sanitizer_link)
  endif()
  add_test(NAME jfd-engine-reader COMMAND jfd_reader_tests)
  add_executable(jfd_controller_tests
    tests/jfd_controller_tests.c
    engine/arculator/src/wd1770.c
    engine/arculator/src/82c711_fdc.c
    engine/arculator/src/disc.c
    engine/arculator/src/disc_adf.c
    engine/arculator/src/disc_jfd.c
    engine/arculator/src/timer.c)
  target_include_directories(jfd_controller_tests PRIVATE
    core engine/compat engine/arculator/src libretro libretro/include)
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
    "${FIXTURE_DIR}/adffs/missing-install-resource.zip"
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
    "${FIXTURE_DIR}/adffs/multi.m3u")
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
    "${FIXTURE_DIR}/adffs/install-fat-3.jfd"
    "${FIXTURE_DIR}/adffs/install-fat.m3u"
    "${FIXTURE_DIR}/adffs/install-too-large.m3u"
    "${FIXTURE_DIR}/adffs/install-raw-limit-2.jfd"
    "${FIXTURE_DIR}/adffs/install-raw-limit.m3u"
    "${FIXTURE_DIR}/adffs/install-wrong-2.jfd"
    "${FIXTURE_DIR}/adffs/install-wrong.m3u"
    "${FIXTURE_DIR}/adffs/eligible-hd.jfd")
  add_custom_command(
    OUTPUT "${FIXTURE_DIR}/arcdroid-test.rom" "${FIXTURE_DIR}/arcdroid-test.adf"
      "${FIXTURE_DIR}/arcdroid-chooser-test.rom"
      "${FIXTURE_DIR}/arcdroid-unsupported-launch-test.rom"
      "${FIXTURE_DIR}/arcdroid-demo.adl"
      "${FIXTURE_DIR}/arcdroid-application-l.adl"
      "${FIXTURE_DIR}/arcdroid-chooser-l.adl"
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
      "${FIXTURE_DIR}/arcdroid-media-test.rom"
      "${FIXTURE_DIR}/arcdroid-jfd-save-test.rom"
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
      ${ADFFS_FIXTURES}
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/generate_fixtures.py" "${FIXTURE_DIR}"
    DEPENDS tests/generate_fixtures.py VERBATIM)
  add_custom_target(test-fixtures ALL DEPENDS
    "${FIXTURE_DIR}/arcdroid-test.rom" "${FIXTURE_DIR}/arcdroid-test.adf"
    "${FIXTURE_DIR}/arcdroid-chooser-test.rom"
    "${FIXTURE_DIR}/arcdroid-unsupported-launch-test.rom"
    "${FIXTURE_DIR}/arcdroid-demo.adl"
    "${FIXTURE_DIR}/arcdroid-application-l.adl"
    "${FIXTURE_DIR}/arcdroid-chooser-l.adl"
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
    "${FIXTURE_DIR}/arcdroid-media-test.rom"
    "${FIXTURE_DIR}/arcdroid-jfd-save-test.rom"
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
  add_test(NAME jfd-store COMMAND jfd_store_tests)
  add_dependencies(jfd_controller_tests test-fixtures)
  add_test(NAME core-services COMMAND core_tests "${FIXTURE_DIR}")
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
    set_target_properties(arcdroid_libretro_test PROPERTIES OUTPUT_NAME arcdroid_libretro_test)
    add_executable(libretro_harness tests/libretro_harness.c)
    target_include_directories(libretro_harness PRIVATE libretro/include)
    target_include_directories(libretro_harness PRIVATE core engine "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_link_libraries(libretro_harness PRIVATE arcdroid_core ${CMAKE_DL_LIBS})
    if(ARCDROID_SANITIZERS)
      target_link_libraries(libretro_harness PRIVATE
        arcdroid_sanitizer_compile arcdroid_sanitizer_link)
    endif()
    target_compile_options(libretro_harness PRIVATE $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG>)
    add_dependencies(libretro_harness test-fixtures arcdroid_libretro_test)
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
      "${FIXTURE_DIR}/zip-content")
    add_test(NAME jfd-writes-libretro-harness COMMAND libretro_harness
      $<TARGET_FILE:arcdroid_libretro_test>
      "${FIXTURE_DIR}/arcdroid-jfd-save-test.rom"
      "${FIXTURE_DIR}/jfd-writes/rw-d.jfd"
      "${CMAKE_CURRENT_BINARY_DIR}/jfd-writes-harness-data"
      --jfd-writes "${FIXTURE_DIR}/jfd-writes")
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
      "${FIXTURE_DIR}/hd-family/valid-f.adf")
    add_test(NAME mouse-script-harness COMMAND libretro_harness
      $<TARGET_FILE:arcdroid_libretro_test>
      "${FIXTURE_DIR}/arcdroid-test.rom"
      "${FIXTURE_DIR}/arcdroid-test.adf"
      "${CMAKE_CURRENT_BINARY_DIR}/mouse-script-data"
      --real-boot --osk-script "${CMAKE_CURRENT_SOURCE_DIR}/tests/mouse-script.txt")
    set_tests_properties(libretro-harness PROPERTIES TIMEOUT 300)
    set_tests_properties(newio-libretro-harness PROPERTIES TIMEOUT 300)
  endif()

  add_executable(firmware_probe tools/firmware_probe.c)
  target_link_libraries(firmware_probe PRIVATE arcdroid_core)
  if(ARCDROID_SANITIZERS)
    target_link_libraries(firmware_probe PRIVATE arcdroid_sanitizer_link)
  endif()
endif()
