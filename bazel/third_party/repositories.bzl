"""Expose seekdb's bootstrapped third-party SDK as a Bazel repository."""

def _windows_sdk(repository_ctx):
    sdk = str(repository_ctx.workspace_root) + "/deps/3rd"
    headers = {}
    for directory in ["vcpkg/x64-windows/include", "openssl/include", "vsag/include"]:
        for child in repository_ctx.path(sdk + "/" + directory).readdir():
            headers[child.basename] = child
    for name, path in headers.items():
        if name != "dirent.h":
            repository_ctx.symlink(path, "include/" + name)
    # Patch a repository-owned copy, never mutate the shared installed SDK.
    python = repository_ctx.which("python")
    if not python or "dirent.h" not in headers:
        fail("Windows SDK requires Python and its pinned dirent.h")
    repository_ctx.read(repository_ctx.attr.windows_dirent_patch)
    repository_ctx.read(headers["dirent.h"])
    result = repository_ctx.execute([
        python,
        repository_ctx.path(repository_ctx.attr.windows_dirent_patch),
        headers["dirent.h"],
        repository_ctx.path("include/dirent.h"),
    ], quiet = True)
    if result.return_code:
        fail("Cannot prepare Windows dirent header: " + result.stderr)
    for source, destination in {
        "vcpkg/x64-windows/lib": "lib",
        "vcpkg/x64-windows/bin": "bin",
        "vcpkg/x64-windows/tools/protobuf": "protoc",
        "vcpkg/x64-windows/tools/grpc": "grpc_tools",
        "openssl/lib/VC/x64/MD": "openssl",
        "openssl/bin": "openssl_bin",
        "vsag/lib": "vsag",
        "vsag/bin": "vsag_bin",
        "tools/llvm18": "devtools",
    }.items():
        repository_ctx.symlink(sdk + "/" + source, destination)
    redist = repository_ctx.getenv("VCToolsRedistDir")
    if not redist:
        fail("Run the Windows build from a Visual Studio Developer Command Prompt (VCToolsRedistDir is required)")
    for directory in repository_ctx.path(redist).get_child("x64").readdir():
        if directory.basename.startswith("Microsoft.VC") and directory.basename.endswith(".CRT"):
            repository_ctx.symlink(directory, "msvc_runtime")
    repository_ctx.file("BUILD.bazel", '''load("@seekdb//bazel/third_party:targets.bzl", "sdk_targets")
package(default_visibility = ["//visibility:public"])
sdk_targets(windows = True)
''', executable = False)

def _third_party_headers_repository_impl(repository_ctx):
    if repository_ctx.attr.platform == "native" and repository_ctx.os.name.startswith("windows"):
        _windows_sdk(repository_ctx)
        return
    sdk = str(repository_ctx.workspace_root) + "/deps/3rd/usr/local/oceanbase"
    headers = sdk + "/deps/devel/include"
    libraries = sdk + "/deps/devel/lib"
    libraries64 = sdk + "/deps/devel/lib64"
    platform = repository_ctx.attr.platform
    ios_sdk = platform if platform in ["iphoneos", "iphonesimulator"] else ""
    if platform == "android":
        prefix = str(repository_ctx.workspace_root) + "/deps/android/usr/local/oceanbase/deps/devel"
        headers = prefix + "/include"
        libraries = prefix + "/lib"
        libraries64 = prefix + "/lib64"
    if platform == "wasm":
        prefix = repository_ctx.getenv("SEEKDB_WASM_DEPS") or str(repository_ctx.workspace_root) + "/deps/wasm"
        headers = prefix + "/include"
        libraries = prefix + "/lib"
        libraries64 = libraries
    source_headers = [
        "absl", "s2", "roaring", "vsag", "fmt", "cpuinfo.h", "seekdb-vsag",
        "omp.h", "ompx.h", "omp-tools.h", "ompt.h", "lapack.h", "lapacke.h",
        "lapacke_config.h", "lapacke_mangling.h", "lapacke_utils.h", "cblas.h",
    ]
    if ios_sdk:
        prefix = str(repository_ctx.workspace_root) + "/deps/ios/" + ios_sdk + "/devel"
        if not repository_ctx.path(prefix + "/lib/libssl.a").exists:
            fail("Missing iOS dependencies; run ./build.py init --ios" + (" --simulator" if ios_sdk == "iphonesimulator" else ""))
        target_headers = repository_ctx.path(prefix + "/include")
        target_names = [child.basename for child in target_headers.readdir()]
        for child in repository_ctx.path(headers).readdir():
            if child.basename not in target_names and child.basename not in source_headers + ["boost"]:
                repository_ctx.symlink(child, "include/" + child.basename)
        for child in target_headers.readdir():
            if child.basename not in source_headers + ["boost"]:
                repository_ctx.symlink(child, "include/" + child.basename)
        repository_ctx.symlink(prefix + "/verified", "verified")
        repository_ctx.symlink(prefix + "/licenses", "licenses")
        libraries = prefix + "/lib"
        libraries64 = libraries
    elif platform == "wasm":
        for child in repository_ctx.path(headers).readdir():
            if child.basename not in source_headers:
                repository_ctx.symlink(child, "include/" + child.basename)
    else:
        repository_ctx.symlink(headers, "include")
    repository_ctx.symlink(sdk + "/devtools", "devtools")
    repository_ctx.symlink(libraries, "lib")
    repository_ctx.symlink(libraries64, "lib64")
    repository_ctx.file("BUILD.bazel", '''load("@seekdb//bazel/third_party:targets.bzl", "sdk_targets")
package(default_visibility = ["//visibility:public"])
sdk_targets()
''', executable = False)

third_party_headers_repository = repository_rule(
    attrs = {
        "windows_dirent_patch": attr.label(
            default = Label("//bazel/third_party:patch_windows_dirent.py"),
            allow_single_file = True,
        ),
        "platform": attr.string(
            default = "native",
            values = [
                "native",
                "android",
                "iphoneos",
                "iphonesimulator",
                "wasm",
            ],
        ),
    },
    local = True,
    implementation = _third_party_headers_repository_impl,
)
