"""Expose seekdb's bootstrapped third-party SDK as a Bazel repository."""

def _link_directory_contents(repository_ctx, directories, destination):
    names = {}
    for directory in directories:
        directory = repository_ctx.path(directory)
        if not directory.exists:
            continue
        for child in directory.readdir():
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
        repository_ctx.symlink(path, "include/" + name)
    for source, destination in {
        "openssl/lib/VC/x64/MD": "openssl",
        "openssl/bin": "openssl_bin",
    }.items():
        repository_ctx.symlink(sdk + "/" + source, destination)
    repository_ctx.watch(repository_ctx.path(locked_sdk + "/llvm/manifest.json"))
    repository_ctx.symlink(locked_sdk + "/llvm", "devtools")
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
    if platform == "wasm":
        prefix = repository_ctx.getenv("SEEKDB_WASM_DEPS") or str(repository_ctx.workspace_root) + "/deps/wasm"
        headers = prefix + "/include"
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
        "roaring", "vsag", "fmt", "cpuinfo.h", "seekdb-vsag",
        "omp.h", "ompx.h", "omp-tools.h", "ompt.h", "lapack.h", "lapacke.h",
        "lapacke_config.h", "lapacke_mangling.h", "lapacke_utils.h", "cblas.h",
    ]
    if ios_sdk or platform in ["android", "wasm"]:
        libraries = locked_sdk + "/lib"
        libraries64 = libraries
        if not repository_ctx.path(libraries + "/libssl.a").exists:
            option = "--ios" + (" --simulator" if ios_sdk == "iphonesimulator" else "") if ios_sdk else "--" + platform
            fail("Missing target dependencies; run ./build.py init " + option)
        locked_names = []
        for child in repository_ctx.path(locked_sdk + "/include").readdir():
            source = repository_ctx.path(headers).get_child("boost") if platform == "wasm" and child.basename == "boost" else child
            if not source.exists:
                fail("Missing prepared Wasm Boost headers; run ./build.py init --wasm")
            repository_ctx.symlink(source, "include/" + child.basename)
            locked_names.append(child.basename)
        if ios_sdk and repository_ctx.path(headers).exists:
            for child in repository_ctx.path(headers).readdir():
                if child.basename not in source_headers + locked_names:
                    repository_ctx.symlink(child, "include/" + child.basename)
    else:
        locked_headers = locked_sdk + "/include"
        if repository_ctx.path(locked_headers).exists:
            _link_directory_contents(repository_ctx, [locked_headers, headers], "include")
        else:
            repository_ctx.symlink(headers, "include")
    host_prebuilt = repository_ctx.workspace_root.get_child("deps", "prebuilt", "native")
    llvm = host_prebuilt.get_child("llvm")
    repository_ctx.watch(llvm.get_child("manifest.json"))
    repository_ctx.symlink(llvm, "devtools")
    if platform == "native" and repository_ctx.os.name == "linux":
        repository_ctx.symlink(host_prebuilt.get_child("ob-sanity"), "sanity")
    locked_libraries = locked_sdk + "/lib"
    if locked_libraries != libraries and repository_ctx.path(locked_libraries).exists:
        _link_directory_contents(repository_ctx, [locked_libraries, libraries], "lib")
    else:
        repository_ctx.symlink(libraries, "lib")
    if ios_sdk:
        repository_ctx.symlink(locked_sdk + "/manifests", "locked_manifests")
        repository_ctx.symlink(locked_sdk + "/share/licenses", "locked_licenses")
    if repository_ctx.path(libraries64).exists:
        repository_ctx.symlink(libraries64, "lib64")
    repository_ctx.file("BUILD.bazel", '''load("@seekdb//bazel/third_party:targets.bzl", "sdk_targets")
package(default_visibility = ["//visibility:public"])
sdk_targets()
''', executable = False)

third_party_headers_repository = repository_rule(
    attrs = {
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
