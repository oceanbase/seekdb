"""Expose CXX interfaces backed by the single Rust archive."""

load(":cxx.bzl", "cxx_bridge", "cxx_generate")
load(":defs.bzl", "cc_library")

def _platform_label(platform):
    return "//conditions:default" if platform == "default" else "@platforms//os:" + platform

def rust_modules(modules, generated_headers = []):
    """Paths in each module are relative to its crate directory.

    features configures the CXX generator; the crate declares its Rust features.
    Each module exposes headers and a library with the Rust archive.
    exclude_platforms disables that library and omits its bridge from the final link.
    """
    native.alias(
        name = "cxxbridge",
        actual = "@cxxbridge_source//:cxxbridge",
        visibility = ["//visibility:public"],
    )
    native.alias(
        name = "archive",
        actual = "//rust/seekdb-rust:archive",
        visibility = ["//visibility:public"],
    )
    cxx_generate(name = "_cxx_header", header = "cxx.h")
    cc_library(
        name = "cxx_headers",
        hdrs = ["cxx.h"],
        visibility = ["//visibility:public"],
    )

    headers = ["cxx.h"] + generated_headers
    bridges = []
    for crate, module in modules.items():
        name = crate.replace("-", "_")
        cxx_bridge(
            name = name,
            src = "//rust/" + crate + ":" + module["bridge"],
            hdrs = ["//rust/" + crate + ":" + header for header in module.get("headers", [])],
            deps = module.get("deps", []),
            cfgs = select({
                platform: ['feature="%s"' % feature for feature in features]
                for platform, features in module.get("features", {"//conditions:default": []}).items()
            }),
        )
        headers.append(name + ".h")
        excluded = module.get("exclude_platforms", [])
        compatible_with = []
        if excluded:
            bridges += select({
                "//conditions:default": [":" + name + "_bridge"],
            } | {_platform_label(platform): [] for platform in excluded})
            compatible_with = select({
                "//conditions:default": [],
            } | {_platform_label(platform): ["@platforms//:incompatible"] for platform in excluded})
        else:
            bridges += [":" + name + "_bridge"]
        cc_library(
            name = name,
            deps = [":" + name + "_bridge", ":archive"],
            target_compatible_with = compatible_with,
            visibility = ["//visibility:public"],
        )

    native.filegroup(
        name = "generated_headers",
        srcs = headers,
        visibility = ["//visibility:public"],
    )
    cc_library(
        name = "bridges",
        deps = bridges,
        visibility = ["//visibility:public"],
    )
