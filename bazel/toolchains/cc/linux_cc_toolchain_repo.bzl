"""Creates seekdb's local, repository-owned Clang toolchain."""

load(":repository_utils.bzl", "builtin_include_directories", "host_substitutions")

def _discover_builtin_include_directories(repository_ctx, compiler):
    resource_result = repository_ctx.execute(
        [compiler, "-print-resource-dir"],
        environment = {"LC_ALL": "C"},
        quiet = True,
    )
    if resource_result.return_code != 0:
        fail("failed to discover Clang resource directory:\n%s" % resource_result.stderr)
    resource_directory = resource_result.stdout.strip()
    resource_version = resource_directory.split("/")[-1]
    bundled_resource_directory = repository_ctx.path(
        "devtools/lib/clang/%s" % resource_version,
    )
    if not bundled_resource_directory.exists:
        fail(
            "Clang resource directory %s is absent from the bundled toolchain" %
            bundled_resource_directory,
        )

    directories = []
    repository_root = str(repository_ctx.path(""))
    for value in builtin_include_directories(repository_ctx, compiler):
        if value == resource_directory or value.startswith(resource_directory + "/"):
            value = "%crosstool_top%/devtools/lib/clang/" + resource_version + value[len(resource_directory):]
        elif value.startswith(repository_root + "/"):
            value = "%crosstool_top%/" + value[len(repository_root) + 1:]
        if value not in directories:
            directories.append(value)

    return directories

def _write_builtin_module_map(repository_ctx, builtin_include_directories):
    find = repository_ctx.which("find")
    if not find:
        fail("find is required to configure the bundled Clang toolchain")

    repository_root = str(repository_ctx.path(""))
    roots = []
    for directory in builtin_include_directories:
        crosstool_prefix = "%crosstool_top%/"
        if directory.startswith(crosstool_prefix):
            directory = repository_root + "/" + directory[len(crosstool_prefix):]
        path = repository_ctx.path(directory)
        if path.exists and str(path) not in roots:
            roots.append(str(path))
    result = repository_ctx.execute(
        [find] + roots + ["-type", "f", "-print"],
        environment = {"LC_ALL": "C"},
        quiet = True,
    )
    if result.return_code != 0:
        fail("failed to enumerate bundled Clang headers:\n%s" % result.stderr)

    repository_prefix = repository_root + "/"
    headers = {}
    for path in result.stdout.splitlines():
        if path.startswith(repository_prefix):
            path = path[len(repository_prefix):]
        headers[path] = True
    headers = sorted(headers.keys())
    if not headers:
        fail("no bundled Clang headers were found under %s" % roots)

    lines = ['module "crosstool" [system] {']
    lines.extend(["  textual header %s" % json.encode(path) for path in headers])
    lines.append("}")
    repository_ctx.file("module.modulemap", "\n".join(lines) + "\n")

def _linux_cc_toolchain_repository_impl(repository_ctx):
    if repository_ctx.os.name != "linux":
        repository_ctx.file("BUILD.bazel", "# The bundled toolchain is Linux-only.\n")
        return
    architecture = host_substitutions(repository_ctx)["@HOST_CPU@"]
    devtools = repository_ctx.workspace_root.get_child("deps", "3rd", "usr", "local", "oceanbase", "devtools")
    for tool_name in [
        "clang",
        "ld.lld",
        "lld",
        "llvm-ar",
        "llvm-cov",
        "llvm-cxxfilt",
        "llvm-dwp",
        "llvm-nm",
        "llvm-objcopy",
        "llvm-objdump",
        "llvm-ranlib",
        "llvm-strip",
    ]:
        tool = devtools.get_child("bin").get_child(tool_name)
        if not tool.exists:
            fail("bundled tool is missing: %s" % tool)

    repository_ctx.symlink(devtools, "devtools")

    repository_ctx.template(
        "validate_static_library.sh",
        Label("@rules_cc//cc/private/toolchain:validate_static_library.sh.tpl"),
        substitutions = {
            "%{nm}": '$(dirname "${BASH_SOURCE[0]}")/devtools/bin/llvm-nm',
            "%{c++filt}": '$(dirname "${BASH_SOURCE[0]}")/devtools/bin/llvm-cxxfilt',
            "%{nm_extra_args}": "",
        },
        executable = True,
    )

    builtin_include_directories = _discover_builtin_include_directories(
        repository_ctx,
        str(repository_ctx.path("devtools/bin/clang")),
    )
    repository_ctx.template(
        "BUILD.bazel",
        Label(":BUILD.linux_cc_toolchain.tpl"),
        substitutions = {
            "@CXX_BUILTIN_INCLUDE_DIRECTORIES@": repr(builtin_include_directories),
            "@HOST_CPU@": architecture,
            "@BAZEL_CPU@": "aarch64" if architecture == "aarch64" else "k8",
        },
        executable = False,
    )
    repository_ctx.file(
        "builtin_include_directory_paths",
        "\n".join(builtin_include_directories) + "\n",
    )
    _write_builtin_module_map(repository_ctx, builtin_include_directories)

linux_cc_toolchain_repository = repository_rule(
    implementation = _linux_cc_toolchain_repository_impl,
    configure = True,
    local = True,
)
