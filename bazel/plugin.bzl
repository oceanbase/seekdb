"""Linux native plugins: closed linking, explicit profiles and audited outputs.

Public DSOs use rules_cc directly, never the server's cc_library wrapper.
Server-dev receives compilation context only, not a server linking context.
"""

load("@rules_cc//cc:cc_binary.bzl", "cc_binary")
load("@rules_cc//cc/common:cc_common.bzl", "cc_common")
load("@rules_cc//cc/common:cc_info.bzl", "CcInfo")
load("@rules_cc//cc:find_cc_toolchain.bzl", "find_cpp_toolchain", "use_cc_toolchain")
load("//bazel:compile_config.bzl", "MODULE_CXX_COPTS", "RELEASE_LOCAL_DEFINES", "VERSION")

_LINUX = select({"@platforms//os:linux": [], "//conditions:default": ["@platforms//:incompatible"]})

def _private_deps_impl(ctx):
    infos = []
    for dep in ctx.attr.deps:
        info = dep[CcInfo]
        for header in info.compilation_context.headers.to_list():
            if header.short_path.startswith("src/"):
                fail("plugin-private dependency exposes a core header: " + header.short_path)
        for linker in info.linking_context.linker_inputs.to_list():
            if linker.owner.package.startswith("src/"):
                fail("plugin-private dependency links core code: " + str(linker.owner))
            for library in linker.libraries:
                if library.dynamic_library or library.interface_library:
                    fail("plugin-private dependencies must be static: " + str(linker.owner))
        infos.append(info)
    return [cc_common.merge_cc_infos(cc_infos = infos)]

_private_deps = rule(
    implementation = _private_deps_impl,
    attrs = {"deps": attr.label_list(providers = [CcInfo])},
)

def _headers_only_impl(ctx):
    info = cc_common.merge_cc_infos(cc_infos = [dep[CcInfo] for dep in ctx.attr.deps])
    return [CcInfo(compilation_context = info.compilation_context), DefaultInfo(files = info.compilation_context.headers)]

# This bridge must be instantiated by the core owner, with narrow visibility.
server_dev_headers = rule(
    implementation = _headers_only_impl,
    attrs = {"deps": attr.label_list(providers = [CcInfo])},
)

def _audit_impl(ctx):
    cc = find_cpp_toolchain(ctx)
    output = ctx.actions.declare_file(ctx.attr.output_name)
    args = ctx.actions.args()
    args.add(ctx.file._tool)
    args.add("audit")
    args.add("--manifest", ctx.file.manifest)
    args.add("--binary", ctx.file.binary)
    args.add("--output", output)
    args.add("--nm", cc.nm_executable)
    args.add("--profile", ctx.attr.profile)
    args.add_all("--exports", ctx.attr.exports)
    args.add_all("--server-headers", ctx.attr.server_headers)
    ctx.actions.run(
        executable = ctx.file._python,
        arguments = [args],
        inputs = depset([ctx.file._tool, ctx.file.manifest, ctx.file.binary] + ctx.files.srcs + ctx.files._audits, transitive = [cc.all_files]),
        outputs = [output],
        mnemonic = "PluginAudit",
        use_default_shell_env = True,
        execution_requirements = {"no-remote": "1"},
    )
    return [DefaultInfo(files = depset([output]))]

audit_plugin = rule(
    implementation = _audit_impl,
    attrs = {
        "binary": attr.label(allow_single_file = True, mandatory = True),
        "manifest": attr.label(allow_single_file = True, mandatory = True),
        "output_name": attr.string(mandatory = True),
        "srcs": attr.label_list(allow_files = True),
        "profile": attr.string(default = "public", values = ["public", "server-dev"]),
        "exports": attr.string_list(),
        "server_headers": attr.string_list(),
        "_tool": attr.label(default = Label("//bazel:plugin_tools.py"), allow_single_file = True),
        "_audits": attr.label(default = Label("//cmake:plugin_audit_tools")),
        "_python": attr.label(default = "@host_tools//:python", allow_single_file = True, cfg = "exec"),
    },
    toolchains = use_cc_toolchain(),
    fragments = ["cpp"],
)

