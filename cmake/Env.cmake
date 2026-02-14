ob_define(DEBUG_PREFIX "-fdebug-prefix-map=${CMAKE_SOURCE_DIR}=.")
ob_define(FILE_PREFIX "-ffile-prefix-map=${CMAKE_SOURCE_DIR}=.")
ob_define(OB_LD_BIN ld)
ob_define(ASAN_IGNORE_LIST "${CMAKE_SOURCE_DIR}/asan_ignore_list.txt")

ob_define(DEP_3RD_DIR "${CMAKE_SOURCE_DIR}/deps/3rd")
ob_define(DEVTOOLS_DIR "${CMAKE_SOURCE_DIR}/deps/3rd/usr/local/oceanbase/devtools")
ob_define(DEP_DIR "${CMAKE_SOURCE_DIR}/deps/3rd/usr/local/oceanbase/deps/devel")

ob_define(BUILD_CDC_ONLY OFF)
ob_define(BUILD_EMBED_MODE OFF)
ob_define(OB_USE_CLANG ON)
ob_define(OB_ERRSIM OFF)
ob_define(BUILD_NUMBER 1)
ob_define(OB_GPERF_MODE OFF)
ob_define(ENABLE_OBJ_LEAK_CHECK OFF)
ob_define(ENABLE_FATAL_ERROR_HANG ON)
ob_define(DETECT_RECURSION OFF)
ob_define(ENABLE_COMPILE_DLL_MODE OFF)
ob_define(OB_CMAKE_RULES_CHECK ON)
ob_define(OB_STATIC_LINK_LGPL_DEPS ON)
ob_define(OB_BUILD_CCLS OFF)
ob_define(LTO_JOBS all)
ob_define(LTO_CACHE_DIR "${CMAKE_BINARY_DIR}/cache")
ob_define(LTO_CACHE_POLICY cache_size=100%:cache_size_bytes=0k:cache_size_files=0:prune_after=0s:prune_interval=72h)
ob_define(NEED_PARSER_CACHE ON)
# get compiler from build.sh
ob_define(OB_CC "")
ob_define(OB_CXX "")
ob_define(OB_BUILD_STANDALONE OFF)
ob_define(OB_BUILD_LITE ON)
ob_define(DEFAULT_LOG_LEVEL OB_LOG_LEVEL_WARN)
ob_define(DEFAULT_LOG_FILE_SIZE_MB 256)

# 'ENABLE_PERF_MODE' use for offline system insight performance test
# PERF_MODE macro controls many special code path in system
# we can open this to benchmark our system partial/layered
ob_define(ENABLE_PERF_MODE OFF)

# begin of unity build config
ob_define(OB_MAX_UNITY_BATCH_SIZE 30)
# the global switch of unity build, default is 'ON'
ob_define(OB_ENABLE_UNITY ON)

ob_define(OB_DISABLE_LSE OFF)

ob_define(OB_DISABLE_PIE OFF)

ob_define(OB_ENABLE_MCMODEL OFF)

ob_define(USE_LTO_CACHE OFF)

ob_define(ASAN_DISABLE_STACK ON)

# 开源模式默认支持系统租户使用向量索引
ob_define(OB_BUILD_SYS_VEC_IDX ON)

EXECUTE_PROCESS(COMMAND uname -m COMMAND tr -d '\n' OUTPUT_VARIABLE ARCHITECTURE)

if(WITH_COVERAGE)
  # -ftest-coverage to generate .gcno file
  # -fprofile-arcs to generate .gcda file
  # -DDBUILD_COVERAGE marco use to mark 'coverage build type' and to handle some special case
  set(CMAKE_COVERAGE_COMPILE_OPTIONS -ftest-coverage -fprofile-arcs -Xclang -coverage-version=408R -DBUILD_COVERAGE)
  set(CMAKE_COVERAGE_EXE_LINKER_OPTIONS "-ftest-coverage -fprofile-arcs")

  add_compile_options(${CMAKE_COVERAGE_COMPILE_OPTIONS})
  set(DEBUG_PREFIX "")
  set(FILE_PREFIX "")
endif()

