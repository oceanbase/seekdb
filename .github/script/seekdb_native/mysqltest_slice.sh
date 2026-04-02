#!/usr/bin/env bash
# Run one mysqltest slice for SeekDB native execution (执行下沉).
# 使用本仓已复制的 scripts/mysqltest_for_farm.sh。
set -e

WORKSPACE="${GITHUB_WORKSPACE:-.}"
TASK_DIR="${SEEKDB_TASK_DIR:-$WORKSPACE/seekdb_build/$GITHUB_RUN_ID}"
SLICE_IDX="${SLICE_IDX:-0}"
SLICES="${SLICES:-4}"
MYSQLTEST_RUNDIR="$TASK_DIR/mysqltest_rundir_$SLICE_IDX"
SCRIPTS_DIR="$WORKSPACE/.github/script/seekdb_native/scripts"
mkdir -p "$MYSQLTEST_RUNDIR"

for f in observer.zst obproxy.zst; do
  if [[ ! -f "$TASK_DIR/$f" ]]; then
    echo "Missing $TASK_DIR/$f. Run compile first."
    exit 1
  fi
done
if [[ ! -d "$TASK_DIR/oceanbase" ]]; then
  echo "Missing $TASK_DIR/oceanbase. Run compile first (frame prepare clones it)."
  exit 1
fi

if [[ ! -f "$SCRIPTS_DIR/mysqltest_for_farm.sh" ]]; then
  echo "Missing mysqltest_for_farm.sh under $SCRIPTS_DIR."
  exit 1
fi
ln -sfn "$SCRIPTS_DIR" "$MYSQLTEST_RUNDIR/scripts"

export HOME="$MYSQLTEST_RUNDIR"
export _CONDOR_JOB_IWD="$MYSQLTEST_RUNDIR"
export CODE_URL="${CODE_URL:-https://github.com/${GITHUB_REPOSITORY}.git}"
export BRANCH="${BRANCH:-$GITHUB_REF_NAME}"
export REPO="server"
export SLICE_IDX SLICES
export GID="${GITHUB_RUN_ID:-$GITHUB_RUN_ID}"
export JOBNAME=mysqltest
export MRID=""
export WITH_PROXY="1"
export ARGV="psmall log-pattern=*"
export CLUSTER_SPEC="2x1"
export INPUT_FILES="observer,obproxy"
export MINI="1"
export FROM_FARM="1"
export SLB=""
export _CONDOR_SLOT="slot$SLICE_IDX"

# 将 observer/obproxy 放入 HOME，供 mysqltest_for_farm run() 使用
zstd -d -f "$TASK_DIR/observer.zst" -o "$MYSQLTEST_RUNDIR/observer"
zstd -d -f "$TASK_DIR/obproxy.zst" -o "$MYSQLTEST_RUNDIR/obproxy"
chmod +x "$MYSQLTEST_RUNDIR/observer" "$MYSQLTEST_RUNDIR/obproxy" 2>/dev/null || true

if [[ -n "${FORWARDING_HOST:-}" ]]; then
  echo "$FORWARDING_HOST mirrors.oceanbase.com" >> /etc/hosts 2>/dev/null || true
fi

cd "$HOME"
bash "$HOME/scripts/mysqltest_for_farm.sh"
MYSQLTEST_EXIT=$?

# 收集 slice 产出到 TASK_DIR
for fn in mysqltest.output."$SLICE_IDX" mysqltest.error."$SLICE_IDX" collected_log_"$SLICE_IDX".tar.gz mysqltest_compare_output."$SLICE_IDX"; do
  [[ -f "$MYSQLTEST_RUNDIR/$fn" ]] && cp "$MYSQLTEST_RUNDIR/$fn" "$TASK_DIR/" || true
done
[[ -f "$MYSQLTEST_RUNDIR/oceanbase/tools/deploy/compare.out" ]] && cp "$MYSQLTEST_RUNDIR/oceanbase/tools/deploy/compare.out" "$TASK_DIR/mysqltest_compare_output.$SLICE_IDX" 2>/dev/null || true

if [[ $MYSQLTEST_EXIT -ne 0 ]]; then
  echo "++mysqltest++${SLICE_IDX}++" >> "$TASK_DIR/fail_cases.output"
fi
exit "$MYSQLTEST_EXIT"
