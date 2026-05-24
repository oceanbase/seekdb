#!/usr/bin/env bash
# Write pack-metadata.json next to the packed libseekdb (before zipping).
# Usage: collect-pack-metadata.sh <pack_dir> [output_json]

set -euo pipefail

PACK_DIR="${1:?usage: $0 <pack_dir> [output_json]}"
OUT="${2:-$PACK_DIR/pack-metadata.json}"

MAIN=""
if [[ -f "$PACK_DIR/libseekdb.dylib" ]]; then
  MAIN="$PACK_DIR/libseekdb.dylib"
elif [[ -f "$PACK_DIR/libseekdb.so" ]]; then
  MAIN="$PACK_DIR/libseekdb.so"
else
  echo "error: no libseekdb.dylib/.so in $PACK_DIR" >&2
  exit 1
fi

python3 - "$PACK_DIR" "$MAIN" "$OUT" <<'PY'
import json, os, platform, subprocess, hashlib, sys
from datetime import datetime, timezone

pack_dir, main_lib, out_path = sys.argv[1:4]

def run(cmd):
    try:
        return subprocess.check_output(cmd, stderr=subprocess.STDOUT, text=True).strip()
    except Exception as e:
        return f"<error: {e}>"

def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()

libs_dir = os.path.join(pack_dir, "libs")
libs = []
if os.path.isdir(libs_dir):
    for name in sorted(os.listdir(libs_dir)):
        if name.endswith((".dylib", ".so")):
            p = os.path.join(libs_dir, name)
            libs.append({"name": name, "bytes": os.path.getsize(p), "sha256": sha256(p)})

meta = {
    "generated_at": datetime.now(timezone.utc).isoformat(),
    "platform": {
        "system": platform.system(),
        "machine": platform.machine(),
        "platform": platform.platform(),
        "python": platform.python_version(),
    },
    "uname": run(["uname", "-a"]),
    "main_library": {
        "path": os.path.basename(main_lib),
        "bytes": os.path.getsize(main_lib),
        "sha256": sha256(main_lib),
        "file": run(["file", "-b", main_lib]),
    },
    "libs_count": len(libs),
    "libs": libs,
    "otool_L": run(["otool", "-L", main_lib]) if platform.system() == "Darwin" else None,
    "readelf_dynamic": run(["readelf", "-d", main_lib]) if platform.system() == "Linux" else None,
    "ci_env": {
        k: os.environ.get(k)
        for k in (
            "GITHUB_ACTIONS",
            "GITHUB_SHA",
            "RUNNER_OS",
            "RUNNER_ARCH",
            "CMAKE_OSX_DEPLOYMENT_TARGET",
            "BUILD_TYPE",
            "ARCH",
        )
        if os.environ.get(k)
    },
}

if platform.system() == "Darwin":
    meta["sw_vers"] = run(["sw_vers"])
    meta["otool_minos"] = run(["otool", "-l", main_lib])  # includes LC_BUILD_VERSION / minos

with open(out_path, "w", encoding="utf-8") as f:
    json.dump(meta, f, indent=2)
    f.write("\n")
print(out_path)
PY

echo "[metadata] wrote $OUT"
