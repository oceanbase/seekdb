#!/usr/bin/env python3
import hashlib
import json
from pathlib import Path
import sys

data = Path(sys.argv[1]).read_bytes()
spec = json.loads(Path(sys.argv[2]).read_text())["sources"]["icu"]
if hashlib.sha256(data).hexdigest() != spec["data_sha256"]:
    raise SystemExit("ICU Unicode data checksum mismatch")
with Path(sys.argv[3]).open("w") as out:
    out.write('_Alignas(16) const unsigned char icudt69_dat[] =\n')
    for start in range(0, len(data), 256):
        out.write('"' + ''.join('\\x%02x' % b for b in data[start:start + 256]) + '"\n')
    out.write(';\n')
