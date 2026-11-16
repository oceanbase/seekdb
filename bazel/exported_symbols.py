#!/usr/bin/env python3
"""Emit the libseekdb export list from the public C API header.

The embedded library hides every symbol except the seekdb_* API, so the
linker needs an explicit allow list. Linux/Android read it through a version
script (see seekdb.version); macOS and Windows need a generated file:

  macos   one "_seekdb_xxx" symbol per line, for -exported_symbols_list
  windows an "EXPORTS" section, for lld-link /DEF

Usage: exported_symbols.py <macos|windows> <seekdb.h> <output>
"""

import re
import sys

_COMMENT = re.compile(r"/\*.*?\*/|//[^\n]*", re.DOTALL)
_PREPROCESSOR = re.compile(r"^[ \t]*#[^\n]*", re.MULTILINE)
# A declaration or definition of the public API, e.g. "int seekdb_open(".
_SYMBOL = re.compile(r"\b(seekdb_[A-Za-z0-9_]+)[ \t\r\n]*\(")


def exported_symbols(header):
    text = _PREPROCESSOR.sub("", _COMMENT.sub("", header))
    return sorted({match.group(1) for match in _SYMBOL.finditer(text)})


def render(format, symbols):
    if not symbols:
        raise SystemExit("no seekdb_* declaration found in the header")
    if format == "macos":
        # Mach-O prefixes C symbols with an underscore.
        return "".join("_" + symbol + "\n" for symbol in symbols)
    if format == "windows":
        return "EXPORTS\n" + "".join(symbol + "\n" for symbol in symbols)
    raise SystemExit("unknown format: " + format)


def main(arguments):
    if len(arguments) != 3:
        raise SystemExit("usage: exported_symbols.py <macos|windows> <seekdb.h> <output>")
    format, header_path, output_path = arguments
    with open(header_path, encoding="utf-8") as stream:
        header = stream.read()
    with open(output_path, "w", encoding="utf-8", newline="\n") as stream:
        stream.write(render(format, exported_symbols(header)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
