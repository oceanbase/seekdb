"""Toolchain-backed rules for seekdb relocatable object linking."""

load(
    "@rules_cc//cc:find_cc_toolchain.bzl",
    "CC_TOOLCHAIN_ATTRS",
    "find_cpp_toolchain",
    "use_cc_toolchain",
)
load("@rules_cc//cc/common:cc_info.bzl", "CcInfo")
load("//bazel:platforms.bzl", "PLATFORM_ATTRS", "target_platform")

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

def _localized_partial_link_impl(ctx):
    cc_toolchain = find_cpp_toolchain(ctx)
    objects = []
    for dep in ctx.attr.deps:
        objects.extend(_direct_pic_objects(dep))
    if not objects:
        fail("%s received no direct C++ objects" % ctx.label)

    platform = target_platform(ctx)
    is_apple = platform.apple
    output = ctx.outputs.out
    # Apple ld -r already localizes hidden (private extern) symbols.
    temporary = output if is_apple else ctx.actions.declare_file(ctx.label.name + ".tmp.o")

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

    if is_apple:
        return [DefaultInfo(files = depset([output]))]

    objcopy_args = ctx.actions.args()
    objcopy_args.add("--localize-hidden")
    objcopy_args.add(temporary)
    objcopy_args.add(output)
    ctx.actions.run(
        executable = cc_toolchain.objcopy_executable,
        arguments = [objcopy_args],
        inputs = depset(
            direct = [temporary],
            transitive = [cc_toolchain.all_files],
        ),
        outputs = [output],
        mnemonic = "SeekdbLocalizeHidden",
        progress_message = "Localizing hidden symbols in %{output}",
    )

    return [DefaultInfo(files = depset([output]))]

localized_partial_link = rule(
    implementation = _localized_partial_link_impl,
    attrs = {
        "deps": attr.label_list(
            mandatory = True,
            providers = [CcInfo],
        ),
        "out": attr.output(mandatory = True),
    } | CC_TOOLCHAIN_ATTRS | PLATFORM_ATTRS,
    fragments = ["cpp"],
    toolchains = use_cc_toolchain(),
)
