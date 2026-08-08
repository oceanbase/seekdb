#!/usr/bin/env bash

set -euo pipefail

readonly TOOLCHAIN_ROOT="$(dirname "${BASH_SOURCE[0]}")"
readonly NM="${TOOLCHAIN_ROOT}/devtools/bin/llvm-nm"
readonly CXXFILT="${TOOLCHAIN_ROOT}/devtools/bin/llvm-cxxfilt"

duplicate_symbols="$(${NM} -A -g -P "$1" |
  sed -E -e 's/.*\[([^][]+)\]: (.+) ([A-TX-Z]) [a-f0-9]+ [a-f0-9]+/\1: \3 \2/g' -e t -e d |
  LC_ALL=C sort -k 3 |
  LC_ALL=C uniq -D -f 2 |
  "${CXXFILT}")"

if [[ -n "${duplicate_symbols}" ]]; then
  echo "Duplicate symbols found in $1:" >&2
  echo "${duplicate_symbols}" >&2
  exit 1
fi

touch "$2"
