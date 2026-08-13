"""Toolchain-backed rules for seekdb relocatable object linking."""

load(
    "@rules_cc//cc:find_cc_toolchain.bzl",
    "CC_TOOLCHAIN_ATTRS",
    "find_cpp_toolchain",
    "use_cc_toolchain",
)
load("@rules_cc//cc/common:cc_info.bzl", "CcInfo")

def _target_is_macos(ctx):
    return ctx.target_platform_has_constraint(
        ctx.attr._macos_constraint[platform_common.ConstraintValueInfo],
    )

def _direct_pic_objects(target):
    objects = []
    for linker_input in target[CcInfo].linking_context.linker_inputs.to_list():
        if linker_input.owner != target.label:
            continue
        for library in linker_input.libraries:
            if library.pic_objects:
                objects.extend(library.pic_objects)
            elif library.objects:
                objects.extend(library.objects)
    return objects

def _seekdb_localized_partial_link_impl(ctx):
    cc_toolchain = find_cpp_toolchain(ctx)
    objects = []
    for dep in ctx.attr.deps:
        objects.extend(_direct_pic_objects(dep))
    if not objects:
        fail("%s received no direct C++ objects" % ctx.label)

    temporary = ctx.actions.declare_file(ctx.label.name + ".tmp.o")
    output = ctx.outputs.out

    link_args = ctx.actions.args()
    link_args.add("-r")
    link_args.add_all(objects)
    link_args.add("-o")
    link_args.add(temporary)
    ctx.actions.run(
        executable = cc_toolchain.ld_executable,
        arguments = [link_args],
        inputs = depset(
            direct = objects,
            transitive = [cc_toolchain.all_files],
        ),
        outputs = [temporary],
        mnemonic = "SeekdbPartialLink",
        progress_message = "Partially linking %{output}",
    )

    objcopy_args = ctx.actions.args()
    objcopy_args.add("--localize-hidden")
    objcopy_args.add(temporary)
    objcopy_args.add(output)
    macos_objcopy = ctx.attr._macos_objcopy[DefaultInfo].files_to_run
    ctx.actions.run(
        executable = (
            macos_objcopy
            if _target_is_macos(ctx)
            else cc_toolchain.objcopy_executable
        ),
        arguments = [objcopy_args],
        inputs = depset(
            direct = [temporary],
            transitive = [cc_toolchain.all_files],
        ),
        outputs = [output],
        tools = [macos_objcopy] if _target_is_macos(ctx) else [],
        mnemonic = "SeekdbLocalizeHidden",
        progress_message = "Localizing hidden symbols in %{output}",
    )

    return [DefaultInfo(files = depset([output]))]

seekdb_localized_partial_link = rule(
    implementation = _seekdb_localized_partial_link_impl,
    attrs = {
        "deps": attr.label_list(
            mandatory = True,
            providers = [CcInfo],
        ),
        "out": attr.output(mandatory = True),
        "_macos_constraint": attr.label(
            default = Label("@platforms//os:macos"),
        ),
        "_macos_objcopy": attr.label(
            allow_files = True,
            default = Label("@seekdb_3rd_headers//:devtools/bin/llvm-objcopy"),
            cfg = "exec",
            executable = True,
        ),
    } | CC_TOOLCHAIN_ATTRS,
    fragments = ["cpp"],
    toolchains = use_cc_toolchain(),
)
