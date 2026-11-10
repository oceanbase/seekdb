def _openmp_headers_impl(ctx):
    substitutions = {
        "@LIBOMP_VERSION_MAJOR@": "5",
        "@LIBOMP_VERSION_MINOR@": "0",
        "@LIBOMP_VERSION_BUILD@": "20140926",
        "@LIBOMP_BUILD_DATE@": "No_Timestamp",
    }
    for template, output in [
        (ctx.file.omp_template, ctx.outputs.omp),
        (ctx.file.ompx_template, ctx.outputs.ompx),
        (ctx.file.config_template, ctx.outputs.config),
    ]:
        ctx.actions.expand_template(
            template = template,
            output = output,
            substitutions = substitutions,
        )
    return [DefaultInfo(files = depset([ctx.outputs.omp, ctx.outputs.ompx, ctx.outputs.config]))]

openmp_headers = rule(
    implementation = _openmp_headers_impl,
    attrs = {
        "omp_template": attr.label(allow_single_file = True),
        "ompx_template": attr.label(allow_single_file = True),
        "config_template": attr.label(
            default = Label("//bazel/sdk_sources:openmp/kmp_config.h"),
            allow_single_file = True,
        ),
    },
    outputs = {
        "omp": "generated/omp.h",
        "ompx": "generated/ompx.h",
        "config": "generated/kmp_config.h",
    },
)
