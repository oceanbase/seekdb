"""Configure the installed Android NDK for arm64, API 28."""

load(":repository_utils.bzl", "host_substitutions")

def _android_cc_toolchain_repository_impl(ctx):
    ndk = ctx.getenv("ANDROID_NDK_HOME")
    if not ndk:
        ctx.file("BUILD.bazel", "# Set ANDROID_NDK_HOME to enable Android builds.\n")
        return
    host = "darwin-x86_64" if ctx.os.name == "mac os x" else "linux-x86_64"
    toolchain = ctx.path(ndk + "/toolchains/llvm/prebuilt/" + host)
    compiler = toolchain.get_child("bin").get_child("clang")
    if not compiler.exists:
        fail("Android NDK compiler is missing: %s" % compiler)
    ctx.symlink(toolchain, "ndk")
    resource = ctx.execute([str(compiler), "-print-resource-dir"])
    if resource.return_code:
        fail("Cannot run Android NDK Clang: %s" % resource.stderr)
    version = resource.stdout.strip().split("/")[-1]
    ctx.template("BUILD.bazel", Label(":BUILD.android_cc_toolchain.tpl"), substitutions = host_substitutions(ctx) | {
        "@CLANG_VERSION@": version,
        "@REPOSITORY_ROOT@": "external/" + ctx.name,
    })

android_cc_toolchain_repository = repository_rule(
    implementation = _android_cc_toolchain_repository_impl,
    configure = True,
    local = True,
)
