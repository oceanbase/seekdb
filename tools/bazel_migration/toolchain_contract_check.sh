#!/usr/bin/env bash

set -euo pipefail

readonly ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
readonly BAZEL="${ROOT}/bazel.py"

compile_action="$(
  "${BAZEL}" aquery \
    'mnemonic("CppCompile", //src/sql/parser:_server_parser_c)' \
    --output=text 2>/dev/null
)"
if grep -Fq -- "-fstack-protector" <<<"${compile_action}"; then
  printf 'production compile action unexpectedly enables -fstack-protector\n' >&2
  exit 1
fi

final_link_action="$(
  "${BAZEL}" aquery \
    'mnemonic("SeekdbFinalLink", //src/observer:seekdb_link)' \
    --output=text 2>/dev/null
)"
final_link_command="$(
  sed -n '/  Command Line:/,/^# Configuration:/p' <<<"${final_link_action}"
)"
if grep -Fq -- "-lstdc++" <<<"${final_link_command}"; then
  printf 'final link still selects libstdc++ through -lstdc++\n' >&2
  exit 1
fi
if ! grep -Fq "libstdc++.a" <<<"${final_link_command}"; then
  printf 'final link does not consume the toolchain static C++ runtime archive\n' >&2
  exit 1
fi

printf '[OK] compile hardening and static C++ runtime match the release contract\n'
