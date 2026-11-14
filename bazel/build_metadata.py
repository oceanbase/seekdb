import os
from pathlib import Path
import re
import subprocess
import sys


# Placeholders are expanded in one pass over the template. Replacing them one
# key at a time re-scans the text inserted by earlier replacements, so a
# metadata value that happens to contain something like "@BUILD_FLAGS@" would
# be expanded as if it were a placeholder.
PLACEHOLDER = re.compile(r"@([A-Za-z0-9_]+)@")


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


def escape_cpp_string(value):
    """Escapes a value so it can be embedded in a C++ string literal.

    Version metadata is interpolated into the double-quoted literals of
    src/share/ob_version.cpp.in. Git allows double quotes and backslashes in
    branch names, which would otherwise terminate or corrupt the literal.
    """
    escaped = []
    for character in value:
        if character == "\\":
            escaped.append("\\\\")
        elif character == '"':
            escaped.append('\\"')
        elif character == "\n":
            escaped.append("\\n")
        elif character == "\r":
            escaped.append("\\r")
        elif character == "\t":
            escaped.append("\\t")
        elif ord(character) < 0x20 or ord(character) == 0x7f:
            # Octal escapes are at most three digits, so they cannot consume
            # the character that follows them.
            escaped.append("\\%03o" % ord(character))
        else:
            escaped.append(character)
    return "".join(escaped)


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
    escaped = {key: escape_cpp_string(value) for key, value in values.items()}
    source = Path(template).read_text(encoding="utf-8")
    source = PLACEHOLDER.sub(
        lambda match: escaped.get(match.group(1), match.group(0)), source
    )
    with open(output, "w", encoding="utf-8", newline="\n") as stream:
        stream.write(source)


if __name__ == "__main__":
    if sys.argv[1] == "status":
        workspace_status()
    else:
        version_source(*sys.argv[2:])
