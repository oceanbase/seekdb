"""Generates seekdb's version source from Bazel workspace status."""

def _version_source_impl(ctx):
    ctx.actions.run(
        executable = ctx.file._python,
        inputs = [ctx.file._codegen, ctx.file.template, ctx.info_file],
        outputs = [ctx.outputs.out],
        arguments = [
            ctx.file._codegen.path,
            "version",
            ctx.file.template.path,
            ctx.info_file.path,
            ctx.outputs.out.path,
        ],
        mnemonic = "SeekdbVersionSource",
    )
    return [DefaultInfo(files = depset([ctx.outputs.out]))]

version_source = rule(
    implementation = _version_source_impl,
    attrs = {
        "_python": attr.label(default = "@host_tools//:python", allow_single_file = True, cfg = "exec"),
        "_codegen": attr.label(default = "//bazel:build_metadata.py", allow_single_file = True, cfg = "exec"),
        "out": attr.output(mandatory = True),
        "template": attr.label(
            allow_single_file = True,
            mandatory = True,
        ),
    },
)
