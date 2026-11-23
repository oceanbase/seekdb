"""Expose seekdb's bootstrapped third-party SDK as a Bazel repository."""

def _link_directory_contents(repository_ctx, directories, destination):
    names = {}
    for directory in directories:
        for child in repository_ctx.path(directory).readdir():
            if child.basename not in names:
                repository_ctx.symlink(child, destination + "/" + child.basename)
                names[child.basename] = True

def _windows_sdk(repository_ctx):
    sdk = str(repository_ctx.workspace_root) + "/deps/3rd"
    headers = {}
    for directory in ["vcpkg/x64-windows/include", "openssl/include"]:
        for child in repository_ctx.path(sdk + "/" + directory).readdir():
            headers[child.basename] = child
    locked_sdk = str(repository_ctx.workspace_root) + "/deps/prebuilt/native"
    locked_headers = locked_sdk + "/include"
    if repository_ctx.path(locked_headers).exists:
        for child in repository_ctx.path(locked_headers).readdir():
            headers[child.basename] = child
    for name, path in headers.items():
        # Boost comes from the pinned @sdk_boost source repository on every
        # platform; leaving the installed SDK copy here would mix its version.
        if name not in ("dirent.h", "boost"):
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
        "openssl/lib/VC/x64/MD": "openssl",
        "openssl/bin": "openssl_bin",
        "tools/llvm18": "devtools",
    }.items():
        repository_ctx.symlink(sdk + "/" + source, destination)
    locked_bins = locked_sdk + "/bin"
    if repository_ctx.path(locked_bins).exists:
        _link_directory_contents(repository_ctx, [locked_bins, sdk + "/vcpkg/x64-windows/bin"], "bin")
    else:
        repository_ctx.symlink(sdk + "/vcpkg/x64-windows/bin", "bin")
    locked_libraries = locked_sdk + "/lib"
    if repository_ctx.path(locked_libraries).exists:
        _link_directory_contents(repository_ctx, [locked_libraries, sdk + "/vcpkg/x64-windows/lib"], "lib")
    else:
        repository_ctx.symlink(sdk + "/vcpkg/x64-windows/lib", "lib")
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
    locked_sdk = str(repository_ctx.workspace_root) + "/deps/prebuilt/native"
    if platform == "android":
        locked_sdk = str(repository_ctx.workspace_root) + "/deps/prebuilt/android"
    elif platform == "iphoneos":
        locked_sdk = str(repository_ctx.workspace_root) + "/deps/prebuilt/ios"
    elif platform == "iphonesimulator":
        locked_sdk = str(repository_ctx.workspace_root) + "/deps/prebuilt/ios_simulator"
    elif platform == "wasm":
        locked_sdk = str(repository_ctx.workspace_root) + "/deps/prebuilt/wasm"
    source_headers = [
        "absl", "roaring", "vsag", "fmt", "cpuinfo.h", "seekdb-vsag",
        "omp.h", "ompx.h", "omp-tools.h", "ompt.h", "lapack.h", "lapacke.h",
        "lapacke_config.h", "lapacke_mangling.h", "lapacke_utils.h", "cblas.h",
    ]
    if ios_sdk or platform == "wasm":
        libraries = locked_sdk + "/lib"
        libraries64 = libraries
        if not repository_ctx.path(libraries + "/libssl.a").exists:
            option = ("--ios" + (" --simulator" if ios_sdk == "iphonesimulator" else "")) if ios_sdk else "--wasm"
            fail("Missing target dependencies; run ./build.py init " + option)
        locked_names = []
        for child in repository_ctx.path(locked_sdk + "/include").readdir():
            repository_ctx.symlink(child, "include/" + child.basename)
            locked_names.append(child.basename)
        skipped = source_headers + locked_names + (["boost"] if ios_sdk else [])
        for child in repository_ctx.path(headers).readdir():
            if child.basename not in skipped:
                repository_ctx.symlink(child, "include/" + child.basename)
    else:
        locked_headers = locked_sdk + "/include"
        if repository_ctx.path(locked_headers).exists:
            _link_directory_contents(repository_ctx, [locked_headers, headers], "include")
        else:
            repository_ctx.symlink(headers, "include")
    repository_ctx.symlink(sdk + "/devtools", "devtools")
    locked_libraries = locked_sdk + "/lib"
    if locked_libraries != libraries and repository_ctx.path(locked_libraries).exists:
        _link_directory_contents(repository_ctx, [locked_libraries, libraries], "lib")
    else:
        repository_ctx.symlink(libraries, "lib")
    if ios_sdk:
        repository_ctx.symlink(locked_sdk + "/manifests", "locked_manifests")
        repository_ctx.symlink(locked_sdk + "/share/licenses", "locked_licenses")
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
