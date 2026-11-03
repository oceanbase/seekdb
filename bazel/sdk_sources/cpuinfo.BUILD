load("@rules_cc//cc:cc_library.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "library",
    srcs = [
        "src/api.c",
        "src/cache.c",
        "src/init.c",
        "src/log.c",
    ] + select({
        "@platforms//os:emscripten": ["src/emscripten/init.c"],
        "//conditions:default": [
            "src/arm/cache.c",
            "src/arm/mach/init.c",
            "src/arm/uarch.c",
            "src/mach/topology.c",
        ],
    }),
    hdrs = glob(
        [
            "include/**/*.h",
            "src/**/*.h",
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
    includes = [
        "include",
        "src",
    ],
    linkstatic = True,
    local_defines = [
        "CPUINFO_LOG_TO_STDIO=1",
        "CPUINFO_LOG_LEVEL=2",
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