ob_define(AUTO_FDO_OPT "")
if(ENABLE_AUTO_FDO)
  if( ${ARCHITECTURE} STREQUAL "x86_64" )
    set(AUTO_FDO_PATH "${CMAKE_SOURCE_DIR}/profile/observer-x86_64.prof")
  elseif( ${ARCHITECTURE} STREQUAL "aarch64" )
    set(AUTO_FDO_PATH "${CMAKE_SOURCE_DIR}/profile/observer-aarch64.prof")
  endif()
  set(AUTO_FDO_OPT "-finline-functions -fprofile-sample-use=${AUTO_FDO_PATH}")
  message(STATUS "auto fdo path: " ${AUTO_FDO_PATH})
endif()

ob_define(THIN_LTO_OPT "")
ob_define(THIN_LTO_CONCURRENCY_LINK "")

if(ENABLE_THIN_LTO)
  set(THIN_LTO_OPT "-flto=thin")
  if(APPLE)
    set(THIN_LTO_CONCURRENCY_LINK "-flto-jobs=${LTO_JOBS}")
  else()
    set(THIN_LTO_CONCURRENCY_LINK "-Wl,--thinlto-jobs=${LTO_JOBS}")
    if(USE_LTO_CACHE)
      set(THIN_LTO_CONCURRENCY_LINK "${THIN_LTO_CONCURRENCY_LINK},--thinlto-cache-dir=${LTO_CACHE_DIR},--thinlto-cache-policy=${LTO_CACHE_POLICY}")
    endif()
  endif()
endif()

set(HOTFUNC_OPT "")
if(ENABLE_HOTFUNC)
  if( ${ARCHITECTURE} STREQUAL "x86_64" )
    set(HOTFUNC_PATH "${CMAKE_SOURCE_DIR}/profile/hotfuncs-x86_64.txt")
  elseif( ${ARCHITECTURE} STREQUAL "aarch64" )
    set(HOTFUNC_PATH "${CMAKE_SOURCE_DIR}/profile/hotfuncs-aarch64.txt")
  endif()
  set(HOTFUNC_OPT "-Wl,--no-warn-symbol-ordering,--symbol-ordering-file,${HOTFUNC_PATH}")
  message(STATUS "hotfunc path: " ${HOTFUNC_PATH})
endif()

set(BOLT_OPT "")

message(STATUS "Using C++20 standard")
set(CMAKE_CXX_FLAGS "-std=gnu++20")

if(OB_DISABLE_PIE)
  message(STATUS "build without pie")
  set(PIE_OPT "-no-pie")
else()
  message(STATUS "build with pie")
  set(PIE_OPT "-pie")
endif()

set(ob_close_deps_static_name "")

set(OB_BUILD_CLOSE_MODULES OFF)

# observer lite
ob_define(OB_BUILD_OBSERVER_LITE ON)

if(OB_BUILD_STANDALONE)
  add_definitions(-DOB_BUILD_STANDALONE)
endif()

if (OB_USE_TEST_PUBKEY)
  add_definitions(-DOB_USE_TEST_PUBKEY)
endif()

if(OB_BUILD_LITE)
  add_definitions(-DOB_BUILD_LITE)
endif()

if(OB_BUILD_OBSERVER_LITE)
  add_definitions(-DOB_BUILD_OBSERVER_LITE)
endif()

if (OB_BUILD_SYS_VEC_IDX)
 add_definitions(-DOB_BUILD_SYS_VEC_IDX)
endif()
 
# should not use initial-exec for tls-model if building OBCDC.
if(BUILD_CDC_ONLY)
  add_definitions(-DOB_BUILD_CDC_DISABLE_VSAG)
else()
  if(NOT BUILD_EMBED_MODE)
    add_definitions(-DENABLE_INITIAL_EXEC_TLS_MODEL)
  endif()
endif()

if(BUILD_EMBED_MODE)
  add_definitions(-DOB_BUILD_EMBED_MODE)
endif()

# Find objcopy - on macOS it may be installed via Homebrew or available as llvm-objcopy
set(OB_CLANG_BIN "clang-17")
set(OB_CLANGXX_BIN "clang++-17")

add_definitions(-DDEFAULT_LOG_LEVEL=${DEFAULT_LOG_LEVEL})
add_definitions(-DDEFAULT_LOG_FILE_SIZE_MB=${DEFAULT_LOG_FILE_SIZE_MB})

