#!/usr/bin/env bash
# Compile: 步骤与 .github/workflows/buildbase 一致（init → build.sh $TARGET → cd build_* && make）。
# Required env: GITHUB_WORKSPACE, SEEKDB_TASK_DIR
# Optional: RELEASE_MODE, FORWARDING_HOST, MAKE, MAKE_ARGS
set -e

WORKSPACE="${GITHUB_WORKSPACE:?}"
TASK_DIR="${SEEKDB_TASK_DIR:?}"

# 调试：便于排查 k8s/container 下 No build.sh
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
  (cd "$WORKSPACE/$BUILD_DIR" && $MAKE $MAKE_ARGS observer) 2>&1 | tee "$TASK_DIR/compile.output"
  compile_ret=${PIPESTATUS[0]}
  command -v ccache >/dev/null 2>&1 && ccache -s || true
  set -e
fi

# 产物落到 build_*，打包 observer/obproxy 为 zst 并拷贝到任务目录
for binary in observer obproxy; do
  for base in . build_debug build_release build; do
    if [[ -f "$WORKSPACE/$base/$binary" ]]; then
      cp -f "$WORKSPACE/$base/$binary" "$WORKSPACE/$binary" 2>/dev/null || true
      break
    fi
  done
  if [[ -f "$WORKSPACE/$binary" ]]; then
    command -v zstd >/dev/null 2>&1 && zstd -f "$WORKSPACE/$binary" || true
    [[ -f "$WORKSPACE/$binary.zst" ]] && cp -f "$WORKSPACE/$binary.zst" "$TASK_DIR/" || true
  fi
done

exit "$compile_ret"
