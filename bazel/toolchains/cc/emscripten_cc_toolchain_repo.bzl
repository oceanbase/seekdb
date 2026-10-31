"""Expose an installed Emscripten SDK as a C++ toolchain."""

load(":repository_utils.bzl", "host_substitutions")

def _emscripten_cc_toolchain_repository_impl(ctx):
    emscripten = ctx.getenv("EMSCRIPTEN")
    if not emscripten:
        ctx.file("BUILD.bazel", "# Set EMSCRIPTEN to the directory containing emcc.\n")
        return
    root = ctx.path(emscripten).dirname
    ctx.symlink(root, "sdk")
    prefix = ctx.getenv("SEEKDB_WASM_DEPS") or str(ctx.workspace_root) + "/deps/wasm"
    cache = ctx.path(prefix + "/em-cache")
    # A native build may discover this toolchain before the Wasm SDK has
    # been prepared. Only Wasm compile/link actions require the frozen cache.
    ctx.watch(cache)
    ctx.symlink(cache, "cache")
    resource = ctx.execute([str(root.get_child("bin").get_child("clang")), "-print-resource-dir"])
    if resource.return_code:
        fail("Cannot inspect Emscripten Clang: " + resource.stderr)
    ctx.symlink(resource.stdout.strip() + "/include", "clang_builtin_include")
    includes = ["%crosstool_top%/" + path for path in [
        "cache/sysroot/include/c++/v1", "clang_builtin_include",
        "cache/sysroot/include", "cache/sysroot/include/compat",
    ]] + [str(cache) + "/sysroot/include", resource.stdout.strip() + "/include"]
    ctx.file("emcc.sh", """#!/bin/sh
export EM_FROZEN_CACHE=1
export EM_CACHE="$(cd "$(dirname "$0")" && pwd)/cache"
if [ ! -d "$EM_CACHE/sysroot" ]; then
  echo "Missing Emscripten sysroot; run ./build.py init --wasm" >&2
  exit 1
fi
exec "$(dirname "$0")/sdk/emscripten/emcc" --sysroot="$(dirname "$0")/cache/sysroot" "$@"
""")
    ctx.template("BUILD.bazel", Label(":BUILD.emscripten_cc_toolchain.tpl"), substitutions = host_substitutions(ctx) | {
        "@INCLUDES@": repr(includes),
    })

emscripten_cc_toolchain_repository = repository_rule(
    implementation = _emscripten_cc_toolchain_repository_impl,
    configure = True,
    local = True,
)
