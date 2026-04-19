cmake_minimum_required(VERSION 3.20)

project(objit
  VERSION 1.0.0
  LANGUAGES C CXX)


if(NOT DEP_DIR OR NOT DEVTOOLS_DIR)
  message(FATAL_ERROR "DEP_DIR and DEVTOOLS_DIR must be set")
endif()

if(ANDROID)
  set(LLVM_DIR "${DEP_DIR}/lib/cmake/llvm")
elseif(APPLE)
  set(LLVM_DIR "${DEVTOOLS_DIR}/lib/cmake/llvm")
  list(APPEND CMAKE_LIBRARY_PATH "/usr/lib64")
  message(STATUS "LLVM_DIR: ${LLVM_DIR}")
  set(zstd_DIR "/opt/homebrew/lib/cmake/zstd")
  if(zstd_DIR)
    list(APPEND CMAKE_PREFIX_PATH "${zstd_DIR}/..")
  endif()
  find_package(zstd REQUIRED CONFIG)
elseif(WIN32)
  set(LLVM_DIR "${OB_LLVM_DIR}/lib/cmake/llvm")
  set(LIBXML2_LIBRARY "${OB_VCPKG_DIR}/lib")
  set(LIBXML2_INCLUDE_DIR "${OB_VCPKG_DIR}/include")
  message(STATUS "LLVM_DIR: ${LLVM_DIR}")
else()
  set(LLVM_DIR "${DEVTOOLS_DIR}/lib/cmake/llvm")
  set(ZLIB_LIBRARY "${DEP_DIR}/lib64/libz.a")
  set(Terminfo_LINKABLE "${DEP_DIR}/lib/libtinfo.a")
  set(Terminfo_LIBRARIES "${DEP_DIR}/lib/libtinfo.a")
  list(APPEND CMAKE_LIBRARY_PATH "${DEP_DIR}/lib")
  list(APPEND CMAKE_INCLUDE_PATH "${DEP_DIR}/include")
endif()
find_package(LLVM REQUIRED CONFIG)


include(cmake/libutils.cmake)
include(cmake/custom.cmake)

message(STATUS "This is BINARY dir " ${PROJECT_BINARY_DIR})
message(STATUS "This is SOURCE dir " ${PROJECT_SOURCE_DIR})

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})
if(WIN32)
  add_definitions(-g -O2 /GR)
else()
  add_definitions(-g -O2 -frtti)
  add_definitions(-D_GLIBCXX_USE_CXX11_ABI=1)
endif()

# Find the libraries that correspond to the LLVM components
# that we wish to use
if( ${ARCHITECTURE} STREQUAL "x86_64" OR ${ARCHITECTURE} STREQUAL "AMD64" OR ${ARCHITECTURE} STREQUAL "amd64" )
  LLVM_MAP_COMPONENTS_TO_LIBNAMES(llvm_libs Support Core IRReader ExecutionEngine OrcJit McJit X86CodeGen X86AsmParser runtimedyld bitreader bitwriter object objectyaml target DebugInfoDWARF Symbolize)
else()
  LLVM_MAP_COMPONENTS_TO_LIBNAMES(llvm_libs Support Core IRReader ExecutionEngine OrcJit McJit AArch64CodeGen AArch64AsmParser runtimedyld bitreader bitwriter object objectyaml target DebugInfoDWARF Symbolize)
endif()

# Set linker flags for Windows before including HandleLLVMOptions.cmake
# This prevents the /Brepro and /INCREMENTAL conflict warning
if(WIN32)
  set(CMAKE_EXE_LINKER_FLAGS "/INCREMENTAL:NO ${CMAKE_EXE_LINKER_FLAGS}")
  set(CMAKE_SHARED_LINKER_FLAGS "/INCREMENTAL:NO ${CMAKE_SHARED_LINKER_FLAGS}")
  set(CMAKE_MODULE_LINKER_FLAGS "/INCREMENTAL:NO ${CMAKE_MODULE_LINKER_FLAGS}")
endif()

set(CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(${LLVM_CMAKE_DIR}/HandleLLVMOptions.cmake)

add_subdirectory(src)

