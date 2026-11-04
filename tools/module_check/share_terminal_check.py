#!/usr/bin/env python3

import re
import sys
from pathlib import Path

UPPER_MODULES = {
    "data_plane",
    "logservice",
    "objit",
    "observer",
    "pl",
    "query",
    "rootserver",
    "sql",
    "storage",
}
CPP_SOURCE_SUFFIXES = {
    ".c",
    ".cc",
    ".cpp",
    ".cxx",
    ".h",
    ".hh",
    ".hpp",
    ".hxx",
    ".inc",
    ".ipp",
    ".tcc",
}
UPPER_INCLUDE_PATTERN = re.compile(
    r'^\s*#\s*include\s*[<"]('
    + "|".join(sorted(UPPER_MODULES))
    + r')(?:/|[>"])'
)


def _iter_share_cpp_files(repo):
    root = repo / "src/share"
    if not root.is_dir():
        return
    for path in sorted(root.rglob("*")):
        if (
            path.is_file()
            and not path.is_symlink()
            and path.suffix.lower() in CPP_SOURCE_SUFFIXES
        ):
            yield path


def check(repo):
    errors = set()
    share_root = repo / "src/share"
    if not share_root.is_dir():
        errors.add("%s: Share source tree is missing" % share_root)
        return errors
    for path in _iter_share_cpp_files(repo):
        relative = path.relative_to(repo)
        try:
            lines = path.read_text(encoding="utf-8").splitlines()
        except (OSError, UnicodeDecodeError) as error:
            errors.add("%s: cannot inspect Share source: %s" % (relative, error))
            continue
        for line_number, line in enumerate(lines, 1):
            include = UPPER_INCLUDE_PATTERN.search(line)
            if include:
                errors.add(
                    "%s:%d: Share source includes upper module %s"
                    % (relative, line_number, include.group(1))
                )
    return errors


def main():
    repo = (
        Path(sys.argv[1]).resolve()
        if len(sys.argv) > 1
        else Path(__file__).resolve().parents[2]
    )
    errors = check(repo)
    if errors:
        for error in sorted(errors):
            print("[FAIL] " + error, file=sys.stderr)
        print(
            "share terminal architecture check: %d violation(s)"
            % len(errors),
            file=sys.stderr,
        )
        return 1
    print("share terminal architecture: Share source upper-module include check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
