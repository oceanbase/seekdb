#!/usr/bin/env bash
# Compare two packed libseekdb zips (e.g. local pack vs S3) and highlight differences.
#
# Usage:
#   ./diagnose-packed-artifact.sh package/libseekdb/libseekdb-darwin-arm64.zip
#   ./diagnose-packed-artifact.sh local.zip https://.../libseekdb-darwin-arm64.zip
#   ./diagnose-packed-artifact.sh local.zip /path/to/other.zip

set -euo pipefail

A="${1:?usage: $0 <zip-a> [zip-b-url-or-path]}"
B="${2:-}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

unpack() {
  local label="$1"
  local ref="$2"
  local dest="$WORK/$label"
  mkdir -p "$dest"
  if [[ -f "$ref" ]]; then
    unzip -q "$ref" -d "$dest/tree"
  else
    echo "[diagnose] downloading $ref"
    curl -fsSL "$ref" -o "$dest/archive.zip"
    unzip -q "$dest/archive.zip" -d "$dest/tree"
  fi
  echo "$dest/tree"
}

TREE_A="$(unpack a "$A")"
if [[ -n "$B" ]]; then
  TREE_B="$(unpack b "$B")"
else
  TREE_B=""
fi

python3 - "$TREE_A" "$TREE_B" <<'PY'
import hashlib, json, os, sys

def main_path(tree):
    for name in ("libseekdb.dylib", "libseekdb.so"):
        p = os.path.join(tree, name)
        if os.path.isfile(p):
            return p
    return None

def sha(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for c in iter(lambda: f.read(1 << 20), b""):
            h.update(c)
    return h.hexdigest()

def tree_digest(tree):
    libs = os.path.join(tree, "libs")
    files = {}
    for root, _, names in os.walk(tree):
        for n in names:
            rel = os.path.relpath(os.path.join(root, n), tree)
            if rel.endswith("/") or n == "pack-metadata.json":
                pass
            p = os.path.join(root, n)
            if os.path.isfile(p):
                files[rel] = {"bytes": os.path.getsize(p), "sha256": sha(p)}
    return files

def summarize(label, tree):
    print(f"\n=== {label}: {tree} ===")
    m = main_path(tree)
    if not m:
        print("  (no main library)")
        return
    print(f"  main: {os.path.basename(m)}")
    print(f"  bytes: {os.path.getsize(m)}")
    print(f"  sha256: {sha(m)}")
    meta = os.path.join(tree, "pack-metadata.json")
    if os.path.isfile(meta):
        with open(meta, encoding="utf-8") as f:
            md = json.load(f)
        print(f"  pack-metadata.generated_at: {md.get('generated_at')}")
        print(f"  pack-metadata.uname: {md.get('uname', '')[:120]}")
        ce = md.get("ci_env") or {}
        if ce:
            print(f"  ci_env: {ce}")
    libs = os.path.join(tree, "libs")
    if os.path.isdir(libs):
        print(f"  libs/: {len([x for x in os.listdir(libs) if x.endswith('.dylib') or x.endswith('.so')])} files")

tree_a = sys.argv[1]
summarize("A", tree_a)
tree_b = sys.argv[2] if len(sys.argv) > 2 and sys.argv[2] else ""
if tree_b:
    summarize("B", tree_b)
    fa, fb = tree_digest(tree_a), tree_digest(tree_b)
    only_a = sorted(set(fa) - set(fb))
    only_b = sorted(set(fb) - set(fa))
    diff = sorted(k for k in fa if k in fb and fa[k]["sha256"] != fb[k]["sha256"])
    print("\n=== diff summary ===")
    print(f"  only in A: {len(only_a)}")
    for x in only_a[:10]:
        print(f"    + {x}")
    print(f"  only in B: {len(only_b)}")
    for x in only_b[:10]:
        print(f"    + {x}")
    print(f"  same path, different content: {len(diff)}")
    for x in diff[:15]:
        print(f"    * {x}")
        print(f"      A {fa[x]['sha256'][:16]}... ({fa[x]['bytes']} bytes)")
        print(f"      B {fb[x]['sha256'][:16]}... ({fb[x]['bytes']} bytes)")
    if "libseekdb.dylib" in diff or "libseekdb.so" in diff:
        print("\n  >>> main library differs — likely different compile or pack input")
    if diff and all(x.startswith("libs/") for x in diff) and not any(
        x in ("libseekdb.dylib", "libseekdb.so") for x in diff
    ):
        print("\n  >>> only bundled libs differ — check dylibbundler / brew versions")
PY

echo ""
echo "Run smoke on A:"
echo "  $SCRIPT_DIR/test-packed-artifact-smoke.sh $(cd "$(dirname "$A")" && pwd)/$(basename "$A")"
