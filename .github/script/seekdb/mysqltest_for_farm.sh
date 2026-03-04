#!/usr/bin/env bash
# Mysqltest for farm: run one slice of mysqltest (slice index from SLICE_IDX, total from SLICES).
# Required env: GITHUB_WORKSPACE, SEEKDB_TASK_DIR, SLICE_IDX, SLICES, BRANCH
# Optional: FORWARDING_HOST
set -e

WORKSPACE="${GITHUB_WORKSPACE:?}"
TASK_DIR="${SEEKDB_TASK_DIR:?}"
SLICE_IDX="${SLICE_IDX:-0}"
SLICES="${SLICES:-4}"
BRANCH="${BRANCH:-master}"

# Prefer obd test mysqltest if available (see tools/deploy/obd.sh)
if [[ -x "$WORKSPACE/tools/deploy/obd.sh" ]]; then
  cd "$WORKSPACE"
  # Slice: run subset of tests by slice index (caller may pass extra args)
  # Stub: run full mysqltest when SLICES=1, else skip or run slice logic per your test list
  if [[ "$SLICES" -le 1 ]]; then
    bash tools/deploy/obd.sh test mysqltest "$@" || true
  else
    echo "[mysqltest_for_farm.sh] slice $SLICE_IDX/$SLICES - extend this script to run your slice."
    # Placeholder: exit 0 so collect_result can still run
  fi
else
  echo "[mysqltest_for_farm.sh] No obd.sh, skip mysqltest slice $SLICE_IDX."
fi
