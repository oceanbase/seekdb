# Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.

"""Shared Cargo static-library build rules for Rust C ABI packages."""

def rust_ffi_archive(name, package, srcs):
    # Match CMake's Cargo build; keep local registry access until dependencies
    # are vendored, as in the original sql-nio rule.
    native.genrule(
        name = "_" + name + "_static_unix",
        srcs = srcs,
        outs = ["lib" + name + ".a"],
        cmd = """
set -eu
export CARGO_TARGET_DIR="$(@D)/{name}-cargo-target"
cargo build --release --manifest-path "$(location Cargo.toml)" --package {package}
cp "$$CARGO_TARGET_DIR/release/lib{name}.a" "$@"
""".format(name = name, package = package),
        tags = ["no-remote", "no-sandbox"],
        target_compatible_with = select({
            ":windows": ["@platforms//:incompatible"],
            "//conditions:default": [],
        }),
    )
    native.genrule(
        name = "_" + name + "_static_windows",
        srcs = srcs,
        outs = [name + ".lib"],
        cmd_bat = """
set CARGO_TARGET_DIR=$(@D)\\{name}-cargo-target
cargo build --release --manifest-path "$(location Cargo.toml)" --package {package}
copy /Y "%CARGO_TARGET_DIR%\\release\\{name}.lib" "$@"
""".format(name = name, package = package),
        tags = ["no-remote", "no-sandbox"],
        target_compatible_with = select({
            ":windows": [],
            "//conditions:default": ["@platforms//:incompatible"],
        }),
    )
    native.filegroup(
        name = name + "_archive",
        srcs = select({
            ":windows": [":_" + name + "_static_windows"],
            "//conditions:default": [":_" + name + "_static_unix"],
        }),
        visibility = ["//src/observer:__pkg__"],
    )
