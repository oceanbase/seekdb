seekdb_apply_unity_inventory(ob_server SEEKDB_OBSERVER_UNITY)
seekdb_apply_standalone_inventory(ob_server SEEKDB_OBSERVER_STANDALONE)
seekdb_apply_standalone_inventory(
  ob_server SEEKDB_OBSERVER_RETRIEVAL_COMPOSITION)
ob_server_add_target(ob_server)

add_library(oceanbase_static STATIC EXCLUDE_FROM_ALL
  ${CMAKE_CURRENT_BINARY_DIR}/ob_version.cpp)
target_link_libraries(oceanbase_static PUBLIC
  ob_base
  "${ob_objects}"
  ob_sql_static
  ob_storage_static
  ob_share_static
  oblib)

# malloc_hook conflicts with LLVM's early global constructors on macOS and is
# not part of the Windows/Android runtime contract.
if(NOT OB_USE_ASAN AND NOT APPLE AND NOT WIN32 AND NOT ANDROID)
  set(link_malloc_hook malloc_hook)
else()
  set(link_malloc_hook "")
endif()

# Pretest is Linux-only and compiles its module binaries against
# liboceanbase.so. Farm jobs pass OB_SO_CACHE=ON after downloading a
# production-built library; local builds materialize the shared implementation
# from the same object graph as seekdb.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  if(OB_SO_CACHE)
    add_library(oceanbase SHARED IMPORTED GLOBAL)
    set_target_properties(oceanbase PROPERTIES
      IMPORTED_LOCATION "${CMAKE_CURRENT_BINARY_DIR}/liboceanbase.so")
    target_link_libraries(oceanbase INTERFACE
      ob_base
      sql_nio
      ${STDC_SHARED_LIB})
  else()
    add_library(oceanbase SHARED EXCLUDE_FROM_ALL
      ${CMAKE_CURRENT_BINARY_DIR}/ob_version.cpp)
    target_link_libraries(oceanbase
      PUBLIC ob_base
      INTERFACE ${STDC_SHARED_LIB}
      PRIVATE
      -Wl,-z,notext
      -Wl,--whole-archive
      "${ob_objects}"
      "${oblib_object_libraries}"
      "${oblib_extra_objects}"
      ob_malloc
      ${link_malloc_hook}
      ob_sql_static
      ob_storage_static
      ob_share_static
      -Wl,--no-whole-archive
      sql_nio
      -static-libgcc
      -static-libstdc++)
  endif()

  if(STDC_SHARED_LIB)
    get_filename_component(STDC_SHARED_LIB_DIR ${STDC_SHARED_LIB} DIRECTORY)
    target_link_options(oceanbase INTERFACE
      "-Wl,-rpath=${STDC_SHARED_LIB_DIR}")
  endif()
endif()

add_library(ob_main OBJECT ${SEEKDB_OBSERVER_MAIN})
if(WIN32)
  target_sources(ob_main PRIVATE ob_win_service.cpp)
endif()
target_link_libraries(ob_main PUBLIC ob_base)

add_executable(seekdb)
if(WIN32)
  target_link_options(seekdb PRIVATE /FORCE:MULTIPLE)
  get_filename_component(CLANG_BIN_DIR ${CMAKE_CXX_COMPILER} DIRECTORY)
  get_filename_component(LLVM_ROOT ${CLANG_BIN_DIR} DIRECTORY)
  file(GLOB CLANG_RT_BUILTINS
    "${LLVM_ROOT}/lib/clang/*/lib/windows/clang_rt.builtins-x86_64.lib")
  if(CLANG_RT_BUILTINS)
    list(GET CLANG_RT_BUILTINS 0 CLANG_RT_BUILTINS_LIB)
    target_link_libraries(seekdb PRIVATE ${CLANG_RT_BUILTINS_LIB})
  endif()
  target_link_libraries(seekdb PRIVATE
    ${CMAKE_BINARY_DIR}/src/oblib/lib/compress/zstd_1_3_8/zstd_1_3_8_objs.lib)
endif()

