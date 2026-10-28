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

def observer_third_party_link_files(platform):
    android = platform == "android"
    repository = "@third_party_android" if android else "@third_party_native"
    macos = platform == "macos"
    lib64 = "lib" if android or macos else "lib64"
    vsag = "lib" if android else "lib/vsag_lib"
    files = []
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
    files += ["lib/libssl.a", "lib/libcrypto.a"]
    files += [lib64 + "/" + name for name in _ABSL_ARCHIVE_NAMES]
    labels += [repository + "//:" + file for file in files]
    labels += [":_roaring_core_link"]
    files = []
    if not macos and not android:
        files += ["lib/libaio.so"]
    return labels + [repository + "//:" + file for file in files]
