load("@rules_cc//cc:cc_library.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "library",
    hdrs = glob(["boost/**"]),
    includes = ["."],
    linkstatic = True,
)

filegroup(
    name = "licenses",
    srcs = ["LICENSE_1_0.txt"],
)

filegroup(
    name = "source_metadata",
    srcs = ["source.json"],
)
