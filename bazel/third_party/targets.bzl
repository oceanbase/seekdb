"""Shared declarations for SDK targets and their platform-selected aliases."""

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_import.bzl", "cc_import")

_HEADER_EXTENSIONS = ["def", "h", "hh", "hpp", "hxx", "inc", "inl", "ipp", "tcc"]

def _headers(directory, includes = ["include"]):
    return {"hdrs": ["include/" + directory + "/**/*." + extension for extension in _HEADER_EXTENSIONS], "includes": includes}

SDK_HEADERS = {
    "gtest": _headers("gtest") | {
        "deps": [":gtest_archive"],
        "defines": ["_GLIBCXX_USE_CXX11_ABI=1"],
        "linkopts": select({"@platforms//os:linux": ["-pthread"], "//conditions:default": []}),
    },
    "sanity_headers": {"hdrs": ["devtools/include/sanity/**/*.h"], "includes": ["devtools/include"]},
    "absl_headers": _headers("absl"),
    "boost_headers": _headers("boost"),
    "curl_headers": _headers("curl"),
    "fast_float_headers": _headers("fast_float", ["include", "include/fast_float"]),
    "libaio_headers": {"hdrs": ["include/libaio.h"], "includes": ["include"]},
    "libunwind_headers": {"hdrs": ["include/libunwind*.h", "include/unwind.h"], "includes": ["include"]},
    "libxml2_headers": _headers("libxml2", ["include/libxml2"]),
    "mariadb_headers": _headers("mariadb", ["include", "include/mariadb"]),
    "openssl_headers": _headers("openssl"),
    "protobuf_c_headers": _headers("protobuf-c"),
    "protobuf_cpp_headers": _headers("google/protobuf"),
    "grpc_headers": {
        "hdrs": ["include/" + directory + "/**/*." + extension for directory in ["grpc", "grpc++", "grpcpp"] for extension in _HEADER_EXTENSIONS],
        "includes": ["include"],
    },
    "rapidjson_headers": _headers("rapidjson"),
    "roaring_headers": _headers("roaring"),
    "s2_headers": _headers("s2"),
    "vsag_headers": _headers("vsag"),
    "zlib_headers": {"hdrs": ["include/zconf.h", "include/zlib.h"], "includes": ["include"]},
    "windows_headers": {"hdrs": ["include/pthread*.h", "include/_ptw32.h", "include/sched.h", "include/semaphore.h", "include/dirent.h"], "includes": ["include"]},
}
SDK_IMPORTS = {
    "gtest_archive": {"static_library": "lib/libgtest.a"},
    "libaio": {
        "static_library": "lib/libaio.a",
        "hdrs": ["include/libaio.h"],
        "includes": ["include"],
    },
    "libcrypto": {
        "static_library": "lib/libcrypto.a",
        "hdrs": ["include/openssl/*.h"],
        "includes": ["include"],
    },
    "libssl": {"static_library": "lib/libssl.a"},
    "libunwind": {"static_library": "lib/libunwind.a"},
    "zlib": {"static_library": "lib/libz.a"},
}

SDK_IMPORTS.update({
    name: {"static_library": "lib/" + archive}
    for name, archive in {
        "libxml2": "libxml2.a",
        "protobuf_c": "libprotobuf-c.a",
        "curl": "libcurl.a",
        "lzma": "liblzma.a",
    }.items()
})

SDK_FILEGROUPS = {
    "ios_base_metadata": {"srcs": ["verified/*.json"]},
    "ios_base_licenses": {"srcs": ["licenses/**/*"]},
    "sanity_pass": {"srcs": ["devtools/lib64/libsanitypass.so"]},
}

HOST_FILES = [
    "devtools/bin/llvm-objcopy",
    "devtools/lib64/libsanitypass.so",
]

NATIVE_LINK_FILES = [
    "lib/libroaring.a",
    "lib/libaio.so.1",
    "lib/libaio.so.1.0.1",
]

# Each import library names its DLL and its non-system dependencies.
_WINDOWS_SHARED = {
    "libcrypto": ("openssl/libcrypto.lib", "openssl_bin/libcrypto-3-x64.dll", []),
    "libssl": ("openssl/libssl.lib", "openssl_bin/libssl-3-x64.dll", [":libcrypto"]),
    "zlib": ("lib/zlib.lib", "bin/zlib1.dll", []),
    "iconv": ("lib/iconv.lib", "bin/iconv-2.dll", []),
    "libxml2": ("lib/libxml2.lib", "bin/libxml2.dll", [":zlib", ":iconv"]),
    "protobuf_c": ("lib/protobuf-c.lib", "bin/protobuf-c.dll", []),
    "curl": ("lib/libcurl.lib", "bin/libcurl.dll", [":zlib"]),
    "abseil_dll": ("lib/abseil_dll.lib", "bin/abseil_dll.dll", []),
    "protobuf_cpp": ("lib/libprotobuf.lib", "bin/libprotobuf.dll", [":absl", ":utf8_range"]),
    "roaring": ("lib/roaring.lib", "bin/roaring.dll", []),
    "pthreads": ("lib/pthreadVC3.lib", "bin/pthreadVC3.dll", []),
    "cares": ("lib/cares.lib", "bin/cares.dll", []),
    "re2": ("lib/re2.lib", "bin/re2.dll", [":absl"]),
    "openmp": ("vsag/libomp.lib", "vsag_bin/libomp.dll", []),
}

_WINDOWS_HEADER_LIBRARIES = {
    "absl_headers": [":absl"],
    "curl_headers": [":curl"],
    "libxml2_headers": [":libxml2"],
    "openssl_headers": [":libssl"],
    "protobuf_c_headers": [":protobuf_c"],
    "protobuf_cpp_headers": [":protobuf_cpp"],
    "grpc_headers": [":grpc"],
    "roaring_headers": [":roaring"],
    "s2_headers": [":s2"],
    "vsag_headers": [":vsag"],
    "zlib_headers": [":zlib"],
    "windows_headers": [":pthreads", ":compiler_runtime"],
}

