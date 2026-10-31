"""Expose the Cargo workspace's declared C++ exports to Bazel."""

load("//bazel:defs.bzl", "cc_library")
load("@apple_support//lib:apple_support.bzl", "apple_support")
load("//bazel:platforms.bzl", "PLATFORM_ATTRS", "WINDOWS_STL_COMPAT_DEFINE", "target_platform")
load("//bazel:cargo_toolchain.bzl", "CARGO_TOOLCHAIN_TYPE")
load("@rules_cc//cc/common:cc_common.bzl", "cc_common")
load("@rules_cc//cc:action_names.bzl", "C_COMPILE_ACTION_NAME", "CPP_COMPILE_ACTION_NAME")
load("@rules_cc//cc:find_cc_toolchain.bzl", "CC_TOOLCHAIN_ATTRS", "find_cpp_toolchain", "use_cc_toolchain")

CARGO_TOOLCHAINS = use_cc_toolchain() + [CARGO_TOOLCHAIN_TYPE]

CARGO_ACTION_ATTRS = PLATFORM_ATTRS | CC_TOOLCHAIN_ATTRS | apple_support.action_required_attrs() | {
    "default_log_level": attr.string(default = "OB_LOG_LEVEL_ERROR"),
    "_cargo_runner": attr.label(default = Label("//bazel:cargo_runner.py"), allow_single_file = True),
}

def cargo_action(ctx, command, sources, outputs, work_dir, **kwargs):
    """Run a sandboxed, offline Cargo action using the selected local toolchain."""
    platform = target_platform(ctx)
    toolchain = ctx.toolchains[CARGO_TOOLCHAIN_TYPE]
    cc = find_cpp_toolchain(ctx)
    features = cc_common.configure_features(ctx = ctx, cc_toolchain = cc)
    compiler = cc_common.get_tool_for_action(feature_configuration = features, action_name = C_COMPILE_ACTION_NAME)
    source_manifest = ctx.actions.declare_file(ctx.label.name + ".cargo-sources.json")
    ctx.actions.write(source_manifest, json.encode([file.path for file in sources]))
    arguments = [
        ctx.file._cargo_runner.path,
        "--work-dir", work_dir.path, "--vendor", toolchain.vendor.path,
        "--cargo", toolchain.cargo.path, "--rustc", toolchain.rustc.path,
        # Cargo's cc crate expects ar syntax; Apple's C++ archive action uses libtool.
        "--sources", source_manifest.path, "--target", platform.rust_target, "--cc", compiler, "--ar", "/usr/bin/ar" if platform.apple else cc.ar_executable,
        "--",
    ]
    environment = {"DEFAULT_LOG_LEVEL": ctx.attr.default_log_level, "PYTHONHOME": toolchain.python_home}
    # Cargo's C/C++ compiler and linker need the selected toolchain's environment.
    environment.update(cc_common.get_environment_variables(
        feature_configuration = features,
        action_name = CPP_COMPILE_ACTION_NAME,
        variables = cc_common.create_compile_variables(cc_toolchain = cc, feature_configuration = features),
    ))
    # The installed toolchain uses this machine's system runtime and SDK.
    execution_requirements = {"no-remote": "1"}
    if platform.os == "windows":
        environment["CXXFLAGS"] = "/D" + WINDOWS_STL_COMPAT_DEFINE
        arguments += command
    else:
        if platform.apple:
            config = ctx.attr._xcode_config[apple_common.XcodeVersionConfig]
            execution_requirements.update(config.execution_info())
            if platform.os == "ios":
                environment["IPHONEOS_DEPLOYMENT_TARGET"] = str(config.minimum_os_for_platform_type(apple_common.platform_type.ios))
            else:
                environment["MACOSX_DEPLOYMENT_TARGET"] = str(config.minimum_os_for_platform_type(apple_common.platform_type.macos))
        arguments += ["/bin/bash", "-c", "set -eu\n" + command, "cargo-action"] + kwargs.pop("arguments", [])
    ctx.actions.run(
        executable = toolchain.python,
        arguments = arguments,
        inputs = depset(
            direct = [ctx.file._cargo_runner, source_manifest, toolchain.vendor] + sources,
            transitive = [toolchain.files, cc.all_files],
        ),
        outputs = outputs + [work_dir],
        env = environment,
        use_default_shell_env = True,
        execution_requirements = execution_requirements,
        **kwargs
    )


def _cargo_exports_repository_impl(ctx):
    exports = json.decode(ctx.read(ctx.attr.manifest))
    ctx.file("exports.bzl", "EXPORTS = " + repr(exports) + "\n")
    ctx.file("BUILD.bazel", 'exports_files(["exports.bzl"])\n')

cargo_exports_repository = repository_rule(
    implementation = _cargo_exports_repository_impl,
    attrs = {"manifest": attr.label(mandatory = True, allow_single_file = True)},
)

