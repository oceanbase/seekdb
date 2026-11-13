# Copyright (c) 2026 OceanBase.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Expose prebuilt jemalloc, retaining the Rust build for iOS and Windows."""

load("@rules_cc//cc:cc_import.bzl", "cc_import")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_rust//rust:rust_common.bzl", "BuildInfo")
load("//bazel:platforms.bzl", "PLATFORM_ATTRS", "target_platform")

def _jemalloc_build_impl(ctx):
    windows = target_platform(ctx).os == "windows"
    archive_name = "jemalloc.lib" if windows else "libjemalloc_pic.a"
    built_archive = "jemalloc_s.lib" if windows else "libjemalloc_pic.a"
    archive = ctx.actions.declare_file("jemalloc/lib/" + archive_name)
    public_header = ctx.actions.declare_file("jemalloc/include/jemalloc/jemalloc.h")
    output = ctx.attr.build_script[BuildInfo].out_dir
    ctx.actions.run_shell(
        inputs = [output],
        outputs = [archive, public_header],
        arguments = [output.path, archive.path, public_header.path, built_archive],
        command = 'cp "$1/jemalloc-build/lib/$4" "$2" && cp "$1/jemalloc-build/include/jemalloc/jemalloc.h" "$3"',
        mnemonic = "JemallocArtifacts",
        use_default_shell_env = windows,
    )
    return [
        DefaultInfo(files = depset([archive, public_header])),
        OutputGroupInfo(
            jemalloc_archive = depset([archive]),
            jemalloc_header = depset([public_header]),
        ),
    ]

_jemalloc_build = rule(
    implementation = _jemalloc_build_impl,
    attrs = {
        "build_script": attr.label(default = "@external_crates//:jemalloc_build", providers = [BuildInfo]),
    } | PLATFORM_ATTRS,
)

def jemalloc(name):
    """Expose the platform's jemalloc archive and public header to C++."""
    build_target = "_%s_build" % name
    rust_archive = "_%s_rust_archive" % name
    rust_header = "_%s_rust_header" % name

    _jemalloc_build(
        name = build_target,
        target_compatible_with = select({
            "@platforms//os:ios": [],
            "@platforms//os:windows": [],
            "//conditions:default": ["@platforms//:incompatible"],
        }),
    )

    native.filegroup(
        name = rust_archive,
        srcs = [":" + build_target],
        output_group = "jemalloc_archive",
    )

    native.filegroup(
        name = rust_header,
        srcs = [":" + build_target],
        output_group = "jemalloc_header",
    )

    cc_library(
        name = "_%s_rust_headers" % name,
        hdrs = [":" + rust_header],
        strip_include_prefix = "jemalloc/include",
    )

    native.filegroup(
        name = "%s_archive" % name,
        srcs = select({
            "@platforms//os:linux": ["@third_party_native//:lib/libjemalloc_pic.a"],
            "@platforms//os:macos": ["@third_party_native//:lib/libjemalloc_pic.a"],
            "@platforms//os:android": ["@third_party_android//:lib/libjemalloc_pic.a"],
            "//conditions:default": [":" + rust_archive],
        }),
        visibility = ["//src/observer:__pkg__"],
    )

    native.alias(
        name = "%s_headers" % name,
        actual = select({
            "@platforms//os:linux": "@third_party_native//:jemalloc_headers",
            "@platforms//os:macos": "@third_party_native//:jemalloc_headers",
            "@platforms//os:android": "@third_party_android//:jemalloc_headers",
            "//conditions:default": ":_%s_rust_headers" % name,
        }),
        visibility = ["//src/oblib:__pkg__", "//src/observer:__pkg__"],
    )

    cc_import(
        name = "%s_library" % name,
        static_library = ":%s_archive" % name,
        visibility = ["//src/oblib:__pkg__", "//unittest/oblib:__pkg__"],
    )
