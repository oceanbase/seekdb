#!/usr/bin/env python3
# Copyright (c) 2026 OceanBase.
# SPDX-License-Identifier: Apache-2.0
"""Reuse the production inventory translator for the standalone Wasm project."""
import argparse
from pathlib import Path
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    repo = args.repo.resolve()
    sys.path.insert(0, str(repo / "tools/cmake"))
    from emit_bazel_source_inventory import emit
    # The native entry point is at the repository root; the Wasm project is
    # below src/wasm. Preserve the generated lists and remap only their root.
    emit(repo, args.output)
    args.output.write_text(args.output.read_text().replace(
        "${CMAKE_SOURCE_DIR}/", "${SEEKDB_ROOT}/"))


if __name__ == "__main__":
    main()
