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
    "sanity_headers": {"hdrs": ["sanity/include/sanity/**/*.h"], "includes": ["sanity/include"]},
    "boost_headers": _headers("boost"),
    "mariadb_headers": _headers("mariadb", ["include", "include/mariadb"]),
    "openssl_headers": _headers("openssl"),
    "curl_headers": _headers("curl") | {
        "linkopts": select({
            "@platforms//os:android": ["-lz"],
            "@platforms//os:ios": ["-lz"],
            "//conditions:default": [],
        }),
    },
    "roaring_headers": _headers("roaring") | {
        "defines": select({
            "@platforms//os:emscripten": ["ROARING_EXCEPTIONS=0"],
            "//conditions:default": [],
        }),
    },
    "jemalloc_headers": _headers("jemalloc"),
    "vsag_headers": _headers("vsag"),
    "windows_headers": {"hdrs": ["include/pthread*.h", "include/_ptw32.h", "include/sched.h", "include/semaphore.h"], "includes": ["include"]},
}
SDK_IMPORTS = {
    "roaring": {"static_library": "lib/libroaring.a"},
    "gtest_archive": {"static_library": "lib/libgtest.a"},
    "libcrypto": {
        "static_library": "lib/libcrypto.a",
        "hdrs": ["include/openssl/*.h"],
        "includes": ["include"],
    },
    "libssl": {"static_library": "lib/libssl.a"},
    "curl": {
        "static_library": "lib/libcurl.a",
        "deps": [":curl_headers", ":libssl", ":libcrypto"],
    },
}

SDK_FILEGROUPS = {
    "ios_locked_metadata": {"srcs": ["locked_manifests/*.json"]},
    "ios_locked_licenses": {"srcs": ["locked_licenses/**/*"]},
    "sanity_pass": {"srcs": ["sanity/lib/libsanitypass.so"]},
}

HOST_FILES = [
    "devtools/bin/llvm-objcopy",
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
    "roaring": ("lib/roaring.lib", "bin/roaring.dll", []),
    "pthreads": ("lib/pthreadVC3.lib", "bin/pthreadVC3.dll", []),
    "openmp": ("lib/libomp.lib", "bin/libomp.dll", []),
}

_WINDOWS_HEADER_LIBRARIES = {
    "openssl_headers": [":libssl"],
    "roaring_headers": [":roaring"],
    "vsag_headers": [":vsag"],
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
    cc_import(name = "_curl_archive", static_library = "lib/libcurl.lib", visibility = ["//visibility:private"])
    cc_import(name = "_zlib_archive", static_library = "lib/libzs.lib", visibility = ["//visibility:private"])
    cc_library(
        name = "curl",
        deps = [":curl_headers", ":_curl_archive", ":_zlib_archive"],
        linkopts = ["ws2_32.lib", "iphlpapi.lib", "bcrypt.lib", "advapi32.lib", "crypt32.lib", "secur32.lib"],
    )
    # Archives within one upstream package stay private to its library target.
    archives = {
        "vsag": (["lib/" + name + ".lib" for name in [
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
    native.exports_files(HOST_FILES + native.glob(["lib/**/*.a", "lib/**/*.so", "lib/**/*.so.*", "lib64/**/*.a", "lib64/**/*.so", "lib64/**/*.so.*"] + (["lib/**/*.lib"] if windows else []), allow_empty = True))
    for name, attributes in SDK_HEADERS.items():
        if windows and name in _WINDOWS_HEADER_LIBRARIES:
            attributes = attributes | {"deps": _WINDOWS_HEADER_LIBRARIES[name]}
        if windows and name == "curl_headers":
            attributes = attributes | {"defines": ["CURL_STATICLIB"]}
        cc_library(name = name, **(attributes | {"hdrs": native.glob(attributes["hdrs"], allow_empty = True)}))
    for name, attributes in SDK_IMPORTS.items():
        if windows:
            continue
        cc_import(name = name, **(attributes | ({"hdrs": native.glob(attributes["hdrs"], allow_empty = True)} if "hdrs" in attributes else {})))
    for name, attributes in SDK_FILEGROUPS.items():
        native.filegroup(name = name, **(attributes | {"srcs": native.glob(attributes["srcs"], allow_empty = True)}))
    vsag_imports = []
    for library in native.glob(["lib/" + name for name in [
        "libvsag_static.a", "libdiskann.a", "libsimd.a", "libcpuinfo.a",
        "libopenblas.a", "libio.a", "libomp_static.a",
        "libantlr4-runtime.a", "libantlr4-autogen.a", "libfmt.a",
    ]], allow_empty = True):
        target = "_vsag_sdk_" + library.rsplit("/", 1)[-1]
        cc_import(name = target, static_library = library, visibility = ["//visibility:private"])
        vsag_imports.append(":" + target)
    cc_library(name = "vsag_sdk", deps = [":vsag_headers"] + vsag_imports)
    if windows:
        _windows_libraries()

def sdk_aliases():
    cc_library(
        name = "base_libraries",
        deps = [":libcrypto", ":libssl"],
    )
    for name in list(SDK_HEADERS) + list(SDK_IMPORTS) + list(SDK_FILEGROUPS) + ["vsag_sdk"] + HOST_FILES + NATIVE_LINK_FILES:
        if name in HOST_FILES + ["sanity_pass", "sanity_headers"]:
            actual = "@third_party_native//:" + name
        else:
            actual = select({
                "@platforms//os:android": "@third_party_android//:" + name,
                "@platforms//os:emscripten": "@third_party_wasm//:" + name,
                "@platforms//os:ios": "@third_party_ios//:" + name,
                Label("//bazel:ios_simulator"): "@third_party_ios_simulator//:" + name,
                "//conditions:default": "@third_party_native//:" + name,
            })
        native.alias(name = name, actual = actual)
