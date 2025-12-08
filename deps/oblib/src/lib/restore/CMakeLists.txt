add_definitions(-D_GLIBCXX_USE_CXX11_ABI=0)
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
  hdfs/ob_storage_hdfs_cache.cpp
  hdfs/ob_storage_hdfs_cache.h
  hdfs/ob_storage_hdfs_jni_base.cpp
  hdfs/ob_storage_hdfs_jni_base.h
  ob_object_device.cpp
  hmac_signature.cpp
  ob_object_device.h
  ob_object_storage_base.cpp
)

oblib_add_library(s3 ob_storage_s3_base.cpp)
target_link_directories(s3 PUBLIC ${DEP_3RD_DIR}/usr/local/oceanbase/deps/devel/lib64)
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
  libs2n.a
  libaws-c-compression.a
  libaws-c-cal.a
  libaws-c-sdkutils.a
  libaws-checksums.a
  libaws-c-common.a
)
target_link_libraries(s3 PUBLIC oblib_base_without_pass)

target_link_libraries(restore PUBLIC oblib_base s3)
