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

load(
    "@rules_cc//cc:find_cc_toolchain.bzl",
    "CC_TOOLCHAIN_ATTRS",
    "find_cpp_toolchain",
    "use_cc_toolchain",
)
load("@rules_cc//cc:cc_library.bzl", "cc_library")

def _seekdb_jemalloc_build_impl(ctx):
    cc_toolchain = find_cpp_toolchain(ctx)
    archive = ctx.actions.declare_file("jemalloc/lib/libjemalloc_pic.a")
    public_header = ctx.actions.declare_file(
        "jemalloc/include/jemalloc/jemalloc.h",
    )
    cargo_target = ctx.actions.declare_directory("jemalloc/cargo-target")

    ctx.actions.run(
        inputs = depset(
            direct = [
                ctx.file.external_src,
                ctx.file.lockfile,
                ctx.file.manifest,
            ],
            transitive = [cc_toolchain.all_files],
        ),
        outputs = [archive, public_header, cargo_target],
        arguments = [
            "build",
            "--locked",
            "--release",
            "--jobs",
            "4",
            "--manifest-path",
            ctx.file.manifest.path,
        ],
        env = {
            "AR": cc_toolchain.ar_executable,
            "CARGO_TARGET_DIR": cargo_target.path,
            "CC": cc_toolchain.compiler_executable,
            "CFLAGS": "-O2 -fPIC",
            "JEMALLOC_SYS_CONFIGURE_ARGS": "--with-jemalloc-prefix=je_",
            "JEMALLOC_SYS_OUTPUT_DIR": archive.dirname + "/..",
            "MAKEFLAGS": "",
        },
        executable = ctx.attr.cargo,
        execution_requirements = {
            "no-remote": "1",
            "no-sandbox": "1",
        },
        mnemonic = "SeekdbJemallocBuild",
        progress_message = "Building seekdb-jemalloc-sys",
        use_default_shell_env = True,
    )

    return [
        DefaultInfo(files = depset([archive, public_header])),
        OutputGroupInfo(
            jemalloc_archive = depset([archive]),
            jemalloc_header = depset([public_header]),
        ),
    ]

_seekdb_jemalloc_build = rule(
    implementation = _seekdb_jemalloc_build_impl,
    attrs = {
        "external_src": attr.label(allow_single_file = True, mandatory = True),
        "lockfile": attr.label(allow_single_file = True, mandatory = True),
        "manifest": attr.label(allow_single_file = True, mandatory = True),
        "cargo": attr.string(default = "cargo"),
    } | CC_TOOLCHAIN_ATTRS,
    fragments = ["cpp"],
    toolchains = use_cc_toolchain(),
)

def seekdb_jemalloc(name, external_src, lockfile, manifest):
    """Declares the jemalloc Cargo build and its C++ archive/header targets."""
    build_target = "_%s_build" % name
    public_header_target = "%s_public_header" % name

    _seekdb_jemalloc_build(
        name = build_target,
        external_src = external_src,
        lockfile = lockfile,
        manifest = manifest,
        # Windows and other platforms will be supported later, after their
        # jemalloc toolchains are provisioned and covered by CI.
        target_compatible_with = select({
            "@platforms//os:linux": [],
            "@platforms//os:macos": [],
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
        visibility = ["//src/oblib:__pkg__"],
    )
