#!/usr/bin/env bash
# Run one mysqltest slice (slice index from SLICE_IDX).
# Required env: GITHUB_WORKSPACE, SEEKDB_TASK_DIR, SLICE_IDX, SLICES, BRANCH
# Optional: FORWARDING_HOST
set -e

WORKSPACE="${GITHUB_WORKSPACE:?}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIPTS_DIR="$SCRIPT_DIR/scripts"

export GITHUB_WORKSPACE="$WORKSPACE"
export SEEKDB_TASK_DIR="${SEEKDB_TASK_DIR:?}"
export SLICE_IDX="${SLICE_IDX:-0}"
export SLICES="${SLICES:-4}"
export BRANCH="${BRANCH:-master}"


# Copy compile artifacts from task dir to workspace if running in container
for f in observer.zst obproxy.zst; do
  if [[ -f "$SEEKDB_TASK_DIR/$f" ]] && [[ ! -f "$WORKSPACE/$f" ]]; then
    cp -f "$SEEKDB_TASK_DIR/$f" "$WORKSPACE/" || true
  fi
done

if [[ -f "$SCRIPTS_DIR/mysqltest_for_seekdb.sh" ]]; then
  bash "$SCRIPTS_DIR/mysqltest_for_seekdb.sh" "$@"
else
  echo "[mysqltest_slice.sh] No mysqltest_for_seekdb.sh, skip slice $SLICE_IDX."
fi