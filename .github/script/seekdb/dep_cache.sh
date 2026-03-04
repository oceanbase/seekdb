#!/usr/bin/env bash
# Dep cache: optional dependency cache for compile (e.g. restore from NFS/OSS).
# No-op by default; override or extend for your environment.
CACHE_HOME="${CACHE_HOME:-/home/jenkins/agent/dep_cache}"
export CACHE_HOME
# Restore dep_cache.tar.zst to current dir if present (e.g. from previous run)
if [[ -f "$SEEKDB_TASK_DIR/dep_cache.tar.zst" ]]; then
  zstd -dqc "$SEEKDB_TASK_DIR/dep_cache.tar.zst" | tar -xf - -C "$WORKSPACE" 2>/dev/null || true
fi
return 0
