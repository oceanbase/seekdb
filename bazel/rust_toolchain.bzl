"""Expose the project's installed Rust SDK to native Bazel Rust rules."""

load("@rules_rust//cargo:cargo_toolchain.bzl", "cargo_toolchain")
load("@rules_rust//rust:rust_stdlib_filegroup.bzl", "rust_stdlib_filegroup")
load("@rules_rust//rust:rust_toolchain.bzl", "rust_toolchain")
load("@rules_rust//rust/platform:triple.bzl", "triple")
load(
    "@rules_rust//rust/platform:triple_mappings.bzl",
    "system_to_binary_ext",
    "system_to_dylib_ext",
    "system_to_staticlib_ext",
    "system_to_stdlib_linkflags",
    "triple_to_constraint_set",
)

RUST_TARGETS = [
    "x86_64-unknown-linux-gnu",
    "aarch64-unknown-linux-gnu",
    "x86_64-apple-darwin",
    "aarch64-apple-darwin",
    "x86_64-pc-windows-msvc",
    "aarch64-pc-windows-msvc",
    "aarch64-linux-android",
    "aarch64-apple-ios",
    "aarch64-apple-ios-sim",
]

def declare_installed_rust(host, version, targets, channel = "stable", wasm_flags = []):
    """Declare only target standard libraries already installed by init."""
    host_info = triple(host)
    suffix = system_to_binary_ext(host_info.system)
    host_constraints = triple_to_constraint_set(host)
    native.exports_files(["BUILD.bazel", "bin/cargo" + suffix])
    native.filegroup(
        name = "compiler_libraries",
        srcs = native.glob([
            "bin/*.dll",
            "lib/*.so*",
            "lib/*.dylib",
            "lib/rustlib/%s/codegen-backends/**" % host,
            "lib/rustlib/%s/lib/*.so" % host,
            "lib/rustlib/%s/lib/*.dylib" % host,
        ], allow_empty = True),
    )
    cargo_toolchain(
        name = "cargo_impl",
        cargo = "bin/cargo" + suffix,
        version = version,
        channel = channel,
    )
    for target in targets:
        target_info = triple(target)
        name = target.replace("-", "_")
        constraints = triple_to_constraint_set(target)
        wasm = target == "wasm32-unknown-emscripten"
        rust_stdlib_filegroup(
            name = name + "_std",
            srcs = native.glob([
                "lib/rustlib/%s/lib/*.rlib" % target,
                "lib/rustlib/%s/lib/*.rmeta" % target,
                "lib/rustlib/%s/lib/*.so" % target,
                "lib/rustlib/%s/lib/*.dylib" % target,
                "lib/rustlib/%s/lib/*.dll" % target,
                "lib/rustlib/%s/lib/*.lib" % target,
                "lib/rustlib/%s/lib/self-contained/**" % target,
            ], allow_empty = True),
        )
        rust_toolchain(
            name = name + "_impl",
            rustc = "bin/rustc" + suffix,
            rust_doc = "bin/rustdoc" + suffix,
            rustc_lib = ":compiler_libraries",
            rust_std = "@rust_wasm_std//:std" if wasm else ":" + name + "_std",
            exec_triple = host,
            target_triple = target,
            version = version,
            channel = channel,
            binary_ext = system_to_binary_ext(target_info.system),
            dylib_ext = system_to_dylib_ext(target_info.system),
            staticlib_ext = ".a" if wasm else system_to_staticlib_ext(target_info.system, target_info.abi),
            stdlib_linkflags = system_to_stdlib_linkflags(target_info.system, target_info.abi),
            default_edition = "2021",
            opt_level = {"dbg": "3", "fastbuild": "3", "opt": "3"},
            debug_info = {"dbg": "2", "fastbuild": "2", "opt": "2"},
            strip_level = {"dbg": "none", "fastbuild": "none", "opt": "none"},
            extra_rustc_flags = wasm_flags if wasm else ["-Cpanic=abort"],
            # Build scripts expect ar flags; the Apple C++ toolchain uses libtool.
            env = {"AR": "/usr/bin/ar"} if target_info.system in ["macos", "ios"] else {},
        )
        for kind, implementation, toolchain_type in [
            ("rust", name + "_impl", "@rules_rust//rust:toolchain_type"),
            ("cargo", "cargo_impl", "@rules_rust//cargo:toolchain_type"),
        ]:
            native.toolchain(
                name = name + "_" + kind,
                toolchain = ":" + implementation,
                toolchain_type = toolchain_type,
                exec_compatible_with = host_constraints,
                target_compatible_with = constraints,
                target_settings = ["@rules_rust//rust/toolchain/channel:" + channel],
            )

