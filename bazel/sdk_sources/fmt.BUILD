load("@rules_cc//cc:cc_library.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "library",
    srcs = [
        "src/format.cc",
        "src/os.cc",
    ],
    hdrs = glob(
        ["include/fmt/*.h"],
        allow_empty = True,
    ),
    copts = ["-std=c++17"] + select({
        "@platforms//os:emscripten": [
            "-pthread",
            "-fexceptions",
        ],
        "//conditions:default": [],
    }),
    includes = ["include"],
    linkstatic = True,
)

filegroup(
    name = "licenses",
    srcs = ["LICENSE"],
)

filegroup(
    name = "source_metadata",
    srcs = ["source.json"],
)
