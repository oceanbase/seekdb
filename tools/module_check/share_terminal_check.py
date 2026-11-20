#!/usr/bin/env python3
# Copyright (c) 2026 OceanBase.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Check that the terminal ``share`` module does not depend on upper modules.

``share`` is the lowest module below the SQL facing modules: ``sql``,
``observer``, ``rootserver``, ``pl``, ``storage`` and the remaining upper
modules all include ``share`` headers. A ``share`` source file that includes
an upper module header introduces an upward dependency and turns the terminal
module into a non terminal one.

The check is intentionally simple and dependency free so that it can run in
CI without a build of the server.

Usage:
    python3 tools/module_check/share_terminal_check.py [repo_root]
"""

import argparse
import os
import re
import sys


# Modules that are above ``share``. Every one of them includes ``share``
# headers today, so ``share`` must not include any of them back.
UPPER_MODULES = (
    "data_plane",
    "logservice",
    "observer",
    "pl",
    "query",
    "rootserver",
    "sql",
    "standby",
    "storage",
    "wasm",
)

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^">]+)[>"]')
SOURCE_SUFFIXES = (".c", ".cc", ".cpp", ".h", ".hpp", ".ipp")


class CheckError(RuntimeError):
    """Raised when the repository cannot be inspected reliably."""


def iter_sources(root):
    share_dir = os.path.join(root, "src", "share")
    if not os.path.isdir(share_dir):
        raise CheckError("cannot find the share module under %s" % root)
    for dirpath, dirnames, filenames in os.walk(share_dir):
        dirnames.sort()
        for filename in sorted(filenames):
            if filename.endswith(SOURCE_SUFFIXES):
                yield os.path.join(dirpath, filename)


def check_file(path, root):
    """Return the list of upward dependencies found in one file."""
    violations = []
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for lineno, line in enumerate(handle, start=1):
            match = INCLUDE_RE.match(line)
            if match is None:
                continue
            include = match.group(1)
            top = include.split("/")[0]
            if top in UPPER_MODULES:
                violations.append((lineno, include, top))
    return violations


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "repo_root",
        nargs="?",
        default=os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")),
        help="path to the seekdb repository root",
    )
    args = parser.parse_args(argv)

    root = os.path.abspath(args.repo_root)
    try:
        sources = list(iter_sources(root))
    except CheckError as error:
        print("[ERROR] %s" % error, file=sys.stderr)
        return 2

    violation_count = 0
    for path in sources:
        relative = os.path.relpath(path, root).replace(os.sep, "/")
        for lineno, include, top in check_file(path, root):
            violation_count += 1
            print("[FAIL] %s:%d: Share source includes upper module %s (%s)"
                  % (relative, lineno, top, include))

    if violation_count:
        print("share terminal architecture check: %d violation(s)" % violation_count)
        return 1

    print("share terminal architecture check: 0 violation(s) in %d file(s)" % len(sources))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
