load("@rules_cc//cc/toolchains:cc_toolchain.bzl", "cc_toolchain")
load("@rules_cc//cc/private/toolchain:unix_cc_toolchain_config.bzl", "cc_toolchain_config")

package(default_visibility = ["//visibility:public"])

filegroup(name = "empty")
filegroup(
    name = "compiler_files",
    srcs = glob([
        "ndk/bin/**",
        "ndk/lib/clang/@CLANG_VERSION@/include/**",
        "ndk/lib/*.dylib",
        "ndk/lib/*.so*",
        "ndk/lib64/*.so*",
        "ndk/sysroot/usr/include/**",
    ], allow_empty = True),
)
filegroup(
    name = "linker_files",
    srcs = [":compiler_files"] + glob([
        "ndk/lib/clang/@CLANG_VERSION@/lib/**",
        "ndk/sysroot/usr/lib/aarch64-linux-android/**",
    ]),
)
filegroup(
    name = "static_cpp_runtime",
    srcs = [
        "ndk/sysroot/usr/lib/aarch64-linux-android/libc++_static.a",
        "ndk/sysroot/usr/lib/aarch64-linux-android/libc++abi.a",
    ],
)
cc_toolchain_config(
    name = "config",
    toolchain_identifier = "seekdb-android-arm64-api28",
    host_system_name = "local",
    target_system_name = "aarch64-linux-android28",
    target_libc = "bionic",
    cpu = "aarch64",
    compiler = "clang",
    abi_version = "aarch64",
    abi_libc_version = "bionic",
    builtin_sysroot = "@REPOSITORY_ROOT@/ndk/sysroot",
    cxx_builtin_include_directories = [
        "%crosstool_top%/ndk/sysroot/usr/include",
        "%crosstool_top%/ndk/lib/clang/@CLANG_VERSION@/include",
    ],
    compile_flags = [
        "--target=aarch64-linux-android28",
        "-resource-dir=@REPOSITORY_ROOT@/ndk/lib/clang/@CLANG_VERSION@",
    ],
    cxx_flags = ["-std=c++20", "-stdlib=libc++"],
    link_flags = ["--target=aarch64-linux-android28", "-fuse-ld=lld", "-Wl,-z,max-page-size=16384"],
    link_libs = ["-lm", "-ldl"],
    opt_compile_flags = ["-O2"],
    dbg_compile_flags = ["-O0", "-g"],
    supports_start_end_lib = True,
    tool_paths = {
        "gcc": "ndk/bin/clang",
        "cpp": "ndk/bin/clang",
        "ld": "ndk/bin/ld.lld",
        "ar": "ndk/bin/llvm-ar",
        "nm": "ndk/bin/llvm-nm",
        "objcopy": "ndk/bin/llvm-objcopy",
        "objdump": "ndk/bin/llvm-objdump",
        "strip": "ndk/bin/llvm-strip",
        "gcov": "ndk/bin/llvm-cov",
        "dwp": "ndk/bin/llvm-dwp",
    },
)
cc_toolchain(
    name = "cc",
    toolchain_identifier = "seekdb-android-arm64-api28",
    toolchain_config = ":config",
    all_files = ":linker_files",
    compiler_files = ":compiler_files",
    linker_files = ":linker_files",
    ar_files = ":compiler_files",
    as_files = ":compiler_files",
    dwp_files = ":empty",
    objcopy_files = ":compiler_files",
    strip_files = ":compiler_files",
    static_runtime_lib = ":static_cpp_runtime",
    supports_param_files = 1,
)
toolchain(
    name = "android_arm64",
    toolchain = ":cc",
    toolchain_type = "@bazel_tools//tools/cpp:toolchain_type",
    exec_compatible_with = ["@platforms//os:@HOST_OS@", "@platforms//cpu:@HOST_CPU@"],
    target_compatible_with = ["@platforms//os:android", "@platforms//cpu:aarch64"],
)
