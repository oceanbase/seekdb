"""Expose the Cargo workspace's declared C++ exports to Bazel."""

load("//bazel:defs.bzl", cc_library = "seekdb_cc_library")

def _cargo_exports_repository_impl(ctx):
    exports = json.decode(ctx.read(ctx.attr.manifest))
    ctx.file("exports.bzl", "EXPORTS = " + repr(exports) + "\n")
    ctx.file("BUILD.bazel", 'exports_files(["exports.bzl"])\n')

cargo_exports_repository = repository_rule(
    implementation = _cargo_exports_repository_impl,
    attrs = {"manifest": attr.label(mandatory = True, allow_single_file = True)},
)

def _archive(name, windows):
    return name + ".lib" if windows else "lib" + name + ".a"

def _platform_files(unix, windows = None):
    if windows == None:
        windows = unix
    return select({
        ":windows": ["windows/" + f for f in windows],
        "//conditions:default": ["unix/" + f for f in unix],
    })

def cargo_workspace(exports, srcs, cxx = None, cxx_inputs = []):
    """Build once per platform; declare each library, headers, and archive target.

    cxx: optional label of a hermetic C++ driver, injected via $(location) for
         the Linux genrule only.  cxx_inputs: labels providing that driver's
         minimal input closure (declared as tools so Bazel stages them).
    """
    native.config_setting(name = "windows", constraint_values = ["@platforms//os:windows"])
    native.config_setting(name = "macos", constraint_values = ["@platforms//os:macos"])
    headers = {"include/" + h: True for export in exports.values() for h in export["headers"]}
    for platform in ["unix", "windows"]:
        windows = platform == "windows"
        command = '"$(location build.py)" --target-dir "$(@D)/cargo-target" --output "$(@D)/%s"' % platform
        # Linux-only C++ depfile evidence: -MD is appended to this genrule's
        # own CXXFLAGS (never CFLAGS, RUSTFLAGS, or the global/toolchain env).
        # cc reads CXXFLAGS only for cpp(true) actions, so only C++ objects
        # emit depfiles; build.py collects them into the declared stable out.
        depfile_outs = [platform + "/cxx-depfiles.tar"] if not windows else []
        if not windows:
            command += ' --depfiles-archive "$(@D)/%s/cxx-depfiles.tar"' % platform
        cxxflags = "-D_GLIBCXX_USE_CXX11_ABI=1" + ("" if windows else " -MD")
        cxx_export = ('export CXX="$$PWD/$(location %s)"\n' % cxx) if (not windows and cxx) else ""
        cxx_tools = ([cxx] + cxx_inputs) if (not windows and cxx) else []
        native.genrule(
            name = "_rust_static_" + platform,
            srcs = srcs,
            outs = [platform + "/" + _archive(name, windows) for name in exports] +
                   [platform + "/" + h for h in headers] + depfile_outs,
            cmd = ('set -eu\nexport DEFAULT_LOG_LEVEL=OB_LOG_LEVEL_ERROR\n' +
                   cxx_export +
                   'export CXXFLAGS="$${CXXFLAGS:-} ' + cxxflags + '"\n' +
                   'python3 ' + command) if not windows else "",
            tools = cxx_tools,
            cmd_bat = "set DEFAULT_LOG_LEVEL=OB_LOG_LEVEL_ERROR\npython " + command if windows else "",
            tags = ["no-remote", "no-sandbox"],
            target_compatible_with = select({
                ":windows": [] if windows else ["@platforms//:incompatible"],
                "//conditions:default": ["@platforms//:incompatible"] if windows else [],
            }),
        )

    cc_library(
        name = "_rust_runtime",
        linkopts = select({
            ":windows": ["ntdll.lib", "userenv.lib", "ws2_32.lib", "bcrypt.lib", "advapi32.lib", "synchronization.lib"],
            ":macos": [],
            "//conditions:default": ["-lpthread", "-ldl", "-lm"],
        }),
    )
    for name, export in exports.items():
        archives = _platform_files([_archive(name, False)], [_archive(name, True)])
        generated_headers = ["include/" + h for h in export["headers"]]
        include_dir = export["crate"] + "/include"
        native.filegroup(
            name = name + "_archive",
            srcs = archives,
            visibility = ["//visibility:public"],
        )
        cc_library(
            name = name + "_headers",
            hdrs = native.glob([include_dir + "/**/*.h"], allow_empty = True) + _platform_files(generated_headers),
            includes = [include_dir] + _platform_files(["include"]),
            deps = [":" + dep + "_headers" for dep in export["deps"]],
            visibility = ["//visibility:public"],
        )
        cc_library(
            name = name,
            srcs = archives,
            deps = [":" + name + "_headers", ":_rust_runtime"] + [":" + dep for dep in export["deps"]],
            visibility = ["//visibility:public"],
        )
