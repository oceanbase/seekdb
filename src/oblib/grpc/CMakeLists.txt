if(NOT OB_ENABLE_STANDBY)
  return()
endif()

if(WIN32)
  list(APPEND CMAKE_PREFIX_PATH "${OB_VCPKG_DIR}")
  find_package(gRPC CONFIG REQUIRED)
  find_package(protobuf CONFIG REQUIRED)

  # The checked-in stubs target the protobuf version used by Unix builds.
  # Generate Windows stubs with vcpkg's matching protoc/runtime pair instead.
  set(STANDBY_PROTO "${CMAKE_CURRENT_SOURCE_DIR}/standbyservice.proto")
  # Use the project's top-level generated include root so these headers take
  # precedence over the checked-in Unix stubs in every consuming target.
  set(STANDBY_PROTO_GEN_DIR "${CMAKE_BINARY_DIR}/generated")
  set(STANDBY_PROTO_GEN_GRPC_DIR "${STANDBY_PROTO_GEN_DIR}/grpc")
  set(STANDBY_PROTO_SRCS
    "${STANDBY_PROTO_GEN_GRPC_DIR}/standbyservice.pb.cc"
    "${STANDBY_PROTO_GEN_GRPC_DIR}/standbyservice.grpc.pb.cc")
  set(STANDBY_PROTO_HDRS
    "${STANDBY_PROTO_GEN_GRPC_DIR}/standbyservice.pb.h"
    "${STANDBY_PROTO_GEN_GRPC_DIR}/standbyservice.grpc.pb.h")
  get_target_property(PROTOC_EXECUTABLE protobuf::protoc IMPORTED_LOCATION_RELEASE)
  get_target_property(GRPC_CPP_PLUGIN_EXECUTABLE gRPC::grpc_cpp_plugin IMPORTED_LOCATION_RELEASE)
  file(MAKE_DIRECTORY "${STANDBY_PROTO_GEN_DIR}")
  execute_process(
    COMMAND "${PROTOC_EXECUTABLE}"
      --proto_path=${CMAKE_CURRENT_SOURCE_DIR}/..
      --cpp_out=${STANDBY_PROTO_GEN_DIR}
      --grpc_out=${STANDBY_PROTO_GEN_DIR}
      --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN_EXECUTABLE}
      grpc/standbyservice.proto
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
    RESULT_VARIABLE STANDBY_PROTOC_RESULT
    ERROR_VARIABLE STANDBY_PROTOC_ERROR)
  if(NOT STANDBY_PROTOC_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to generate Windows standby gRPC sources: ${STANDBY_PROTOC_ERROR}")
  endif()
  set_source_files_properties(${STANDBY_PROTO_SRCS} ${STANDBY_PROTO_HDRS}
    PROPERTIES GENERATED TRUE)
else()
  set(STANDBY_PROTO_SRCS
    standbyservice.pb.cc
    standbyservice.grpc.pb.cc)
endif()

oblib_add_library(oblib_grpc
  ob_grpc_context.cpp
  ob_grpc_server.cpp
  ${STANDBY_PROTO_SRCS}
)

if(WIN32)
  target_include_directories(oblib_grpc BEFORE PUBLIC "${STANDBY_PROTO_GEN_DIR}")
  target_link_libraries(oblib_grpc PUBLIC
    oblib_base
    gRPC::grpc++
    gRPC::grpc++_reflection
    protobuf::libprotobuf)
else()
  if(APPLE OR OB_ANDROID)
    set(GRPC_LIB_DIR "lib")
    set(ABSL_LIB_DIR "lib")
  else()
    set(GRPC_LIB_DIR "lib")
    set(ABSL_LIB_DIR "lib64")
  endif()

  set(GRPC_STATIC_LIB_DIR "${DEP_DIR}/${GRPC_LIB_DIR}/grpc")
  set(ABSL_STATIC_LIB_DIR "${DEP_DIR}/${ABSL_LIB_DIR}/grpc")
  set(GRPC_CORE_LIB_NAMES
    grpc++ grpc grpc_unsecure grpc++_reflection grpc++_error_details
    grpc_plugin_support grpcpp_channelz gpr
    address_sorting re2 upb)
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
    absl_leak_check absl_leak_check_disable absl_scoped_set_env)

  set(GRPC_STATIC_LIBS)
  foreach(name ${GRPC_CORE_LIB_NAMES})
    list(APPEND GRPC_STATIC_LIBS "${GRPC_STATIC_LIB_DIR}/lib${name}.a")
  endforeach()
  foreach(name ${ABSL_LIB_NAMES})
    list(APPEND GRPC_STATIC_LIBS "${ABSL_STATIC_LIB_DIR}/lib${name}.a")
  endforeach()

  target_link_libraries(oblib_grpc PUBLIC
    oblib_base
    ${GRPC_STATIC_LIBS}
    ${ABSL_STATIC_LIB_DIR}/libcares.a
    ${ABSL_STATIC_LIB_DIR}/libprotobuf.a)
endif()

if(APPLE)
  target_link_libraries(oblib_grpc PUBLIC resolv)
endif()
