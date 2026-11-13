# CMakeLists.txt for SeekDB Java JNI binding
# Builds libseekdb_jni.so (Linux) or libseekdb_jni.dylib (macOS)
# Links against libseekdb from build_release

cmake_minimum_required(VERSION 3.10)
project(seekdb_jni C)

set(CMAKE_C_STANDARD 11)

# Paths relative to unittest/include/java/
set(SEEKDB_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../src/include")
set(SEEKDB_BUILD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../build_release/src/include")

# Find JNI
find_package(JNI REQUIRED)

add_library(seekdb_jni SHARED native/seekdb_jni.c)

target_include_directories(seekdb_jni PRIVATE
    ${JNI_INCLUDE_DIRS}
    ${SEEKDB_SRC_DIR}
)

target_link_directories(seekdb_jni PRIVATE ${SEEKDB_BUILD_DIR})
if(WIN32)
  target_link_libraries(seekdb_jni PRIVATE "${SEEKDB_BUILD_DIR}/seekdb.lib")
else()
  target_link_libraries(seekdb_jni PRIVATE seekdb)
endif()

# Set rpath so libseekdb can be found at runtime (Unix; Windows uses PATH / java.library.path)
if(APPLE)
    set_target_properties(seekdb_jni PROPERTIES
        INSTALL_RPATH "${SEEKDB_BUILD_DIR}"
        BUILD_WITH_INSTALL_RPATH TRUE
        SOVERSION 1
    )
    set_target_properties(seekdb_jni PROPERTIES
        INSTALL_NAME_DIR "${SEEKDB_BUILD_DIR}"
    )
elseif(UNIX)
    set_target_properties(seekdb_jni PROPERTIES
        INSTALL_RPATH "${SEEKDB_BUILD_DIR}"
        BUILD_WITH_INSTALL_RPATH TRUE
        SOVERSION 1
    )
endif()