set(OB_OBJCOPY_BIN "${DEVTOOLS_DIR}/bin/objcopy")
set(CMAKE_TOOLCHAIN_PATH "${DEVTOOLS_DIR}")
set(GCC_DEVTOOL_PATH "${CMAKE_SOURCE_DIR}/deps/3rd/usr/local/oceanbase")
set(COMPACT_UNWIND_FLAG "")
if(APPLE)
  ob_define(SYS_INCLUDE_DIR "/usr")
  add_definitions(-D_DARWIN_C_SOURCE)
  set(OB_CLANG_BIN "clang")
  set(OB_CLANGXX_BIN "clang++")
  set(CMAKE_TOOLCHAIN_PATH "/usr")
  set(GCC_DEVTOOL_PATH "/usr/lib/")
  # Set macOS deployment target to match the current system version
  # This eliminates linker warnings when linking with Homebrew libraries
  execute_process(
    COMMAND sw_vers -productVersion
    OUTPUT_VARIABLE MACOS_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  set(CMAKE_OSX_DEPLOYMENT_TARGET "${MACOS_VERSION}" CACHE STRING "Minimum macOS deployment version")
  # extract major version like 15.6.1 -> 15
  string(REPLACE "." ";" MACOS_VERSION_LISTS "${MACOS_VERSION}")
  list(GET MACOS_VERSION_LISTS 0 MACOS_MAJOR)
  if(MACOS_MAJOR LESS 15)
    set(COMPACT_UNWIND_FLAG "-Wl,-no_compact_unwind")
  endif()
else()
  # NO RELERO: -Wl,-znorelro
  # Partial RELRO: -Wl,-z,relro
  # Full RELRO: -Wl,-z,relro,-z,now
  # macOS doesn't support RELRO flags
  ob_define(OB_RELRO_FLAG "-Wl,-z,relro,-z,now")
endif()

ob_define(OB_USE_CCACHE OFF)
if (OB_USE_CCACHE)
  find_program(OB_CCACHE ccache PATHS "${DEVTOOLS_DIR}/bin" NO_DEFAULT_PATH)
  if (NOT OB_CCACHE)
    message(FATAL_ERROR "cannot find ccache.")
  else()
    set(CMAKE_C_COMPILER_LAUNCHER ${OB_CCACHE})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${OB_CCACHE})
  endif()
endif(OB_USE_CCACHE)

