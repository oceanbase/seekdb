#!/usr/bin/env bash
# Copyright (c) 2026 OceanBase. SPDX-License-Identifier: Apache-2.0
set -euo pipefail
seekdb_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
seekdb_build_dir="${1:-${seekdb_root}/build_wasm_rust_runtime}"
source "${seekdb_root}/tools/wasm/rust-build-env.sh"
cargo build --manifest-path "${seekdb_root}/rust/Cargo.toml" -p sql-nio \
  --locked --release --no-default-features --features memory-transport \
  --target wasm32-unknown-emscripten -Zbuild-std=std,panic_abort
em++ -std=c++20 -Oz -pthread -fexceptions -UNDEBUG \
  -I"${seekdb_root}/rust/sql-nio/include" \
  "${seekdb_root}/unittest/wasm/test_wasm_nio_memory.cpp" \
  "${CARGO_TARGET_DIR}/wasm32-unknown-emscripten/release/libsql_nio.a" \
  -sPROXY_TO_PTHREAD=1 -sPTHREAD_POOL_SIZE=6 -sPTHREAD_POOL_SIZE_STRICT=2 \
  -sINITIAL_MEMORY=67108864 -sSTACK_SIZE=1048576 -sASSERTIONS=2 -sEXIT_RUNTIME=1 \
  -Wl,--fatal-warnings -o "${seekdb_build_dir}/test_wasm_nio_memory.js"
"${EMSDK_NODE:-node}" "${seekdb_build_dir}/test_wasm_nio_memory.js"
python3 "${seekdb_root}/tools/wasm/rust-nio-manifest.py" "${seekdb_build_dir}" --record
