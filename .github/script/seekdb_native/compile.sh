#!/usr/bin/env bash
# Compile step for SeekDB native execution (执行下沉).
# 使用本仓已复制的 farm 脚本：.github/script/seekdb_native/scripts/
set -e

WORKSPACE="${GITHUB_WORKSPACE:-.}"
TASK_DIR="${SEEKDB_TASK_DIR:-$WORKSPACE/seekdb_build/$GITHUB_RUN_ID}"
COMPILE_RUNDIR="$TASK_DIR/compile_rundir"
# 脚本来自本仓，不再 clone
SCRIPTS_DIR="$WORKSPACE/.github/script/seekdb_native/scripts"
mkdir -p "$COMPILE_RUNDIR"

if [[ ! -f "$TASK_DIR/jobargs.output" ]] || [[ ! -f "$TASK_DIR/run_jobs.output" ]]; then
  echo "Missing jobargs.output or run_jobs.output in $TASK_DIR. Run prepare first."
  exit 1
fi

if [[ ! -f "$SCRIPTS_DIR/farm_compile.sh" ]] || [[ ! -f "$SCRIPTS_DIR/frame.sh" ]]; then
  echo "Missing farm_compile.sh / frame.sh under $SCRIPTS_DIR. Scripts should be copied from farm-jenkins."
  exit 1
fi

# 链到 HOME，供 source $HOME/scripts/frame.sh 与 farm_compile.sh 使用
ln -sfn "$SCRIPTS_DIR" "$COMPILE_RUNDIR/scripts"

export HOME="$COMPILE_RUNDIR"
export _CONDOR_JOB_IWD="$COMPILE_RUNDIR"
export REPO="server"
export CREATE_AGENTSERVER=0
export CREATE_LIBOBSERVER_SO=0
export ENABLE_LIBOBLOG=0
export BUILD_TARGET=""
export CODE_URL="${CODE_URL:-https://github.com/${GITHUB_REPOSITORY}.git}"
export BRANCH="${BRANCH:-$GITHUB_REF_NAME}"
export COMMIT="${COMMIT:-}"
if [[ -n "${RELEASE_MODE:-}" ]]; then
  export PACKAGE_TYPE="release"
else
  export PACKAGE_TYPE="debug"
fi
if [[ -n "${FORWARDING_HOST:-}" ]]; then
  echo "$FORWARDING_HOST mirrors.oceanbase.com" >> /etc/hosts 2>/dev/null || true
fi

# 执行 farm_compile.sh：内部 source frame.sh && main
cd "$HOME"
bash "$HOME/scripts/farm_compile.sh"
COMPILE_EXIT=$?
[[ -f "$HOME/scripts/farm_post_compile.sh" ]] && bash "$HOME/scripts/farm_post_compile.sh" "$COMPILE_EXIT" || true

# 压缩并拷贝产物到 TASK_DIR
for f in observer obproxy; do
  [[ -f "$COMPILE_RUNDIR/$f" ]] && zstd -f "$COMPILE_RUNDIR/$f" 2>/dev/null || true
done
for fn in observer.zst obproxy.zst compile.output; do
  [[ -f "$COMPILE_RUNDIR/$fn" ]] && cp "$COMPILE_RUNDIR/$fn" "$TASK_DIR/"
done
[[ -f "$COMPILE_RUNDIR/dep_cache.tar.zst" ]] && cp "$COMPILE_RUNDIR/dep_cache.tar.zst" "$TASK_DIR/" || true
[[ -f "$COMPILE_RUNDIR/post_compile.output" ]] && cp "$COMPILE_RUNDIR/post_compile.output" "$TASK_DIR/" || true

echo "Compile done. Artifacts in $TASK_DIR"
ls -la "$TASK_DIR"
