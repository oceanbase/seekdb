def _source_impl(ctx):
    manifest = json.decode(ctx.read(ctx.attr.manifest))
    source = manifest["sources"][ctx.attr.package]
    ctx.download_and_extract(
        sha256 = source["sha256"],
        stripPrefix = source["source_directory"],
        type = "tar.xz" if source["url"].endswith(".xz") else ("tar.bz2" if source["url"].endswith(".bz2") else "tar.gz"),
        url = source["url"],
    )
    ctx.watch(ctx.path(ctx.attr.prepare))
    result = ctx.execute([
        "python3",
        str(ctx.path(ctx.attr.prepare)),
        str(ctx.path(ctx.attr.manifest)),
        ctx.attr.package,
        ctx.attr.profile,
    ])
    if result.return_code:
        fail(result.stderr)
    ctx.delete("BUILD.bazel")
    ctx.delete("BUILD")
    ctx.symlink(ctx.attr.build_file, "BUILD.bazel")

_source = repository_rule(
    attrs = {
        "package": attr.string(mandatory = True),
        "profile": attr.string(),
        "manifest": attr.label(default = Label("//bazel/sdk_sources:dependencies.json")),
        "prepare": attr.label(default = Label("//bazel/sdk_sources:prepare.py")),
        "build_file": attr.label(mandatory = True),
    },
    implementation = _source_impl,
)

def _sdk_sources_impl(ctx):
    manifest = json.decode(ctx.read(Label("//bazel/sdk_sources:dependencies.json")))
    for name in manifest["sources"]:
        profiles = ["ios", "wasm"] if name in ["s2", "vsag"] else [""]
        for profile in profiles:
            _source(
                name = "sdk_" + name + ("_" + profile if profile else ""),
                build_file = Label("//bazel/sdk_sources:%s.BUILD" % name),
                package = name,
                profile = profile,
            )

sdk_sources = module_extension(implementation = _sdk_sources_impl)
