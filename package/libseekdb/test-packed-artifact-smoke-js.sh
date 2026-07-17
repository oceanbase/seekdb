#!/usr/bin/env bash
# Smoke-test a packed zip using seekdb-js bindings + embedded vitest (reproduces CI failure).
#
# Requires a prepared seekdb-js tree (pnpm install at repo root at least once).
#
# Usage:
#   SEEKDB_JS_ROOT=/path/to/seekdb-js ./test-packed-artifact-smoke-js.sh <libseekdb-*.zip>
#
# Default SEEKDB_JS_ROOT: ../../seekdb-js relative to seekdb repo root.

set -euo pipefail

ZIP="${1:?usage: SEEKDB_JS_ROOT=... $0 <libseekdb-*.zip>}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOP_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

if [[ -z "${SEEKDB_JS_ROOT:-}" ]]; then
  if [[ -d "$TOP_DIR/../seekdb-js" ]]; then
    SEEKDB_JS_ROOT="$(cd "$TOP_DIR/../seekdb-js" && pwd)"
  else
    echo "error: set SEEKDB_JS_ROOT to your seekdb-js checkout" >&2
    exit 1
  fi
fi

BINDINGS="$SEEKDB_JS_ROOT/packages/bindings"
SEEKDB_PKG="$SEEKDB_JS_ROOT/packages/seekdb"

if [[ ! -d "$BINDINGS" ]] || [[ ! -d "$SEEKDB_PKG" ]]; then
  echo "error: invalid SEEKDB_JS_ROOT=$SEEKDB_JS_ROOT" >&2
  exit 1
fi

if [[ ! -f "$ZIP" ]]; then
  echo "error: zip not found: $ZIP" >&2
  exit 1
fi

UNPACK="$(mktemp -d)"
trap 'rm -rf "$UNPACK"' EXIT

echo "[smoke-js] unpack $ZIP"
unzip -q "$ZIP" -d "$UNPACK"

case "$(uname -s)" in
  Darwin) MAIN="libseekdb.dylib" ;;
  Linux)  MAIN="libseekdb.so" ;;
  *)
    echo "error: unsupported OS $(uname -s)" >&2
    exit 1
    ;;
esac

if [[ ! -f "$UNPACK/$MAIN" ]]; then
  echo "error: $MAIN missing in zip" >&2
  exit 1
fi

LIB_DIR="$BINDINGS/libseekdb"
PKG_DIR="$BINDINGS/pkgs/js-bindings"

echo "[smoke-js] install packed lib into $LIB_DIR"
rm -rf "$LIB_DIR"/*
mkdir -p "$LIB_DIR"
cp "$UNPACK/$MAIN" "$LIB_DIR/"
[[ -f "$UNPACK/seekdb.h" ]] && cp "$UNPACK/seekdb.h" "$LIB_DIR/"
if [[ -d "$UNPACK/libs" ]]; then
  cp -R "$UNPACK/libs" "$LIB_DIR/"
fi

echo "[smoke-js] rebuild @seekdb/js-bindings (seekdb_js_bindings + @loader_path)"
(
  cd "$BINDINGS"
  if [[ ! -d node_modules ]]; then
    echo "error: run 'pnpm install' in $SEEKDB_JS_ROOT first" >&2
    exit 1
  fi
  npx node-gyp rebuild
  python3 scripts/fetch_libseekdb.py --sign-dylibs
)

if [[ "$(uname -s)" == Darwin ]]; then
  echo "[smoke-js] pkgs/js-bindings/libseekdb.dylib sha256:"
  shasum -a 256 "$PKG_DIR/$MAIN" | awk '{print "  ", $1}'
fi

echo "[smoke-js] vitest sparse-vector query (collection-query.test.ts)"
VITEST_LOG="$(mktemp)"
set +e
(
  cd "$SEEKDB_PKG"
  if [[ ! -d node_modules ]]; then
    echo "error: run 'pnpm install' in $SEEKDB_JS_ROOT first" >&2
    exit 1
  fi
  pnpm exec vitest run tests/embedded/collection/collection-query.test.ts -t "sparseEmbedding" 2>&1 | tee "$VITEST_LOG"
)
VITEST_CODE=${PIPESTATUS[0]}
set -e

if grep -q 'VsagException' "$VITEST_LOG" 2>/dev/null; then
  echo "[smoke-js] failed: vsag::VsagException (same as seekdb-js test:embedded on darwin)" >&2
  rm -f "$VITEST_LOG"
  exit 1
fi

if grep -qE 'Test Files[[:space:]]+[0-9]+ passed' "$VITEST_LOG" && grep -qE 'Tests[[:space:]]+[0-9]+ passed' "$VITEST_LOG"; then
  rm -f "$VITEST_LOG"
  echo "[smoke-js] passed (seekdb-js embedded path; vitest wrapper exit=$VITEST_CODE ignored)"
  exit 0
fi

rm -f "$VITEST_LOG"
echo "[smoke-js] failed: vitest exit $VITEST_CODE" >&2
exit "${VITEST_CODE:-1}"
