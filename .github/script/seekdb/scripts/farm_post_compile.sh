#!/usr/bin/env bash
# Farm post-compile: pack observer/obproxy to zst and copy to SEEKDB_TASK_DIR.
# Arg: exit code from farm_compile (0 = success).
set -e

compile_ret="${1:-0}"
cd "$WORKSPACE"

# Find observer/obproxy (build dir may be build_debug or build_release, or current dir)
for binary in observer obproxy; do
  for base in . build_debug build_release build; do
    if [[ -f "$WORKSPACE/$base/$binary" ]]; then
      cp -f "$WORKSPACE/$base/$binary" "$WORKSPACE/$binary" 2>/dev/null || true
      break
    fi
  done
  if [[ -f "$WORKSPACE/$binary" ]]; then
    command -v zstd >/dev/null 2>&1 && zstd -f "$WORKSPACE/$binary" || true
    [[ -f "$WORKSPACE/$binary.zst" ]] && cp -f "$WORKSPACE/$binary.zst" "$SEEKDB_TASK_DIR/" || true
  fi
done

exit "$compile_ret"
