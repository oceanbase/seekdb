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
load("//bazel:cargo.bzl", "CARGO_ACTION_ATTRS", "CARGO_TOOLCHAINS", "cargo_action")
load("//bazel:platforms.bzl", "target_platform")

def _jemalloc_build_impl(ctx):
    archive = ctx.actions.declare_file("jemalloc/lib/libjemalloc_pic.a")
    public_header = ctx.actions.declare_file(
        "jemalloc/include/jemalloc/jemalloc.h",
    )
    cargo_target = ctx.actions.declare_directory("jemalloc/cargo-work")

    platform = target_platform(ctx)
    command = """export CFLAGS='-O2 -fPIC'
export MAKEFLAGS=
export JEMALLOC_SYS_OUTPUT_DIR="$PWD/$1"
export JEMALLOC_SYS_CONFIGURE_ARGS='--with-jemalloc-prefix=je_'
manifest="$CARGO_WORKSPACE/$2"
"""
    if platform.os == "ios":
        command += """export CC="$(xcrun --sdk %s --find clang)"
export AR="$(xcrun --sdk %s --find ar)"
target="arm64-apple-ios$IPHONEOS_DEPLOYMENT_TARGET%s"
export CFLAGS="-O2 -fPIC -target $target -isysroot $SDKROOT"
export CPPFLAGS= LDFLAGS=
export JEMALLOC_SYS_CONFIGURE_ARGS='--with-jemalloc-prefix=je_
--host=aarch64-apple-ios
--with-lg-page=14
--disable-zone-allocator'
""" % (platform.ios_sdk, platform.ios_sdk, "-simulator" if platform.ios_sdk == "iphonesimulator" else "")
    command += '"$CARGO" build --offline --locked --release --jobs 4 --manifest-path "$manifest"'
    if platform.rust_target:
        command += " --target " + platform.rust_target
    cargo_action(
        ctx,
        command = command,
        sources = [ctx.file.external_src, ctx.file.lockfile, ctx.file.manifest],
        outputs = [archive, public_header],
        work_dir = cargo_target,
        arguments = [archive.dirname + "/..", ctx.file.manifest.path],
        mnemonic = "SeekdbJemallocBuild",
        progress_message = "Building seekdb-jemalloc-sys for " + platform.os,
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
        "external_src": attr.label(allow_single_file = True, mandatory = True),
        "lockfile": attr.label(allow_single_file = True, mandatory = True),
        "manifest": attr.label(allow_single_file = True, mandatory = True),
    } | CARGO_ACTION_ATTRS,
    fragments = ["cpp", "apple"],
    toolchains = CARGO_TOOLCHAINS,
)

def jemalloc(name, external_src, lockfile, manifest):
    """Declares the jemalloc Cargo build and its C++ archive/header targets."""
    build_target = "_%s_build" % name
    public_header_target = "%s_public_header" % name

    _jemalloc_build(
        name = build_target,
        external_src = external_src,
        lockfile = lockfile,
        manifest = manifest,
        target_compatible_with = select({
            "@platforms//os:linux": [],
            "@platforms//os:macos": [],
            "@platforms//os:ios": [],
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
        visibility = ["//src/oblib:__pkg__"],
    )
