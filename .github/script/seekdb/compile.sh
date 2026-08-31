#!/usr/bin/env bash
# Compile: initialize and build seekdb.
# Required env: GITHUB_WORKSPACE, SEEKDB_TASK_DIR
# Optional: FORWARDING_HOST, MAKE_ARGS, DEP_CACHE_DIR, CCACHE_DIR,
#           SEEKDB_CCACHE_MAX_SIZE

if [[ -f ~/.bashrc ]]; then
  # shellcheck disable=SC1090
  source ~/.bashrc
fi

set -euo pipefail

WORKSPACE="${GITHUB_WORKSPACE:?}"
TASK_DIR="${SEEKDB_TASK_DIR:?}"

# Diagnostics for container/workspace path issues.
echo "[compile.sh] WORKSPACE=$WORKSPACE"
echo "[compile.sh] pwd=$(pwd)"
# shellcheck disable=SC2012
ls -la "$WORKSPACE/" 2>/dev/null | head -20 || true
echo "[compile.sh] build.sh: -f=$([[ -f "$WORKSPACE/build.sh" ]] && echo 1 || echo 0) -x=$([[ -x "$WORKSPACE/build.sh" ]] && echo 1 || echo 0)"

export GITHUB_WORKSPACE="$WORKSPACE"
export SEEKDB_TASK_DIR="$TASK_DIR"
export PACKAGE_TYPE="${PACKAGE_TYPE:-release}"
export MAKE="${MAKE:-make}"
export MAKE_ARGS="${MAKE_ARGS:--j32}"
export PATH="$WORKSPACE/deps/3rd/usr/local/oceanbase/devtools/bin:$PATH"
if [[ -n "${FORWARDING_HOST:-}" ]]; then
  echo "$FORWARDING_HOST mirrors.oceanbase.com" >> /etc/hosts 2>/dev/null || true
fi

cd "$WORKSPACE"
mkdir -p "$TASK_DIR"

BUILD_TARGET="${PACKAGE_TYPE:-debug}"
BUILD_DIR="build_${BUILD_TARGET}"
compile_ret=0
compile_start=$SECONDS

echo "[compile.sh] nproc=$(nproc 2>/dev/null || echo unknown)"
[[ -f /sys/fs/cgroup/cpu.max ]] && echo "[compile.sh] cgroup cpu.max=$(< /sys/fs/cgroup/cpu.max)"
[[ -f /sys/fs/cgroup/memory.max ]] && echo "[compile.sh] cgroup memory.max=$(< /sys/fs/cgroup/memory.max)"

# 存在即可（不要求 -x），用 bash 执行
if [[ ! -f "$WORKSPACE/build.sh" ]]; then
  echo "[compile.sh] No build.sh at $WORKSPACE/build.sh, skip."
else
  # Step 1: Build init（与 buildbase 一致，只传 init，先拉取/安装 deps 再才能用 cmake）
  phase_start=$SECONDS
  set +e
  bash "$WORKSPACE/build.sh" init 2>&1 | tee "$TASK_DIR/compile_init.output"
  phase_ret=${PIPESTATUS[0]}
  set -e
  echo "[compile.sh] dependency init elapsed=$((SECONDS - phase_start))s"
  [[ $phase_ret -ne 0 ]] && exit "$phase_ret"

  if command -v ccache >/dev/null 2>&1; then
    ccache -M "${SEEKDB_CCACHE_MAX_SIZE:-3G}" || true
    ccache -c || true
  fi
  CACHE_SCRIPT="$WORKSPACE/.github/script/seekdb/cache.sh"
  if [[ -f "$CACHE_SCRIPT" && -n "${SEEKDB_CACHE_NFS_ROOT:-}" ]]; then
    bash "$CACHE_SCRIPT" baseline-ccache || echo "[compile.sh] failed to record ccache baseline"
  fi

  phase_start=$SECONDS
  set +e
  bash "$WORKSPACE/build.sh" "$BUILD_TARGET" -DOB_USE_CCACHE=ON -DNEED_PARSER_CACHE=OFF 2>&1 | tee "$TASK_DIR/compile_configure.output"
  phase_ret=${PIPESTATUS[0]}
  set -e
  echo "[compile.sh] configure elapsed=$((SECONDS - phase_start))s"
  [[ $phase_ret -ne 0 ]] && exit "$phase_ret"

  set +e
  if command -v ccache >/dev/null 2>&1; then
    ccache -z || true
  fi
  phase_start=$SECONDS
  # MAKE and MAKE_ARGS intentionally support a command plus separate arguments.
  # shellcheck disable=SC2086
  (cd "$WORKSPACE/$BUILD_DIR" && $MAKE $MAKE_ARGS seekdb) 2>&1 | tee "$TASK_DIR/compile.output"
  compile_ret=${PIPESTATUS[0]}
  echo "[compile.sh] make elapsed=$((SECONDS - phase_start))s"
  if command -v ccache >/dev/null 2>&1; then
    ccache -s || true
  fi
  set -e
fi

echo "[compile.sh] total elapsed=$((SECONDS - compile_start))s"
exit "$compile_ret"
