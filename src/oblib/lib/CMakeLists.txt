add_subdirectory(compress)
add_subdirectory(restore)

seekdb_apply_unity_inventory(oblib_lib SEEKDB_OBLIB_LIB_UNITY)
# Bazel deliberately compiles the compression implementation outside its
# historical OBLib Unity group; preserve that action boundary here.
set_source_files_properties(
  ${CMAKE_SOURCE_DIR}/src/oblib/lib/oblog/ob_log_compressor.cpp
  PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
ob_lib_add_target(oblib_lib)

if(OB_BUILD_HIPVS)
  set(HIPVS_ROOT "/opt/hipvs" CACHE PATH "hipVS installation prefix")
  set(ROCM_ROOT "/opt/rocm" CACHE PATH "ROCm installation prefix")
  find_library(HIPVS_C_LIBRARY NAMES cuvs_c PATHS "${HIPVS_ROOT}/lib" REQUIRED NO_DEFAULT_PATH)
  add_library(seekdb_hipvs_hnsw SHARED vector/hipvs_hnsw_bridge.c)
  # The database invokes a separate worker; it never loads ROCm/hipVS itself.
  set_target_properties(seekdb_hipvs_hnsw PROPERTIES
    C_VISIBILITY_PRESET hidden
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    BUILD_RPATH "${HIPVS_ROOT}/lib;${ROCM_ROOT}/lib"
    INSTALL_RPATH "${HIPVS_ROOT}/lib;${ROCM_ROOT}/lib")
  target_compile_definitions(seekdb_hipvs_hnsw PRIVATE __HIP_PLATFORM_AMD__)
  target_include_directories(seekdb_hipvs_hnsw PRIVATE "${HIPVS_ROOT}/include" "${ROCM_ROOT}/include")
  target_link_libraries(seekdb_hipvs_hnsw PRIVATE "${HIPVS_C_LIBRARY}")
  include(CheckSymbolExists)
  set(CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)
  check_symbol_exists(posix_spawn_file_actions_addclosefrom_np "spawn.h" HIPVS_HAVE_SPAWN_CLOSEFROM)
  unset(CMAKE_REQUIRED_DEFINITIONS)
  if(NOT HIPVS_HAVE_SPAWN_CLOSEFROM)
    message(FATAL_ERROR "hipVS worker isolation requires glibc 2.34 or newer")
  endif()
  add_executable(seekdb_hipvs_hnsw_worker vector/hipvs_hnsw_worker.c)
  target_link_libraries(seekdb_hipvs_hnsw_worker PRIVATE seekdb_hipvs_hnsw)
  set_target_properties(seekdb_hipvs_hnsw_worker PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/src/observer"
    BUILD_RPATH "$ORIGIN/../lib"
    INSTALL_RPATH "$ORIGIN/../libexec/seekdb/lib")
  # Package the worker beside seekdb so /proc/self/exe-based discovery also
  # works outside the build tree. CPack selects the server component.
  install(TARGETS seekdb_hipvs_hnsw_worker
    RUNTIME DESTINATION usr/bin COMPONENT server)
  install(TARGETS seekdb_hipvs_hnsw
    LIBRARY DESTINATION usr/libexec/seekdb/lib COMPONENT server)
endif()

seekdb_apply_unity_inventory(oblib_lib_simd SEEKDB_OBLIB_SIMD_UNITY)
ob_lib_add_target(oblib_lib_simd)
if(ARCHITECTURE STREQUAL "x86_64")
  target_compile_options(oblib_lib_simd PRIVATE -mavx2 -mssse3)
elseif(ARCHITECTURE STREQUAL "amd64")
  target_compile_options(oblib_lib_simd PRIVATE
    -mavx2 -mssse3 -msse3 -msse4.1)
endif()

seekdb_apply_unity_inventory(oblib_lib_bitmap SEEKDB_OBLIB_BITMAP_UNITY)
ob_lib_add_target(oblib_lib_bitmap)

seekdb_apply_unity_inventory(ob_malloc_object SEEKDB_OBLIB_MALLOC_UNITY)
ob_add_new_object_target(ob_malloc_object ob_malloc_object)
target_link_libraries(ob_malloc_object oblib_base_without_pass)
add_library(ob_malloc STATIC)
target_link_libraries(ob_malloc ob_malloc_object)
if(OB_HAVE_BUNDLED_JEMALLOC)
  target_link_libraries(ob_malloc_object seekdb_jemalloc)
  target_link_libraries(ob_malloc seekdb_jemalloc)
endif()

# malloc_hook is only part of the Linux production runtime.
if(NOT APPLE AND NOT WIN32 AND NOT ANDROID)
  add_library(malloc_hook STATIC ${SEEKDB_OBLIB_MALLOC_HOOK_STANDALONE})
  target_link_libraries(malloc_hook oblib_base)
  if(OB_HAVE_BUNDLED_JEMALLOC)
    target_link_libraries(malloc_hook seekdb_jemalloc)
  endif()
endif()

if(WIN32)
  set(VCPKG_LIB_DIR "${OB_VCPKG_DIR}/lib")
  target_link_libraries(oblib_lib
    PUBLIC ob_malloc compress restore
    ${VCPKG_LIB_DIR}/libxml2.lib)
else()
  target_link_libraries(oblib_lib
    PUBLIC ob_malloc compress restore
    ${DEP_DIR}/lib/libxml2.a
    ${DEP_DIR}/lib/liblzma.a)
endif()

if(ANDROID)
  # Android dependencies are installed directly under DEP_DIR/lib.
  target_link_directories(oblib_lib PUBLIC ${DEP_DIR}/lib)
  target_compile_options(oblib_lib PUBLIC -fopenmp)
  target_link_libraries(oblib_lib PUBLIC
    libvsag_static.a
    libantlr4-autogen.a
    libantlr4-runtime.a
    libdiskann.a
    libsimd.a
    libcpuinfo.a
    libfmt.a
    libopenblas.a
    libspdlog.a
    libroaring.a
    -fopenmp -static-openmp)
else()
  target_link_directories(oblib_lib PUBLIC
    ${DEP_DIR}/lib ${DEP_DIR}/lib/vsag_lib)
  if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    # Apple Clang accepts OpenMP in its frontend; the iOS runtime is linked separately.
    target_compile_options(oblib_lib PUBLIC "SHELL:-Xpreprocessor -fopenmp")
  elseif(UNIX AND CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    # AppleClang's driver does not expose -fopenmp, but its frontend does.
    # Keep using the libomp runtime packaged with the arm64 VSAG libraries.
    target_compile_options(oblib_lib PUBLIC "SHELL:-Xclang -fopenmp")
  elseif(UNIX)
    target_compile_options(oblib_lib PUBLIC -fopenmp)
  endif()

  if(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
    target_link_libraries(oblib_lib PUBLIC
      libcpuinfo.a
      libsimd.a
      libvsag_static.a
      libdiskann.a
      libopenblas.a
      libgfortran_static.a
      libquadmath_static.a
      libgomp_static.a
      libroaring.a
      libantlr4-runtime.a
      libantlr4-autogen.a
      libfmt.a)
  elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    target_link_libraries(oblib_lib PUBLIC
      libcpuinfo.a
      libsimd.a
      libvsag_static.a
      libdiskann.a
      libopenblas.a
      libgfortran_static.a
      libgomp_static.a
      libroaring.a
      libantlr4-runtime.a
      libantlr4-autogen.a
      libfmt.a)
  elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
    target_link_libraries(oblib_lib PUBLIC
      libcpuinfo.a
      libsimd.a
      libvsag_static.a
      libdiskann.a
      libio.a
      libroaring.a
      libantlr4-runtime.a
      libantlr4-autogen.a
      libfmt.a)
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
      target_link_libraries(oblib_lib PUBLIC
        libomp.a liblapacke.a "-framework Accelerate")
    else()
      target_link_libraries(oblib_lib PUBLIC
        libgfortran_static.a libquadmath_static.a libgcc.a
        libomp_static.a libopenblas.a)
    endif()
  elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "AMD64")
    target_link_directories(oblib_lib PUBLIC ${OB_VSAG_DIR}/lib)
    target_include_directories(oblib_lib PUBLIC ${OB_VSAG_DIR}/include)
    target_link_libraries(oblib_lib PUBLIC
      cpuinfo.lib
      simd.lib
      vsag_static.lib
      diskann.lib
      openblas.lib
      libomp.lib
      roaring.lib
      antlr4-runtime-static.lib
      antlr4-autogen.lib
      fmt.lib)
  else()
    message(FATAL_ERROR "Unsupported architecture: ${CMAKE_SYSTEM_PROCESSOR}")
  endif()
endif()
