#!/usr/bin/env bash
# Copyright (c) 2026 OceanBase.
# Licensed under the Apache License, Version 2.0.

set -euo pipefail

if [[ $# -lt 2 ]]; then
  echo "usage: $0 RUST_TOOLCHAIN_MANIFEST CARGO_ARGS..." >&2
  exit 2
fi

toolchain_manifest=$1
shift
toolchain=$(awk -F'"' \
  '/^[[:space:]]*channel[[:space:]]*=[[:space:]]*"[0-9]+\.[0-9]+\.[0-9]+"/ { print $2; exit }' \
  "${toolchain_manifest}")
if [[ -z ${toolchain} ]]; then
  echo "cannot read pinned Rust toolchain from ${toolchain_manifest}" >&2
  exit 2
fi

RUSTUP_TOOLCHAIN=${toolchain} exec cargo "$@"
