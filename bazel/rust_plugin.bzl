"""Build independent Rust cdylibs and version-bound server-dev contracts."""

load("//bazel:cargo.bzl", "CARGO_ACTION_ATTRS", "CARGO_TOOLCHAINS", "cargo_action")
load("//bazel:platforms.bzl", "target_platform")
load("//bazel:plugin.bzl", "audit_plugin", "plugin_package")

_LINUX = select({"@platforms//os:linux": [], "//conditions:default": ["@platforms//:incompatible"]})

def _contract_tool_impl(ctx):
    output = ctx.actions.declare_file("server_dev_contract")
    platform = target_platform(ctx)
    target = " --target " + platform.rust_target if platform.rust_target else ""
    directory = platform.rust_target + "/" if platform.rust_target else ""
    cargo_action(
        ctx,
        command = '"$CARGO" build --offline --locked --release --manifest-path "$CARGO_WORKSPACE/rust/Cargo.toml" -p seekdb-plugin-runtime --example server_dev_contract' + target +
                  '\ncp "$CARGO_TARGET_DIR/' + directory + 'release/examples/server_dev_contract" "$1"',
        sources = ctx.files.srcs,
        outputs = [output],
        work_dir = ctx.actions.declare_directory("contract-tool-work"),
        arguments = [output.path],
        mnemonic = "PluginContractTool",
    )
    return [DefaultInfo(files = depset([output]), executable = output)]

contract_tool = rule(
    implementation = _contract_tool_impl,
    attrs = CARGO_ACTION_ATTRS | {"srcs": attr.label_list(allow_files = True)},
    toolchains = CARGO_TOOLCHAINS,
    fragments = ["cpp", "apple"],
    executable = True,
)

def _contract_impl(ctx):
    output = ctx.actions.declare_file(ctx.attr.output_name)
    ctx.actions.run_shell(
        tools = [ctx.executable.tool],
        inputs = [ctx.file.host],
        outputs = [output],
        command = '"$1" "$2" - $4 > "$3"',
        arguments = [ctx.executable.tool.path, ctx.file.host.path, output.path, "--rust" if ctx.attr.rust else ""],
        mnemonic = "PluginHostContract",
    )
    return [DefaultInfo(files = depset([output]))]

host_contract = rule(
    implementation = _contract_impl,
    attrs = {
        "host": attr.label(allow_single_file = True, default = Label("//src/observer:seekdb")),
        "tool": attr.label(executable = True, cfg = "exec", default = Label("//plugins:server_dev_contract_tool")),
        "output_name": attr.string(default = "server_dev_contract.h"),
        "rust": attr.bool(default = False),
    },
)

def _rust_plugin_impl(ctx):
    platform = target_platform(ctx)
    output = ctx.actions.declare_file(ctx.label.name + "/lib" + ctx.attr.library + ".so")
    package = ctx.label.package
    sources = ctx.files.srcs + ctx.files.sdk + [ctx.file._boundary]
    command = 'manifest="$CARGO_WORKSPACE/' + package + '/Cargo.toml"\n'
    command += 'python "$CARGO_WORKSPACE/cmake/rust_plugin_boundary_check.py" --cargo "$CARGO" --manifest "$manifest" --plugin-root "$CARGO_WORKSPACE/' + package + '" --sdk-root "$CARGO_WORKSPACE/rust/extension-sdk"\n'
    if ctx.file.contract:
        # Contract comes from the completed host, not from a source revision.
        sources += [ctx.file.contract]
        command += 'export SEEKDB_SERVER_DEV_CONTRACT="$CARGO_WORKSPACE/' + ctx.file.contract.path + '"\n'
    target = " --target " + platform.rust_target if platform.rust_target else ""
    directory = platform.rust_target + "/" if platform.rust_target else ""
    command += '"$CARGO" rustc --offline --locked --release --manifest-path "$manifest"' + target + ' -- -C link-arg=-Wl,-z,defs\n'
    command += 'cp "$CARGO_TARGET_DIR/' + directory + 'release/lib' + ctx.attr.library + '.so" "$1"'
    cargo_action(
        ctx,
        command = command,
        sources = sources,
        outputs = [output],
        work_dir = ctx.actions.declare_directory(ctx.label.name + "-work"),
        arguments = [output.path],
        mnemonic = "RustPlugin",
    )
    return [DefaultInfo(files = depset([output]))]

_rust_plugin = rule(
    implementation = _rust_plugin_impl,
    attrs = CARGO_ACTION_ATTRS | {
        "library": attr.string(mandatory = True),
        "srcs": attr.label_list(allow_files = True),
        "sdk": attr.label(default = Label("//rust:extension_sdk_sources")),
        "contract": attr.label(allow_single_file = True),
        "_boundary": attr.label(default = Label("//cmake:rust_plugin_boundary_check.py"), allow_single_file = True),
    },
    toolchains = CARGO_TOOLCHAINS,
    fragments = ["cpp", "apple"],
)

def rust_plugin(name, library, manifest = "plugin.toml", server_dev = False):
    srcs = ["Cargo.toml", "Cargo.lock"] + native.glob(["src/**/*.rs"]) + native.glob(["build.rs"], allow_empty = True)
    contract = None
    if server_dev:
        contract = ":" + name + "_contract"
        host_contract(name = name + "_contract", output_name = "server_dev_contract.rs", rust = True, target_compatible_with = _LINUX)
    _rust_plugin(
        name = name + "_unchecked",
        library = library,
        srcs = srcs,
        contract = contract,
        target_compatible_with = _LINUX,
    )
    audit_plugin(
        name = name,
        binary = ":" + name + "_unchecked",
        manifest = manifest,
        output_name = "lib" + library + ".so",
        profile = "server-dev" if server_dev else "public",
        srcs = srcs,
        visibility = ["//visibility:public"],
        target_compatible_with = _LINUX,
    )
    plugin_package(name, ":" + name, manifest)
