cmake_minimum_required(VERSION 3.14)

set(SQLITE_BASE ${CMAKE_SOURCE_DIR}/deps/3rd/usr/local/oceanbase/deps/devel)
set(BENCH_SOURCES speedtest1 simple_insert)

# Always build SQLite backend
foreach(t ${BENCH_SOURCES})
    add_executable(${t}_sqlite ${t}.c)
    target_include_directories(${t}_sqlite PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(${t}_sqlite PRIVATE USE_BACKEND_SQLITE)
    target_include_directories(${t}_sqlite PRIVATE ${SQLITE_BASE}/include/sqlite)
    target_link_libraries(${t}_sqlite PRIVATE ${SQLITE_BASE}/lib/sqlite/libsqlite3.a $<$<NOT:$<BOOL:${ANDROID}>>:pthread> dl m)
endforeach()

# Build SeekDB backend when available
if(TARGET seekdb_embed_c)
    foreach(t ${BENCH_SOURCES})
        add_executable(${t}_seekdb ${t}.c)
        target_include_directories(${t}_seekdb PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        target_compile_definitions(${t}_seekdb PRIVATE USE_BACKEND_SEEKDB)
        target_link_libraries(${t}_seekdb PRIVATE seekdb_embed_c $<$<NOT:$<BOOL:${ANDROID}>>:pthread> dl m)
    endforeach()
endif()
