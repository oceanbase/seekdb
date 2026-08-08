"""Creates seekdb's local, repository-owned Clang toolchain."""

def _quote(value):
    return "\"%s\"" % value.replace("\\", "\\\\").replace("\"", "\\\"")

def _format_string_list(values):
    return "[\n%s\n]" % "".join(["    %s,\n" % _quote(value) for value in values])

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

    result = repository_ctx.execute(
        [compiler, "-E", "-x", "c++", "/dev/null", "-v"],
        environment = {"LC_ALL": "C"},
        quiet = True,
    )
    if result.return_code != 0:
        fail("failed to discover Clang include directories:\n%s" % result.stderr)

    marker = "#include <...> search starts here:"
    end_marker = "End of search list."
    collecting = False
    directories = []
    repository_root = str(repository_ctx.path(""))
    for line in result.stderr.splitlines():
        value = line.strip()
        if value == marker:
            collecting = True
        elif collecting and value == end_marker:
            break
        elif collecting and value:
            framework_suffix = " (framework directory)"
            if value.endswith(framework_suffix):
                value = value[:-len(framework_suffix)]
            if value == resource_directory:
                value = "%crosstool_top%/devtools/lib/clang/" + resource_version
            elif value.startswith(resource_directory + "/"):
                value = (
                    "%crosstool_top%/devtools/lib/clang/" +
                    resource_version +
                    "/" +
                    value[len(resource_directory) + 1:]
                )
            elif value.startswith(repository_root + "/"):
                value = "%crosstool_top%/" + value[len(repository_root) + 1:]
            if value not in directories:
                directories.append(value)

    if not directories:
        fail("Clang did not report any builtin include directories:\n%s" % result.stderr)
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
    lines.extend(["  textual header %s" % _quote(path) for path in headers])
    lines.append("}")
    repository_ctx.file("module.modulemap", "\n".join(lines) + "\n")

def _seekdb_cc_toolchain_repository_impl(repository_ctx):
    workspace_root = str(repository_ctx.workspace_root)
    devtools = repository_ctx.path(workspace_root + "/" + repository_ctx.attr.devtools_path)
    tools = {}
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
        tools[tool_name] = tool
    compiler = tools["clang"]

    repository_ctx.symlink(devtools, "devtools")

    repository_ctx.template(
        "BUILD.bazel",
        repository_ctx.attr.build_file,
        executable = False,
    )
    repository_ctx.template(
        "validate_static_library.sh",
        repository_ctx.attr.validate_static_library,
        executable = True,
    )

    builtin_include_directories = _discover_builtin_include_directories(
        repository_ctx,
        str(repository_ctx.path("devtools/bin/clang")),
    )
    repository_ctx.template(
        "toolchain_config.bzl",
        repository_ctx.attr.toolchain_config,
        substitutions = {
            "@CXX_BUILTIN_INCLUDE_DIRECTORIES@": _format_string_list(builtin_include_directories),
        },
        executable = False,
    )
    repository_ctx.file(
        "builtin_include_directory_paths",
        "\n".join(builtin_include_directories) + "\n",
    )
    _write_builtin_module_map(repository_ctx, builtin_include_directories)

seekdb_cc_toolchain_repository = repository_rule(
    implementation = _seekdb_cc_toolchain_repository_impl,
    attrs = {
        "build_file": attr.label(
            allow_single_file = True,
            default = Label("//bazel/toolchains/cc:BUILD.seekdb_cc_toolchain.tpl"),
        ),
        "devtools_path": attr.string(
            default = "deps/3rd/usr/local/oceanbase/devtools",
        ),
        "toolchain_config": attr.label(
            allow_single_file = True,
            default = Label("//bazel/toolchains/cc:toolchain_config.bzl.tpl"),
        ),
        "validate_static_library": attr.label(
            allow_single_file = True,
            default = Label("//bazel/toolchains/cc:validate_static_library.sh"),
        ),
    },
    configure = True,
    local = True,
)
