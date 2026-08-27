#!/usr/bin/env bash
# Run one mysqltest slice directly against seekdb.
# Required env: GITHUB_WORKSPACE, SEEKDB_TASK_DIR, SEEKDB_BINARY, SLICE_IDX, SLICES
# Optional: SEEKDB_RUNTIME_DIR, MYSQLTEST_PORT
set -euo pipefail

WORKSPACE="${GITHUB_WORKSPACE:?}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TASK_DIR="${SEEKDB_TASK_DIR:?}"
SLICE_IDX="${SLICE_IDX:-0}"
SLICES="${SLICES:-4}"
RUNTIME_DIR="${SEEKDB_RUNTIME_DIR:-$WORKSPACE/.seekdb_runtime}"
SEEKDB_BINARY="${SEEKDB_BINARY:?}"
CLIENT_ROOT="$RUNTIME_DIR/obclient"
PORT="${MYSQLTEST_PORT:-$((5000 + SLICE_IDX * 100))}"

export PATH="$CLIENT_ROOT/bin:$PATH"
for lib_dir in "$CLIENT_ROOT/lib" "$CLIENT_ROOT/lib64"; do
  if [[ -d "$lib_dir" ]]; then
    export LD_LIBRARY_PATH="$lib_dir${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  fi
done

exec python3 "$SCRIPT_DIR/mysqltest_for_seekdb.py" run \
  --seekdb "$SEEKDB_BINARY" \
  --obclient "$CLIENT_ROOT/bin/obclient" \
  --mysqltest "$CLIENT_ROOT/bin/mysqltest" \
  --base-dir "$TASK_DIR/instance" \
  --work-dir "$TASK_DIR" \
  --port "$PORT" \
  --slice-index "$SLICE_IDX" \
  --slice-count "$SLICES" \
  "$@"
