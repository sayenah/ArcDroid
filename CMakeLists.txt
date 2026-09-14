cmake_minimum_required(VERSION 3.20)
project(ArcDroid VERSION 0.1.0 LANGUAGES C)

include(CTest)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

foreach(cmos_name IN ITEMS arthur riscos2 riscos3_old riscos3_new)
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/engine/arculator/cmos/${cmos_name}/cmos.bin"
       cmos_${cmos_name}_hex HEX LIMIT 256)
  string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," cmos_${cmos_name}_bytes
         "${cmos_${cmos_name}_hex}")
endforeach()
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file(core/cmos_defaults_data.h.in generated/cmos_defaults_data.h @ONLY)

add_library(arcdroid_core STATIC
  core/hash.c
  core/input.c
  core/firmware_table.c
  core/launch.c
  core/persistence.c
  core/cmos_defaults.c
)
target_include_directories(arcdroid_core PUBLIC core "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_options(arcdroid_core PRIVATE
  $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic;-Werror=implicit-function-declaration>
)

set(ARCDROID_LIBRETRO_SOURCES
  engine/arcdroid_engine.c
  engine/arcdroid_stubs.c
  engine/arculator/src/arm.c
  engine/arculator/src/cmos.c
  engine/arculator/src/cp15.c
  engine/arculator/src/disc.c
  engine/arculator/src/disc_adf.c
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
  libretro/arcdroid_libretro.c
  platform/arcdroid_input.c
  platform/arcdroid_joystick.c
  platform/arcdroid_sound.c
  platform/arcdroid_video.c
)

function(arcdroid_add_libretro target shipping)
  add_library(${target} SHARED ${ARCDROID_LIBRETRO_SOURCES})
  target_include_directories(${target} PRIVATE
    engine engine/compat engine/arculator/src platform libretro libretro/include core)
  target_compile_definitions(${target} PRIVATE
    ARCDROID_VERSION="${PROJECT_VERSION}"
    RELEASE_BUILD=1)
  target_compile_options(${target} PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic;-Werror=implicit-function-declaration;-fno-strict-aliasing>)
  set_target_properties(${target} PROPERTIES PREFIX "")
  target_link_libraries(${target} PRIVATE arcdroid_core)

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
    target_link_libraries(${target} PRIVATE m dl z)
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

if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_executable(core_tests tests/core_tests.c)
  target_link_libraries(core_tests PRIVATE arcdroid_core)
  target_compile_options(core_tests PRIVATE $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG>)
  add_test(NAME core-services COMMAND core_tests)
  add_test(NAME shipping-io-guard
    COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tools/check-shipping-io.sh" --self-test)
  add_test(NAME asset-guard-self-test
    COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_assets_guard.sh"
      "${CMAKE_CURRENT_SOURCE_DIR}/tools/check-assets.sh")
  set_tests_properties(shipping-io-guard PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

  set(FIXTURE_DIR "${CMAKE_CURRENT_BINARY_DIR}/tests/fixtures/generated")
  add_custom_command(
    OUTPUT "${FIXTURE_DIR}/arcdroid-test.rom" "${FIXTURE_DIR}/arcdroid-test.adf"
      "${FIXTURE_DIR}/arcdroid-demo.adl"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/generate_fixtures.py" "${FIXTURE_DIR}"
    DEPENDS tests/generate_fixtures.py VERBATIM)
  add_custom_target(test-fixtures ALL DEPENDS
    "${FIXTURE_DIR}/arcdroid-test.rom" "${FIXTURE_DIR}/arcdroid-test.adf"
    "${FIXTURE_DIR}/arcdroid-demo.adl")

  if(WIN32)
    add_executable(windows_load_harness tests/windows_load_harness.c)
    target_include_directories(windows_load_harness PRIVATE libretro/include)
    target_compile_options(windows_load_harness PRIVATE $<$<C_COMPILER_ID:GNU,Clang>:-UNDEBUG>)
    add_dependencies(windows_load_harness arcdroid_libretro test-fixtures)
    add_test(NAME windows-load-harness COMMAND windows_load_harness
      $<TARGET_FILE:arcdroid_libretro>
      "${FIXTURE_DIR}/arcdroid-test.rom"
      "${FIXTURE_DIR}/arcdroid-test.adf"
      "${CMAKE_CURRENT_BINARY_DIR}/windows-harness-data")
  else()
    # Fault injection and engine test probes live only in this non-installed
    # host test core; release/RetroArch artifacts use arcdroid_libretro above.
    arcdroid_add_libretro(arcdroid_libretro_test FALSE)
    target_compile_definitions(arcdroid_libretro_test PRIVATE ARCDROID_TEST_FAULTS=1)
    set_target_properties(arcdroid_libretro_test PROPERTIES OUTPUT_NAME arcdroid_libretro_test)
    add_executable(libretro_harness tests/libretro_harness.c)
    target_include_directories(libretro_harness PRIVATE libretro/include)
    target_include_directories(libretro_harness PRIVATE core "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_link_libraries(libretro_harness PRIVATE arcdroid_core ${CMAKE_DL_LIBS})
    target_compile_options(libretro_harness PRIVATE $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG>)
    add_dependencies(libretro_harness test-fixtures arcdroid_libretro_test)
    add_test(NAME libretro-harness COMMAND libretro_harness
      $<TARGET_FILE:arcdroid_libretro_test>
      "${FIXTURE_DIR}/arcdroid-test.rom"
      "${FIXTURE_DIR}/arcdroid-test.adf"
      "${CMAKE_CURRENT_BINARY_DIR}/harness-data")
  endif()

  add_executable(firmware_probe tools/firmware_probe.c)
  target_link_libraries(firmware_probe PRIVATE arcdroid_core)
endif()
