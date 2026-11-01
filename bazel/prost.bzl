"""Generate Rust protobuf bindings with declared host tools and locked dependencies."""

load("//bazel:cargo.bzl", "CARGO_ACTION_ATTRS", "CARGO_TOOLCHAINS", "cargo_action")
load("//bazel:platforms.bzl", "target_platform")

def _prost_codegen_tool_impl(ctx):
    suffix = ".exe" if target_platform(ctx).os == "windows" else ""
    tool = ctx.actions.declare_file("prost-codegen/bin/prost-codegen" + suffix)
    cargo_action(
        ctx,
        command = ["${CARGO}", "install", "--offline", "--locked", "--debug",
                   "--config", "profile.dev.debug=0", "--path", "${CARGO_WORKSPACE}/bazel/prost-codegen",
                   "--root", tool.dirname + "/.."],
        sources = ctx.files.srcs,
        outputs = [tool],
        work_dir = ctx.actions.declare_directory("prost-codegen-work"),
        mnemonic = "ProstCodegenTool",
        progress_message = "Building the host protobuf generator",
    )
    return [DefaultInfo(executable = tool)]

prost_codegen_tool = rule(
    implementation = _prost_codegen_tool_impl,
    attrs = CARGO_ACTION_ATTRS | {"srcs": attr.label_list(allow_files = True)},
    fragments = ["apple", "cpp"],
    toolchains = CARGO_TOOLCHAINS,
    executable = True,
)

def _prost_generate_impl(ctx):
    arguments = [ctx.file.src.path, ctx.outputs.out.path]
    if not ctx.attr.services:
        arguments.append("--messages-only")
    for field in ctx.attr.raw_strings:
        arguments += ["--raw-string", field]
    ctx.actions.run(
        executable = ctx.executable._generator,
        arguments = arguments,
        inputs = [ctx.file.src],
        outputs = [ctx.outputs.out],
        mnemonic = "ProstGenerate",
        progress_message = "Generating " + ctx.outputs.out.short_path,
    )
    return [DefaultInfo(files = depset([ctx.outputs.out]))]

prost_generate = rule(
    implementation = _prost_generate_impl,
    attrs = {
        "src": attr.label(allow_single_file = [".proto"], mandatory = True),
        "out": attr.output(mandatory = True),
        "services": attr.bool(default = True),
        "raw_strings": attr.string_list(),
        "_generator": attr.label(default = "//bazel:prost_codegen", executable = True, cfg = "exec"),
    },
)
