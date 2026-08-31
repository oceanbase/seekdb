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

"""Check covered source files added by a pull request for an Apache 2.0 header."""

import argparse
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys


HEADER_MAX_BYTES = 16 * 1024
HEADER_MAX_LINES = 80

# The current repository baseline has standard headers on these first-party
# source formats. Script, build, generated, and configuration formats are
# deliberately not covered when their existing files do not carry that header.
REQUIRED_SUFFIXES = frozenset(
    {
        ".c",
        ".cc",
        ".cpp",
        ".h",
        ".hpp",
        ".ipp",
        ".rs",
    }
)

TEMPLATE_SUFFIXES = frozenset({".in", ".template", ".tpl"})

COPYRIGHT_RE = re.compile(
    rb"Copyright\s*\(c\)\s*20\d{2}(?:\s*-\s*20\d{2})?\s+OceanBase"
    rb"(?:\s+Inc\.)?\.?",
    re.IGNORECASE,
)
APACHE_LICENSE_RE = re.compile(
    rb"Licensed under the Apache License, Version 2\.0",
    re.IGNORECASE,
)


class CheckError(RuntimeError):
    """Raised when the repository cannot be inspected reliably."""


class GitRepository:
    """Read commit and blob data without following worktree symlinks."""

    def __init__(self, root):
        self.root = Path(root).resolve()

    def git(self, *args):
        command = ["git"] + list(args)
        result = subprocess.run(
            command,
            cwd=str(self.root),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        if result.returncode != 0:
            detail = result.stderr.decode("utf-8", "replace").strip()
            raise CheckError("{} failed: {}".format(" ".join(command), detail))
        return result.stdout

    def resolve_commit(self, ref):
        output = self.git(
            "rev-parse", "--verify", "--end-of-options", "{}^{{commit}}".format(ref)
        )
        return output.decode("ascii").strip()

    def added_paths(self, base_ref, head_ref):
        base_commit = self.resolve_commit(base_ref)
        head_commit = self.resolve_commit(head_ref)
        merge_base = (
            self.git("merge-base", base_commit, head_commit)
            .decode("ascii")
            .strip()
        )
        output = self.git(
            "diff",
            "--name-only",
            "--diff-filter=A",
            "--find-renames=50%",
            "-z",
            merge_base,
            head_commit,
            "--",
        )
        paths = [
            item.decode("utf-8", "surrogateescape")
            for item in output.split(b"\0")
            if item
        ]
        return head_commit, sorted(paths)

    def read_blob(self, commit, path):
        object_spec = "{}:{}".format(commit, path)
        object_id = self.git(
            "rev-parse", "--verify", "--end-of-options", object_spec
        ).decode("ascii").strip()
        object_type = self.git("cat-file", "-t", object_id).decode("ascii").strip()
        if object_type != "blob":
            return None
        return self.git("cat-file", "blob", object_id)


def header_region(content):
    """Return the bounded beginning of a file where a header may appear."""

    prefix = content[:HEADER_MAX_BYTES]
    return b"\n".join(prefix.splitlines()[:HEADER_MAX_LINES])


def has_license_header(content):
    header = header_region(content)
    return bool(COPYRIGHT_RE.search(header) and APACHE_LICENSE_RE.search(header))


def is_required_source_file(path):
    pure_path = PurePosixPath(path)
    while pure_path.suffix.lower() in TEMPLATE_SUFFIXES:
        pure_path = pure_path.with_suffix("")
    return pure_path.suffix.lower() in REQUIRED_SUFFIXES


def check_added_files(repository, base_ref, head_ref):
    head_commit, added_paths = repository.added_paths(base_ref, head_ref)
    checked = []
    missing = []
    for path in added_paths:
        if not is_required_source_file(path):
            continue
        content = repository.read_blob(head_commit, path)
        if content is None:
            continue
        checked.append(path)
        if not has_license_header(content):
            missing.append(path)
    return checked, missing


def printable_path(path):
    return path.encode("utf-8", "backslashreplace").decode("utf-8")


def github_property(value):
    return (
        value.replace("%", "%25")
        .replace("\r", "%0D")
        .replace("\n", "%0A")
        .replace(":", "%3A")
        .replace(",", "%2C")
    )


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--base-ref",
        required=True,
        help="base commit or ref for the pull request",
    )
    parser.add_argument(
        "--head-ref",
        default="HEAD",
        help="pull-request head commit or ref (default: HEAD)",
    )
    parser.add_argument(
        "--repo-root",
        default=".",
        help="path inside the Git repository (default: current directory)",
    )
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_args(argv)
    try:
        repository = GitRepository(args.repo_root)
        checked, missing = check_added_files(
            repository, args.base_ref, args.head_ref
        )
    except CheckError as error:
        print("license-header check could not run: {}".format(error), file=sys.stderr)
        return 2

    if not checked:
        print(
            "No newly added source files use a suffix covered by the "
            "repository's license-header policy."
        )
        return 0

    if not missing:
        print(
            "License headers are valid in all {} new source file(s).".format(
                len(checked)
            )
        )
        return 0

    print("The following new source files are missing the required license header:")
    for path in missing:
        display_path = printable_path(path)
        print("  - {}".format(display_path))
        if os.environ.get("GITHUB_ACTIONS") == "true":
            print(
                "::error file={}::New source files must have the OceanBase Apache "
                "2.0 license header.".format(github_property(display_path))
            )

    print()
    print("Add the repository's standard header near the beginning of each file:")
    print("  Copyright (c) YYYY OceanBase.")
    print('  Licensed under the Apache License, Version 2.0 (the "License");')
    return 1


if __name__ == "__main__":
    sys.exit(main())