if (OB_USE_CLANG)

  if (OB_CC)
    message(STATUS "Using OB_CC compiler: ${OB_CC}")
  else()
    find_program(OB_CC ${OB_CLANG_BIN}
    "${DEVTOOLS_DIR}/bin"
      NO_DEFAULT_PATH)
  endif()

  if (OB_CXX)
    message(STATUS "Using OB_CXX compiler: ${OB_CXX}")
  else()
    find_program(OB_CXX ${OB_CLANGXX_BIN}
    "${DEVTOOLS_DIR}/bin"
      NO_DEFAULT_PATH)
  endif()

  set(OB_OBJCOPY_BIN "${DEVTOOLS_DIR}/bin/llvm-objcopy")

  find_file(GCC9 devtools
    PATH ${GCC_DEVTOOL_PATH}
    NO_DEFAULT_PATH)
  set(_CMAKE_TOOLCHAIN_PREFIX llvm-)
  set(_CMAKE_TOOLCHAIN_LOCATION "${CMAKE_TOOLCHAIN_PATH}/bin")

  if (OB_USE_ASAN)
    if (ASAN_DISABLE_STACK)
      ob_define(CMAKE_ASAN_FLAG "-mllvm -asan-stack=0 -fsanitize=address -fno-optimize-sibling-calls -fsanitize-blacklist=${ASAN_IGNORE_LIST}")
    else()
      ob_define(CMAKE_ASAN_FLAG "-fstack-protector-strong -fsanitize=address -fno-optimize-sibling-calls -fsanitize-blacklist=${ASAN_IGNORE_LIST}")
    endif()
  endif()

  if (OB_USE_LLD)
    if(APPLE)
      set(LD_OPT "-Wl,-dead_strip")
      set(REORDER_COMP_OPT "-ffunction-sections -fdata-sections")
      set(REORDER_LINK_OPT "-Wl,-dead_strip")
      set(OB_LD_BIN "ld")
    else()
      set(LD_OPT "-fuse-ld=${DEVTOOLS_DIR}/bin/ld.lld -Wno-unused-command-line-argument")
      set(REORDER_COMP_OPT "-ffunction-sections -fdata-sections -fdebug-info-for-profiling")
      set(REORDER_LINK_OPT "-Wl,--no-rosegment,--build-id=sha1,--gc-sections,--icf=safe ${HOTFUNC_OPT}")
      set(OB_LD_BIN "${DEVTOOLS_DIR}/bin/ld.lld")
    endif()
  endif()

  if(APPLE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${DEBUG_PREFIX} ${FILE_PREFIX} ${AUTO_FDO_OPT} ${THIN_LTO_OPT} -fcolor-diagnostics ${REORDER_COMP_OPT} -fmax-type-align=8 ${CMAKE_ASAN_FLAG}")
    set(CMAKE_C_FLAGS "${DEBUG_PREFIX} ${FILE_PREFIX} ${AUTO_FDO_OPT} ${THIN_LTO_OPT} -fcolor-diagnostics ${REORDER_COMP_OPT} -fmax-type-align=8 ${CMAKE_ASAN_FLAG}")
    set(CMAKE_CXX_LINK_FLAGS "${LD_OPT} ${DEBUG_PREFIX} ${FILE_PREFIX} ${AUTO_FDO_OPT}")
    set(CMAKE_SHARED_LINKER_FLAGS "${LD_OPT} ${THIN_LTO_CONCURRENCY_LINK} ${REORDER_LINK_OPT} ${COMPACT_UNWIND_FLAG}")
    set(CMAKE_EXE_LINKER_FLAGS "${LD_OPT} ${THIN_LTO_CONCURRENCY_LINK} ${REORDER_LINK_OPT} ${CMAKE_COVERAGE_EXE_LINKER_OPTIONS} ${COMPACT_UNWIND_FLAG}")
  else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --gcc-toolchain=${GCC9} -gdwarf-4 ${DEBUG_PREFIX} ${FILE_PREFIX} ${AUTO_FDO_OPT} ${THIN_LTO_OPT} -fcolor-diagnostics ${REORDER_COMP_OPT} -fmax-type-align=8 ${CMAKE_ASAN_FLAG}")
    set(CMAKE_C_FLAGS "--gcc-toolchain=${GCC9} -gdwarf-4 ${DEBUG_PREFIX} ${FILE_PREFIX} ${AUTO_FDO_OPT} ${THIN_LTO_OPT} -fcolor-diagnostics ${REORDER_COMP_OPT} -fmax-type-align=8 ${CMAKE_ASAN_FLAG}")
    set(CMAKE_CXX_LINK_FLAGS "${LD_OPT} --gcc-toolchain=${GCC9} ${DEBUG_PREFIX} ${FILE_PREFIX} ${AUTO_FDO_OPT}")
    set(CMAKE_SHARED_LINKER_FLAGS "${LD_OPT} -Wl,-z,noexecstack ${THIN_LTO_CONCURRENCY_LINK} ${REORDER_LINK_OPT}")
    set(CMAKE_EXE_LINKER_FLAGS "${LD_OPT} -Wl,-z,noexecstack ${PIE_OPT} ${THIN_LTO_CONCURRENCY_LINK} ${REORDER_LINK_OPT} ${CMAKE_COVERAGE_EXE_LINKER_OPTIONS}")
  endif()
else() # not clang, use gcc
  message("gcc9 not support currently, please set OB_USE_CLANG ON and we will finish it as soon as possible")
endif()

if (OB_BUILD_CCLS)
  # ccls场景采用更大的unity的联合编译单元，ccls是非完整编译，调用clang AST接口，单元的size和耗时成指数衰减
  set(OB_MAX_UNITY_BATCH_SIZE 200)
  # -DCCLS_LASY_ENABLE 给全局设置上，将采用ccls懒加载模式，主要针对单测case，当添加上-DCCLS_LASY_OFF，首次将会进行检索
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DCCLS_LASY_ENABLE")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DCCLS_LASY_ENABLE")
endif()

