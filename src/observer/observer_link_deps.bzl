"""Ordered third-party archives for the server's supported SDK layouts."""

def observer_third_party_link_files(platform):
    android = platform == "android"
    repository = "@third_party_android" if android else "@third_party_native"
    macos = platform == "macos"
    vsag = "lib"
    files = []
    files += [vsag + "/" + name for name in [
        "libcpuinfo.a", "libsimd.a", "libvsag_static.a", "libdiskann.a",
    ]]
    if macos:
        files += [vsag + "/" + name for name in [
            "libio.a", "libomp_static.a", "libopenblas.a",
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
    labels += [repository + "//:" + file for file in files]
    labels += [":_roaring_core_link"]
    files = []
    if not macos and not android:
        files += ["lib/libaio.so"]
    return labels + [repository + "//:" + file for file in files]
