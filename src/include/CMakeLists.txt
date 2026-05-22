# C API for SeekDB
# Provides C API for multi-language bindings (Node.js N-API, FFI, Rust, Go, etc.)

# Note: ENABLE_INITIAL_EXEC_TLS_MODEL removal is handled in src/CMakeLists.txt
# before observer subdirectory is added, ensuring liboceanbase.so uses local-dynamic TLS
# TLS model is controlled by BUILD_EMBED_MODE in cmake/Env.cmake

# Set source files
set(FFI_SOURCES
  seekdb.cpp
)

# Create object library for FFI
ob_set_subtarget(seekdb_object_list common
  ${FFI_SOURCES}
)

ob_add_new_object_target(seekdb_objects seekdb_object_list)

# seekdb.cpp pulls headers that include generated gRPC/protobuf outputs under deps/oblib/src/grpc/.
# Ensure protoc runs before compiling libseekdb / seekdb_objects (Ninja can otherwise race).
add_dependencies(seekdb_objects oblib_grpc)

target_include_directories(seekdb_objects
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/libtable/src
)

# Link against oceanbase for object compilation (headers/symbols)
target_link_libraries(seekdb_objects PUBLIC oceanbase)

# Create self-contained shared library for FFI (target name: libseekdb to avoid
# conflict with the seekdb custom target in src/observer/CMakeLists.txt)
# This statically links liboceanbase_static.a into libseekdb.so
# so users only need this single .so file
add_library(libseekdb
  SHARED
  ${FFI_SOURCES}
)

add_dependencies(libseekdb oblib_grpc)

target_include_directories(libseekdb
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/libtable/src
)

# Link oceanbase_static into the shared library
# Use --whole-archive (Linux/Android) / -force_load (macOS) to include all symbols from static library
# On Linux desktop: -static-libstdc++ and -static-libgcc to avoid runtime dependency on system C++ libs
# Android NDK: use libc++ from sysroot; no static-libstdc++/libgcc (same idea as oceanbase target)
# (macOS/clang does not support -static-libgcc/-static-libstdc++)
# Use version script (Linux/Android) / exported_symbols_list (macOS) to hide internal symbols
if(APPLE)
  # macOS: -exported_symbols_list does NOT support wildcards; generate list from seekdb.h
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/seekdb.h" SEEKDB_HEADER_CONTENT)
  string(REGEX MATCHALL "seekdb_[a-zA-Z0-9_]+" SEEKDB_SYMBOLS "${SEEKDB_HEADER_CONTENT}")
  list(REMOVE_DUPLICATES SEEKDB_SYMBOLS)
  list(FILTER SEEKDB_SYMBOLS EXCLUDE REGEX "seekdb_cell_callback_t")
  # Exclude false positives from comments (e.g. "seekdb_stmt_*" yields "seekdb_stmt_")
  list(FILTER SEEKDB_SYMBOLS EXCLUDE REGEX "_$")
  list(SORT SEEKDB_SYMBOLS)
  set(SEEKDB_EXPORT_CONTENT "# Generated from seekdb.h - do not edit\n")
  foreach(SYM ${SEEKDB_SYMBOLS})
    # macOS C symbols have leading underscore in the symbol table
    string(APPEND SEEKDB_EXPORT_CONTENT "_${SYM}\n")
  endforeach()
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/seekdb_export.txt" "${SEEKDB_EXPORT_CONTENT}")
  set(SEEKDB_EXPORT_FILE "${CMAKE_CURRENT_BINARY_DIR}/seekdb_export.txt")

  # macOS: use -force_load only for liboceanbase_static.a to avoid -all_load pulling in both
  # oblib (with its snappy/lz4) and deps/3rd liblz4.a/libsnappy.a, which causes 68 duplicate symbols.
  # Linking oceanbase_static without -all_load may yield undefined symbols; if so we need to revisit.
  target_link_options(libseekdb PRIVATE "LINKER:-force_load,$<TARGET_FILE:oceanbase_static>")
  target_link_libraries(libseekdb
    PRIVATE
      oceanbase_static
      -Wl,-exported_symbols_list,${SEEKDB_EXPORT_FILE}
  )
