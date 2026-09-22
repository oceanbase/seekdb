#!/usr/bin/env python3
"""Exercise cargo-seekdb with the audited Rust plugin and real native loader.

No live catalog/server, signatures or publication. Failure uses a separate
generated CMake project so the real build tree and existing outputs stay intact.
"""
import argparse
import hashlib
import os
import pathlib
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cargo", required=True)
    parser.add_argument("--build-dir", required=True, type=pathlib.Path)
    parser.add_argument("--artifact", required=True, type=pathlib.Path)
    parser.add_argument("--loader", required=True, type=pathlib.Path)
    args = parser.parse_args()
    source = pathlib.Path(__file__).resolve().parents[3]
    command = [args.cargo, "run", "--offline", "--manifest-path",
               str(source / "rust/cargo-seekdb/Cargo.toml"), "--", "seekdb", "package"]

    def invoke(build, target, output, success=True, env=None):
        result = subprocess.run(command + ["--build-dir", str(build), "--target", target,
                                "--output", str(output)], text=True, capture_output=True, env=env,
                                cwd=source / "rust")
        if (result.returncode == 0) != success:
            raise AssertionError(result.stdout + result.stderr)
        return result

    def digest(path):
        with path.open("rb") as stream:
            result = hashlib.sha256()
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                result.update(block)
        return result.digest()

    with tempfile.TemporaryDirectory(prefix="seekdb-rust-package-") as temporary:
        root = pathlib.Path(temporary)
        output = root / "package with spaces;literal"
        environment = os.environ.copy()
        environment["DESTDIR"] = str(root / "must-not-install-here")
        invoke(args.build_dir, "seekdb_rust_text_plugin", output, env=environment)
        assert not (root / "must-not-install-here").exists()
        assert not (output / ".seekdb-package-incomplete").exists()
        library = output / args.artifact.name
        assert {path.name for path in output.iterdir()} == {library.name, "plugin.toml"}
        assert digest(library) == digest(args.artifact)
        assert (output / "plugin.toml").read_bytes() == (source / "plugins/rust_text/plugin.toml").read_bytes()
        subprocess.run([str(args.loader), str(output), library.name, "rust"], check=True)
        before = {path.name: digest(path) for path in output.iterdir()}
        invoke(args.build_dir, "seekdb_rust_text_plugin", output, success=False)
        assert {path.name: digest(path) for path in output.iterdir()} == before
        absent = root / "missing-target-output"
        invoke(args.build_dir, "not_a_plugin", absent, success=False)
        assert not absent.exists()
        link = root / "existing-symlink"
        link.symlink_to(root / "absent")
        invoke(args.build_dir, "seekdb_rust_text_plugin", link, success=False)
        assert link.is_symlink() and not (root / "absent").exists()

        failing = root / "failing-source"
        failing.mkdir()
        (failing / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.20)
project(PackageFailure NONE)
add_custom_target(fail_plugin COMMAND "${CMAKE_COMMAND}" -E false)
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/seekdb-plugin-packages/fail_plugin.cmake"
  CONTENT "message(FATAL_ERROR \\"install must not run after failed build\\")\\n")
''', encoding="utf-8")
        failed_build = root / "failing-build"
        subprocess.run(["cmake", "-S", str(failing), "-B", str(failed_build)], check=True)
        incomplete = root / "incomplete"
        failed = invoke(failed_build, "fail_plugin", incomplete, success=False)
        assert "install must not run after failed build" not in failed.stdout + failed.stderr
        assert (incomplete / ".seekdb-package-incomplete").is_file()
        assert not (incomplete / "plugin.toml").exists()
    print("Rust package build/audit/copy/load, no-overwrite and retained-failure checks passed")


if __name__ == "__main__":
    main()
