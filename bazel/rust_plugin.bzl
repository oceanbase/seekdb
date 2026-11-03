"""Build independent Rust cdylibs and version-bound server-dev contracts."""

load("@rules_rust//rust:defs.bzl", "rust_shared_library")
load("//bazel:plugin.bzl", "audit_plugin", "plugin_package")

_LINUX = select({"@platforms//os:linux": [], "//conditions:default": ["@platforms//:incompatible"]})

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

def rust_plugin(name, library, manifest = "plugin.toml", server_dev = False):
    srcs = ["Cargo.toml", "Cargo.lock"] + native.glob(["src/**/*.rs"]) + native.glob(["build.rs"], allow_empty = True)
    contract = None
    if server_dev:
        contract = ":" + name + "_contract"
        host_contract(name = name + "_contract", output_name = "server_dev_contract.rs", rust = True, target_compatible_with = _LINUX)
    rust_shared_library(
        name = name + "_unchecked",
        crate_name = library,
        crate_root = "src/lib.rs",
        srcs = native.glob(["src/**/*.rs"]),
        edition = "2021",
        deps = ["//rust/extension-sdk:extension"],
        compile_data = [contract] if contract else [],
        rustc_env = {"SEEKDB_SERVER_DEV_CONTRACT": "$(location %s)" % contract} if contract else {},
        rustc_flags = ["-Cpanic=unwind", "-Clink-arg=-Wl,-z,defs"],
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