def _installed_rust_repository_impl(ctx):
    wasm = ctx.attr.kind == "wasm"
    if wasm:
        channel = ctx.read(ctx.attr.wasm_pin).strip()
    else:
        pin = ctx.read(ctx.attr.pin)
        channel = [line.split('"')[1] for line in pin.splitlines() if line.startswith("channel =")][0]
    rustup = ctx.which("rustup")
    if not rustup:
        fail("Run ./build.py init to install the pinned Rust toolchain")
    result = ctx.execute([rustup, "which", "rustc", "--toolchain", channel], environment = {"RUSTUP_AUTO_INSTALL": "0"})
    if result.return_code:
        if wasm:
            home = ctx.execute([rustup, "show", "home"])
            if home.return_code:
                fail("Cannot locate the Rust SDK directory:\n" + home.stderr)
            toolchains = ctx.path(home.stdout.strip()).get_child("toolchains")
            ctx.watch(toolchains)
            if toolchains.exists:
                toolchains.readdir()
            ctx.file("BUILD.bazel", "")
            return
        fail("Run ./build.py init to install Rust %s:\n%s" % (channel, result.stderr))
    rustc = ctx.path(result.stdout.strip())
    root = rustc.dirname.dirname
    ctx.watch(root)
    ctx.watch(rustc)
    details = ctx.execute([rustc, "-vV"])
    if details.return_code:
        fail("Cannot inspect the installed Rust compiler:\n" + details.stderr)
    fields = dict([line.split(": ", 1) for line in details.stdout.splitlines() if ": " in line])
    for entry in root.readdir():
        ctx.symlink(entry, entry.basename)
    targets = [fields["host"], "wasm32-unknown-emscripten"] if wasm else [
        target for target in RUST_TARGETS if root.get_child("lib", "rustlib", target, "lib").exists
    ]
    wasm_flags = json.decode(ctx.read(ctx.attr.wasm_config))["rust_std_build"]["rustflags"] if wasm else []
    root.get_child("lib", "rustlib").readdir()
    ctx.file("BUILD.bazel", '''load("@seekdb//bazel:rust_toolchain.bzl", "declare_installed_rust")
package(default_visibility = ["//visibility:public"])
declare_installed_rust(host = %r, version = %r, targets = %r, channel = %r, wasm_flags = %r)
''' % (fields["host"], fields["release"], targets, "nightly" if wasm else "stable", wasm_flags))

installed_rust_repository = repository_rule(
    implementation = _installed_rust_repository_impl,
    attrs = {
        "kind": attr.string(default = "native", values = ["native", "wasm"]),
        "pin": attr.label(default = Label("//:rust-toolchain.toml")),
        "wasm_pin": attr.label(default = Label("//deps/init/wasm:rust-toolchain-version")),
        "wasm_config": attr.label(default = Label("//deps/init/wasm:dependencies.json")),
    },
    environ = ["PATH", "RUSTUP_HOME", "CARGO_HOME", "HOME"],
    local = True,
    configure = True,
)

def _wasm_std_repository_impl(ctx):
    python = ctx.which("python3")
    if not python:
        fail("Install Python 3 and run ./build.py init --wasm")
    for label in [ctx.attr.prepare, ctx.attr.config, ctx.attr.emscripten_version]:
        ctx.read(label)
    compiler = ctx.path(ctx.attr.compiler).dirname
    library = compiler.get_child("lib", "rustlib", "src", "rust", "library")
    if not library.exists:
        fail("Missing nightly rust-src; run ./build.py init --wasm")
    ctx.watch_tree(library)
    prefix = ctx.getenv("SEEKDB_WASM_DEPS") or str(ctx.workspace_root.get_child("deps", "wasm"))
    result = ctx.execute([
        python, ctx.path(ctx.attr.prepare), "inspect",
        "--prefix", prefix, "--sysroot", compiler,
    ], timeout = 120)
    if result.return_code:
        fail(result.stderr)
    sdk = ctx.path(json.decode(result.stdout)["sdk"])
    ctx.read(sdk.get_child("sdk.json"))
    ctx.symlink(sdk.get_child("lib"), "lib")
    ctx.file("BUILD.bazel", '''load("@rules_rust//rust:rust_stdlib_filegroup.bzl", "rust_stdlib_filegroup")
package(default_visibility = ["//visibility:public"])
rust_stdlib_filegroup(name = "std", srcs = glob(["lib/**/*.rlib", "lib/**/*.rmeta"]))
''')

wasm_std_repository = repository_rule(
    implementation = _wasm_std_repository_impl,
    attrs = {
        "compiler": attr.label(default = Label("@rust_wasm//:BUILD.bazel")),
        "prepare": attr.label(default = Label("//deps/init/wasm:rust.py")),
        "config": attr.label(default = Label("//deps/init/wasm:dependencies.json")),
        "emscripten_version": attr.label(default = Label("//deps/init/wasm:emscripten-version")),
    },
    environ = ["PATH"],
    local = True,
    configure = True,
)