if (OB_CC AND OB_CXX)
  set(CMAKE_C_COMPILER ${OB_CC})
  set(CMAKE_CXX_COMPILER ${OB_CXX})
else()
  message(FATAL_ERROR "can't find suitable compiler")
endif()

find_program(OB_COMPILE_EXECUTABLE ob-compile)
if (NOT OB_COMPILE_EXECUTABLE)
  message(STATUS "ob-compile not found, compile locally.")
else()
  set(CMAKE_C_COMPILER_LAUNCHER ${OB_COMPILE_EXECUTABLE})
  set(CMAKE_CXX_COMPILER_LAUNCHER ${OB_COMPILE_EXECUTABLE})
  set(CMAKE_C_LINKER_LAUNCHER ${OB_COMPILE_EXECUTABLE})
  set(CMAKE_CXX_LINKER_LAUNCHER ${OB_COMPILE_EXECUTABLE})
endif()

option(OB_ENABLE_AVX2 "enable AVX2 and related instruction set support for x86_64" OFF)

include(CMakeFindBinUtils)
EXECUTE_PROCESS(COMMAND uname -m COMMAND tr -d '\n' OUTPUT_VARIABLE ARCHITECTURE )
if( ${ARCHITECTURE} STREQUAL "x86_64" )
    set(MTUNE_CFLAGS -mtune=core2)
    set(ARCH_LDFLAGS "")
    set(OCI_DEVEL_INC "${DEP_3RD_DIR}/usr/include/oracle/12.2/client64")
else()
    if (${OB_DISABLE_LSE})
      message(STATUS "build with no-lse")
      set(MARCH_CFLAGS "-march=armv8-a+crc")
    else()
      message(STATUS "build with lse")
      set(MARCH_CFLAGS "-march=armv8-a+crc+lse")
    endif()
    set(MTUNE_CFLAGS "-mtune=generic" )
    if(APPLE)
      # macOS doesn't support -l:libatomic.a format, use -latomic or find the library
      find_library(ATOMIC_LIB atomic)
      if(ATOMIC_LIB)
        set(ARCH_LDFLAGS "${ATOMIC_LIB}")
      endif()
    else()
      set(ARCH_LDFLAGS "-l:libatomic.a")
    endif()
    set(OCI_DEVEL_INC "${DEP_3RD_DIR}/usr/include/oracle/19.10/client64")
endif()

# AIO library detection for Ubuntu >= 24.04 and Debian >= 13
# Set OB_AIO_LINK_OPTION for linking and OB_AIO_PACKAGE_DEPENDENCY for package dependencies
set(OB_AIO "libaio")
find_program(LSB_RELEASE_EXEC lsb_release)
if(LSB_RELEASE_EXEC)
  execute_process(
    COMMAND ${LSB_RELEASE_EXEC} -is
    OUTPUT_VARIABLE DEBIAN_NAME
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  if(DEBIAN_NAME)
    string(TOLOWER "${DEBIAN_NAME}" DEBIAN_NAME)
    execute_process(
      COMMAND ${LSB_RELEASE_EXEC} -rs
      OUTPUT_VARIABLE DEBIAN_VERSION
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
    )
    if(DEBIAN_VERSION)
      # Check for Ubuntu >= 24.04
      if(DEBIAN_NAME STREQUAL "ubuntu")
        if(DEBIAN_VERSION VERSION_GREATER_EQUAL "24.04")
          set(OB_AIO "libaio1t64")
          message(STATUS "Ubuntu ${DEBIAN_VERSION} detected, using ${OB_AIO}")
        endif()
      # Check for Debian >= 13
      elseif(DEBIAN_NAME STREQUAL "debian")
        if(DEBIAN_VERSION VERSION_GREATER_EQUAL "13")
          set(OB_AIO "libaio1t64")
          message(STATUS "Debian ${DEBIAN_VERSION} detected, using ${OB_AIO}")
        endif()
      endif()
    endif()
  endif()
endif()

EXECUTE_PROCESS(COMMAND grep -Po "release [0-9]{1}" /etc/redhat-release COMMAND awk "{print $2}" COMMAND tr -d '\n' OUTPUT_VARIABLE KERNEL_RELEASE ERROR_QUIET)
