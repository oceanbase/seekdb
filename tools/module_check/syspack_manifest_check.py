#!/usr/bin/env python3
"""Keep Bazel's syspack inputs synchronized with syspack_codegen.py."""

import ast
import runpy
import sys
from pathlib import Path


def _bazel_inputs(build_file):
    tree = ast.parse(build_file.read_text(encoding="utf-8"), filename=str(build_file))
    for node in tree.body:
        if not isinstance(node, ast.Assign):
            continue
        if any(
            isinstance(target, ast.Name) and target.id == "_SYSPACK_SQL_FILES"
            for target in node.targets
        ):
            return ast.literal_eval(node.value)
    raise ValueError("_SYSPACK_SQL_FILES is missing from %s" % build_file)


def _configured_inputs(codegen):
    namespace = runpy.run_path(str(codegen), run_name="syspack_manifest_check")
    inputs = []
    for package in namespace["syspack_config"]:
        inputs.append(package.header_file)
        if package.body_file:
            inputs.append(package.body_file)
    return inputs


def check(repo):
    package_dir = repo / "src/share/inner_table/sys_package"
    declared = _bazel_inputs(package_dir / "BUILD.bazel")
    configured = _configured_inputs(package_dir / "syspack_codegen.py")
    errors = []

    if len(declared) != len(set(declared)):
        errors.append("Bazel syspack manifest contains duplicate inputs")
    missing_from_bazel = sorted(set(configured) - set(declared))
    stale_in_bazel = sorted(set(declared) - set(configured))
    if missing_from_bazel:
        errors.append("syspack inputs missing from BUILD: %s" % missing_from_bazel)
    if stale_in_bazel:
        errors.append("stale syspack BUILD inputs: %s" % stale_in_bazel)
    missing_files = sorted(
        source for source in configured if not (package_dir / source).is_file()
    )
    if missing_files:
        errors.append("configured syspack files do not exist: %s" % missing_files)
    return errors, len(configured)


def main():
    repo = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
    errors, count = check(repo)
    if errors:
        for error in errors:
            print("[FAIL] " + error, file=sys.stderr)
        return 1
    print("syspack manifest: %d configured SQL files" % count)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
