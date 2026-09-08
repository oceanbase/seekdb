#!/usr/bin/env bash
# Copyright (c) 2026 OceanBase.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

seekdb_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
seekdb_build_dir="${1:-${seekdb_root}/build_wasm_platform}"
seekdb_sdk_version="$(cat "${seekdb_root}/tools/wasm/emscripten-version")"
if ! command -v emcc >/dev/null || ! command -v emcmake >/dev/null; then
  echo "Activate Emscripten ${seekdb_sdk_version} with emsdk_env.sh first." >&2
  exit 1
fi
if ! emcc --version | head -n 1 | grep -Eq " ${seekdb_sdk_version//./\\.}( |$)"; then
  echo "This platform gate is pinned to Emscripten ${seekdb_sdk_version}." >&2
  exit 1
fi
emcmake cmake -S "${seekdb_root}/unittest/wasm" -B "${seekdb_build_dir}" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build "${seekdb_build_dir}" --parallel 4
ctest --test-dir "${seekdb_build_dir}" --output-on-failure
