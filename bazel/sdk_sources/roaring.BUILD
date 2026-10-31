load("@rules_cc//cc:cc_library.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "library",
    srcs = [
        "src/array_util.c",
        "src/art/art.c",
        "src/bitset.c",
        "src/bitset_util.c",
        "src/containers/array.c",
        "src/containers/bitset.c",
        "src/containers/containers.c",
        "src/containers/convert.c",
        "src/containers/mixed_andnot.c",
        "src/containers/mixed_equal.c",
        "src/containers/mixed_intersection.c",
        "src/containers/mixed_negation.c",
        "src/containers/mixed_subset.c",
        "src/containers/mixed_union.c",
        "src/containers/mixed_xor.c",
        "src/containers/run.c",
        "src/isadetection.c",
        "src/memory.c",
        "src/roaring.c",
        "src/roaring64.c",
        "src/roaring_array.c",
        "src/roaring_priority_queue.c",
    ],
    hdrs = glob(
        [
            "include/**/*.h",
            "include/**/*.hh",
            "cpp/*.hh",
        ],
        allow_empty = True,
    ),
    copts = select({
        "@platforms//os:emscripten": [
            "-pthread",
            "-fexceptions",
        ],
        "//conditions:default": [],
    }),
    defines = select({
        "@platforms//os:emscripten": ["ROARING_EXCEPTIONS=0"],
        "//conditions:default": [],
    }),
    includes = [
        "cpp",
        "include",
        "include/roaring",
    ],
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
