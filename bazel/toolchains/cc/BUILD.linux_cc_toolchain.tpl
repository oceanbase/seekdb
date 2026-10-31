load("@rules_cc//cc/toolchains:cc_toolchain.bzl", "cc_toolchain")
load("@rules_cc//cc/toolchains:feature.bzl", "cc_feature")
load("@rules_cc//cc/private/toolchain:unix_cc_toolchain_config.bzl", "cc_toolchain_config")

package(default_visibility = ["//visibility:public"])

filegroup(
    name = "clang_compiler_files",
    srcs = ["devtools/bin/clang"] + glob([
        "devtools/include/c++/**",
        "devtools/lib/clang/**/include/**",
        "devtools/lib/clang/**/share/**",
        "devtools/lib/gcc/**",
    ], allow_empty = True) + [
        "builtin_include_directory_paths",
        "module.modulemap",
    ],
)

filegroup(
    name = "archive_files",
    srcs = [
        "devtools/bin/llvm-ar",
        "devtools/bin/llvm-nm",
        "devtools/bin/llvm-ranlib",
    ],
)

filegroup(
    name = "base_linker_files",
    srcs = [
        ":clang_compiler_files",
        "devtools/bin/ld.lld",
        "devtools/bin/lld",
    ] + glob([
        "devtools/lib/clang/**/lib/**",
        "devtools/lib64/**",
    ], allow_empty = True),
)

filegroup(
    name = "static_cpp_runtime",
    srcs = glob(
        ["devtools/lib64/libstdc++.a"] + (
            ["devtools/lib64/libatomic.a"] if "@HOST_CPU@" == "aarch64" else []
        ),
        allow_empty = True,
    ),
)

filegroup(
    name = "objcopy_files",
    srcs = ["devtools/bin/llvm-objcopy"],
)

filegroup(
    name = "strip_files",
    srcs = [
        "devtools/bin/llvm-objcopy",
        "devtools/bin/llvm-strip",
    ],
)

filegroup(
    name = "dwp_files",
    srcs = ["devtools/bin/llvm-dwp"],
)

filegroup(
    name = "local_all_files",
    srcs = [
        ":archive_files",
        ":base_linker_files",
        ":dwp_files",
        ":objcopy_files",
        ":strip_files",
        "validate_static_library.sh",
    ],
)

cc_feature(
    name = "static_cpp_runtime_feature",
    feature_name = "static_cpp_runtime",
    implies = ["@rules_cc//cc/toolchains/features:static_link_cpp_runtimes"],
)

cc_toolchain_config(
    name = "local_compile_config",
    abi_libc_version = "glibc",
    abi_version = "@HOST_CPU@",
    compile_flags = [
        "-Wall",
        "-Wthread-safety",
        "-Wself-assign",
        "-Wunused-but-set-parameter",
        "-Wno-free-nonheap-object",
        "-fcolor-diagnostics",
        "-fno-omit-frame-pointer",
    ],
    compiler = "clang",
    coverage_compile_flags = ["--coverage"],
    coverage_link_flags = ["--coverage"],
    cpu = "@BAZEL_CPU@",
    cxx_builtin_include_directories = @CXX_BUILTIN_INCLUDE_DIRECTORIES@,
    cxx_flags = ["-std=c++17"],
    dbg_compile_flags = [
        "-O0",
        "-g",
    ],
    extra_flags_per_feature = {
        "use_module_maps": [
            "-Xclang",
            "-fno-cxx-modules",
        ],
    },
    extra_enabled_features = [":static_cpp_runtime_feature"],
    host_system_name = "local",
    link_flags = [
        "-fuse-ld=lld",
        "-Wl,-no-as-needed",
        "-Wl,-z,relro,-z,now",
    ],
    link_libs = [
        "-Wl,--push-state,-as-needed",
        "-lm",
        "-Wl,--pop-state",
    ],
    opt_compile_flags = ["-O2"],
    supports_start_end_lib = True,
    target_libc = "glibc",
    target_system_name = "@HOST_CPU@-redhat-linux",
    tool_paths = {
        "ar": "devtools/bin/llvm-ar",
        "c++filt": "devtools/bin/llvm-cxxfilt",
        "cpp": "devtools/bin/clang",
        "dwp": "devtools/bin/llvm-dwp",
        "gcc": "devtools/bin/clang",
        "gcov": "devtools/bin/llvm-cov",
        "ld": "devtools/bin/ld.lld",
        "nm": "devtools/bin/llvm-nm",
        "objcopy": "devtools/bin/llvm-objcopy",
        "objdump": "devtools/bin/llvm-objdump",
        "strip": "devtools/bin/llvm-strip",
        "validate_static_library": "validate_static_library.sh",
    },
    toolchain_identifier = "seekdb-clang17-local",
    unfiltered_compile_flags = [
        "-no-canonical-prefixes",
        "-Wno-builtin-macro-redefined",
        "-D__DATE__=\"redacted\"",
        "-D__TIMESTAMP__=\"redacted\"",
        "-D__TIME__=\"redacted\"",
    ],
)

cc_toolchain(
    name = "local_compile_cc_toolchain",
    all_files = ":local_all_files",
    ar_files = ":archive_files",
    as_files = ":clang_compiler_files",
    compiler_files = ":clang_compiler_files",
    dwp_files = ":dwp_files",
    linker_files = ":base_linker_files",
    module_map = ":module.modulemap",
    objcopy_files = ":objcopy_files",
    static_runtime_lib = ":static_cpp_runtime",
    strip_files = ":strip_files",
    supports_param_files = 1,
    toolchain_config = ":local_compile_config",
    toolchain_identifier = "seekdb-clang17-local",
)

toolchain(
    name = "local_compile_toolchain",
    exec_compatible_with = [
        "@platforms//cpu:@HOST_CPU@",
        "@platforms//os:linux",
    ],
    target_compatible_with = [
        "@platforms//cpu:@HOST_CPU@",
        "@platforms//os:linux",
    ],
    toolchain = ":local_compile_cc_toolchain",
    toolchain_type = "@bazel_tools//tools/cpp:toolchain_type",
)
