#!/usr/bin/env python3
"""Prepare the pinned Windows dirent header with sticky end-of-directory state."""

import hashlib
from pathlib import Path
import sys


ORIGINAL_SHA256 = "de26601714921e855f45c3318ef01ae19a892db3d77d0c9b769a4f5c5f8478a7"
PATCHED_SHA256 = "0a12fc6d3172c2254a27b598447dc313bf3a979fdc3da98a761e380cfc9a4425"
PATCHED_MIXED_EOL_SHA256 = "7d9bada78520dc2fe11e7a0976f166f61cb9c2ddf82743442dfcceb3aceaa2ca"


def prepare(source, destination):
    content = source.read_bytes()
    checksum = hashlib.sha256(content).hexdigest()
    if checksum == ORIGINAL_SHA256:
        content = content.replace(
            b"\t/* A directory entry is now waiting in memory */\n\tdirp->cached = 1;\n",
            b"\t/* A directory entry is now waiting in memory */\n\tdirp->cached = 1;\n\tdirp->invalid = 0;\n",
        ).replace(
            b"\t\t/* End of directory stream */\n\t\treturn NULL;\n",
            b"\t\t/* End of directory stream */\n\t\tdirp->invalid = 1;\n\t\treturn NULL;\n",
        )
    elif checksum == PATCHED_MIXED_EOL_SHA256:
        content = content.replace(b"\r\n", b"\n")
    elif checksum != PATCHED_SHA256:
        raise ValueError("unrecognized dirent SDK header SHA256: " + checksum)
    if hashlib.sha256(content).hexdigest() != PATCHED_SHA256:
        raise ValueError("dirent EOF patch output does not match the pinned header")
    destination.write_bytes(content)


if __name__ == "__main__":
    try:
        prepare(Path(sys.argv[1]), Path(sys.argv[2]))
    except (OSError, ValueError, IndexError) as error:
        print("ERROR: " + str(error), file=sys.stderr)
        sys.exit(1)
