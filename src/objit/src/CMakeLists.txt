add_library(objit_base INTERFACE)
target_include_directories(objit_base INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
  )

# GLOB all source files according to its suffix.
file(GLOB_RECURSE objit_SRC "*.h" "*.cpp" "${CMAKE_CURRENT_SROUCE_DIR}/../include/*.h")
# Exclude the Android stub -- real LLVM implementations are used instead
list(FILTER objit_SRC EXCLUDE REGEX ".*stub\\.cpp$")

add_library(objit_objects OBJECT ${objit_SRC})
target_link_libraries(objit_objects PRIVATE oblib_base objit_base)

# Define a static library target named OBJIT_RAW.
add_library(objit STATIC dummy.cpp)

target_link_libraries(objit
  PUBLIC
  objit_objects objit_base oblib "${llvm_libs}"
  INTERFACE
  $<$<PLATFORM_ID:Linux>:-lrt -ldl -lpthread -lm>
  $<$<PLATFORM_ID:Android>:-ldl -lm>
)