elseif(ANDROID)
  # NDK: libc++ from sysroot; do not use -static-libstdc++ / -static-libgcc (Linux desktop only).
  target_link_libraries(libseekdb
    PRIVATE
      -Wl,--whole-archive
      oceanbase_static
      -Wl,--no-whole-archive
      -Wl,--allow-multiple-definition
      -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/seekdb.version
  )
elseif(WIN32)
  # Windows (Clang + lld): export C API from seekdb.cpp; pull full static archive like Linux --whole-archive.
  set_target_properties(libseekdb PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
  # Same Itanium ABI stubs as observer_without_bolt — PL/shared code references _Unwind_* (non-functional on Win).
  target_sources(libseekdb PRIVATE "${CMAKE_SOURCE_DIR}/src/observer/win32_unwind_stubs.c")
  target_link_options(libseekdb PRIVATE /FORCE:MULTIPLE
    "/WHOLEARCHIVE:$<TARGET_FILE:oceanbase_static>")
  # oceanbase_static is EXCLUDE_FROM_ALL and only referenced via LINK_OPTIONS; some generators do not
  # infer a build dependency, so ninja may link libseekdb before oceanbase_static.lib exists.
  add_dependencies(libseekdb oceanbase_static)
  get_filename_component(_seekdb_clang_bin_dir ${CMAKE_CXX_COMPILER} DIRECTORY)
  get_filename_component(_seekdb_llvm_root ${_seekdb_clang_bin_dir} DIRECTORY)
  file(GLOB _seekdb_clang_rt "${_seekdb_llvm_root}/lib/clang/*/lib/windows/clang_rt.builtins-x86_64.lib")
  if(_seekdb_clang_rt)
    list(GET _seekdb_clang_rt 0 _seekdb_clang_rt_lib)
    target_link_libraries(libseekdb PRIVATE ${_seekdb_clang_rt_lib})
  endif()
  # oceanbase_static.lib only carries its own members; CMake does not merge PUBLIC static deps into it
  # on MSVC. Link the same concrete libs as oceanbase_static PUBLIC (cf. observer_without_bolt), or
  # symbols such as ObSqlString (~destructor in oblib) stay unresolved.
  target_link_libraries(libseekdb PRIVATE
    ob_sql_static
    ob_storage_static
    ob_share_static
    oblib
    objit
    ob_malloc
    synchronization
    ${CMAKE_BINARY_DIR}/deps/oblib/src/lib/compress/zstd_1_3_8/zstd_1_3_8_objs.lib
    ${CMAKE_BINARY_DIR}/deps/oblib/src/lib/compress/zstd/zstd_objs.lib
    ${CMAKE_BINARY_DIR}/deps/oblib/src/lib/compress/lz4/lz4-all.lib
  )
  # oceanbase_static PUBLIC links Hyperscan (see src/observer/CMakeLists.txt); DLL link does not inherit it.
  if(HYPERSCAN_LIB)
    target_link_libraries(libseekdb PRIVATE ${HYPERSCAN_LIB})
  endif()
  # Bundle third-party runtime DLLs next to seekdb.dll (mirrors seekdb.exe POST_BUILD in
  # src/observer/CMakeLists.txt). Binding tests load seekdb.dll from src/include via ctypes;
  # without colocated DLLs, Python 3.8+ LoadLibrary search does not use PATH for dependencies.
  add_custom_command(TARGET libseekdb POST_BUILD
    COMMAND ${CMAKE_COMMAND}
      "-DEXE=$<TARGET_FILE:libseekdb>"
      "-DOUT_DIR=$<TARGET_FILE_DIR:libseekdb>"
      "-DSEARCH_DIRS=${OB_VCPKG_DIR}/bin;${OB_VSAG_DIR}/bin;${OB_OPENSSL_DIR}/bin"
      -P "${CMAKE_SOURCE_DIR}/cmake/BundleRuntimeDllsWindows.cmake"
    COMMENT "Bundling runtime DLLs next to seekdb.dll"
    VERBATIM)
else()
  target_link_libraries(libseekdb
    PRIVATE
      -Wl,--whole-archive
      oceanbase_static
      -Wl,--no-whole-archive
      -static-libstdc++
      -static-libgcc
      -Wl,--allow-multiple-definition
      -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/seekdb.version
  )
endif()

# Output file name remains libseekdb.so / libseekdb.dylib
set_target_properties(libseekdb PROPERTIES
  OUTPUT_NAME "seekdb"
)

# Strip debug symbols in release builds to reduce library size
if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT CMAKE_BUILD_TYPE STREQUAL "debug")
  # Android on macOS host: CMAKE_STRIP still points at host strip (Mach-O only); use NDK llvm-strip for ELF.
  # NDK path: CMAKE_ANDROID_NDK from android.toolchain.cmake, else same derivation as cmake/Env.cmake from CMAKE_TOOLCHAIN_FILE.
  # Do not rely on ENV{ANDROID_NDK_HOME}: build.sh does not export it, so CMake often has no such env.
  if(ANDROID)
    set(_seekdb_ndk "")
    if(CMAKE_ANDROID_NDK)
      set(_seekdb_ndk "${CMAKE_ANDROID_NDK}")
    elseif(CMAKE_TOOLCHAIN_FILE)
      # CMAKE_TOOLCHAIN_FILE is .../ndk/<ver>/build/cmake/android.toolchain.cmake; we need the NDK root for toolchains/.
      # Each DIRECTORY step goes up one level: cmake -> build -> NDK root (three times; same as cmake/Env.cmake).
      get_filename_component(_seekdb_ndk "${CMAKE_TOOLCHAIN_FILE}" DIRECTORY)
      get_filename_component(_seekdb_ndk "${_seekdb_ndk}" DIRECTORY)
      get_filename_component(_seekdb_ndk "${_seekdb_ndk}" DIRECTORY)
    endif()
    set(_seekdb_llvm_strip "")
    if(_seekdb_ndk)
      file(GLOB _seekdb_llvm_strip "${_seekdb_ndk}/toolchains/llvm/prebuilt/*/bin/llvm-strip")
      if(_seekdb_llvm_strip)
        list(GET _seekdb_llvm_strip 0 _seekdb_llvm_strip)
      endif()
    endif()
    if(_seekdb_llvm_strip)
      add_custom_command(TARGET libseekdb POST_BUILD
        COMMAND "${_seekdb_llvm_strip}" $<TARGET_FILE:libseekdb>
        COMMENT "Stripping debug symbols from libseekdb (Android ELF via NDK llvm-strip)"
      )
    else()
      message(WARNING "Android libseekdb: llvm-strip not found under NDK; skipping strip")
    endif()
  elseif(APPLE)
    # macOS: -S strip __DWARF (debug), -x strip local symbol table; full strip fails due to indirect symbol table
    add_custom_command(TARGET libseekdb POST_BUILD
      COMMAND ${CMAKE_STRIP} -Sx $<TARGET_FILE:libseekdb>
      COMMENT "Stripping debug and local symbols from libseekdb (macOS)"
    )
  elseif(UNIX)
    add_custom_command(TARGET libseekdb POST_BUILD
      COMMAND ${CMAKE_STRIP} $<TARGET_FILE:libseekdb>
      COMMENT "Stripping debug symbols from libseekdb to reduce size"
    )
  endif()
endif()

# Install the shared library and header
install(TARGETS libseekdb
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  RUNTIME DESTINATION bin
)

install(FILES seekdb.h
  DESTINATION include
)
