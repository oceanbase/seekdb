"""Expose seekdb's bootstrapped third-party SDK as a Bazel repository."""

_BUILD_FILE = """
load("@rules_cc//cc:cc_import.bzl", "cc_import")
load("@rules_cc//cc:cc_library.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

HEADER_EXTENSIONS = [
    "def",
    "h",
    "hh",
    "hpp",
    "hxx",
    "inc",
    "inl",
    "ipp",
    "tcc",
]

exports_files([
    "devtools/bin/bison",
    "devtools/bin/flex",
    "devtools/bin/llvm-objcopy",
    "devtools/lib64/libsanitypass.so",
    "devtools/share/bison/README",
])

# Keep each archive/shared object addressable as its own file label.  The final
# link rule, rather than this repository, owns ordering and linker mode changes.
exports_files(glob([
    "lib/**/*.a",
    "lib/**/*.so",
    "lib/**/*.so.*",
    "lib64/**/*.a",
    "lib64/**/*.so",
    "lib64/**/*.so.*",
], allow_empty = True))

filegroup(
    name = "bison_runtime",
    srcs = glob(["devtools/share/bison/**/*"], allow_empty = True),
)

cc_import(
    name = "gtest_archive",
    static_library = "lib/libgtest.a",
)

# Keep include propagation in cc_library and use cc_import only for the
# prebuilt archive.  Bazel 8 accepts includes on cc_import but does not add
# that directory to consumers' compile commands.
cc_library(
    name = "gtest",
    hdrs = glob(["include/gtest/**/*.h"], allow_empty = True),
    includes = ["include"],
    deps = [":gtest_archive"],
)

cc_import(
    name = "gmock_archive",
    static_library = "lib/libgmock.a",
)

cc_library(
    name = "gmock",
    hdrs = glob(["include/gmock/**/*.h"], allow_empty = True),
    includes = ["include"],
    deps = [
        ":gmock_archive",
        ":gtest",
    ],
)

cc_library(
    name = "sanity_headers",
    hdrs = glob(["devtools/include/sanity/**/*.h"], allow_empty = True),
    includes = ["devtools/include"],
)

filegroup(
    name = "sanity_pass",
    srcs = ["devtools/lib64/libsanitypass.so"],
)

cc_import(
    name = "sanity_runtime",
    static_library = "devtools/lib64/libsanity.a",
)

cc_import(
    name = "libaio",
    hdrs = ["include/libaio.h"],
    static_library = "lib/libaio.a",
    includes = ["include"],
)

cc_import(
    name = "libcrypto",
    static_library = "lib/libcrypto.a",
)

cc_import(
    name = "libssl",
    static_library = "lib/libssl.a",
)

cc_import(
    name = "libunwind",
    static_library = "lib/libunwind.a",
)

cc_import(
    name = "zlib",
    static_library = "lib/libz.a",
)

cc_library(
    name = "absl_headers",
    hdrs = glob(["include/absl/**/*." + extension for extension in HEADER_EXTENSIONS], allow_empty = True),
    includes = ["include"],
)

cc_library(
    name = "boost_headers",
    hdrs = glob(["include/boost/**/*." + extension for extension in HEADER_EXTENSIONS], allow_empty = True),
    includes = ["include"],
)

cc_library(
    name = "curl_headers",
    hdrs = glob(["include/curl/**/*." + extension for extension in HEADER_EXTENSIONS], allow_empty = True),
    includes = ["include"],
)

cc_library(
    name = "fast_float_headers",
    hdrs = glob(["include/fast_float/**/*." + extension for extension in HEADER_EXTENSIONS], allow_empty = True),
    includes = [
        "include",
        "include/fast_float",
    ],
)

cc_library(
    name = "icu_headers",
    hdrs = glob(["include/icu/**/*." + extension for extension in HEADER_EXTENSIONS], allow_empty = True),
    includes = [
        "include/icu",
        "include/icu/common",
    ],
)

cc_library(
    name = "libaio_headers",
    # macOS uses seekdb's in-tree asynchronous-I/O compatibility types and
    # therefore has no vendor libaio header.
    hdrs = glob(["include/libaio.h"], allow_empty = True),
    includes = ["include"],
)

cc_library(
    name = "libunwind_headers",
    hdrs = glob([
        "include/libunwind*.h",
        "include/unwind.h",
    ], allow_empty = True),
    includes = ["include"],
)

cc_library(
    name = "libxml2_headers",
    hdrs = glob(["include/libxml2/**/*." + extension for extension in HEADER_EXTENSIONS], allow_empty = True),
    includes = ["include/libxml2"],
)

cc_library(
    name = "mariadb_headers",
    hdrs = glob(["include/mariadb/**/*." + extension for extension in HEADER_EXTENSIONS], allow_empty = True),
    includes = [
        "include",
        "include/mariadb",
    ],
)

cc_library(
    name = "openssl_headers",
    hdrs = glob(["include/openssl/**/*." + extension for extension in HEADER_EXTENSIONS], allow_empty = True),
    includes = ["include"],
)

cc_library(
    name = "protobuf_c_headers",
    hdrs = glob(["include/protobuf-c/**/*." + extension for extension in HEADER_EXTENSIONS], allow_empty = True),
    includes = ["include"],
)

cc_library(
    name = "protobuf_cpp_headers",
    hdrs = glob(["include/google/protobuf/**/*." + extension for extension in HEADER_EXTENSIONS], allow_empty = True),
    includes = ["include"],
)

cc_library(
    name = "grpc_headers",
    hdrs = glob([
        "include/grpc/**/*.h",
        "include/grpc++/**/*.h",
        "include/grpcpp/**/*.h",
    ], allow_empty = True),
    includes = ["include"],
)

cc_library(
    name = "rapidjson_headers",
    hdrs = glob(["include/rapidjson/**/*." + extension for extension in HEADER_EXTENSIONS], allow_empty = True),
    includes = ["include"],
)

cc_library(
    name = "roaring_headers",
    hdrs = glob(["include/roaring/**/*." + extension for extension in HEADER_EXTENSIONS], allow_empty = True),
    includes = ["include"],
)

cc_library(
    name = "s2_headers",
    hdrs = glob(["include/s2/**/*." + extension for extension in HEADER_EXTENSIONS], allow_empty = True),
    includes = ["include"],
)

cc_library(
    name = "sqlite_headers",
    hdrs = glob(["include/sqlite/**/*." + extension for extension in HEADER_EXTENSIONS], allow_empty = True),
    includes = ["include"],
)

cc_library(
    name = "vsag_headers",
    hdrs = glob(["include/vsag/**/*." + extension for extension in HEADER_EXTENSIONS], allow_empty = True),
    includes = ["include"],
)

cc_library(
    name = "zlib_headers",
    hdrs = glob([
        "include/zconf.h",
        "include/zlib.h",
    ], allow_empty = True),
    includes = ["include"],
)
"""

