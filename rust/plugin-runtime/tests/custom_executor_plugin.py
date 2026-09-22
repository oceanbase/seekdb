#!/usr/bin/env python3
"""Raw ABI fault matrix, independent of the Rust SDK's defensive wrappers."""
import argparse
import pathlib
import subprocess
import tempfile

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--loader", type=pathlib.Path, required=True)
    parser.add_argument("--cc", required=True)
    args = parser.parse_args()
    repo = pathlib.Path(__file__).resolve().parents[3]
    with tempfile.TemporaryDirectory(prefix="seekdb-custom-executor-") as temporary:
        stage = pathlib.Path(temporary)
        subprocess.run(["cargo", "run", "--offline", "--manifest-path", str(repo / "rust/Cargo.toml"),
            "-p", "seekdb-plugin-runtime", "--example", "server_dev_contract", "--",
            str(args.loader.resolve()), str(stage / "server_dev_contract.h")], check=True)
        for variant in range(47):
            binary = stage / f"custom_{variant}.so"
            subprocess.run([args.cc, "-shared", "-fPIC", "-fvisibility=hidden", "-Werror", f"-DVARIANT={variant}",
                "-I" + str(repo / "include"), "-I" + str(stage),
                str(repo / "rust/plugin-runtime/tests/custom_executor_plugin.c"), "-Wl,-z,defs", "-o", str(binary)], check=True)
            subprocess.run(["python3", str(repo / "cmake/plugin_binary_check.py"), "--binary", str(binary), "--nm", "nm"], check=True)
            subprocess.run([str(args.loader.resolve()), str(stage), binary.name, f"custom-{variant}"], check=True, timeout=30)
    print("custom executor raw ABI protocol, exact errors, generation binding and lease lifetime passed")

if __name__ == "__main__":
    main()
