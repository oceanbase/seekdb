"""Installed Cargo/Rust tools and locked sources exposed as Bazel inputs."""

CARGO_TOOLCHAIN_TYPE = "//bazel:cargo_toolchain_type"

def _cargo_toolchain_impl(ctx):
    return [platform_common.ToolchainInfo(
        cargo = ctx.file.cargo,
        rustc = ctx.file.rustc,
        python = ctx.file.python,
        python_home = ctx.file.python.dirname + "/..",
        files = ctx.attr.files[DefaultInfo].files,
        vendor = ctx.file.vendor,
    )]

cargo_toolchain = rule(
    implementation = _cargo_toolchain_impl,
    attrs = {
        "cargo": attr.label(allow_single_file = True, mandatory = True),
        "rustc": attr.label(allow_single_file = True, mandatory = True),
        "python": attr.label(allow_single_file = True, mandatory = True),
        "files": attr.label(mandatory = True),
        "vendor": attr.label(allow_single_file = True, mandatory = True),
    },
)

def declare_cargo_toolchains(kinds, executable_suffix, execution_constraints):
    """Declare the installed SDKs in the generated toolchain repository."""
    for kind in kinds:
        native.filegroup(
            name = kind + "_files",
            srcs = native.glob([kind + "/bin/**", kind + "/lib/**", "python/**"], exclude = ["python/**/__pycache__/**", "python/**/site-packages/**"]),
        )
        cargo_toolchain(
            name = kind + "_impl",
            cargo = kind + "/bin/cargo" + executable_suffix,
            rustc = kind + "/bin/rustc" + executable_suffix,
            python = "python/bin/python" + executable_suffix,
            files = ":" + kind + "_files",
            vendor = "@cargo_%s_sources//:vendor.tar" % kind,
        )
        targets = ["emscripten"] if kind == "wasm" else ["linux", "macos", "ios", "android", "windows"]
        for target in targets:
            native.toolchain(
                name = kind + "_" + target,
                toolchain_type = "@seekdb//bazel:cargo_toolchain_type",
                toolchain = ":" + kind + "_impl",
                exec_compatible_with = execution_constraints,
                target_compatible_with = ["@platforms//os:" + target],
            )


def _local_cargo_repository_impl(ctx):
    python = ctx.which("python" if ctx.os.name.startswith("windows") else "python3")
    rustup = ctx.which("rustup")
    if not python or not rustup:
        fail("Install Python 3 and run ./build.py init before building seekdb")
    ctx.read(ctx.attr.prepare)
    native_pin = ctx.read(ctx.attr.rust_toolchain)
    channel = [line.split('"')[1] for line in native_pin.splitlines() if line.startswith("channel =")][0]
    wasm_channel = ctx.read(ctx.attr.wasm_toolchain).strip()
    ctx.report_progress("Locating Rust " + channel + " and Python toolchains")
    result = ctx.execute([
        python, ctx.path(ctx.attr.prepare), "discover", str(ctx.path(".")),
        str(rustup), channel, wasm_channel,
    ], environment = {"RUSTUP_AUTO_INSTALL": "0"}, timeout = 600, quiet = True)
    if result.return_code:
        fail("Cannot prepare Cargo toolchain; run ./build.py init: " + result.stderr)
    config = json.decode(ctx.read("toolchains.json"))
    ctx.symlink(config["python"], "python/bin/python" + config["exe"])
    ctx.symlink(config["python"], "python/bin/python3" + config["exe"])
    for name, path in config["python_libs"].items():
        ctx.symlink(path, "python/" + name)
    for name, path in config["rust"].items():
        ctx.watch(path)
        ctx.symlink(path, name)
    # Detect installing another pinned toolchain without requiring a clean.
    ctx.watch(ctx.path(config["rust"]["native"]).dirname)
    ctx.file("BUILD.bazel", '''load("@seekdb//bazel:cargo_toolchain.bzl", "declare_cargo_toolchains")
package(default_visibility = ["//visibility:public"])
exports_files(["toolchains.json"])
declare_cargo_toolchains(kinds = %r, executable_suffix = %r, execution_constraints = %r)
''' % (config["rust"].keys(), config["exe"], config["execution_constraints"]))

local_cargo_repository = repository_rule(
    implementation = _local_cargo_repository_impl,
    attrs = {
        "prepare": attr.label(default = Label("//bazel:prepare_cargo.py")),
        "rust_toolchain": attr.label(default = Label("//rust:rust-toolchain.toml")),
        "wasm_toolchain": attr.label(default = Label("//deps/init/wasm:rust-toolchain-version")),
    },
    environ = ["PATH", "RUSTUP_HOME", "CARGO_HOME", "HOME"],
    local = True,
    configure = True,
)


def _cargo_sources_repository_impl(ctx):
    for source in ctx.attr.manifests + ctx.attr.locks:
        ctx.read(source)
    ctx.read(ctx.attr.prepare)
    config = json.decode(ctx.read(ctx.attr.toolchain_metadata))
    if ctx.attr.kind == "wasm":
        library = config["rust"]["wasm"] + "/lib/rustlib/src/rust/library/"
        for name in ["Cargo.lock", "Cargo.toml", "sysroot/Cargo.toml"]:
            ctx.read(library + name)
    ctx.report_progress("Fetching locked Cargo dependencies (" + ctx.attr.kind + ")")
    result = ctx.execute([
        config["python"], ctx.path(ctx.attr.prepare), "vendor", str(ctx.workspace_root),
        str(ctx.path(".")), ctx.path(ctx.attr.toolchain_metadata), ctx.attr.kind,
    ], timeout = 600, quiet = False)
    if result.return_code:
        fail("Cannot fetch locked Cargo dependencies: " + result.stderr)
    ctx.file("BUILD.bazel", 'exports_files(["vendor.tar"])\n')

cargo_sources_repository = repository_rule(
    implementation = _cargo_sources_repository_impl,
    attrs = {
        "prepare": attr.label(default = Label("//bazel:prepare_cargo.py")),
        "toolchain_metadata": attr.label(default = Label("@cargo//:toolchains.json")),
        "manifests": attr.label_list(mandatory = True),
        "locks": attr.label_list(mandatory = True),
        "kind": attr.string(values = ["native", "wasm"], mandatory = True),
    },
    environ = ["CARGO_HOME", "HOME"],
)
