#!/usr/bin/env bash
# Smoke-test a packed libseekdb zip (post libseekdb-build.sh).
# Loads the packaged dylib/so (with libs/ on macOS) via nodejs_napi unittest.
# Catches regressions that only appear after dylibbundler/codesign, which
# pre-pack binding tests do not exercise.
#
# Usage:
#   ./test-packed-artifact-smoke.sh package/libseekdb/libseekdb-darwin-arm64.zip
#   ./test-packed-artifact-smoke.sh package/libseekdb/libseekdb-linux-x64.zip

set -euo pipefail

ZIP="${1:?usage: $0 <libseekdb-*.zip>}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOP_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
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

echo "[smoke] unpacking $ZIP"
unzip -q "$ZIP" -d "$UNPACK_DIR"

case "$(uname -s)" in
  Darwin)
    MAIN_LIB="$UNPACK_DIR/libseekdb.dylib"
    ;;
  Linux)
    MAIN_LIB="$UNPACK_DIR/libseekdb.so"
    ;;
  *)
    echo "error: unsupported host OS for smoke test: $(uname -s)" >&2
    exit 1
    ;;
esac

if [[ ! -f "$MAIN_LIB" ]]; then
  echo "error: main library not found in zip (expected libseekdb.dylib or libseekdb.so)" >&2
  ls -la "$UNPACK_DIR" >&2
  exit 1
fi

if [[ "$(uname -s)" == Darwin && -d "$UNPACK_DIR/libs" ]]; then
  echo "[smoke] packaged libs/: $(find "$UNPACK_DIR/libs" -name '*.dylib' | wc -l | tr -d ' ') dylibs"
fi

export SEEKDB_LIB_PATH="$MAIN_LIB"
echo "[smoke] SEEKDB_LIB_PATH=$SEEKDB_LIB_PATH"

cd "$NAPI_DIR"
if [[ ! -d node_modules ]]; then
  echo "[smoke] npm install (nodejs_napi)"
  npm install
fi
if [[ ! -f build/Release/seekdb.node ]]; then
  echo "[smoke] building nodejs_napi (node-gyp)"
  npx node-gyp rebuild
fi

DB_DIR="$UNPACK_DIR/smoke-seekdb.db"
rm -rf "$DB_DIR"
echo "[smoke] running nodejs_napi against packaged lib (includes VECTOR / hybrid tests)"
export SEEKDB_NODE_NAPI_SKIP_HEAVY=0
node test.js "$DB_DIR" "test"
NODE_EXIT=$?
if [[ "$NODE_EXIT" -ne 0 ]]; then
  echo "[smoke] failed: node test.js exited $NODE_EXIT" >&2
  exit "$NODE_EXIT"
fi

echo "[smoke] passed"
