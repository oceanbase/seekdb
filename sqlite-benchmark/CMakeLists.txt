cmake_minimum_required(VERSION 3.14)

set(BENCH_SOURCES speedtest1 simple_insert)

# simple_insert uses POSIX-only APIs (unistd.h, pthread, clock_gettime with
# CLOCK_MONOTONIC). Skip it on Windows until it gets a portable rewrite.
if(WIN32)
    list(REMOVE_ITEM BENCH_SOURCES simple_insert)
endif()

# Resolve SQLite include/library per-platform.
#   - Windows: use vcpkg-provided sqlite3 (OB_VCPKG_DIR is defined in cmake/Env.cmake)
#   - Linux/macOS: use the bundled devdeps sqlite (DEP_DIR is defined in cmake/Env.cmake)
if(WIN32)
    set(SQLITE_INCLUDE_DIR "${OB_VCPKG_DIR}/include")
    set(SQLITE_LIBRARY     "${OB_VCPKG_DIR}/lib/sqlite3.lib")
    set(SQLITE_EXTRA_LIBS  "")
else()
    set(SQLITE_INCLUDE_DIR "${DEP_DIR}/include/sqlite")
    set(SQLITE_LIBRARY     "${DEP_DIR}/lib/sqlite/libsqlite3.a")
    set(SQLITE_EXTRA_LIBS  $<$<NOT:$<BOOL:${ANDROID}>>:pthread> dl m)
endif()

if(NOT EXISTS "${SQLITE_LIBRARY}")
    message(WARNING
        "sqlite-benchmark: SQLite library not found at ${SQLITE_LIBRARY}; "
        "skipping *_sqlite benchmark targets.")
else()
    foreach(t ${BENCH_SOURCES})
        add_executable(${t}_sqlite ${t}.c)
        target_include_directories(${t}_sqlite PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        target_compile_definitions(${t}_sqlite PRIVATE USE_BACKEND_SQLITE)
        target_include_directories(${t}_sqlite PRIVATE ${SQLITE_INCLUDE_DIR})
        # speedtest1.c is upstream SQLite benchmark code; silence MSVC CRT
        # deprecation (fopen/unlink) and const-qualifier-drop warnings so the
        # build stays quiet on Windows without touching vendored sources.
        if(WIN32)
            target_compile_definitions(${t}_sqlite PRIVATE
                _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
            target_compile_options(${t}_sqlite PRIVATE
                -Wno-incompatible-pointer-types-discards-qualifiers
                -Wno-deprecated-declarations)
        endif()
        target_link_libraries(${t}_sqlite PRIVATE ${SQLITE_LIBRARY} ${SQLITE_EXTRA_LIBS})
    endforeach()
endif()

# Build SeekDB backend when available
if(TARGET seekdb_embed_c)
    if(WIN32)
        set(SEEKDB_EXTRA_LIBS "")
    else()
        set(SEEKDB_EXTRA_LIBS $<$<NOT:$<BOOL:${ANDROID}>>:pthread> dl m)
    endif()
    foreach(t ${BENCH_SOURCES})
        add_executable(${t}_seekdb ${t}.c)
        target_include_directories(${t}_seekdb PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        target_compile_definitions(${t}_seekdb PRIVATE USE_BACKEND_SEEKDB)
        target_link_libraries(${t}_seekdb PRIVATE seekdb_embed_c ${SEEKDB_EXTRA_LIBS})
    endforeach()
endif()
