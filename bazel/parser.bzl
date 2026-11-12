def _host_tools_repository_impl(ctx):
    windows = ctx.os.name.startswith("windows")
    python = ctx.which("python" if windows else "python3")
    if not python:
        fail("Install Python 3 before building seekdb")
    ctx.symlink(python.dirname, "python_runtime")
    ctx.file("BUILD.bazel", '''package(default_visibility = ["//visibility:public"])
exports_files(["python_runtime/%s"])
alias(name = "python", actual = "python_runtime/%s")
''' % (python.basename, python.basename))

host_tools_repository = repository_rule(
    implementation = _host_tools_repository_impl,
    local = True,
    configure = True,
    environ = ["PATH"],
)

def _type_names_impl(ctx):
    ctx.actions.run(
        executable = ctx.file._python,
        inputs = [ctx.file._codegen, ctx.file.src],
        outputs = [ctx.outputs.out],
        arguments = [ctx.file._codegen.path, ctx.file.src.path, ctx.outputs.out.path],
        mnemonic = "ParserTypeNames",
    )

parser_type_names = rule(
    implementation = _type_names_impl,
    attrs = {
        "src": attr.label(allow_single_file = True, mandatory = True),
        "out": attr.output(mandatory = True),
        "_python": attr.label(default = "@host_tools//:python", allow_single_file = True, cfg = "exec"),
        "_codegen": attr.label(default = "//bazel:parser_codegen.py", allow_single_file = True, cfg = "exec"),
    },
)
