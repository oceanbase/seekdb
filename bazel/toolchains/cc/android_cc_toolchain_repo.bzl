"""Configure the installed Android NDK for arm64."""

load(":repository_utils.bzl", "host_substitutions")

# Keep the compiler/linker target aligned with seekdb-deps' API 26 profile.
_ANDROID_API_LEVEL = 26
_ANDROID_TARGET = "aarch64-linux-android{}".format(_ANDROID_API_LEVEL)

def _android_cc_toolchain_repository_impl(ctx):
    # Native Windows runners may have an NDK installed, but this toolchain
    # uses Unix compiler paths and only supports Linux/macOS build hosts.
    if ctx.os.name not in ["linux", "mac os x"]:
        ctx.file("BUILD.bazel", "# Android cross-compilation requires a Linux or macOS host.\n")
        return
    ndk = ctx.getenv("ANDROID_NDK_HOME")
    if not ndk:
        ctx.file("BUILD.bazel", "# Set ANDROID_NDK_HOME to enable Android builds.\n")
        return
    host = "darwin-x86_64" if ctx.os.name == "mac os x" else "linux-x86_64"
    toolchain = ctx.path(ndk + "/toolchains/llvm/prebuilt/" + host)
    compiler = toolchain.get_child("bin").get_child(_ANDROID_TARGET + "-clang")
    if not compiler.exists:
        fail("Android NDK compiler is missing: %s" % compiler)
    ctx.symlink(toolchain, "ndk")
    resource = ctx.execute([str(compiler), "-print-resource-dir"])
    if resource.return_code:
        fail("Cannot run Android NDK Clang: %s" % resource.stderr)
    version = resource.stdout.strip().split("/")[-1]
    ctx.template("BUILD.bazel", Label(":BUILD.android_cc_toolchain.tpl"), substitutions = host_substitutions(ctx) | {
        "@ANDROID_API_LEVEL@": str(_ANDROID_API_LEVEL),
        "@ANDROID_TARGET@": _ANDROID_TARGET,
        "@CLANG_VERSION@": version,
        "@REPOSITORY_ROOT@": "external/" + ctx.name,
    })

android_cc_toolchain_repository = repository_rule(
    implementation = _android_cc_toolchain_repository_impl,
    configure = True,
    local = True,
)
