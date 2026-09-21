#!/usr/bin/env python3
"""Real Rust SDK policy + C ABI manifest, bound to the supplied linked host."""
import argparse
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[3]

def build(host, stage, cc="cc", variants=range(20)):
    stage = pathlib.Path(stage)
    stage.mkdir(parents=True, exist_ok=True)
    subprocess.run(["cargo", "build", "--offline", "--manifest-path", str(ROOT / "rust/extension-sdk/Cargo.toml")], check=True)
    subprocess.run(["cargo", "run", "--offline", "--manifest-path", str(ROOT / "rust/Cargo.toml"),
        "-p", "seekdb-plugin-runtime", "--example", "server_dev_contract", "--", str(host),
        str(stage / "server_dev_contract.h")], check=True)
    sdk_target = ROOT / "rust/extension-sdk/target/debug"
    archive = stage / "libcandidate.a"
    subprocess.run(["rustc", "--edition=2021", "--crate-type=staticlib", "-C", "opt-level=1",
        str(ROOT / "rust/plugin-runtime/tests/fixtures/candidate_plugin.rs"), "--extern",
        "seekdb_extension=" + str(sdk_target / "libseekdb_extension.rlib"),
        "-L", "dependency=" + str(sdk_target / "deps"), "-o", str(archive)], check=True)
    exports = stage / "exports.map"
    exports.write_text("{ global: seekdb_plugin_entry_v1; local: *; };\n")
    for variant in variants:
        binary = stage / f"candidate_{variant}.so"
        subprocess.run([cc, "-shared", "-fPIC", "-fvisibility=hidden", "-Werror", f"-DVARIANT={variant}",
            "-I" + str(ROOT / "include"), "-I" + str(stage),
            str(ROOT / "rust/plugin-runtime/tests/candidate_plugin.c"), str(archive),
            "-Wl,-z,defs", "-Wl,--version-script=" + str(exports), "-ldl", "-lpthread", "-lm",
            "-o", str(binary)], check=True)
        subprocess.run(["python3", str(ROOT / "cmake/plugin_binary_check.py"),
                        "--binary", str(binary), "--nm", "nm"], check=True)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--loader", type=pathlib.Path, required=True)
    parser.add_argument("--cc", default="cc")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="seekdb-candidate-") as temporary:
        stage = pathlib.Path(temporary)
        build(args.loader.resolve(), stage, args.cc)
        for variant in range(20):
            subprocess.run([str(args.loader.resolve()), str(stage), f"candidate_{variant}.so", f"candidate-{variant}"], check=True, timeout=30)

if __name__ == "__main__":
    main()
