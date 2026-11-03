#!/usr/bin/env python3
"""Create an external plugin, test/build/audit/package it, then load the DSO.

Only test-owned temporary projects are modified. Catalog/verifier admission in
the native loader remains controlled, not live server deployment evidence.
"""
import argparse
import pathlib
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cargo", required=True)
    parser.add_argument("--loader", required=True, type=pathlib.Path)
    args = parser.parse_args()
    source = pathlib.Path(__file__).resolve().parents[3]
    cli = [args.cargo, "run", "--offline", "--manifest-path",
           str(source / "rust/cargo-seekdb/Cargo.toml"), "--", "seekdb"]

    def run(command, cwd=None, success=True):
        result = subprocess.run(command, cwd=cwd or source / "rust", text=True,
                                capture_output=True, timeout=120)
        if (result.returncode == 0) != success:
            raise AssertionError(result.stdout + result.stderr)
        return result

    with tempfile.TemporaryDirectory(prefix="seekdb-rust-scaffold-") as temporary:
        root = pathlib.Path(temporary)
        project = root / "generated project"
        create = cli + ["new", "generated", "--seekdb-root", str(source),
                        "--plugin-id", "org.seekdb.generated", "--output", str(project)]
        run(create)
        assert {p.name for p in project.iterdir()} == {
            "Cargo.toml", "CMakeLists.txt", "plugin.toml", "src", "README.md",
            "rust-toolchain.toml", ".gitignore", "examples"}
        before = {p.relative_to(project): p.read_bytes() for p in project.rglob("*") if p.is_file()}
        run(create, success=False)
        assert before == {p.relative_to(project): p.read_bytes() for p in project.rglob("*") if p.is_file()}
        assert (project / "rust-toolchain.toml").read_bytes() == (source / "rust/rust-toolchain.toml").read_bytes()
        run([args.cargo, "test", "--offline"], cwd=project)
        run([args.cargo, "clippy", "--offline", "--all-targets", "--", "-D", "warnings"], cwd=project)
        schema = root / "generated schema"
        schema_command = cli + ["schema", "--manifest-path", str(project / "Cargo.toml"), "--output", str(schema)]
        run(schema_command)
        assert {p.name for p in schema.iterdir()} == {"generated_ops.control", "generated_ops--1.0.sql"}
        assert (schema / "generated_ops.control").read_text() == "default_version = '1.0'\nnative_module = 'org.seekdb.generated'\n"
        assert (schema / "generated_ops--1.0.sql").read_text() == (
            "CREATE FUNCTION `generated_length`(`input_text` TEXT)\nRETURNS BIGINT\nDETERMINISTIC\n"
            "NO SQL\nSQL SECURITY INVOKER\nRETURN `generated_chars`(`input_text`);\n")
        schema_before = {p.name: p.read_bytes() for p in schema.iterdir()}
        run(schema_command, success=False)
        assert schema_before == {p.name: p.read_bytes() for p in schema.iterdir()}
        schema_link = root / "schema link"
        schema_link.symlink_to(root / "absent schema", target_is_directory=True)
        run(schema_command[:-1] + [str(schema_link)], success=False)
        assert schema_link.is_symlink() and not (root / "absent schema").exists()
        # A generator returning success without a package must not publish an
        # apparently complete directory. Modify only this test-owned example.
        generator = project / "examples/seekdb_schema.rs"
        generator_source = generator.read_text()
        generator.write_text("fn main() {}\n", encoding="utf-8")
        empty_schema = root / "empty schema"
        run(schema_command[:-1] + [str(empty_schema)], success=False)
        assert (empty_schema / ".seekdb-schema-incomplete").is_file()
        generator.write_text('fn main() { panic!("fixture failure"); }\n', encoding="utf-8")
        failed_schema = root / "failed schema"
        run(schema_command[:-1] + [str(failed_schema)], success=False)
        assert (failed_schema / ".seekdb-schema-incomplete").is_file()
        # A successful generator with invalid native metadata still fails the
        # shared runtime source reader and retains the incomplete marker.
        generator.write_text('''fn main() {
    let output = std::env::args_os().nth(1).unwrap();
    std::fs::write(std::path::Path::new(&output).join("bad.control"),
        "default_version = '1'\\ninstall_source = 'native'\\n").unwrap();
}
''', encoding="utf-8")
        invalid_native = root / "invalid native schema"
        invalid_result = run(schema_command[:-1] + [str(invalid_native)], success=False)
        assert "native installation requires native_module and default_version" in invalid_result.stderr, (
            invalid_result.stdout + invalid_result.stderr)
        assert (invalid_native / ".seekdb-schema-incomplete").is_file()
        generator.write_text(generator_source, encoding="utf-8")
        missing_example = root / "missing example schema"
        run(schema_command[:-1] + [str(missing_example), "--example", "missing_schema"], success=False)
        assert (missing_example / ".seekdb-schema-incomplete").is_file()
        build = root / "standalone build"
        run(["cmake", "-S", str(project), "-B", str(build)])
        package = root / "ready package"
        run(cli + ["package", "--build-dir", str(build), "--target", "seekdb_generated_plugin",
                   "--output", str(package)])
        libraries = [p for p in package.iterdir() if p.suffix in {".so", ".dylib", ".dll"}]
        assert len(libraries) == 1
        assert (package / "plugin.toml").read_bytes() == (project / "plugin.toml").read_bytes()
        assert not (package / ".seekdb-package-incomplete").exists()
        run([str(args.loader), str(package), libraries[0].name, "scaffold"])

        # After a valid binary exists, a private dependency outside the plugin
        # and SDK must STILL fail the always-run gate. Use a dependency-free
        # test-owned host crate so an offline registry miss cannot mask the rule.
        private = root / "private host crate"
        (private / "src").mkdir(parents=True)
        (private / "Cargo.toml").write_text('''[package]
name = "private-host"
version = "0.1.0"
edition = "2021"
[workspace]
''', encoding="utf-8")
        (private / "src/lib.rs").write_text("pub fn host_only() {}\n", encoding="utf-8")
        cargo = project / "Cargo.toml"
        cargo.write_text(cargo.read_text() + '\n[dependencies.private-host]\npath = "' +
                         private.as_posix() + '"\n', encoding="utf-8")
        failed_output = root / "must not deploy"
        rejected = run(cli + ["package", "--build-dir", str(build), "--target", "seekdb_generated_plugin",
                              "--output", str(failed_output)], success=False)
        assert "escapes" in rejected.stdout + rejected.stderr, rejected.stdout + rejected.stderr
        assert (failed_output / ".seekdb-package-incomplete").is_file()
        assert not (failed_output / "plugin.toml").exists()

        link = root / "existing link"
        link.symlink_to(root / "absent", target_is_directory=True)
        run(create[:-1] + [str(link)], success=False)
        assert link.is_symlink() and not (root / "absent").exists()
        missing = root / "invalid name output"
        run(cli + ["new", "../escape", "--seekdb-root", str(source), "--output", str(missing)], success=False)
        assert not missing.exists()

        # The external opt-in cannot turn host source directories into plugins.
        # Use a fake checkout so this rejection test never writes to real src/.
        fake = root / "fake seekdb"
        (fake / "cmake").mkdir(parents=True)
        shutil.copyfile(source / "cmake/RustPlugin.cmake", fake / "cmake/RustPlugin.cmake")
        core_project = fake / "src" / "not_plugin"
        core_project.mkdir(parents=True)
        (core_project / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.20)
