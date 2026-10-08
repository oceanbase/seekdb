#!/usr/bin/env python3
"""Independent pure-Rust Server-dev build/package, actual host binding and load.

Catalog/verifier and candidate pool are controlled loader fixtures, not a live
database. Uses the real SDK, CMake helper, Rust contract generator and packager.
"""
import argparse
import hashlib
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile


def run(command, ok=True, env=None):
    result = subprocess.run(list(map(str, command)), capture_output=True, text=True, timeout=150, env=env)
    if (result.returncode == 0) != ok:
        raise AssertionError(f"{command}\n{result.stdout}\n{result.stderr}")
    return result.stdout + result.stderr


def build_for_host(host, stage, python=None):
    """Build the unmodified in-tree Rust example for a final kernel executable."""
    repo = pathlib.Path(__file__).resolve().parents[3]
    stage = pathlib.Path(stage)
    stage.mkdir(parents=True)
    (stage / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.20)
project(RustKernelCandidate LANGUAGES C)
add_executable(seekdb IMPORTED GLOBAL)
set_target_properties(seekdb PROPERTIES IMPORTED_LOCATION "''' + str(host) + '''")
include("''' + str(repo / "cmake/RustPlugin.cmake") + '''")
add_subdirectory("''' + str(repo / "plugins/rust_candidate") + '''" rust_candidate)
''')
    configure = ["cmake", "-S", stage, "-B", stage / "build"]
    if python:
        configure.append("-DPython3_EXECUTABLE=" + str(python))
    run(configure)
    run(["cargo", "run", "--offline", "--manifest-path", repo / "rust/cargo-seekdb/Cargo.toml",
         "--", "package", "--build-dir", stage / "build", "--target", "seekdb_rust_candidate_plugin",
         "--output", stage / "package"])
    return stage / "package/libseekdb_rust_candidate.so"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--loader", required=True, type=pathlib.Path)
    parser.add_argument("--cargo", required=True)
    args = parser.parse_args()
    repo = pathlib.Path(__file__).resolve().parents[3]
    with tempfile.TemporaryDirectory(prefix="seekdb-rust-server-dev-") as temporary:
        root = pathlib.Path(temporary)
        source, build = root / "independent source", root / "build with spaces"
        shutil.copytree(repo / "plugins/rust_candidate", source,
                        ignore=shutil.ignore_patterns("target", "Cargo.lock"))
        manifest = source / "Cargo.toml"
        manifest.write_text(manifest.read_text().replace("../../rust/extension-sdk", str(repo / "rust/extension-sdk")))
        (source / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.20)
project(RustServerDev LANGUAGES C)
include("''' + str(repo / "cmake/RustPlugin.cmake") + '''")
seekdb_add_rust_plugin(example STANDALONE
  LIBRARY_NAME seekdb_rust_candidate MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/plugin.toml"
  SERVER_DEV_HOST "${SAMPLE_HOST}")
''')
        configure = ["cmake", "-S", source, "-B", build, "-DPython3_EXECUTABLE=" + sys.executable]
        run(configure, ok=False)  # No accidental implicit build host in standalone mode.
        run(configure + ["-DSAMPLE_HOST=" + str(args.loader.resolve())])
        package = [args.cargo, "run", "--offline", "--manifest-path", repo / "rust/cargo-seekdb/Cargo.toml",
                   "--", "package", "--build-dir", build, "--target", "example", "--output"]
        output = root / "package one"
        run(package + [output])
        binary = "libseekdb_rust_candidate.so"
        assert {p.name for p in output.iterdir()} == {binary, "plugin.toml"}
        run([args.loader.resolve(), output, binary, "candidate-native"])
        contract = build / "server-dev-example/contract.rs"
        # Exercise plugin-owned plan parsing in the independently copied source,
        # using the exact host contract already used to build the loaded DSO.
        environment = os.environ.copy()
        environment["SEEKDB_SERVER_DEV_CONTRACT"] = str(contract)
        run([args.cargo, "test", "--offline", "--manifest-path", manifest], env=environment)
        identity = contract.read_bytes()
        mtime = contract.stat().st_mtime_ns
        run(["cmake", "--build", build, "--target", "example", "-j2"])
        assert contract.read_bytes() == identity and contract.stat().st_mtime_ns == mtime
        digest = hashlib.sha256((output / binary).read_bytes()).digest()
        run(package + [output], ok=False)
        assert hashlib.sha256((output / binary).read_bytes()).digest() == digest

        # Switch to an older, distinct executable in the same build directory.
        # The contract and cdylib must change, not merely the package manifest.
        other_host = pathlib.Path("/bin/true").resolve()
        run(configure + ["-DSAMPLE_HOST=" + str(other_host)])
        second = root / "package other host"
        run(package + [second])
        assert contract.read_bytes() != identity
        assert hashlib.sha256((second / binary).read_bytes()).digest() != digest
        run([args.loader.resolve(), second, binary, "candidate-native-reject"])
        # A previous artifact is not enough to pass a failed generation/audit.
        malformed = root / "malformed-host"
        malformed.write_text("not ELF")
        run(configure + ["-DSAMPLE_HOST=" + str(malformed)])
        failed = root / "failed package"
        run(package + [failed], ok=False)
        assert (failed / ".seekdb-package-incomplete").is_file()
        assert not (failed / binary).exists()

        run(configure + ["-DSAMPLE_HOST=" + str(args.loader.resolve())])
        third = root / "package restored"
        run(package + [third])
        assert contract.read_bytes() == identity
        run([args.loader.resolve(), third, binary, "candidate-native"])

        # Shared profile parsing must not silently turn a public manifest into
        # a version-bound artifact, or accept undeclared additional exports.
        toml = source / "plugin.toml"
        original = toml.read_text()
        toml.write_text(original.replace('api_profile = "server-dev"', 'api_profile = "public"'))
        assert "SERVER_DEV_HOST requires" in run(configure, ok=False)
        toml.write_text(original)
        run(configure)
        lib = source / "src/lib.rs"
        lib.write_text(lib.read_text() + '\n#[no_mangle]\npub extern "C" fn extra_probe() -> u32 { 42 }\n')
        audit_failed = root / "undeclared export"
        assert "unexpected dynamic exports: extra_probe" in run(package + [audit_failed], ok=False)
        assert not (audit_failed / binary).exists()
        toml.write_text(original.replace("exports = []", 'exports = ["extra_probe"]'))
        declared = root / "declared export"
        run(package + [declared])
        run([args.loader.resolve(), declared, binary, "candidate-native"])
    print("Pure Rust Server-dev standalone package, construction callback, binding refresh and failure gates passed")


if __name__ == "__main__":
    main()
