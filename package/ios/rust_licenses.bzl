"""Collect Rust source notices for the iOS framework package."""

_LicenseFiles = provider(fields = ["files"])
_DEPENDENCIES = ["deps", "proc_macro_deps", "link_deps"]

def _license_aspect_impl(target, ctx):
    # crate_universe exposes extracted package sources as compile_data for
    # Rust libraries and data for build scripts. Read those declared inputs
    # only when packaging; do not add notices to the libraries' runtime data.
    notices = []
    for attribute in ["compile_data", "data", "srcs", "hdrs"]:
        for file in getattr(ctx.rule.files, attribute, []):
            if file.owner.workspace_name and any([
                file.basename.upper().startswith(prefix)
                for prefix in ["LICENSE", "COPYING", "COPYRIGHT", "NOTICE"]
            ]):
                notices.append(file)
    files = [depset(notices)]
    for attribute in _DEPENDENCIES:
        for dependency in getattr(ctx.rule.attr, attribute, []):
            if _LicenseFiles in dependency:
                files.append(dependency[_LicenseFiles].files)
    return [_LicenseFiles(files = depset(transitive = files))]

_license_aspect = aspect(
    implementation = _license_aspect_impl,
    attr_aspects = _DEPENDENCIES,
)

def _file_path(file):
    return file.path

def _rust_licenses_impl(ctx):
    files = depset(transitive = [dependency[_LicenseFiles].files for dependency in ctx.attr.deps])
    notices = []
    for file in sorted(files.to_list(), key = _file_path):
        repository = file.owner.workspace_name
        relative = file.short_path.removeprefix("../" + repository + "/")
        notices.append({
            "source": file.path,
            "destination": "rust/" + repository.rsplit("__", 1)[-1] + "/" + relative,
        })
    ctx.actions.write(ctx.outputs.manifest, json.encode(notices) + "\n")
    return [
        DefaultInfo(files = depset([ctx.outputs.manifest])),
        OutputGroupInfo(license_files = files),
    ]

rust_licenses = rule(
    implementation = _rust_licenses_impl,
    attrs = {
        "deps": attr.label_list(aspects = [_license_aspect]),
        "manifest": attr.output(mandatory = True),
    },
)
