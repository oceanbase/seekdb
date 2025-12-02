# 混合搜索模块 CMakeLists.txt
# 用于编译向量和全文搜索混合融合系统

# 项目名称和版本
project(hybrid_search C CXX)
cmake_minimum_required(VERSION 3.10)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 包含目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_SOURCE_DIR}/src)
include_directories(${CMAKE_SOURCE_DIR}/deps)

# 源文件列表
set(HYBRID_SEARCH_SOURCES
  ob_rrf_fusion.cpp
  ob_weighted_fusion.cpp
)

set(HYBRID_SEARCH_HEADERS
  ob_hybrid_search_common.h
  ob_rrf_fusion.h
  ob_weighted_fusion.h
)

# 创建静态库
add_library(hybrid_search STATIC ${HYBRID_SEARCH_SOURCES})

# 链接依赖
target_link_libraries(hybrid_search 
  PRIVATE 
    oceanbase_common
    oceanbase_lib
)

# 如果启用测试
if(ENABLE_TESTING)
  # 查找 Google Test
  find_package(GTest REQUIRED)
  
  # 创建测试可执行文件
  add_executable(test_hybrid_search 
    test_hybrid_search.cpp
    ${HYBRID_SEARCH_SOURCES}
  )
  
  # 链接测试依赖
  target_link_libraries(test_hybrid_search
    PRIVATE
      ${GTEST_LIBRARIES}
      oceanbase_common
      oceanbase_lib
  )
  
  # 注册测试
  add_test(NAME HybridSearchTests COMMAND test_hybrid_search)
endif()

# 创建演示可执行文件
add_executable(hybrid_search_demo
  hybrid_search_demo.cpp
  ${HYBRID_SEARCH_SOURCES}
)

# 链接演示依赖
target_link_libraries(hybrid_search_demo
  PRIVATE
    oceanbase_common
    oceanbase_lib
)

# 安装库文件
install(TARGETS hybrid_search
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
)

# 安装头文件
install(FILES ${HYBRID_SEARCH_HEADERS}
  DESTINATION include/oceanbase/common/hybrid_search
)

# 安装文档
install(FILES 
  HYBRID_SEARCH_GUIDE.sql
  DEVELOPMENT_GUIDE.sql
  README.md
  DESTINATION share/doc/oceanbase/hybrid_search
)

# 信息输出
message(STATUS "Hybrid Search Module Configuration:")
message(STATUS "  Sources: ${HYBRID_SEARCH_SOURCES}")
message(STATUS "  Headers: ${HYBRID_SEARCH_HEADERS}")
message(STATUS "  Testing: ${ENABLE_TESTING}")
message(STATUS "  Demo: YES")
