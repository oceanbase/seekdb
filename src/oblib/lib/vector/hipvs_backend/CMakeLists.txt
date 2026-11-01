cmake_minimum_required(VERSION 3.24)
project(seekdb_hipvs LANGUAGES CXX)

# Standalone on purpose: the normal seekdb build has no ROCm dependency.
# Configure with -DCMAKE_CXX_COMPILER=/opt/rocm/bin/hipcc and an installed
# ROCm/hipVS prefix whose cuvs library was built with BUILD_CAGRA_HNSWLIB=ON.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
find_package(cuvs CONFIG REQUIRED)
find_package(hip CONFIG REQUIRED)
add_library(seekdb_hipvs SHARED hipvs_build.cpp)
target_compile_definitions(seekdb_hipvs PRIVATE CUVS_BUILD_CAGRA_HNSWLIB)
target_compile_options(seekdb_hipvs PRIVATE -Wall -Wextra)
target_link_libraries(seekdb_hipvs PRIVATE cuvs::cuvs hip::host)
install(TARGETS seekdb_hipvs LIBRARY DESTINATION lib)
