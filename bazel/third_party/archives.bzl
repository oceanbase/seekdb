load("@rules_cc//cc/common:cc_common.bzl", "cc_common")
load("@rules_cc//cc/common:cc_info.bzl", "CcInfo")

def _sdk_archives_impl(ctx):
    info = cc_common.merge_cc_infos(cc_infos = [dep[CcInfo] for dep in ctx.attr.deps])
    archives = []
    for linker_input in info.linking_context.linker_inputs.to_list():
        for library in linker_input.libraries:
            archive = library.pic_static_library or library.static_library
            if archive:
                archives.append(archive)
    return [info, DefaultInfo(files = depset(archives))]

sdk_archives = rule(
    attrs = {"deps": attr.label_list(providers = [CcInfo])},
    implementation = _sdk_archives_impl,
)
