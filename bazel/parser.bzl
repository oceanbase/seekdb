def _require_parser_dependency(path, command):
    if not path.exists:
        fail("Missing seekdb parser dependency: %s. Run `%s` before building." % (path, command))

def _host_tools_repository_impl(ctx):
    windows = ctx.os.name.startswith("windows")
    suffix = ".exe" if windows else ""
    sdk = ctx.workspace_root.get_child("deps", "3rd")
    if windows:
        sdk = sdk.get_child("tools", "win_flex_bison")
        for dependency in ["win_bison.exe", "win_flex.exe", "data"]:
            _require_parser_dependency(sdk.get_child(dependency), "python build.py init")
        ctx.symlink(sdk.get_child("win_bison.exe"), "bison.exe")
        ctx.symlink(sdk.get_child("win_flex.exe"), "flex.exe")
        ctx.symlink(sdk.get_child("data"), "data")
        for path in sdk.readdir():
            if path.basename.lower().endswith(".dll"):
                ctx.symlink(path, path.basename)
    else:
        sdk = sdk.get_child("usr", "local", "oceanbase", "devtools")
        for dependency in [sdk.get_child("bin", "bison"), sdk.get_child("bin", "flex"), sdk.get_child("share", "bison")]:
            _require_parser_dependency(dependency, "./build.py init")
        ctx.symlink(sdk.get_child("bin", "bison"), "bison")
        ctx.symlink(sdk.get_child("bin", "flex"), "flex")
        ctx.symlink(sdk.get_child("share", "bison"), "data")
        m4 = ctx.which("m4")
        if not m4:
            fail("Install m4 before building the seekdb parsers")
        ctx.symlink(m4, "m4")
    python = ctx.which("python" if windows else "python3")
    if not python:
        fail("Install Python 3 before building seekdb")
    ctx.symlink(python.dirname, "python_runtime")
    ctx.file("BUILD.bazel", '''package(default_visibility = ["//visibility:public"])
exports_files(["bison%s", "flex%s", "python_runtime/%s"])
alias(name = "bison_tool", actual = "bison%s")
alias(name = "flex_tool", actual = "flex%s")
alias(name = "python", actual = "python_runtime/%s")
filegroup(name = "runtime", srcs = glob(["data/**"]) + glob(["*.dll"], allow_empty = True))
filegroup(name = "m4_tool", srcs = %r)
''' % (suffix, suffix, python.basename, suffix, suffix, python.basename, [] if windows else ["m4"]))

host_tools_repository = repository_rule(
    implementation = _host_tools_repository_impl,
    local = True,
    configure = True,
    environ = ["PATH"],
)

def _parser_impl(ctx):
    environment = {"BISON_PKGDATADIR": ctx.file._bison.dirname + "/data"}
    if ctx.files._m4:
        environment["M4"] = ctx.files._m4[0].path
    ctx.actions.run(
        executable = ctx.file._bison,
        inputs = [ctx.file.grammar],
        tools = ctx.files._runtime + ctx.files._m4,
        outputs = [ctx.outputs.parser, ctx.outputs.parser_header],
        arguments = [
            "--defines=" + ctx.outputs.parser_header.path,
            "--output=" + ctx.outputs.parser.path,
            ctx.file.grammar.path,
        ],
        env = environment,
        mnemonic = "Bison",
    )
    lexer = ctx.outputs.lexer
    if ctx.attr.lexer_patch:
        lexer = ctx.actions.declare_file(ctx.label.name + "/" + lexer.basename)
    ctx.actions.run(
        executable = ctx.file._flex,
        inputs = [ctx.file.lexer_source],
        tools = ctx.files._runtime,
        outputs = [lexer, ctx.outputs.lexer_header],
        arguments = ctx.attr.flex_options + [
            "--header-file=" + ctx.outputs.lexer_header.path,
            "--outfile=" + lexer.path,
            ctx.file.lexer_source.path,
        ],
        mnemonic = "Flex",
    )
    if ctx.attr.lexer_patch:
        ctx.actions.run(
            executable = ctx.file._python,
            inputs = [ctx.file._codegen, lexer],
            outputs = [ctx.outputs.lexer],
            arguments = [ctx.file._codegen.path, ctx.attr.lexer_patch, lexer.path, ctx.outputs.lexer.path],
            mnemonic = "ParserLexerPatch",
        )
    return [DefaultInfo(files = depset([
        ctx.outputs.parser,
        ctx.outputs.parser_header,
        ctx.outputs.lexer,
        ctx.outputs.lexer_header,
    ]))]

parser = rule(
    implementation = _parser_impl,
    attrs = {
        "_bison": attr.label(default = "@host_tools//:bison_tool", allow_single_file = True, cfg = "exec"),
        "_flex": attr.label(default = "@host_tools//:flex_tool", allow_single_file = True, cfg = "exec"),
        "_python": attr.label(default = "@host_tools//:python", allow_single_file = True, cfg = "exec"),
        "_m4": attr.label(default = "@host_tools//:m4_tool", cfg = "exec"),
        "_runtime": attr.label(default = "@host_tools//:runtime", cfg = "exec"),
        "_codegen": attr.label(default = "//bazel:parser_codegen.py", allow_single_file = True, cfg = "exec"),
        "grammar": attr.label(allow_single_file = [".y"], mandatory = True),
        "lexer_source": attr.label(allow_single_file = [".l"], mandatory = True),
        "parser": attr.output(mandatory = True),
        "parser_header": attr.output(mandatory = True),
        "lexer": attr.output(mandatory = True),
        "lexer_header": attr.output(mandatory = True),
        "flex_options": attr.string_list(),
        "lexer_patch": attr.string(values = ["", "fts", "sql"]),
    },
)

def _type_names_impl(ctx):
    ctx.actions.run(
        executable = ctx.file._python,
        inputs = [ctx.file._codegen, ctx.file.src],
        outputs = [ctx.outputs.out],
        arguments = [ctx.file._codegen.path, "type_names", ctx.file.src.path, ctx.outputs.out.path],
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

