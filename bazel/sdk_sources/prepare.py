#!/usr/bin/env python3
import hashlib
import json
from pathlib import Path
import sys

manifest = json.loads(Path(sys.argv[1]).read_text())
package, profile = sys.argv[2:]
patches = manifest["patches"]
selected = []
if package == "boost":
    selected += patches["boost_ios_numeric"]["files"]
for patch in selected:
    path = Path(patch["path"])
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != patch["input_sha256"]:
        raise SystemExit(f"Patch input checksum mismatch: {path}")
    for before, after in patch["replacements"]:
        before, after = before.encode(), after.encode()
        if before not in data:
            raise SystemExit(f"Patch context mismatch: {path}")
        data = data.replace(before, after)
    data = patch.get("prepend", "").encode() + data
    if hashlib.sha256(data).hexdigest() != patch["output_sha256"]:
        raise SystemExit(f"Patch output checksum mismatch: {path}")
    path.write_bytes(data)
if package == "roaring":
    for path in Path("cpp").glob("*.hh"):
        Path("include/roaring", path.name).symlink_to("../../" + str(path))
Path("source.json").write_text(json.dumps({
    "package": package,
    "profile": profile,
    **manifest["sources"][package],
    "patches": selected,
}, indent=2) + "\n")
