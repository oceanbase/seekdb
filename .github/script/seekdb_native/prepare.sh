#!/usr/bin/env bash
# Prepare step for SeekDB native execution (执行下沉).
# 仅生成 jobargs.output、run_jobs.output；脚本已从 farm-jenkins 复制到本仓 .github/script/seekdb_native/scripts/，无需 clone。
set -e

MYSQLTEST_SLICES="${MYSQLTEST_SLICES:-4}"
WORKSPACE="${GITHUB_WORKSPACE:-.}"
TASK_DIR="${SEEKDB_TASK_DIR:-$WORKSPACE/seekdb_build/$GITHUB_RUN_ID}"
mkdir -p "$TASK_DIR"

# 1. 生成 run_jobs.output（与 seekdb.groovy / farm-jenkins 格式一致）
echo '++compile++' > "$TASK_DIR/run_jobs.output"
for i in $(seq 0 $((MYSQLTEST_SLICES - 1))); do
  echo "++mysqltest++$i++" >> "$TASK_DIR/run_jobs.output"
done

# 2. 生成 jobargs.output
{
  echo '++is_cmake++'
  echo '++need_agentserver++0'
  echo '++need_libobserver_so++0'
  echo '++need_liboblog++0'
} > "$TASK_DIR/jobargs.output"

echo "Prepare done. TASK_DIR=$TASK_DIR"
ls -la "$TASK_DIR"
