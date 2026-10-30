"""Fetch the CXX generator matching the workspace lockfile."""

def _cxxbridge_repository_impl(ctx):
    # Cargo.lock entries have a fixed, generated format. Derive both the version
    # and checksum here so upgrading cxx also upgrades its code generator.
    packages = {}
    for entry in ctx.read(ctx.attr.lockfile).split("[[package]]")[1:]:
        fields = {}
        for line in entry.splitlines():
            if line.startswith("name = ") or line.startswith("version = ") or line.startswith("checksum = "):
                key, value = line.split(" = ", 1)
                fields[key] = json.decode(value)
        if fields.get("name") in ["cxx", "cxxbridge-cmd"]:
            if fields["name"] in packages:
                fail("The CXX runtime and generator must each have a single locked version")
            packages[fields["name"]] = fields
    runtime = packages["cxx"]
    generator = packages["cxxbridge-cmd"]
    if runtime["version"] != generator["version"]:
        fail("cxx and cxxbridge-cmd must have the same version in Cargo.lock")
    version = generator["version"]
    ctx.download_and_extract(
        url = "https://static.crates.io/crates/cxxbridge-cmd/cxxbridge-cmd-%s.crate" % version,
        sha256 = generator["checksum"],
        type = "tar.gz",
        stripPrefix = "cxxbridge-cmd-" + version,
    )
    ctx.file("BUILD.bazel", ctx.read(ctx.attr.build_file).replace("{version}", version))

cxxbridge_repository = repository_rule(
    implementation = _cxxbridge_repository_impl,
    attrs = {
        "lockfile": attr.label(default = Label("//:Cargo.lock")),
        "build_file": attr.label(default = Label("//bazel:cxxbridge.BUILD.bazel")),
    },
)
