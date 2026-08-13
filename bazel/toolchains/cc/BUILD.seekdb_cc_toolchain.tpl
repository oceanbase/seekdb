load("@rules_cc//cc/toolchains:cc_toolchain.bzl", "cc_toolchain")
load(":toolchain_config.bzl", "seekdb_cc_toolchain_config")

package(default_visibility = ["//visibility:public"])

filegroup(
    name = "empty",
    srcs = [],
)

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
    # The Linux bundle provides libstdc++ here.  Native macOS builds use the
    # system libc++ selected by Apple's configured toolchain instead.
    srcs = glob(
        ["devtools/lib64/libstdc++.a"],
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
    name = "local_compiler_files",
    srcs = [":clang_compiler_files"],
)

filegroup(
    name = "local_linker_files",
    srcs = [
        ":base_linker_files",
        ":local_compiler_files",
    ],
)

filegroup(
    name = "common_tool_files",
    srcs = [
        ":archive_files",
        ":base_linker_files",
        ":dwp_files",
        ":objcopy_files",
        ":strip_files",
        "validate_static_library.sh",
    ],
)

filegroup(
    name = "local_all_files",
    srcs = [
        ":common_tool_files",
        ":local_compiler_files",
    ],
)

seekdb_cc_toolchain_config(
    name = "local_compile_config",
    compiler_path = "devtools/bin/clang",
    toolchain_identifier = "seekdb-clang17-local",
)

cc_toolchain(
    name = "local_compile_cc_toolchain",
    all_files = ":local_all_files",
    ar_files = ":archive_files",
    as_files = ":local_compiler_files",
    compiler_files = ":local_compiler_files",
    dwp_files = ":dwp_files",
    linker_files = ":local_linker_files",
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
        "@platforms//cpu:x86_64",
        "@platforms//os:linux",
    ],
    target_compatible_with = [
        "@platforms//cpu:x86_64",
        "@platforms//os:linux",
    ],
    toolchain = ":local_compile_cc_toolchain",
    toolchain_type = "@bazel_tools//tools/cpp:toolchain_type",
)
