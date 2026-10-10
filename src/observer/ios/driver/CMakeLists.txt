add_library(seekdb_ios_framework SHARED EXCLUDE_FROM_ALL
  seekdb_lifecycle.cpp ../seekdb_ios.cpp seekdb.h)
target_include_directories(seekdb_ios_framework PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(seekdb_ios_framework PRIVATE
  SEEKDB_IOS_BUILD_ID="${SEEKDB_IOS_BUILD_ID}"
  SEEKDB_IOS_HOOK_MODE="disabled")
target_link_libraries(seekdb_ios_framework PRIVATE oceanbase_static sql_nio)
target_link_options(seekdb_ios_framework PRIVATE
  "-Wl,-exported_symbols_list,${CMAKE_CURRENT_SOURCE_DIR}/exports.txt")
set(SEEKDB_IOS_FRAMEWORK_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/framework" CACHE PATH
  "Directory for independently staged framework artifacts")
set_target_properties(seekdb_ios_framework PROPERTIES
  FRAMEWORK TRUE
  FRAMEWORK_VERSION A
  OUTPUT_NAME SeekDB
  MACOSX_FRAMEWORK_IDENTIFIER org.oceanbase.seekdb
  MACOSX_FRAMEWORK_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
  PUBLIC_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/seekdb.h"
  LIBRARY_OUTPUT_DIRECTORY "${SEEKDB_IOS_FRAMEWORK_OUTPUT_DIRECTORY}"
  INSTALL_NAME_DIR "@rpath"
  BUILD_WITH_INSTALL_NAME_DIR TRUE
  SKIP_BUILD_RPATH TRUE
  XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO
  LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/exports.txt;${CMAKE_CURRENT_SOURCE_DIR}/verify_framework.py")
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_custom_command(TARGET seekdb_ios_framework POST_BUILD
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/verify_framework.py"
    --binary "$<TARGET_FILE:seekdb_ios_framework>"
    --source "${CMAKE_SOURCE_DIR}" --build "${CMAKE_BINARY_DIR}"
    --sdk "${CMAKE_OSX_SYSROOT}" --deployment "${CMAKE_OSX_DEPLOYMENT_TARGET}"
  VERBATIM)
