load("@rules_cc//cc:cc_library.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "library",
    hdrs = glob(
        ["ThreadPool.h"],
        allow_empty = True,
    ),
    includes = ["."],
    linkstatic = True,
)

filegroup(
    name = "licenses",
    srcs = ["COPYING"],
)

filegroup(
    name = "source_metadata",
    srcs = ["source.json"],
)
