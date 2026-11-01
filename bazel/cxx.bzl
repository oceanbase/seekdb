"""Generate CXX bindings and configuration before compiling either language."""

load("//bazel:defs.bzl", "cc_library")

def _cxx_generate_impl(ctx):
    arguments = ["--header"] if not ctx.file.src else [ctx.file.src.path]
    for cfg in ctx.attr.cfgs:
        arguments += ["--cfg", cfg]
    arguments += ["-o", ctx.outputs.header.path]
    outputs = [ctx.outputs.header]
    if ctx.outputs.source:
        arguments += ["-o", ctx.outputs.source.path]
        outputs.append(ctx.outputs.source)
    ctx.actions.run(
        executable = ctx.executable._tool,
        arguments = arguments,
        inputs = [ctx.file.src] if ctx.file.src else [],
        outputs = outputs,
        mnemonic = "CxxBridge",
        progress_message = "Generating " + ctx.outputs.header.short_path,
    )
    return [DefaultInfo(files = depset(outputs))]

cxx_generate = rule(
    implementation = _cxx_generate_impl,
    attrs = {
        "src": attr.label(allow_single_file = True),
        "cfgs": attr.string_list(),
        "header": attr.output(mandatory = True),
        "source": attr.output(),
        "_tool": attr.label(default = "//rust:cxxbridge", executable = True, cfg = "exec"),
    },
)

def cxx_bridge(name, src, hdrs = [], deps = [], cfgs = []):
    """Expose <name>.h and compile its bridge independently of the Rust library."""
    cxx_generate(
        name = "_" + name + "_generate",
        src = src,
        cfgs = cfgs,
        header = name + ".h",
        source = name + ".cc",
    )
    cc_library(
        name = name + "_headers",
        hdrs = hdrs + [name + ".h"],
        deps = [":cxx_headers"] + deps,
        visibility = ["//visibility:public"],
    )
    cc_library(
        name = name + "_bridge",
        srcs = [name + ".cc"],
        deps = [":" + name + "_headers"],
    )

def _config_generate_impl(ctx):
    ctx.actions.run(
        executable = ctx.file._python,
        arguments = [
            ctx.file.script.path,
            "--parameters", ctx.file.parameters.path,
            "--internal-state", ctx.file.internal_state.path,
            "--default-log-level", ctx.attr.default_log_level,
            "--rust-output", ctx.outputs.rust.path,
            "--checkers-output", ctx.outputs.checkers.path,
        ],
        inputs = [ctx.file.script, ctx.file.parameters, ctx.file.internal_state] + ctx.files._yaml,
        outputs = [ctx.outputs.rust, ctx.outputs.checkers],
        env = {"PYTHONPATH": ctx.file._yaml_init.dirname + "/..", "PYTHONDONTWRITEBYTECODE": "1"},
        mnemonic = "ConfigGenerate",
        progress_message = "Generating configuration interfaces",
    )
    return [DefaultInfo(files = depset([ctx.outputs.rust, ctx.outputs.checkers]))]

config_generate = rule(
    implementation = _config_generate_impl,
    attrs = {
        "script": attr.label(allow_single_file = True, mandatory = True),
        "parameters": attr.label(allow_single_file = True, mandatory = True),
        "internal_state": attr.label(allow_single_file = True, mandatory = True),
        "default_log_level": attr.string(mandatory = True),
        "rust": attr.output(mandatory = True),
        "checkers": attr.output(mandatory = True),
        "_python": attr.label(default = "@host_tools//:python", allow_single_file = True, cfg = "exec"),
        "_yaml": attr.label(default = "@pyyaml//:files"),
        "_yaml_init": attr.label(default = "@pyyaml//:lib/yaml/__init__.py", allow_single_file = True),
    },
)