project(RejectedCore NONE)
include("../../cmake/RustPlugin.cmake")
seekdb_add_rust_plugin(not_plugin STANDALONE LIBRARY_NAME not_plugin MANIFEST plugin.toml)
''', encoding="utf-8")
        rejected = run(["cmake", "-S", str(core_project), "-B", str(root / "rejected build")], success=False)
        assert "explicit external STANDALONE project root" in " ".join((rejected.stdout + rejected.stderr).split()), rejected.stdout + rejected.stderr

        for nested in (False, True):
            external = root / ("nested external" if nested else "missing opt in")
            external.mkdir()
            callsite = external / "child" if nested else external
            if nested:
                callsite.mkdir()
                (external / "CMakeLists.txt").write_text(
                    "cmake_minimum_required(VERSION 3.20)\nproject(External NONE)\nadd_subdirectory(child)\n", encoding="utf-8")
            prefix = "" if nested else "cmake_minimum_required(VERSION 3.20)\nproject(External NONE)\n"
            (callsite / "CMakeLists.txt").write_text(prefix +
                'include("' + (fake / "cmake/RustPlugin.cmake").as_posix() + '")\n' +
                'seekdb_add_rust_plugin(not_plugin ' + ("STANDALONE " if nested else "") +
                'LIBRARY_NAME not_plugin MANIFEST plugin.toml)\n', encoding="utf-8")
            rejected = run(["cmake", "-S", str(external), "-B", str(root / ("nested build" if nested else "no opt build"))], success=False)
            assert "explicit external STANDALONE project root" in " ".join((rejected.stdout + rejected.stderr).split()), rejected.stdout + rejected.stderr
    print("Generated Rust project tests, audited external build/package/native load, no-overwrite and host-boundary checks passed")


if __name__ == "__main__":
    main()
