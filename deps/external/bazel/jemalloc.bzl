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

"""Build and expose the native artifacts from seekdb-jemalloc-sys."""

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
    """Expose the native outputs of the rules_rust build script to C++."""
    build_target = "_%s_build" % name
    public_header_target = "%s_public_header" % name

    _jemalloc_build(
        name = build_target,
        target_compatible_with = select({
            "@platforms//os:linux": [],
            "@platforms//os:macos": [],
            "@platforms//os:ios": [],
            "@platforms//os:windows": [],
            "@platforms//os:android": [],
            "//conditions:default": ["@platforms//:incompatible"],
        }),
    )

    native.filegroup(
        name = "%s_archive" % name,
        srcs = [":" + build_target],
        output_group = "jemalloc_archive",
        visibility = ["//src/observer:__pkg__"],
    )

    native.filegroup(
        name = public_header_target,
        srcs = [":" + build_target],
        output_group = "jemalloc_header",
    )

    cc_library(
        name = "%s_headers" % name,
        hdrs = [":" + public_header_target],
        strip_include_prefix = "jemalloc/include",
        visibility = ["//src/oblib:__pkg__", "//src/observer:__pkg__"],
    )

    cc_import(
        name = "%s_library" % name,
        static_library = ":%s_archive" % name,
        visibility = ["//src/oblib:__pkg__", "//unittest/oblib:__pkg__"],
    )