target_link_libraries(seekdb PRIVATE
  $<$<AND:$<NOT:$<PLATFORM_ID:Darwin>>,$<NOT:$<PLATFORM_ID:Windows>>>:-Wl,--start-group>
  ob_main
  $<$<AND:$<NOT:$<PLATFORM_ID:Darwin>>,$<NOT:$<PLATFORM_ID:Windows>>>:ob_malloc>
  oceanbase_static
  ob_sql_static
  ob_storage_static
  ob_share_static
  sql_nio
  $<$<PLATFORM_ID:Windows>:ob_malloc>
  $<$<PLATFORM_ID:Windows>:synchronization>
  $<$<AND:$<NOT:$<PLATFORM_ID:Darwin>>,$<NOT:$<PLATFORM_ID:Windows>>>:-Wl,--end-group>
  $<$<PLATFORM_ID:Linux>:-static-libgcc>
  $<$<PLATFORM_ID:Linux>:-static-libstdc++>
  ${link_malloc_hook})

if(ENABLE_SANITY)
  target_link_options(seekdb PRIVATE ${SEEKDB_SANITY_WRAP_LINK_OPTIONS})
  if(NOT OB_SO_CACHE)
    target_link_options(oceanbase PRIVATE ${SEEKDB_SANITY_WRAP_LINK_OPTIONS})
  endif()
endif()

if(APPLE AND OB_HAVE_BUNDLED_JEMALLOC)
  # jemalloc's macOS allocator takeover lives in a standalone archive member.
  # Force it into seekdb so its zone registration constructor is retained.
  target_link_options(seekdb PRIVATE "-Wl,-u,_je_zone_register")
endif()

if(WIN32)
  execute_process(
    COMMAND git rev-parse HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_STRIP_TRAILING_WHITESPACE
    OUTPUT_VARIABLE GIT_REVISION
    ERROR_QUIET)
  execute_process(
    COMMAND git rev-parse --abbrev-ref HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_STRIP_TRAILING_WHITESPACE
    OUTPUT_VARIABLE GIT_BRANCH
    ERROR_QUIET)
else()
  execute_process(
    COMMAND sh -c "cd ${CMAKE_SOURCE_DIR} && git rev-parse HEAD"
    OUTPUT_STRIP_TRAILING_WHITESPACE
    OUTPUT_VARIABLE GIT_REVISION)
  execute_process(
    COMMAND sh -c "cd ${CMAKE_SOURCE_DIR} && git rev-parse --abbrev-ref HEAD"
    OUTPUT_STRIP_TRAILING_WHITESPACE
    OUTPUT_VARIABLE GIT_BRANCH)
endif()
if (NOT CMAKE_BUILD_TYPE)
  set(BUILD_FLAGS "default" CACHE INTERNAL "build flags")
elseif(ENABLE_SANITY)
  set(BUILD_FLAGS "${CMAKE_BUILD_TYPE}|Sanity" CACHE INTERNAL "build flags")
else()
  set(BUILD_FLAGS "${CMAKE_BUILD_TYPE}" CACHE INTERNAL "build flags")
endif()

configure_file(../share/ob_version.cpp.in ob_version.cpp)

if(STDC_SHARED_LIB)
  get_filename_component(STDC_SHARED_LIB_DIR ${STDC_SHARED_LIB} DIRECTORY)
  target_link_options(seekdb PRIVATE "-Wl,-rpath=${STDC_SHARED_LIB_DIR}")
endif()

if(WIN32)
  add_custom_command(TARGET seekdb POST_BUILD
    COMMAND ${CMAKE_COMMAND}
      "-DEXE=${CMAKE_BINARY_DIR}/src/observer/seekdb.exe"
      "-DOUT_DIR=${CMAKE_BINARY_DIR}/src/observer"
      "-DSEARCH_DIRS=${OB_VCPKG_DIR}/bin;${OB_VSAG_DIR}/bin"
      -P "${CMAKE_SOURCE_DIR}/cmake/BundleRuntimeDllsWindows.cmake"
    COMMENT "Bundling runtime DLLs next to seekdb.exe"
    VERBATIM)
  add_custom_target(observer ALL
    DEPENDS seekdb
    COMMAND ${CMAKE_COMMAND} -E copy
      ${CMAKE_BINARY_DIR}/src/observer/seekdb.exe
      ${CMAKE_BINARY_DIR}/src/observer/observer.exe)
else()
  add_custom_target(observer ALL
    DEPENDS seekdb
    COMMAND ${CMAKE_COMMAND} -E create_symlink
      ${CMAKE_BINARY_DIR}/src/observer/seekdb
      ${CMAKE_BINARY_DIR}/src/observer/observer)
endif()
