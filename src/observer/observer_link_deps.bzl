"""Ordered third-party archives for the server's supported SDK layouts."""

_ABSL_ARCHIVE_NAMES = [
    "libabsl_base.a",
    "libabsl_city.a",
    "libabsl_debugging_internal.a",
    "libabsl_demangle_internal.a",
    "libabsl_hash.a",
    "libabsl_int128.a",
    "libabsl_low_level_hash.a",
    "libabsl_malloc_internal.a",
    "libabsl_raw_hash_set.a",
    "libabsl_raw_logging_internal.a",
    "libabsl_spinlock_wait.a",
    "libabsl_stacktrace.a",
    "libabsl_str_format_internal.a",
    "libabsl_strings.a",
    "libabsl_strings_internal.a",
    "libabsl_symbolize.a",
    "libabsl_synchronization.a",
    "libabsl_throw_delegate.a",
    "libabsl_time.a",
    "libabsl_time_zone.a",
]

_GRPC_CORE_ARCHIVE_NAMES = [
    "libgrpc++.a",
    "libgrpc.a",
    "libgrpc_unsecure.a",
    "libgrpc++_reflection.a",
    "libgrpc++_error_details.a",
    "libgrpc_plugin_support.a",
    "libgrpcpp_channelz.a",
    "libgpr.a",
    "libaddress_sorting.a",
    "libre2.a",
    "libupb.a",
]

_GRPC_ABSL_ARCHIVE_NAMES = [
    "libabsl_base.a",
    "libabsl_int128.a",
    "libabsl_throw_delegate.a",
    "libabsl_raw_logging_internal.a",
    "libabsl_log_severity.a",
    "libabsl_spinlock_wait.a",
    "libabsl_malloc_internal.a",
    "libabsl_debugging_internal.a",
    "libabsl_demangle_internal.a",
    "libabsl_stacktrace.a",
    "libabsl_symbolize.a",
    "libabsl_examine_stack.a",
    "libabsl_failure_signal_handler.a",
    "libabsl_strerror.a",
    "libabsl_strings.a",
    "libabsl_strings_internal.a",
    "libabsl_str_format_internal.a",
    "libabsl_cord.a",
    "libabsl_cord_internal.a",
    "libabsl_cordz_info.a",
    "libabsl_cordz_handle.a",
    "libabsl_cordz_functions.a",
    "libabsl_cordz_sample_token.a",
    "libabsl_hash.a",
    "libabsl_low_level_hash.a",
    "libabsl_city.a",
    "libabsl_raw_hash_set.a",
    "libabsl_hashtablez_sampler.a",
    "libabsl_status.a",
    "libabsl_statusor.a",
    "libabsl_bad_any_cast_impl.a",
    "libabsl_bad_optional_access.a",
    "libabsl_bad_variant_access.a",
    "libabsl_synchronization.a",
    "libabsl_graphcycles_internal.a",
    "libabsl_time.a",
    "libabsl_time_zone.a",
    "libabsl_civil_time.a",
    "libabsl_exponential_biased.a",
    "libabsl_periodic_sampler.a",
    "libabsl_random_distributions.a",
    "libabsl_random_seed_sequences.a",
    "libabsl_random_seed_gen_exception.a",
    "libabsl_random_internal_pool_urbg.a",
    "libabsl_random_internal_randen.a",
    "libabsl_random_internal_randen_hwaes.a",
    "libabsl_random_internal_randen_hwaes_impl.a",
    "libabsl_random_internal_randen_slow.a",
    "libabsl_random_internal_seed_material.a",
    "libabsl_random_internal_platform.a",
    "libabsl_random_internal_distribution_test_util.a",
    "libabsl_flags.a",
    "libabsl_flags_commandlineflag.a",
    "libabsl_flags_commandlineflag_internal.a",
    "libabsl_flags_config.a",
    "libabsl_flags_internal.a",
    "libabsl_flags_marshalling.a",
    "libabsl_flags_parse.a",
    "libabsl_flags_private_handle_accessor.a",
    "libabsl_flags_program_name.a",
    "libabsl_flags_reflection.a",
    "libabsl_flags_usage.a",
    "libabsl_flags_usage_internal.a",
    "libabsl_leak_check.a",
    "libabsl_leak_check_disable.a",
    "libabsl_scoped_set_env.a",
]

def observer_third_party_link_files(platform):
    android = platform == "android"
    repository = "@third_party_android" if android else "@third_party_native"
    macos = platform == "macos"
    lib64 = "lib" if android or macos else "lib64"
    vsag = "lib" if android else "lib/vsag_lib"
    grpc = "lib/grpc" if android or macos else "lib64/grpc"
    files = ["lib/libxml2.a", "lib/liblzma.a"]
    files += [vsag + "/" + name for name in [
        "libcpuinfo.a", "libsimd.a", "libvsag_static.a", "libdiskann.a",
    ]]
    if macos:
        files += [vsag + "/" + name for name in [
            "libio.a", "libgfortran_static.a", "libquadmath_static.a",
            "libgcc.a", "libomp_static.a", "libopenblas.a",
        ]]
    else:
        files += [vsag + "/libopenblas.a"]
        if not android:
            files += [vsag + "/libgfortran_static.a"]
            if platform == "linux_x86_64":
                files += [vsag + "/libquadmath_static.a"]
            files += [vsag + "/libgomp_static.a"]
    labels = [repository + "//:" + file for file in files]
    labels += [":_roaring_vsag_link"]
    files = [vsag + "/" + name for name in [
        "libantlr4-runtime.a", "libantlr4-autogen.a", "libfmt.a",
    ]]
    if android:
        files += ["lib/libspdlog.a"]
    files += ["lib/libcurl.a", "lib/libssl.a", "lib/libcrypto.a"]
    # The native macOS SDK retains S2 in lib64.
    files += [("lib" if android else "lib64") + "/libs2.a"]
    files += ["lib/grpc/" + name for name in _GRPC_CORE_ARCHIVE_NAMES]
    files += [grpc + "/" + name for name in _GRPC_ABSL_ARCHIVE_NAMES + [
        "libcares.a", "libprotobuf.a",
    ]]
    files += [lib64 + "/" + name for name in _ABSL_ARCHIVE_NAMES]
    labels += [repository + "//:" + file for file in files]
    labels += [":_roaring_core_link"]
    files = ["lib/libunwind.a"] if platform == "linux_x86_64" else []
    files += ["lib/" + name for name in [
        "libz.a", "libprotobuf-c.a",
    ]]
    if not macos and not android:
        files += ["lib/libaio.so"]
    return labels + [repository + "//:" + file for file in files]
