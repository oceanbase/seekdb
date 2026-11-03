"""Keep AWS-LC build-script features in the execution configuration."""

load("@rules_rust//cargo:defs.bzl", "cargo_build_script")

def build_scripts():
    features = [
        "all-bindings",
        "default",
        "prebuilt-nasm",
    ]

    deps = [
        "@rust_crates__cc-1.5.1//:cc",
        "@rust_crates__cmake-0.1.58//:cmake",
        "@rust_crates__dunce-1.0.5//:dunce",
        "@rust_crates__fs_extra-1.3.0//:fs_extra",
    ]

    attrs = {
        "build_script_env": {
            "DEBUG": "true",
            "PROFILE": "release",
        },
        "compile_data": native.glob(
            ["**"],
            allow_empty = True,
            exclude = [
                "**/* *",
                "**/*.rs",
                ".tmp_git_root/**/*",
                "BUILD",
                "BUILD.bazel",
                "WORKSPACE",
                "WORKSPACE.bazel",
            ],
        ),
        "crate_name": "build_script_main",
        "crate_root": "builder/main.rs",
        "data": native.glob(
            ["**"],
            allow_empty = True,
            exclude = [
                "**/* *",
                ".tmp_git_root/**/*",
                "BUILD",
                "BUILD.bazel",
                "WORKSPACE",
                "WORKSPACE.bazel",
            ],
        ),
        "edition": "2021",
        "emit_warnings": False,
        "links": "aws_lc_0_47_0",
        "pkg_name": "aws-lc-sys",
        "rustc_env_files": [":cargo_toml_env_vars"],
        "rustc_flags": ["--cap-lints=allow"],
        "srcs": native.glob(["**/*.rs"], allow_empty = True),
        "tags": [
            "cargo-bazel",
            "crate-name=aws-lc-sys",
            "manual",
            "noclippy",
            "norustfmt",
        ],
        "version": "0.47.0",
        "visibility": ["//visibility:private"],
    }

    cargo_build_script(
        name = "_bs",
        build_script_env_files = [":cargo_toml_env_vars"],
        crate_features = features,
        deps = deps + select({
            "@platforms//os:windows": [],
            "//conditions:default": ["@rust_crates__pkg-config-0.3.34//:pkg_config"],
        }),
        **attrs
    )

    # Select the builder before cargo_build_script transitions its binary to exec.
    # Features selected inside one builder would be re-evaluated for the macOS host.
    cargo_build_script(
        name = "_ios_bs",
        build_script_env_files = [
            ":cargo_toml_env_vars",
            "@@//bazel:ios_bindgen_environment",
        ],
        crate_features = features + ["bindgen"],
        deps = deps + [
            "@rust_crates__bindgen-0.73.2//:bindgen",
            "@rust_crates__pkg-config-0.3.34//:pkg_config",
        ],
        **attrs
    )

    native.alias(
        name = "platform_build_script",
        actual = select({
            "@rules_rust//rust/platform:aarch64-apple-ios": ":_ios_bs",
            "@rules_rust//rust/platform:aarch64-apple-ios-sim": ":_ios_bs",
            "//conditions:default": ":_bs",
        }),
        tags = ["manual"],
    )
