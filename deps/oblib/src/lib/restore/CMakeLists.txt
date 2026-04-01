oblib_add_library(restore OBJECT
  ob_i_storage.h
  ob_i_storage.cpp
  ob_storage.cpp
  ob_storage_file.cpp
  ob_storage_file.h
  ob_storage.h
  ob_storage_path.cpp
  ob_storage_path.h
  ob_storage_info.cpp
  ob_storage_info.h
  ob_object_device.cpp
  hmac_signature.cpp
  ob_object_device.h
  ob_object_storage_base.cpp
)

oblib_add_library(s3 ob_storage_s3_base.cpp)
if(WIN32)
  # Use Vcpkg-installed AWS SDK: vcpkg install aws-sdk-cpp[s3]; VCPKG_LIB_DIR from deps/oblib/src/CMakeLists.txt
  if(DEFINED VCPKG_LIB_DIR AND EXISTS "${VCPKG_LIB_DIR}/aws-cpp-sdk-s3.lib")
    target_link_directories(s3 PUBLIC ${VCPKG_LIB_DIR})
    target_link_libraries(s3
      PUBLIC
      ${VCPKG_LIB_DIR}/aws-cpp-sdk-s3.lib
      ${VCPKG_LIB_DIR}/aws-cpp-sdk-core.lib
      ${VCPKG_LIB_DIR}/aws-crt-cpp.lib
      ${VCPKG_LIB_DIR}/aws-c-event-stream.lib
      ${VCPKG_LIB_DIR}/aws-c-s3.lib
      ${VCPKG_LIB_DIR}/aws-c-auth.lib
      ${VCPKG_LIB_DIR}/aws-c-http.lib
      ${VCPKG_LIB_DIR}/aws-c-io.lib
      ${VCPKG_LIB_DIR}/aws-c-compression.lib
      ${VCPKG_LIB_DIR}/aws-c-cal.lib
      ${VCPKG_LIB_DIR}/aws-c-sdkutils.lib
      ${VCPKG_LIB_DIR}/aws-checksums.lib
      ${VCPKG_LIB_DIR}/aws-c-common.lib
    )
    # aws-c-mqtt may be optional for S3-only; add if present
    if(EXISTS "${VCPKG_LIB_DIR}/aws-c-mqtt.lib")
      target_link_libraries(s3 PUBLIC ${VCPKG_LIB_DIR}/aws-c-mqtt.lib)
    endif()
  endif()
else()
  target_link_directories(s3 PUBLIC ${DEP_3RD_DIR}/usr/local/oceanbase/deps/devel/lib64 ${DEP_3RD_DIR}/usr/local/oceanbase/deps/devel/lib)
  target_link_libraries(s3
    PUBLIC
    libaws-cpp-sdk-s3.a
    libaws-cpp-sdk-core.a
    libaws-crt-cpp.a
    libaws-c-mqtt.a
    libaws-c-event-stream.a
    libaws-c-s3.a
    libaws-c-auth.a
    libaws-c-http.a
    libaws-c-io.a
    $<$<NOT:$<PLATFORM_ID:Darwin>>:libs2n.a>
    libaws-c-compression.a
    libaws-c-cal.a
    libaws-c-sdkutils.a
    libaws-checksums.a
    libaws-c-common.a
  )
endif()
target_link_libraries(s3 PUBLIC oblib_base_without_pass)

target_link_libraries(restore PUBLIC oblib_base s3)