def _windows_libraries():
    for name, (interface, dll, deps) in _WINDOWS_SHARED.items():
        cc_import(
            name = name,
            interface_library = interface,
            shared_library = dll,
            deps = deps,
            hdrs = native.glob(["include/openssl/*.h"]) if name == "libcrypto" else [],
            includes = ["include"] if name == "libcrypto" else [],
        )
    # Archives within one upstream package stay private to its library target.
    archives = {
        "absl": (native.glob(["lib/absl_*.lib"]), [":abseil_dll"]),
        "s2": (["lib/s2.lib"], [":absl", ":libssl"]),
        "utf8_range": (["lib/utf8_range.lib", "lib/utf8_validity.lib"], []),
        "grpc": (["lib/" + name + ".lib" for name in [
            "grpc++", "grpc", "gpr", "address_sorting", "libupb",
        ]] + native.glob(["lib/upb_*.lib"]), [
            ":absl", ":protobuf_cpp", ":cares", ":re2", ":zlib", ":libssl",
        ]),
        "vsag": (["vsag/" + name + ".lib" for name in [
            "cpuinfo", "simd", "vsag_static", "diskann", "openblas",
            "antlr4-runtime-static", "antlr4-autogen", "fmt",
        ]], [":openmp"]),
        "compiler_runtime": (native.glob([
            "devtools/lib/clang/*/lib/windows/clang_rt.builtins-x86_64.lib",
        ]), []),
    }
    for name, (libraries, deps) in archives.items():
        imports = []
        for library in libraries:
            target = "_" + name + "_" + library.rsplit("/", 1)[-1]
            cc_import(name = target, static_library = library, visibility = ["//visibility:private"])
            imports.append(":" + target)
        cc_library(
            name = name,
            deps = imports + deps,
            data = native.glob(["msvc_runtime/*.dll"], allow_empty = False) if name == "compiler_runtime" else [],
        )

def sdk_targets(windows = False):
    native.exports_files(HOST_FILES + native.glob(["lib/**/*.a", "lib/**/*.so", "lib/**/*.so.*", "lib64/**/*.a", "lib64/**/*.so", "lib64/**/*.so.*"], allow_empty = True))
    for name, attributes in SDK_HEADERS.items():
        if windows and name in _WINDOWS_HEADER_LIBRARIES:
            attributes = attributes | {"deps": _WINDOWS_HEADER_LIBRARIES[name]}
        if windows and name in ["absl_headers", "protobuf_cpp_headers"]:
            attributes = attributes | {"defines": ["ABSL_CONSUME_DLL" if name == "absl_headers" else "PROTOBUF_USE_DLLS"]}
        if windows and name == "protobuf_cpp_headers":
            attributes = attributes | {"hdrs": attributes["hdrs"] + ["include/utf8_range.h", "include/utf8_validity.h"]}
        cc_library(name = name, **(attributes | {"hdrs": native.glob(attributes["hdrs"], allow_empty = True)}))
    for name, attributes in SDK_IMPORTS.items():
        if windows:
            continue
        cc_import(name = name, **(attributes | ({"hdrs": native.glob(attributes["hdrs"], allow_empty = True)} if "hdrs" in attributes else {})))
    for name, attributes in SDK_FILEGROUPS.items():
        native.filegroup(name = name, **(attributes | {"srcs": native.glob(attributes["srcs"], allow_empty = True)}))
    if windows:
        _windows_libraries()
        native.exports_files(["protoc/protoc.exe", "grpc_tools/grpc_cpp_plugin.exe"])
        native.filegroup(name = "protoc_runtime", srcs = native.glob(["protoc/*.dll", "grpc_tools/*.dll"]))

_SOURCE_HEADERS = {
    "absl_headers": ("abseil", "abseil"),
    "roaring_headers": ("roaring", "roaring"),
    "s2_headers": ("s2_ios", "s2_wasm"),
    "vsag_headers": ("vsag_ios", "vsag_wasm"),
}

def sdk_aliases():
    cc_library(
        name = "base_libraries",
        deps = [":libcrypto", ":libssl", ":libxml2", ":protobuf_c"] + select({
            "@platforms//os:emscripten": [],
            "@platforms//os:windows": [":curl", ":zlib"],
            "//conditions:default": [":curl", ":lzma", ":zlib"],
        }),
    )
    for name in list(SDK_HEADERS) + list(SDK_IMPORTS) + list(SDK_FILEGROUPS) + HOST_FILES + NATIVE_LINK_FILES:
        if name in HOST_FILES + ["sanity_pass", "sanity_headers"]:
            actual = "@third_party_native//:" + name
        else:
            ios = "@third_party_ios//:" + name
            simulator = "@third_party_ios_simulator//:" + name
            wasm = "@third_party_wasm//:" + name
            if name in _SOURCE_HEADERS:
                ios = "@sdk_" + _SOURCE_HEADERS[name][0] + "//:library"
                simulator = ios
                wasm = "@sdk_" + _SOURCE_HEADERS[name][1] + "//:library"
            elif name == "boost_headers":
                ios = "@sdk_boost//:library"
                simulator = ios
            actual = select({
                "@platforms//os:android": "@third_party_android//:" + name,
                "@platforms//os:emscripten": wasm,
                "@platforms//os:ios": ios,
                Label("//bazel:ios_simulator"): simulator,
                "//conditions:default": "@third_party_native//:" + name,
            })
        native.alias(name = name, actual = actual)
