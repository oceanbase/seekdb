add_library(benchmark_base OBJECT
    thread.cpp
    timer.cpp
    benchmark_malloc.cpp
    )
target_link_libraries(benchmark_base oblib_base $<$<AND:$<NOT:$<PLATFORM_ID:Darwin>>,$<NOT:$<PLATFORM_ID:Windows>>>:malloc_hook>)
oblib_addtest(benchmark.cpp)
target_link_libraries(benchmark PRIVATE benchmark_base)
