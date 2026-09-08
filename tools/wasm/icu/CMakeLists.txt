# Copyright (c) 2026 OceanBase.
# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20)
project(seekdb_icu LANGUAGES C CXX)
if(NOT EMSCRIPTEN OR NOT EXISTS "${ICU_SOURCE}/common/unicode/uvernum.h")
  message(FATAL_ERROR "Use emcmake and set ICU_SOURCE to the pinned ICU source directory")
endif()
set(CMAKE_CXX_STANDARD 11)
add_compile_options(-pthread)
add_compile_definitions(U_STATIC_IMPLEMENTATION U_ENABLE_DYLOAD=0 UCONFIG_NO_FILE_IO=1)

# Follow the Emscripten ICU port's common/i18n source groups, with ICU 69.1
# and its complete Unicode data instead of the SDK port's empty data library.
foreach(component common i18n)
  file(GLOB sources CONFIGURE_DEPENDS "${ICU_SOURCE}/${component}/*.cpp"
    "${ICU_SOURCE}/${component}/*.c")
  add_library(icu_${component} STATIC ${sources})
  target_include_directories(icu_${component} PRIVATE "${ICU_SOURCE}/common"
    "${ICU_SOURCE}/${component}")
  string(TOUPPER "${component}" implementation)
  target_compile_definitions(icu_${component} PRIVATE "U_${implementation}_IMPLEMENTATION=1")
  install(TARGETS icu_${component} ARCHIVE DESTINATION lib)
  install(DIRECTORY "${ICU_SOURCE}/${component}/unicode" DESTINATION include)
  install(DIRECTORY "${ICU_SOURCE}/${component}/unicode" DESTINATION "include/icu/${component}")
endforeach()
add_library(icu_data STATIC "${ICU_DATA_C}")
install(TARGETS icu_data ARCHIVE DESTINATION lib)
