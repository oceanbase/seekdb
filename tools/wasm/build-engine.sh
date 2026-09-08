#!/usr/bin/env bash
# Copyright (c) 2026 OceanBase.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
seekdb_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
seekdb_build_dir="${1:-${seekdb_root}/build_wasm_engine}"
seekdb_version="$(cat "${seekdb_root}/tools/wasm/emscripten-version")"
if ! emcc --version | head -n 1 | grep -Eq " ${seekdb_version//./\\.}( |$)"; then
  echo "Activate Emscripten ${seekdb_version} before building seekdb." >&2
  exit 1
fi
emcmake cmake -S "${seekdb_root}/src/wasm" -B "${seekdb_build_dir}" \
  -DCMAKE_BUILD_TYPE=Release "${@:2}"
cmake --build "${seekdb_build_dir}" --parallel 4
ctest --test-dir "${seekdb_build_dir}" --output-on-failure
