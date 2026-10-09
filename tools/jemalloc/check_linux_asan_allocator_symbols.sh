#!/usr/bin/env bash
# Copyright (c) 2026 OceanBase.
# Licensed under the Apache License, Version 2.0.

set -euo pipefail

if [[ $# -lt 2 ]]; then
  echo "usage: $0 PROJECT_ALLOCATOR_ARCHIVE ELF [ELF ...]" >&2
  exit 2
fi

allocator_archive=$1
shift
script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
manifest=${script_dir}/linux_allocator_symbols.manifest

if [[ ! -f ${allocator_archive} ]]; then
  echo "missing project allocator archive: ${allocator_archive}" >&2
  exit 2
fi

archive_symbols=$(mktemp)
archive_demangled=$(mktemp)
trap 'rm -f "${archive_symbols}" "${archive_demangled}"' EXIT
nm --defined-only "${allocator_archive}" >"${archive_symbols}"
nm -C --defined-only "${allocator_archive}" >"${archive_demangled}"

while IFS= read -r symbol; do
  [[ -z ${symbol} || ${symbol} == \#* ]] && continue
  if awk -v wanted="${symbol}" '$NF == wanted { found = 1 } END { exit !found }' \
      "${archive_symbols}"; then
    echo "${allocator_archive}: ASAN project archive defines allocator hook ${symbol}" >&2
    exit 1
  fi
done <"${manifest}"

if grep -Eq '[[:space:]][TWVW][[:space:]]+operator (new|delete)(\[\])?\(' \
    "${archive_demangled}"; then
  echo "${allocator_archive}: ASAN project archive defines a C++ allocation hook" >&2
  grep -E '[[:space:]][TWVW][[:space:]]+operator (new|delete)(\[\])?\(' \
    "${archive_demangled}" >&2
  exit 1
fi

check_one()
{
  local binary=$1
  local all_symbols
  local dynamic_symbols
  local symbol
  local address

  if [[ ! -f ${binary} ]]; then
    echo "missing ELF: ${binary}" >&2
    return 1
  fi

  all_symbols=$(mktemp)
  dynamic_symbols=$(mktemp)
  trap 'rm -f "${all_symbols}" "${dynamic_symbols}"' RETURN
  nm -n "${binary}" >"${all_symbols}"
  nm -D --defined-only "${binary}" >"${dynamic_symbols}"

  if awk '$NF ~ /^je_[[:alnum:]_]+$/ { print; found = 1 }
          END { exit !found }' "${all_symbols}"; then
    echo "${binary}: ASAN ELF must not contain a je_ allocator entry" >&2
    return 1
  fi

  # compiler-rt legitimately exports weak malloc-family and mmap symbols from
  # an ASAN executable.  Accept such a definition only when the full symbol
  # table maps the same address to compiler-rt's interceptor trampoline.
  while IFS= read -r symbol; do
    [[ -z ${symbol} || ${symbol} == \#* ]] && continue
    address=$(awk -v wanted="${symbol}" \
        '$NF == wanted { print $1; exit }' "${dynamic_symbols}")
    if [[ -n ${address} ]] && ! awk -v address="${address}" \
        -v wanted="__interceptor_trampoline_${symbol}" \
        '$1 == address && $3 == wanted { found = 1 } END { exit !found }' \
        "${all_symbols}"; then
      echo "${binary}: ${symbol} is defined outside the ASAN interceptor" >&2
      return 1
    fi
  done <"${manifest}"

  for symbol in mmap mmap64 munmap; do
    address=$(awk -v wanted="${symbol}" \
        '$NF == wanted { print $1; exit }' "${dynamic_symbols}")
    if [[ -n ${address} ]] && ! awk -v address="${address}" \
        -v wanted="__interceptor_trampoline_${symbol}" \
        '$1 == address && $3 == wanted { found = 1 } END { exit !found }' \
        "${all_symbols}"; then
      echo "${binary}: ${symbol} is defined outside the ASAN interceptor" >&2
      return 1
    fi
  done

  echo "${binary}: ASAN system allocator domain verified"
}

for binary in "$@"; do
  check_one "${binary}"
done
