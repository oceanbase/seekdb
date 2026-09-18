#!/usr/bin/env python3
"""Compile the public header as C11 and execute against a real Rust archive (Linux)."""
# Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

CRATE = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, help="use an existing server libsql_nio.a")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="embedding-c-abi-") as directory:
        if args.archive:
            archive = args.archive.resolve()
        else:
            subprocess.run(["cargo", "build", "--offline", "--locked", "-p", "embedding-response-ffi",
                            "--profile", "cmake-debug", "--target-dir", directory],
                           cwd=CRATE.parent, check=True)
            archive = Path(directory) / "cmake-debug/libembedding_response_ffi.a"
        executable = Path(directory) / "c-abi-test"
        subprocess.run(shlex.split(os.environ.get("CC", "cc")) + [
            "-std=c11", "-Wall", "-Wextra", "-Werror", "-I" + str(CRATE / "include"),
            str(CRATE / "tests/c_abi_test.c"), str(archive), "-lpthread", "-ldl", "-lm",
            "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
