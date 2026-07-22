#!/usr/bin/env bash
# Smoke-test a packed libseekdb zip the same way seekdb-js loads it:
#   <runtime>/libseekdb.dylib + <runtime>/libs/ + <runtime>/seekdb.node (@loader_path)
#
# The old flow used nodejs_napi linked to build_release (@rpath), which did NOT
# exercise the packaged zip layout and could pass while seekdb-js failed.
#
# Usage:
#   ./test-packed-artifact-smoke.sh package/libseekdb/libseekdb-darwin-arm64.zip

set -euo pipefail

ZIP="${1:?usage: $0 <libseekdb-*.zip>}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOP_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
LOADER_DIR="$SCRIPT_DIR/smoke-loader"
NAPI_DIR="$TOP_DIR/unittest/include/nodejs_napi"

if [[ ! -f "$ZIP" ]]; then
  echo "error: zip not found: $ZIP" >&2
  exit 1
fi

if ! command -v node >/dev/null 2>&1; then
  echo "error: node not found (install Node.js 18+)" >&2
  exit 1
fi

UNPACK_DIR="$(mktemp -d)"
trap 'rm -rf "$UNPACK_DIR"' EXIT

echo "[smoke] unpacking $ZIP -> $UNPACK_DIR"
unzip -q "$ZIP" -d "$UNPACK_DIR"

case "$(uname -s)" in
  Darwin) MAIN_NAME="libseekdb.dylib" ;;
  Linux)  MAIN_NAME="libseekdb.so" ;;
  *)
    echo "error: unsupported host OS: $(uname -s)" >&2
    exit 1
    ;;
esac

if [[ ! -f "$UNPACK_DIR/$MAIN_NAME" ]]; then
  echo "error: $MAIN_NAME not found in zip" >&2
  ls -la "$UNPACK_DIR" >&2
  exit 1
fi

if [[ -d "$UNPACK_DIR/libs" ]]; then
  echo "[smoke] layout: $MAIN_NAME + libs/ ($(find "$UNPACK_DIR/libs" -name '*.dylib' -o -name '*.so' | wc -l | tr -d ' ') deps)"
else
  echo "[smoke] layout: $MAIN_NAME (no libs/)"
fi

# --- Build seekdb.node INTO the unpack tree (@loader_path), matching seekdb-js ---
if [[ ! -d "$LOADER_DIR/node_modules" ]]; then
  echo "[smoke] npm install (smoke-loader, no lifecycle build)"
  (cd "$LOADER_DIR" && npm install --ignore-scripts)
fi

echo "[smoke] building seekdb.node into unpack dir (pack_dir=$UNPACK_DIR)"
(
  cd "$LOADER_DIR"
  npx node-gyp rebuild --pack_dir="$UNPACK_DIR"
)

if [[ ! -f "$UNPACK_DIR/seekdb.node" ]]; then
  echo "error: seekdb.node not produced in $UNPACK_DIR" >&2
  exit 1
fi

echo "[smoke] seekdb.node install_name / rpath:"
if [[ "$(uname -s)" == "Darwin" ]]; then
  otool -L "$UNPACK_DIR/seekdb.node" | head -5
else
  readelf -d "$UNPACK_DIR/seekdb.node" 2>/dev/null | grep -E 'RPATH|RUNPATH|NEEDED' | head -8 || true
  if ! readelf -d "$UNPACK_DIR/seekdb.node" 2>/dev/null | grep -qE 'RUNPATH|RPATH'; then
    echo "error: seekdb.node missing RUNPATH/RPATH; cannot load libseekdb.so from unpack dir" >&2
    exit 1
  fi
fi

# Ad-hoc sign like seekdb-js build:package (macOS only)
if [[ "$(uname -s)" == Darwin ]] && command -v codesign >/dev/null 2>&1; then
  echo "[smoke] codesign (ad-hoc) main + libs/"
  codesign --force --sign - "$UNPACK_DIR/$MAIN_NAME"
  if [[ -d "$UNPACK_DIR/libs" ]]; then
    for d in "$UNPACK_DIR/libs"/*; do
      [[ -f "$d" ]] || continue
      codesign --force --sign - "$d"
    done
  fi
  codesign --force --sign - "$UNPACK_DIR/seekdb.node"
fi

# --- Run the same vector/hybrid N-API tests, loading ./seekdb.node from unpack dir ---
DB_DIR="$UNPACK_DIR/smoke-seekdb.db"
rm -rf "$DB_DIR"

if [[ ! -d "$NAPI_DIR/node_modules" ]]; then
  echo "[smoke] npm install (nodejs_napi test.js)"
  (cd "$NAPI_DIR" && npm install)
fi

echo "[smoke] vsag + hybrid search (seekdb-js-critical path)"
(
  cd "$UNPACK_DIR"
  node "$LOADER_DIR/smoke-vsag.js" "$DB_DIR"
)

# Optional: full nodejs_napi suite (can SIGSEGV on some macOS builds at exit; not required for pack gate)
if [[ "${SMOKE_FULL_NAPI:-0}" == "1" ]]; then
  echo "[smoke] running full nodejs_napi test.js (SMOKE_FULL_NAPI=1)"
  export SEEKDB_NODE_NAPI_SKIP_HEAVY=0
  (
    cd "$UNPACK_DIR"
    node "$NAPI_DIR/test.js" "$DB_DIR" "test"
  )
fi

if [[ -n "${SEEKDB_JS_ROOT:-}" ]] && [[ -x "$SCRIPT_DIR/test-packed-artifact-smoke-js.sh" ]]; then
  echo "[smoke] SEEKDB_JS_ROOT set — running seekdb-js embedded smoke"
  SEEKDB_JS_ROOT="$SEEKDB_JS_ROOT" "$SCRIPT_DIR/test-packed-artifact-smoke-js.sh" "$ZIP"
fi

echo "[smoke] passed (seekdb-js-compatible load path + vsag)"
