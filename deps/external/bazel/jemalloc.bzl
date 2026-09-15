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

    ctx.actions.run_shell(
        inputs = depset(
            direct = [
                ctx.file.external_src,
                ctx.file.lockfile,
                ctx.file.manifest,
                ctx.file.rust_toolchain,
            ],
            transitive = [cc_toolchain.all_files],
        ),
        outputs = [archive, public_header],
        arguments = [
            ctx.file.manifest.path,
            ctx.file.rust_toolchain.path,
            cc_toolchain.compiler_executable,
            cc_toolchain.ar_executable,
            archive.path,
            public_header.path,
        ],
        command = r"""
set -euo pipefail

exec_root="$PWD"
absolute_path() {
  case "$1" in
    /*) printf '%s\n' "$1" ;;
    *) printf '%s/%s\n' "$exec_root" "$1" ;;
  esac
}

manifest="$(absolute_path "$1")"
rust_toolchain_file="$(absolute_path "$2")"
cc="$(absolute_path "$3")"
ar="$(absolute_path "$4")"
archive="$(absolute_path "$5")"
public_header="$(absolute_path "$6")"
output_root="$(dirname "$(dirname "$archive")")"
cargo_target="$(mktemp -d "${TMPDIR:-/tmp}/seekdb-bazel-cargo.XXXXXX")"
trap 'rm -rf "$cargo_target"' EXIT

# Running under this directory lets rustup select rust/rust-toolchain.toml.
cd "$(dirname "$rust_toolchain_file")"
CC="$cc" \
AR="$ar" \
CFLAGS="-O2 -fPIC" \
CARGO_TARGET_DIR="$cargo_target" \
MAKEFLAGS="" \
JEMALLOC_SYS_CONFIGURE_ARGS="--with-jemalloc-prefix=je_" \
JEMALLOC_SYS_OUTPUT_DIR="$output_root" \
cargo build --locked --release --jobs 4 --manifest-path "$manifest"

test -f "$archive"
test -f "$public_header"
""",
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
        "rust_toolchain": attr.label(allow_single_file = True, mandatory = True),
    } | CC_TOOLCHAIN_ATTRS,
    fragments = ["cpp"],
    toolchains = use_cc_toolchain(),
)

def seekdb_jemalloc(name, external_src, lockfile, manifest, rust_toolchain):
    """Declares the jemalloc Cargo build and its C++ archive/header targets."""
    build_target = "_%s_build" % name
    public_header_target = "%s_public_header" % name

    _seekdb_jemalloc_build(
        name = build_target,
        external_src = external_src,
        lockfile = lockfile,
        manifest = manifest,
        rust_toolchain = rust_toolchain,
        # Windows and other platforms will be enabled when their Cargo/native
        # toolchain integration is supported here.
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
