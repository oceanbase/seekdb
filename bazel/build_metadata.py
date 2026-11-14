import os
from pathlib import Path
import re
import subprocess
import sys


def escape_c_string(value):
    """Escape a value so it can be embedded verbatim in a C++ string literal.

    A Git branch name is allowed to contain double quotes and backslashes
    (see ``git check-ref-format``), so inserting the raw value between the
    surrounding quotes of ``return "<value>";`` would either produce invalid
    C++ or silently corrupt the value. Escape both characters exactly once so
    the generated metadata always compiles and preserves the original value at
    runtime.
    """
    return value.replace("\\", "\\\\").replace('"', '\\"')


def workspace_status():
    workspace = os.environ.get("BUILD_WORKSPACE_DIRECTORY", ".")
    for key, arguments in (
        ("GIT_REVISION", ["rev-parse", "HEAD"]),
        ("GIT_BRANCH", ["rev-parse", "--abbrev-ref", "HEAD"]),
    ):
        result = subprocess.run(
            ["git", "-C", workspace] + arguments,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            encoding="utf-8",
        )
        value = result.stdout.strip() if result.returncode == 0 else "unknown"
        sys.stdout.buffer.write(("STABLE_SEEKDB_%s %s\n" % (key, value)).encode("utf-8"))


def version_source(template, status_file, output):
    status = dict(
        line.split(" ", 1)
        for line in Path(status_file).read_text(encoding="utf-8").splitlines()
    )
    values = {
        "BUILD_NUMBER": "1",
        "GIT_REVISION": status["STABLE_SEEKDB_GIT_REVISION"],
        "GIT_BRANCH": status["STABLE_SEEKDB_GIT_BRANCH"],
        "BUILD_FLAGS": "RelWithDebInfo",
        "BUILD_INFO": "",
    }
    source = Path(template).read_text(encoding="utf-8")

    def _replace(match):
        return escape_c_string(values[match.group(1)])

    # Replace every @KEY@ placeholder in a single pass so that placeholder-like
    # text inside a metadata value (e.g. a branch named "fix/@BUILD_FLAGS@") is
    # treated as literal data instead of being substituted a second time.
    source = re.sub(r"@([A-Z_]+)@", _replace, source)
    with open(output, "w", encoding="utf-8", newline="\n") as stream:
        stream.write(source)


if __name__ == "__main__":
    if sys.argv[1] == "status":
        workspace_status()
    else:
        version_source(*sys.argv[2:])
