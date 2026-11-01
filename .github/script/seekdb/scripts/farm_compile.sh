#!/usr/bin/env bash
# Farm compile: build seekdb/observer in workspace.
# Expects: WORKSPACE, SEEKDB_TASK_DIR; optional PACKAGE_TYPE (default release).
# Reuse the checked AMD compile entry instead of maintaining a second set of
# flags, initialization, and artifact checks.
set -euo pipefail

export GITHUB_WORKSPACE="${WORKSPACE:?}"
export PACKAGE_TYPE="${PACKAGE_TYPE:-release}"
# The maintained release profile uses RelWithDebInfo. Legacy farm callers may
# still request debug, which the root build command no longer accepts.
if [[ "$PACKAGE_TYPE" == debug ]]; then
  echo "[farm_compile.sh] legacy debug profile uses release (RelWithDebInfo)"
  export PACKAGE_TYPE=release
fi
exec bash "$GITHUB_WORKSPACE/.github/script/seekdb/compile.sh"
