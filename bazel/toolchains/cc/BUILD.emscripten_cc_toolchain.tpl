load("@rules_cc//cc/toolchains:cc_toolchain.bzl", "cc_toolchain")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc/private/toolchain:unix_cc_toolchain_config.bzl", "cc_toolchain_config")

package(default_visibility = ["//visibility:public"])
filegroup(
    name = "sdk",
    srcs = ["emcc.sh"] + glob(["sdk/**", "clang_builtin_include/**"], exclude = ["sdk/**/*.pyc"]) + glob(["cache/sysroot/**", "cache/sysroot_install.stamp", "cache/ports/**"], allow_empty = True),
)
filegroup(name = "empty")
cc_library(
    name = "wasmfs_headers",
    hdrs = glob(["sdk/emscripten/system/lib/wasmfs/*.h"]),
    includes = ["sdk/emscripten/system/lib/wasmfs"],
)
cc_toolchain_config(
    name = "config",
    toolchain_identifier = "seekdb-emscripten",
    host_system_name = "local",
    target_system_name = "wasm32-unknown-emscripten",
    target_libc = "musl",
    cpu = "wasm32",
    compiler = "clang",
    abi_version = "wasm32",
    abi_libc_version = "musl",
    cxx_builtin_include_directories = @INCLUDES@,
    compile_flags = ["-pthread", "-fexceptions", "-msimd128", "-sUSE_ZLIB=1"],
    cxx_flags = ["-std=c++20"],
    link_flags = ["-pthread", "-fexceptions", "-msimd128", "-sUSE_ZLIB=1"],
    opt_compile_flags = ["-Oz"],
    unfiltered_compile_flags = ["-g0"],
    opt_link_flags = ["-Oz"],
    dbg_compile_flags = ["-O0", "-g"],
    tool_paths = {
        "gcc": "emcc.sh",
        "cpp": "emcc.sh",
        "ld": "sdk/bin/wasm-ld",
        "ar": "sdk/emscripten/emar",
        "nm": "sdk/bin/llvm-nm",
        "objcopy": "sdk/bin/llvm-objcopy",
        "objdump": "sdk/bin/llvm-objdump",
        "strip": "sdk/bin/llvm-strip",
        "gcov": "/usr/bin/false",
        "dwp": "/usr/bin/false",
    },
)
cc_toolchain(
    name = "cc",
    toolchain_identifier = "seekdb-emscripten",
    toolchain_config = ":config",
    all_files = ":sdk",
    compiler_files = ":sdk",
    linker_files = ":sdk",
    ar_files = ":sdk",
    as_files = ":sdk",
    dwp_files = ":empty",
    objcopy_files = ":sdk",
    strip_files = ":sdk",
    supports_param_files = 1,
)
toolchain(
    name = "wasm32",
    toolchain = ":cc",
    toolchain_type = "@bazel_tools//tools/cpp:toolchain_type",
    exec_compatible_with = ["@platforms//os:@HOST_OS@", "@platforms//cpu:@HOST_CPU@"],
    target_compatible_with = ["@platforms//os:emscripten", "@platforms//cpu:wasm32"],
)