def _seekdb_third_party_headers_repository_impl(repository_ctx):
    headers = str(repository_ctx.workspace_root) + "/" + repository_ctx.attr.path
    devtools = str(repository_ctx.workspace_root) + "/" + repository_ctx.attr.devtools_path
    libraries = str(repository_ctx.workspace_root) + "/" + repository_ctx.attr.libraries_path
    libraries64 = str(repository_ctx.workspace_root) + "/" + repository_ctx.attr.libraries64_path
    repository_ctx.symlink(headers, "include")
    repository_ctx.symlink(devtools, "devtools")
    repository_ctx.symlink(libraries, "lib")
    repository_ctx.symlink(libraries64, "lib64")
    repository_ctx.file("BUILD.bazel", _BUILD_FILE)

seekdb_third_party_headers_repository = repository_rule(
    implementation = _seekdb_third_party_headers_repository_impl,
    attrs = {
        "path": attr.string(
            default = "deps/3rd/usr/local/oceanbase/deps/devel/include",
        ),
        "devtools_path": attr.string(
            default = "deps/3rd/usr/local/oceanbase/devtools",
        ),
        "libraries_path": attr.string(
            default = "deps/3rd/usr/local/oceanbase/deps/devel/lib",
        ),
        "libraries64_path": attr.string(
            default = "deps/3rd/usr/local/oceanbase/deps/devel/lib64",
        ),
    },
    local = True,
)
