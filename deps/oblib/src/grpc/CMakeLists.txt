set(PROTO_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(PROTO_GEN_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

set(PROTO_NAMES
  example
  storageservice
  logservice
  serverservice
)

# On Linux: grpc libs are in lib/grpc/, abseil/protobuf libs are in lib64/grpc/
# On macOS: all libs are in lib/grpc/
if(APPLE OR OB_ANDROID)
  set(GRPC_LIB_DIR "lib")
  set(ABSL_LIB_DIR "lib")
else()
  set(GRPC_LIB_DIR "lib")
  set(ABSL_LIB_DIR "lib64")
endif()

if(WIN32)
  list(APPEND CMAKE_PREFIX_PATH "${OB_VCPKG_DIR}")
  set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release)
  set(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL Release)
  find_package(gRPC CONFIG REQUIRED)
  find_package(protobuf CONFIG REQUIRED)
  set(_PROTOC $<TARGET_FILE:protobuf::protoc>)
  set(_GRPC_CPP_PLUGIN $<TARGET_FILE:gRPC::grpc_cpp_plugin>)
else()
  set(DEVEL_PATH "${DEP_3RD_DIR}/usr/local/oceanbase/deps/devel")
  option(protobuf_MODULE_COMPATIBLE TRUE)
  set(_PROTOC "${DEVEL_PATH}/bin/protoc")
  set(_GRPC_CPP_PLUGIN "${DEVEL_PATH}/bin/grpc_cpp_plugin")
  set(_PROTOC_LD_PATH "${DEVTOOLS_DIR}/lib64")
  include_directories(
    ${DEP_3RD_DIR}/usr/include
    ${DEVEL_PATH}/include
  )
endif()

# Generate .pb.cc/.pb.h and .grpc.pb.cc/.grpc.pb.h from .proto files at build time
set(GENERATED_SRCS)
foreach(PROTO_NAME ${PROTO_NAMES})
  set(PROTO_FILE "${PROTO_SRC_DIR}/${PROTO_NAME}.proto")
  set(PB_CC   "${PROTO_GEN_DIR}/${PROTO_NAME}.pb.cc")
  set(PB_H    "${PROTO_GEN_DIR}/${PROTO_NAME}.pb.h")
  set(GRPC_CC "${PROTO_GEN_DIR}/${PROTO_NAME}.grpc.pb.cc")
  set(GRPC_H  "${PROTO_GEN_DIR}/${PROTO_NAME}.grpc.pb.h")

  add_custom_command(
    OUTPUT ${PB_CC} ${PB_H}
    COMMAND ${CMAKE_COMMAND} -E env "LD_LIBRARY_PATH=${_PROTOC_LD_PATH}:$ENV{LD_LIBRARY_PATH}"
      ${_PROTOC}
      --cpp_out=${PROTO_GEN_DIR}
      -I${PROTO_SRC_DIR}
      ${PROTO_FILE}
    DEPENDS ${PROTO_FILE}
    COMMENT "Generating protobuf C++ from ${PROTO_NAME}.proto"
  )

  add_custom_command(
    OUTPUT ${GRPC_CC} ${GRPC_H}
    COMMAND ${CMAKE_COMMAND} -E env "LD_LIBRARY_PATH=${_PROTOC_LD_PATH}:$ENV{LD_LIBRARY_PATH}"
      ${_PROTOC}
      --grpc_out=${PROTO_GEN_DIR}
      --plugin=protoc-gen-grpc=${_GRPC_CPP_PLUGIN}
      -I${PROTO_SRC_DIR}
      ${PROTO_FILE}
    DEPENDS ${PROTO_FILE}
    COMMENT "Generating gRPC C++ from ${PROTO_NAME}.proto"
  )

  list(APPEND GENERATED_SRCS ${PB_CC} ${GRPC_CC})
endforeach()

add_library(oblib_grpc STATIC
  ${GENERATED_SRCS}
  ob_grpc_context.cpp
  ob_grpc_server.cpp
)

target_include_directories(oblib_grpc
  PUBLIC ${PROTO_GEN_DIR}
)

if(WIN32)
  target_link_libraries(oblib_grpc
    PUBLIC oblib_base
    gRPC::grpc++
    gRPC::grpc++_reflection
    protobuf::libprotobuf
  )
else()
  set(GRPC_STATIC_LIB_DIR "${DEVEL_PATH}/${GRPC_LIB_DIR}/grpc")
  set(ABSL_STATIC_LIB_DIR "${DEVEL_PATH}/${ABSL_LIB_DIR}/grpc")

  set(GRPC_CORE_LIB_NAMES
    grpc++ grpc grpc_unsecure grpc++_reflection grpc++_error_details
    grpc_plugin_support grpcpp_channelz gpr
    address_sorting re2 upb
  )

  set(ABSL_LIB_NAMES
    absl_base absl_int128 absl_throw_delegate absl_raw_logging_internal
    absl_log_severity absl_spinlock_wait absl_malloc_internal
    absl_debugging_internal absl_demangle_internal absl_stacktrace
    absl_symbolize absl_examine_stack absl_failure_signal_handler
    absl_strerror
    absl_strings absl_strings_internal absl_str_format_internal
    absl_cord absl_cord_internal absl_cordz_info absl_cordz_handle
    absl_cordz_functions absl_cordz_sample_token
    absl_hash absl_low_level_hash absl_city
    absl_raw_hash_set absl_hashtablez_sampler
    absl_status absl_statusor
    absl_bad_any_cast_impl absl_bad_optional_access absl_bad_variant_access
    absl_synchronization absl_graphcycles_internal absl_time absl_time_zone
    absl_civil_time absl_exponential_biased absl_periodic_sampler
    absl_random_distributions absl_random_seed_sequences
    absl_random_seed_gen_exception absl_random_internal_pool_urbg
    absl_random_internal_randen absl_random_internal_randen_hwaes
    absl_random_internal_randen_hwaes_impl absl_random_internal_randen_slow
    absl_random_internal_seed_material absl_random_internal_platform
    absl_random_internal_distribution_test_util
    absl_flags absl_flags_commandlineflag absl_flags_commandlineflag_internal
    absl_flags_config absl_flags_internal absl_flags_marshalling
    absl_flags_parse absl_flags_private_handle_accessor
    absl_flags_program_name absl_flags_reflection
    absl_flags_usage absl_flags_usage_internal
    absl_leak_check absl_leak_check_disable absl_scoped_set_env
  )

  set(_GRPC_STATIC_LIBS)
  foreach(_name ${GRPC_CORE_LIB_NAMES})
    list(APPEND _GRPC_STATIC_LIBS "${GRPC_STATIC_LIB_DIR}/lib${_name}.a")
  endforeach()
  foreach(_name ${ABSL_LIB_NAMES})
    list(APPEND _GRPC_STATIC_LIBS "${ABSL_STATIC_LIB_DIR}/lib${_name}.a")
  endforeach()

  target_link_libraries(oblib_grpc
    PUBLIC oblib_base
    ${_GRPC_STATIC_LIBS}
    ${ABSL_STATIC_LIB_DIR}/libcares.a
    ${ABSL_STATIC_LIB_DIR}/libprotobuf.a
  )
endif()

# macOS requires libresolv for c-ares DNS resolution
if(APPLE)
  target_link_libraries(oblib_grpc PUBLIC resolv)
endif()
