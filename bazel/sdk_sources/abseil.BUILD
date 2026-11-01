load("@rules_cc//cc:cc_library.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "library",
    linkstatic = True,
    deps = [
        "//absl/base",
        "//absl/base:config",
        "//absl/base:core_headers",
        "//absl/base:dynamic_annotations",
        "//absl/base:endian",
        "//absl/base:log_severity",
        "//absl/cleanup",
        "//absl/container:btree",
        "//absl/container:fixed_array",
        "//absl/container:flat_hash_map",
        "//absl/container:flat_hash_set",
        "//absl/container:inlined_vector",
        "//absl/container:node_hash_map",
        "//absl/flags:flag",
        "//absl/flags:reflection",
        "//absl/hash",
        "//absl/memory",
        "//absl/meta:type_traits",
        "//absl/numeric:bits",
        "//absl/numeric:int128",
        "//absl/strings",
        "//absl/strings:str_format",
        "//absl/synchronization",
        "//absl/types:span",
        "//absl/utility",
    ],
)

filegroup(
    name = "licenses",
    srcs = ["LICENSE"],
)

filegroup(
    name = "source_metadata",
    srcs = ["source.json"],
)
