#!/usr/bin/env bash
# Compile: run farm_compile (and farm_post_compile), output observer.zst / obproxy.zst to SEEKDB_TASK_DIR
# Required env: GITHUB_WORKSPACE, GITHUB_RUN_ID, SEEKDB_TASK_DIR
# Optional: RELEASE_MODE, FORWARDING_HOST
set -e

WORKSPACE="${GITHUB_WORKSPACE:?}"
TASK_DIR="${SEEKDB_TASK_DIR:?}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIPTS_DIR="$SCRIPT_DIR"

export GITHUB_WORKSPACE="$WORKSPACE"
export SEEKDB_TASK_DIR="$TASK_DIR"
export PACKAGE_TYPE="${RELEASE_MODE:+release}"
export PACKAGE_TYPE="${PACKAGE_TYPE:-debug}"
export CREATE_AGENTSERVER=0
export CREATE_LIBOBSERVER_SO=0
export ENABLE_LIBOBLOG=0
export BUILD_TARGET=""
export REPO="server"
[[ -n "$FORWARDING_HOST" ]] && echo "$FORWARDING_HOST mirrors.oceanbase.com" >> /etc/hosts 2>/dev/null || true

cd "$WORKSPACE"
# Source frame (env) then run compile
if [[ -f "$SCRIPTS_DIR/frame.sh" ]]; then
  # shellcheck source=.github/script/seekdb/scripts/frame.sh
  source "$SCRIPTS_DIR/frame.sh"
fi

if [[ "$PACKAGE_TYPE" == "release" ]] && [[ -x "$SCRIPTS_DIR/farm_compile_release.sh" ]]; then
  script_name=farm_compile_release.sh
else
  script_name=farm_compile.sh
fi

mkdir -p "$TASK_DIR"
set +e
if [[ -x "$SCRIPTS_DIR/$script_name" ]]; then
  bash "$SCRIPTS_DIR/$script_name" 2>&1 | tee "$TASK_DIR/compile.output"
  compile_ret=$?
else
  echo "[compile.sh] No $SCRIPTS_DIR/$script_name, skip compile."
  compile_ret=0
fi
set -e

if [[ -x "$SCRIPTS_DIR/farm_post_compile.sh" ]]; then
  bash "$SCRIPTS_DIR/farm_post_compile.sh" "$compile_ret"
fi

# Copy artifacts to task dir if produced in workspace
for f in observer.zst obproxy.zst; do
  if [[ -f "$WORKSPACE/$f" ]]; then
    cp -f "$WORKSPACE/$f" "$TASK_DIR/" || true
  fi
done
if [[ -f "$TASK_DIR/compile.output" ]]; then
  : # already written
elif [[ -f "$WORKSPACE/compile.output" ]]; then
  cp -f "$WORKSPACE/compile.output" "$TASK_DIR/" || true
fi

exit "$compile_ret"
