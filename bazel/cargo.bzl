"""Build offline Cargo libraries and host tools with declared Bazel inputs."""

load("@apple_support//lib:apple_support.bzl", "apple_support")
load("//bazel:platforms.bzl", "PLATFORM_ATTRS", "WINDOWS_STL_COMPAT_DEFINE", "target_platform")
load("//bazel:cargo_toolchain.bzl", "CARGO_TOOLCHAIN_TYPE")
load("@rules_cc//cc/common:cc_common.bzl", "cc_common")
load("@rules_cc//cc:action_names.bzl", "C_COMPILE_ACTION_NAME", "CPP_COMPILE_ACTION_NAME")
load("@rules_cc//cc:find_cc_toolchain.bzl", "CC_TOOLCHAIN_ATTRS", "find_cpp_toolchain", "use_cc_toolchain")

CARGO_TOOLCHAINS = use_cc_toolchain() + [CARGO_TOOLCHAIN_TYPE]

CARGO_ACTION_ATTRS = PLATFORM_ATTRS | CC_TOOLCHAIN_ATTRS | apple_support.action_required_attrs() | {
    "_cargo_runner": attr.label(default = Label("//bazel:cargo_runner.py"), allow_single_file = True),
}

def cargo_action(ctx, command, sources, outputs, work_dir, env = {}, extra_inputs = [], **kwargs):
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
    ]
    if platform.os in ["linux", "android"]:
        arguments += ["--ld", cc.ld_executable, "--nm", cc.nm_executable]
    arguments += ["--"]
    environment = {"PYTHONHOME": toolchain.python_home}
    environment.update(env)
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
    else:
        if platform.apple:
            config = ctx.attr._xcode_config[apple_common.XcodeVersionConfig]
            execution_requirements.update(config.execution_info())
            if platform.os == "ios":
                environment["IPHONEOS_DEPLOYMENT_TARGET"] = str(config.minimum_os_for_platform_type(apple_common.platform_type.ios))
            else:
                environment["MACOSX_DEPLOYMENT_TARGET"] = str(config.minimum_os_for_platform_type(apple_common.platform_type.macos))
    if type(command) == "list":
        arguments += command
    else:
        arguments += ["/bin/bash", "-c", "set -eu\n" + command, "cargo-action"] + kwargs.pop("arguments", [])
    ctx.actions.run(
        executable = toolchain.python,
        arguments = arguments,
        inputs = depset(
            direct = [ctx.file._cargo_runner, source_manifest, toolchain.vendor] + sources + extra_inputs,
            transitive = [toolchain.files, cc.all_files],
        ),
        outputs = outputs + [work_dir],
        env = environment,
        use_default_shell_env = True,
        execution_requirements = execution_requirements,
        **kwargs
    )

def _cargo_build_impl(ctx):
    platform = target_platform(ctx)
    archive = ctx.actions.declare_file("cargo/" + ("seekdb_rust.lib" if platform.os == "windows" else "libseekdb_rust.a"))
    work_dir = ctx.actions.declare_directory("cargo-work")
    command = ["python", "${CARGO_WORKSPACE}/rust/build.py", "--cargo", "${CARGO}",
               "--target-dir", "${CARGO_TARGET_DIR}", "--output", archive.path]
    if platform.rust_target:
        command += ["--target", platform.rust_target]
    if platform.os == "emscripten":
        command = '''wasm="$CARGO_TARGET_DIR/wasm"
python "$CARGO_WORKSPACE/bazel/prepare_wasm_rust.py" "$("$RUSTC" --print sysroot)" "$wasm"
export RUSTFLAGS="--sysroot=$wasm/sysroot -Ctarget-feature=+atomics,+bulk-memory,+mutable-globals -Cpanic=abort"
export CXXFLAGS_wasm32_unknown_emscripten="-pthread -fexceptions"
python "$wasm/workspace/build.py" --cargo "$CARGO" --target wasm32-unknown-emscripten --target-dir "$CARGO_TARGET_DIR" --output "%s" -- --no-default-features --features sql-nio/memory-transport -Zbuild-std=std,panic_abort
''' % archive.path
    cargo_action(
        ctx,
        command = command,
        sources = ctx.files.srcs + ctx.files.platform_inputs,
        extra_inputs = [ctx.file.config_rs],
        env = {"CONFIG_RS": ctx.file.config_rs.path},
        outputs = [archive],
        work_dir = work_dir,
        mnemonic = "CargoBuild",
        progress_message = "Building Rust libraries for " + platform.os,
    )
    return [DefaultInfo(files = depset([archive]))]

cargo_library = rule(
    implementation = _cargo_build_impl,
    attrs = CARGO_ACTION_ATTRS | {
        "srcs": attr.label_list(allow_files = True),
        "config_rs": attr.label(allow_single_file = True, mandatory = True),
        "platform_inputs": attr.label_list(allow_files = True),
    },
    fragments = ["apple", "cpp"],
    toolchains = CARGO_TOOLCHAINS,
)

def _cxxbridge_tool_impl(ctx):
    suffix = ".exe" if target_platform(ctx).os == "windows" else ""
    tool = ctx.actions.declare_file("cxxbridge/bin/cxxbridge" + suffix)
    # The workspace's locked vendor snapshot pins the tool and its dependencies.
    cargo_action(
        ctx,
        command = ["${CARGO}", "install", "--offline", "--debug",
                   "--config", "profile.dev.debug=0", "--path", "${CARGO_VENDOR}/cxxbridge-cmd",
                   "--root", tool.dirname + "/.."],
        sources = [],
        outputs = [tool],
        work_dir = ctx.actions.declare_directory("cxxbridge-work"),
        mnemonic = "CxxbridgeTool",
        progress_message = "Building the host CXX generator",
    )
    return [DefaultInfo(executable = tool)]

cxxbridge_tool = rule(
    implementation = _cxxbridge_tool_impl,
    attrs = CARGO_ACTION_ATTRS,
    fragments = ["apple", "cpp"],
    toolchains = CARGO_TOOLCHAINS,
    executable = True,
)