def plugin_package(name, binary, manifest, sql = []):
    native.genrule(
        name = name + "_package",
        srcs = [binary, manifest] + sql,
        tools = ["@host_tools//:python", "//bazel:plugin_tools.py", "//cmake:plugin_audit_tools"],
        outs = [name + ".tar"],
        cmd = "$(location @host_tools//:python) $(location //bazel:plugin_tools.py) package --name " + name +
              " --binary $(location " + binary + ") --manifest $(location " + manifest + ") --output $@ --sql " +
              " ".join(["$(location " + path + ")" for path in sql]),
        visibility = ["//visibility:public"],
        target_compatible_with = _LINUX,
    )

def native_plugin(name, srcs, output_name, manifest = "plugin.toml", deps = [], local_defines = [], sql = [],
                  server_headers_target = None, server_headers = [], exports = [], contract = None, extra_cxxopts = []):
    package = native.package_name()
    if not package.startswith("plugins/"):
        fail("native_plugin must be declared under plugins/")
    for src in srcs:
        if src.startswith("/") or src.startswith("@") or ".." in src.split("/"):
            fail("plugin sources must be local to their package: " + src)
    if bool(server_headers_target) != bool(contract):
        fail("server-dev requires both a header context and a linked-host contract")
    _private_deps(name = name + "_private_deps", deps = deps + ["//include:plugin_sdk"])
    compiled = name + "_unchecked.so"
    export_map = name + "_exports.map"
    native.genrule(
        name = name + "_export_map",
        outs = [export_map],
        cmd = "echo '{ global: seekdb_plugin_entry_v1; " + " ".join([symbol + ";" for symbol in exports]) + " local: *; };' > $@",
    )
    compile_srcs = srcs
    compile_deps = [":" + name + "_private_deps"]
    copts = ["-fvisibility=hidden"]
    cxxopts = ["-std=c++17", "-fvisibility-inlines-hidden"]
    if server_headers_target:
        compile_srcs = srcs + ["//plugins:server_dev_entry.cpp", contract]
        compile_deps += [server_headers_target]
        copts += MODULE_CXX_COPTS
        copts += ["-I$(BINDIR)/" + package]
        local_defines = local_defines + RELEASE_LOCAL_DEFINES + ["seekdb_plugin_entry_v1=seekdb_plugin_server_dev_entry_impl"]
        cxxopts = ["-std=gnu++20", "-fvisibility-inlines-hidden"]
    cc_binary(
        name = compiled,
        srcs = compile_srcs,
        deps = compile_deps,
        copts = copts,
        cxxopts = cxxopts + extra_cxxopts,
        local_defines = local_defines,
        linkshared = True,
        linkstatic = True,
        linkopts = ["-Wl,-z,defs", "-Wl,--version-script=$(location :" + export_map + ")", "-static-libstdc++", "-static-libgcc"],
        additional_linker_inputs = [":" + export_map],
        target_compatible_with = _LINUX,
    )
    audit_plugin(
        name = name,
        binary = ":" + compiled,
        manifest = manifest,
        output_name = output_name,
        srcs = srcs + ([server_headers_target] if server_headers_target else []),
        profile = "server-dev" if server_headers_target else "public",
        server_headers = server_headers,
        exports = exports,
        visibility = ["//visibility:public"],
        target_compatible_with = _LINUX,
    )
    plugin_package(name, ":" + name, manifest, sql)

def plugin_sdk_archive(name, headers):
    native.genrule(
        name = name,
        srcs = headers,
        tools = ["@host_tools//:python", "//bazel:plugin_tools.py", "//cmake:plugin_audit_tools"],
        outs = [name + ".tar"],
        cmd = "$(location @host_tools//:python) $(location //bazel:plugin_tools.py) sdk --version " + VERSION +
              " --output $@ --headers $(SRCS)",
        visibility = ["//visibility:public"],
    )
