"""Configure the installed Android NDK for arm64."""

load(":repository_utils.bzl", "host_substitutions")

_ANDROID_API_LEVEL = 28
_ANDROID_TARGET = "aarch64-linux-android{}".format(_ANDROID_API_LEVEL)

def _android_cc_toolchain_repository_impl(ctx):
    ndk = ctx.getenv("ANDROID_NDK_HOME")
    if not ndk:
        ctx.file("BUILD.bazel", "# Set ANDROID_NDK_HOME to enable Android builds.\n")
        return
    # The NDK ships one prebuilt toolchain per host OS. This repository is
    # registered unconditionally, so a host without a prebuilt (e.g. Windows)
    # must produce an empty repository instead of failing on fetch.
    host = {
        "linux": "linux-x86_64",
        "mac os x": "darwin-x86_64",
    }.get(ctx.os.name)
    if host == None:
        ctx.file(
            "BUILD.bazel",
            "# Android builds run only on Linux and macOS hosts; this host has no NDK prebuilt.\n",
        )
        return
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
