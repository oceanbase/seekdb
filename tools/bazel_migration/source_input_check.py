#!/usr/bin/env python3
"""Reject missing checked-in source files before Bazel starts compiling."""

import sys
from pathlib import Path, PurePosixPath


def check(repo: Path, labels):
    missing = []
    invalid = []
    for raw_label in labels:
        label = raw_label.strip()
        if not label or label.startswith("@"):
            continue
        if not label.startswith("//") or ":" not in label:
            invalid.append(label)
            continue
        package, name = label[2:].split(":", 1)
        relative = PurePosixPath(package) / PurePosixPath(name)
        if relative.is_absolute() or ".." in relative.parts:
            invalid.append(label)
            continue
        if not (repo / Path(relative)).is_file():
            missing.append((label, relative.as_posix()))
    return invalid, missing


def main():
    repo = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
    invalid, missing = check(repo, sys.stdin)
    if invalid:
        for label in invalid:
            print("[FAIL] invalid main-workspace source label: %s" % label, file=sys.stderr)
    if missing:
        for label, path in missing:
            print("[FAIL] missing source file: %s -> %s" % (label, path), file=sys.stderr)
    if invalid or missing:
        return 1
    print("Bazel production source closure contains no missing checked-in files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
