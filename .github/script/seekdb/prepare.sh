#!/usr/bin/env bash
# Prepare: create task dir and generate jobargs.output / run_jobs.output
# Required env: GITHUB_WORKSPACE, GITHUB_RUN_ID, MYSQLTEST_SLICES, SEEKDB_TASK_DIR
set -e

WORKSPACE="${GITHUB_WORKSPACE:?}"
RUN_ID="${GITHUB_RUN_ID:?}"
SLICES="${MYSQLTEST_SLICES:-4}"
TASK_DIR="${SEEKDB_TASK_DIR:?}"

mkdir -p "$TASK_DIR"

# run_jobs.output: compile + N mysqltest slices (align with seekdb.groovy)
echo '++compile++' > "$TASK_DIR/run_jobs.output"
for i in $(seq 0 $((SLICES - 1))); do
  echo "++mysqltest++${i}++" >> "$TASK_DIR/run_jobs.output"
done

# jobargs.output: build options (align with seekdb.groovy)
{
  echo '++is_cmake++'
  echo '++need_agentserver++0'
  echo '++need_libobserver_so++0'
  echo '++need_liboblog++0'
} > "$TASK_DIR/jobargs.output"

echo "[prepare.sh] SEEKDB_TASK_DIR=$TASK_DIR run_jobs and jobargs written."