def _cargo_build_impl(ctx):
    exports = json.decode(ctx.attr.exports)
    platform = target_platform(ctx)
    groups = {}
    libraries = {}
    generated_headers = {
        header: ctx.actions.declare_file("cargo/include/" + header)
        for header in {h: True for export in exports.values() for h in export["headers"]}
    }
    for name, export in exports.items():
        library_name = export.get("library", name)
        archive = library_name + ".lib" if platform.os == "windows" else "lib" + library_name + ".a"
        if library_name not in libraries:
            libraries[library_name] = ctx.actions.declare_file("cargo/" + archive)
        library = libraries[library_name]
        headers = [generated_headers[h] for h in export["headers"]]
        groups[name + "_archive"] = depset([library])
        groups[name + "_headers"] = depset(headers)

    outputs = libraries.values() + generated_headers.values()
    directory = ctx.bin_dir.path + "/" + ctx.label.package
    work_dir = ctx.actions.declare_directory("cargo-work")
    command = 'python "$CARGO_WORKSPACE/rust/build.py" --cargo "$CARGO" --target-dir "$CARGO_TARGET_DIR" --output "%s/cargo"' % directory
    if platform.rust_target:
        command += " --target " + platform.rust_target
    if platform.os == "windows":
        command = ["python", "${CARGO_WORKSPACE}/rust/build.py", "--cargo", "${CARGO}", "--target-dir", "${CARGO_TARGET_DIR}", "--output", directory + "/cargo"]
    elif platform.os == "emscripten":
        command = '''wasm="$CARGO_TARGET_DIR/wasm"
python "$CARGO_WORKSPACE/bazel/prepare_wasm_rust.py" "$("$RUSTC" --print sysroot)" "$wasm"
export RUSTFLAGS="--sysroot=$wasm/sysroot -Ctarget-feature=+atomics,+bulk-memory,+mutable-globals -Cpanic=abort"
export CXXFLAGS_wasm32_unknown_emscripten="-pthread -fexceptions"
python "$wasm/workspace/build.py" --cargo "$CARGO" --target wasm32-unknown-emscripten --target-dir "$CARGO_TARGET_DIR" --output "%s/cargo" -- --no-default-features --features sql-nio/memory-transport -Zbuild-std=std,panic_abort
''' % directory
    cargo_action(
        ctx,
        command = command,
        sources = ctx.files.srcs + ctx.files.platform_inputs,
        outputs = outputs,
        work_dir = work_dir,
        mnemonic = "CargoBuild",
        progress_message = "Building Rust libraries for " + platform.os,
    )
    return [DefaultInfo(files = depset(outputs)), OutputGroupInfo(**groups)]

_cargo_build = rule(
    implementation = _cargo_build_impl,
    attrs = CARGO_ACTION_ATTRS | {
        "exports": attr.string(mandatory = True),
        "srcs": attr.label_list(allow_files = True),
        "platform_inputs": attr.label_list(allow_files = True),
    },
    fragments = ["apple", "cpp"],
    toolchains = CARGO_TOOLCHAINS,
)

def cargo_workspace(exports, srcs):
    """One build action exports each crate's archive and generated headers."""
    _cargo_build(
        name = "_rust_static",
        exports = json.encode(exports),
        default_log_level = select({
            "//bazel:rpm_logging_enabled": "OB_LOG_LEVEL_DBA_WARN",
            "//conditions:default": "OB_LOG_LEVEL_ERROR",
        }),
        srcs = srcs,
        platform_inputs = select({
            "@platforms//os:emscripten": ["//bazel:wasm_rust_build_tools"],
            "//conditions:default": [],
        }),
    )
    cc_library(
        name = "_rust_runtime",
        linkopts = select({
            "@platforms//os:linux": ["-lpthread", "-ldl", "-lm"],
            "@platforms//os:android": ["-ldl", "-lm"],
            "@platforms//os:windows": ["ntdll.lib", "userenv.lib", "ws2_32.lib", "bcrypt.lib", "advapi32.lib", "synchronization.lib"],
            "//conditions:default": [],
        }),
    )
    for name, export in exports.items():
        for kind in ["archive", "headers"]:
            native.filegroup(
                name = name + ("_archive" if kind == "archive" else "_generated_headers"),
                srcs = [":_rust_static"],
                output_group = name + "_" + kind,
                visibility = ["//visibility:public"] if kind == "archive" else None,
            )
        include_dir = export["crate"] + "/include"
        cc_library(
            name = name + "_headers",
            hdrs = native.glob([include_dir + "/**/*.h"], allow_empty = True) + [":" + name + "_generated_headers"],
            includes = [include_dir, "cargo/include"],
            deps = [":" + dep + "_headers" for dep in export["deps"]],
            visibility = ["//visibility:public"],
        )
        cc_library(
            name = name,
            srcs = [":" + name + "_archive"],
            deps = [":" + name + "_headers", ":_rust_runtime"] + [":" + dep for dep in export["deps"]],
            visibility = ["//visibility:public"],
        )
