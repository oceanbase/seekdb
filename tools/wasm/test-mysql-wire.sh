#!/usr/bin/env bash
# Copyright (c) 2026 OceanBase. SPDX-License-Identifier: Apache-2.0
set -euo pipefail
repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
node_bin=${1:-${EMSDK_NODE:-node}}
rust_toolchain=$(cat "$repo_dir/tools/wasm/rust-toolchain-version")
fixture_dir=$(mktemp -d "${TMPDIR:-/tmp}/seekdb-mysql-wire.XXXXXX")
trap 'rm -rf "$fixture_dir"' EXIT
rustup run "$rust_toolchain" rustc --edition=2021 -O \
  "$repo_dir/unittest/wasm/mysql_wire_fixture.rs" -o "$fixture_dir/fixture"
SEEKDB_MYSQL_WIRE_FIXTURE="$fixture_dir/fixture" \
  "$node_bin" --test "$repo_dir/unittest/wasm/test_mysql_wire.mjs"
