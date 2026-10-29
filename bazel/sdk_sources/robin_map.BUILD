load("@rules_cc//cc:cc_library.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "library",
    hdrs = glob(
        ["include/**/*.h"],
        allow_empty = True,
    ),
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
