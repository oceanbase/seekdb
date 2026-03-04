#!/usr/bin/env bash
# Frame: set common env for farm scripts (WORKSPACE, SEEKDB_TASK_DIR, script dirs).
# Sourced by compile.sh / mysqltest_slice.sh; can be no-op if already set.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export SCRIPTS_DIR="${SCRIPTS_DIR:-$SCRIPT_DIR}"
export WORKSPACE="${GITHUB_WORKSPACE:-$(pwd)}"
export SEEKDB_TASK_DIR="${SEEKDB_TASK_DIR:-$WORKSPACE/seekdb_build/$GITHUB_RUN_ID}"
# Optional: load dep_cache (no-op if not present)
[[ -f "$SCRIPTS_DIR/dep_cache.sh" ]] && source "$SCRIPTS_DIR/dep_cache.sh" || true
