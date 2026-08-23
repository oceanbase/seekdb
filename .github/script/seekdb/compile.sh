#!/usr/bin/env bash
# Compile: initialize and build seekdb.
# Required env: GITHUB_WORKSPACE, SEEKDB_TASK_DIR
# Optional: FORWARDING_HOST, MAKE_ARGS

if [[ -f ~/.bashrc ]]; then
  source ~/.bashrc
fi

set -euo pipefail

WORKSPACE="${GITHUB_WORKSPACE:?}"
TASK_DIR="${SEEKDB_TASK_DIR:?}"

# Diagnostics for container/workspace path issues.
echo "[compile.sh] WORKSPACE=$WORKSPACE"
echo "[compile.sh] pwd=$(pwd)"
ls -la "$WORKSPACE/" 2>/dev/null | head -20 || true
echo "[compile.sh] build.sh: -f=$([[ -f "$WORKSPACE/build.sh" ]] && echo 1 || echo 0) -x=$([[ -x "$WORKSPACE/build.sh" ]] && echo 1 || echo 0)"

export GITHUB_WORKSPACE="$WORKSPACE"
export SEEKDB_TASK_DIR="$TASK_DIR"
export PACKAGE_TYPE="${RELEASE_MODE:+release}"
export PACKAGE_TYPE="${PACKAGE_TYPE:-debug}"
export MAKE="${MAKE:-make}"
export MAKE_ARGS="${MAKE_ARGS:--j32}"
export PATH="$WORKSPACE/deps/3rd/usr/local/oceanbase/devtools/bin:$PATH"
[[ -n "$FORWARDING_HOST" ]] && echo "$FORWARDING_HOST mirrors.oceanbase.com" >> /etc/hosts 2>/dev/null || true

cd "$WORKSPACE"
mkdir -p "$TASK_DIR"

BUILD_TARGET="${PACKAGE_TYPE:-debug}"
BUILD_DIR="build_${BUILD_TARGET}"
compile_ret=0

# 存在即可（不要求 -x），用 bash 执行
if [[ ! -f "$WORKSPACE/build.sh" ]]; then
  echo "[compile.sh] No build.sh at $WORKSPACE/build.sh, skip."
else
  # Step 1: Build init（与 buildbase 一致，只传 init，先拉取/安装 deps 再才能用 cmake）
  bash "$WORKSPACE/build.sh" init 2>&1 | tee "$TASK_DIR/compile_init.output"
  [[ ${PIPESTATUS[0]} -ne 0 ]] && exit 1
  bash "$WORKSPACE/build.sh" "$BUILD_TARGET" -DOB_USE_CCACHE=ON -DNEED_PARSER_CACHE=OFF 2>&1 | tee "$TASK_DIR/compile_configure.output"
  [[ ${PIPESTATUS[0]} -ne 0 ]] && exit 1
  set +e
  command -v ccache >/dev/null 2>&1 && ccache -z || true
  (cd "$WORKSPACE/$BUILD_DIR" && $MAKE $MAKE_ARGS seekdb) 2>&1 | tee "$TASK_DIR/compile.output"
  compile_ret=${PIPESTATUS[0]}
  command -v ccache >/dev/null 2>&1 && ccache -s || true
  set -e
fi

exit "$compile_ret"
