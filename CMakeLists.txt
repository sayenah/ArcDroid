cmake_minimum_required(VERSION 3.20)
project(ArcDroid VERSION 0.1.0 LANGUAGES C)

include(CTest)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

add_library(arcdroid_libretro SHARED
  libretro/arcdroid_libretro.c
)
target_include_directories(arcdroid_libretro PRIVATE libretro/include)
target_compile_definitions(arcdroid_libretro PRIVATE ARCDROID_VERSION="${PROJECT_VERSION}")
target_compile_options(arcdroid_libretro PRIVATE
  $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic;-Werror=implicit-function-declaration;-fvisibility=hidden>
)
set_target_properties(arcdroid_libretro PROPERTIES PREFIX "")

if(ANDROID)
  set_target_properties(arcdroid_libretro PROPERTIES OUTPUT_NAME arcdroid_libretro_android)
  target_link_options(arcdroid_libretro PRIVATE
    "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/libretro/link.T"
    "-Wl,-z,max-page-size=16384")
  target_link_libraries(arcdroid_libretro PRIVATE m dl z)
elseif(WIN32)
  set_target_properties(arcdroid_libretro PROPERTIES OUTPUT_NAME arcdroid_libretro)
  target_sources(arcdroid_libretro PRIVATE libretro/arcdroid.def)
  target_link_options(arcdroid_libretro PRIVATE -static-libgcc)
else()
  if(APPLE)
    target_link_options(arcdroid_libretro PRIVATE
      "-Wl,-exported_symbols_list,${CMAKE_CURRENT_SOURCE_DIR}/libretro/arcdroid.exports")
  else()
    target_link_options(arcdroid_libretro PRIVATE
      "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/libretro/link.T")
  endif()
endif()

configure_file(arcdroid_libretro.info arcdroid_libretro.info COPYONLY)

install(TARGETS arcdroid_libretro DESTINATION .)
install(FILES arcdroid_libretro.info DESTINATION .)
