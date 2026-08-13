#!/usr/bin/env bash
# Prepare: create task dir and generate jobargs.output / run_jobs.output
# Required env: SEEKDB_TASK_DIR
# Optional: MYSQLTEST_SLICES
set -euo pipefail

SLICES="${MYSQLTEST_SLICES:-4}"
TASK_DIR="${SEEKDB_TASK_DIR:?}"

mkdir -p "$TASK_DIR"

# run_jobs.output: compile + N mysqltest slices (align with seekdb.groovy)
echo '++compile++' > "$TASK_DIR/run_jobs.output"
for i in $(seq 0 $((SLICES - 1))); do
  echo "++mysqltest++${i}++" >> "$TASK_DIR/run_jobs.output"
done

# jobargs.output: the GitHub build is always the Bazel release Unity build.
{
  echo '++release_mode++'
  echo '++need_agentserver++0'
  echo '++need_libobserver_so++0'
} > "$TASK_DIR/jobargs.output"

echo "[prepare.sh] SEEKDB_TASK_DIR=$TASK_DIR run_jobs and jobargs written."
