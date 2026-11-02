"""Generate Rust protobuf bindings with declared host tools and locked dependencies."""

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
        "_generator": attr.label(default = "//bazel/prost-codegen:prost_codegen", executable = True, cfg = "exec"),
    },
)
