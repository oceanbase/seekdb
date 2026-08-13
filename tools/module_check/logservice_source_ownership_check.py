#!/usr/bin/env python3
"""Verify that Logservice's Unity inventory owns every checked-in source once."""

import re
import subprocess
import sys
from collections import Counter
from pathlib import Path


SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx"}
EXPECTED_GROUPS = {
    "ob_logservice_common_0",
    "ob_logservice_common_mixed_0",
    "ob_logservice_common_util_0",
    "ob_logservice_palf_0",
    "ob_logservice_palf_1",
}
EXPECTED_SOURCE_COUNT = 58


def _tracked_sources(repo):
    output = subprocess.check_output(
        [
            "git",
            "ls-files",
            "--cached",
            "--others",
            "--exclude-standard",
            "src/logservice",
        ],
        cwd=repo,
        universal_newlines=True,
    )
    return {
        path
        for path in output.splitlines()
        if Path(path).suffix in SOURCE_SUFFIXES and (repo / path).is_file()
    }


def _inventory(repo):
    manifest = (repo / "src/logservice/logservice_build_defs.bzl").read_text(
        encoding="utf-8"
    )
    groups = set(re.findall(r'name = "(ob_logservice_[^"]+)"', manifest))
    sources = re.findall(
        r'"(src/logservice/[^"\n]+\.(?:c|cc|cpp|cxx))"',
        manifest,
    )
    return groups, sources


def check(repo):
    groups, owned = _inventory(repo)
    tracked = _tracked_sources(repo)
    owned_set = set(owned)
    errors = []

    if groups != EXPECTED_GROUPS:
        errors.append(
            "Unity groups differ: missing=%s extra=%s"
            % (sorted(EXPECTED_GROUPS - groups), sorted(groups - EXPECTED_GROUPS))
        )
    duplicates = sorted(path for path, count in Counter(owned).items() if count > 1)
    if duplicates:
        errors.append("duplicate inventory sources: %s" % duplicates)
    missing = sorted(tracked - owned_set)
    stale = sorted(owned_set - tracked)
    if missing:
        errors.append("unowned checked-in Logservice sources: %s" % missing)
    if stale:
        errors.append("stale Logservice inventory sources: %s" % stale)
    if len(owned) != EXPECTED_SOURCE_COUNT:
        errors.append(
            "Logservice inventory has %d sources, expected %d"
            % (len(owned), EXPECTED_SOURCE_COUNT)
        )
    return errors


def main():
    repo = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
    errors = check(repo)
    if errors:
        for error in errors:
            print("[FAIL] " + error, file=sys.stderr)
        return 1
    print(
        "logservice source ownership: %d sources in %d Unity groups"
        % (EXPECTED_SOURCE_COUNT, len(EXPECTED_GROUPS))
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
