set(DEVEL_PATH "${DEP_3RD_DIR}/usr/local/oceanbase/deps/devel")

option(protobuf_MODULE_COMPATIBLE TRUE)
set(_REFLECTION "${DEVEL_PATH}/lib/grpc/libgrpc++_reflection.a")
set(_GRPC_GRPCPP "${DEVEL_PATH}/lib/grpc/libgrpc++.a")
set(_PROTOBUF_LIBPROTOBUF "${DEVEL_PATH}/lib64/grpc/libprotobuf.a")

include_directories(
      ${CMAKE_CURRENT_BINARY_DIR}
      ${DEP_3RD_DIR}/usr/include
      ${DEP_3RD_DIR}/usr/local/oceanbase/deps/devel/include
)

add_library(oblib_grpc STATIC
    example.pb.cpp
    example.grpc.pb.cpp
    storageservice.pb.cpp
    storageservice.grpc.pb.cpp
    logservice.pb.cpp
    logservice.grpc.pb.cpp
    serverservice.pb.cpp
    serverservice.grpc.pb.cpp
    ob_grpc_context.cpp
    ob_grpc_server.cpp
)
target_link_libraries(oblib_grpc
      PUBLIC oblib_base
      ${DEVEL_PATH}/lib/grpc/libgrpc_unsecure.a
      ${DEVEL_PATH}/lib/grpc/libaddress_sorting.a
      ${DEVEL_PATH}/lib64/grpc/libcares.a
      ${DEVEL_PATH}/lib/grpc/libgrpc++_error_details.a
      ${DEVEL_PATH}/lib/grpc/libgrpc_plugin_support.a
      ${DEVEL_PATH}/lib/grpc/libgpr.a
      ${DEVEL_PATH}/lib/grpc/libgrpcpp_channelz.a
      ${DEVEL_PATH}/lib/grpc/libgrpc.a
      ${DEVEL_PATH}/lib/grpc/libgrpc++_reflection.a
      ${DEVEL_PATH}/lib/grpc/libupb.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_flags_commandlineflag_internal.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_status.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_examine_stack.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_random_internal_randen_hwaes_impl.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_hash.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_time_zone.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_cordz_info.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_strings.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_strings_internal.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_random_internal_pool_urbg.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_synchronization.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_failure_signal_handler.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_strerror.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_log_severity.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_debugging_internal.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_hashtablez_sampler.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_stacktrace.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_time.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_int128.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_symbolize.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_cordz_handle.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_random_internal_seed_material.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_random_internal_randen_hwaes.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_bad_any_cast_impl.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_cord.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_flags_program_name.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_random_internal_randen.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_flags_commandlineflag.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_raw_hash_set.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_bad_variant_access.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_leak_check_disable.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_graphcycles_internal.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_statusor.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_flags.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_bad_optional_access.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_str_format_internal.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_raw_logging_internal.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_civil_time.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_flags_private_handle_accessor.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_random_seed_sequences.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_exponential_biased.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_flags_usage.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_spinlock_wait.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_cordz_functions.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_base.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_cordz_sample_token.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_random_internal_randen_slow.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_periodic_sampler.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_scoped_set_env.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_random_seed_gen_exception.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_cord_internal.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_malloc_internal.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_low_level_hash.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_flags_usage_internal.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_city.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_random_distributions.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_demangle_internal.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_flags_internal.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_flags_parse.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_flags_reflection.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_flags_marshalling.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_throw_delegate.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_random_internal_distribution_test_util.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_flags_config.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_random_internal_platform.a
      ${DEVEL_PATH}/lib64/grpc/libabsl_leak_check.a
      ${_REFLECTION}
      ${_GRPC_GRPCPP}
      ${_PROTOBUF_LIBPROTOBUF}
)
