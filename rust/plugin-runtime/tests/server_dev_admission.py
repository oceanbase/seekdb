#!/usr/bin/env python3
# Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
"""Build DSOs against the actual test host's linked identity, then load them."""
import argparse
import pathlib
import subprocess
import tempfile

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--loader", required=True, type=pathlib.Path)
    parser.add_argument("--cc", required=True)
    parser.add_argument("--cargo", required=True)
    args = parser.parse_args()
    source = pathlib.Path(__file__).resolve().parents[3]
    with tempfile.TemporaryDirectory(prefix="seekdb-server-dev-") as temporary:
        stage = pathlib.Path(temporary)
        header = stage / "server_dev_contract.h"
        generate = [args.cargo, "run", "--offline", "--manifest-path", str(source / "rust/Cargo.toml"),
                    "-p", "seekdb-plugin-runtime", "--example", "server_dev_contract", "--",
                    str(args.loader.resolve()), str(header)]
        subprocess.run(generate, check=True, timeout=120)
        before = header.read_bytes()
        if subprocess.run(generate, capture_output=True, timeout=120).returncode == 0:
            raise AssertionError("generator overwrote an existing contract")
        assert header.read_bytes() == before
        for variant in range(7):
            binary = stage / f"server_dev_{variant}.so"
            subprocess.run([args.cc, "-shared", "-fPIC", "-Werror", "-fvisibility=hidden", f"-DVARIANT={variant}",
                "-I" + str(source / "include"), "-I" + str(stage),
                str(source / "rust/plugin-runtime/tests/server_dev_fixture.c"), "-o", str(binary)],
                check=True, timeout=30)
            subprocess.run([str(args.loader.resolve()), str(stage), binary.name,
                "serverdev" if variant == 0 else "serverdev-reject"], check=True, timeout=30)
    print("linked-host Server-dev admission, pre-init rejection and contract generation passed")

if __name__ == "__main__":
    main()
