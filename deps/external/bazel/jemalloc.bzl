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

"""Build jemalloc from the Cargo-locked registry source package."""

load(
    "@rules_cc//cc:find_cc_toolchain.bzl",
    "CC_TOOLCHAIN_ATTRS",
    "find_cpp_toolchain",
    "use_cc_toolchain",
)

def _seekdb_jemalloc_build_impl(ctx):
    cc_toolchain = find_cpp_toolchain(ctx)
    archive = ctx.actions.declare_file("jemalloc/lib/libjemalloc_pic.a")
    public_header = ctx.actions.declare_file(
        "jemalloc/include/jemalloc/jemalloc.h",
    )

    ctx.actions.run_shell(
        inputs = depset(
            direct = [
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

rust_toolchain="$({
  sed -n 's/^[[:space:]]*channel[[:space:]]*=[[:space:]]*"\([0-9][0-9.]*\)".*/\1/p' \
    "$rust_toolchain_file"
} | head -1)"
if [[ -z "$rust_toolchain" ]]; then
  echo "Cannot read pinned Rust toolchain from $rust_toolchain_file" >&2
  exit 1
fi

jemalloc_source="$({
  RUSTUP_TOOLCHAIN="$rust_toolchain" \
    cargo metadata --locked --format-version 1 --manifest-path "$manifest"
} | python3 -c '
import json
import os
import sys

for package in json.load(sys.stdin)["packages"]:
    if package["name"] == "seekdb-jemalloc-sys":
        if not (package.get("source") or "").startswith("registry+"):
            raise SystemExit("seekdb-jemalloc-sys must come from a registry")
        print(os.path.join(os.path.dirname(package["manifest_path"]), "vendor", "jemalloc"))
        break
else:
    raise SystemExit("seekdb-jemalloc-sys is absent from Cargo metadata")
')"
if [[ ! -f "$jemalloc_source/configure" ]]; then
  echo "Cargo package does not contain jemalloc configure: $jemalloc_source" >&2
  exit 1
fi

build_dir="$(mktemp -d "${TMPDIR:-/tmp}/seekdb-bazel-jemalloc.XXXXXX")"
trap 'rm -rf "$build_dir"' EXIT
cd "$build_dir"
CC="$cc" AR="$ar" CFLAGS="-O2 -fPIC" \
  sh "$jemalloc_source/configure" \
    --with-version=VERSION \
    --with-jemalloc-prefix=je_ \
    --enable-static \
    --disable-shared \
    --disable-cxx \
    --disable-doc \
    --enable-stats

make_command=make
if command -v gmake >/dev/null 2>&1; then
  make_command=gmake
fi
"$make_command" -j4 build_lib_static

mkdir -p "$(dirname "$archive")" "$(dirname "$public_header")"
cp lib/libjemalloc_pic.a "$archive"
cp include/jemalloc/jemalloc.h "$public_header"
""",
        execution_requirements = {
            "no-remote": "1",
            "no-sandbox": "1",
        },
        mnemonic = "SeekdbJemallocBuild",
        progress_message = "Building Cargo-sourced jemalloc",
        use_default_shell_env = True,
    )

    return [
        DefaultInfo(files = depset([archive, public_header])),
        OutputGroupInfo(
            jemalloc_archive = depset([archive]),
            jemalloc_header = depset([public_header]),
        ),
    ]

seekdb_jemalloc_build = rule(
    implementation = _seekdb_jemalloc_build_impl,
    attrs = {
        "lockfile": attr.label(allow_single_file = True, mandatory = True),
        "manifest": attr.label(allow_single_file = True, mandatory = True),
        "rust_toolchain": attr.label(allow_single_file = True, mandatory = True),
    } | CC_TOOLCHAIN_ATTRS,
    fragments = ["cpp"],
    toolchains = use_cc_toolchain(),
)
