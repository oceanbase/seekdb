#!/usr/bin/env bash
# Copyright (c) 2026 OceanBase.
# Licensed under the Apache License, Version 2.0.

set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "usage: $0 ELF [ELF ...]" >&2
  exit 2
fi

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
manifest=${script_dir}/linux_allocator_symbols.manifest

check_one()
{
  local binary=$1
  local symbols
  local demangled
  local symbol

  if [[ ! -f ${binary} ]]; then
    echo "missing ELF: ${binary}" >&2
    return 1
  fi

  symbols=$(mktemp)
  demangled=$(mktemp)
  trap 'rm -f "${symbols}" "${demangled}"' RETURN
  nm -D --defined-only "${binary}" >"${symbols}"
  nm -D -C --defined-only "${binary}" >"${demangled}"

  while IFS= read -r symbol; do
    [[ -z ${symbol} || ${symbol} == \#* ]] && continue
    if ! awk -v wanted="${symbol}" '$NF == wanted { found = 1 } END { exit !found }' "${symbols}"; then
      echo "${binary}: required allocator symbol is not defined: ${symbol}" >&2
      return 1
    fi
  done <"${manifest}"

  local -a cpp_patterns=(
    'operator new(unsigned long)'
    'operator new[](unsigned long)'
    'operator new(unsigned long, std::nothrow_t const&)'
    'operator new[](unsigned long, std::nothrow_t const&)'
    'operator new(unsigned long, std::align_val_t)'
    'operator new[](unsigned long, std::align_val_t)'
    'operator new(unsigned long, std::align_val_t, std::nothrow_t const&)'
    'operator new[](unsigned long, std::align_val_t, std::nothrow_t const&)'
    'operator delete(void*)'
    'operator delete[](void*)'
    'operator delete(void*, std::nothrow_t const&)'
    'operator delete[](void*, std::nothrow_t const&)'
    'operator delete(void*, unsigned long)'
    'operator delete[](void*, unsigned long)'
    'operator delete(void*, std::align_val_t)'
    'operator delete[](void*, std::align_val_t)'
    'operator delete(void*, std::align_val_t, std::nothrow_t const&)'
    'operator delete[](void*, std::align_val_t, std::nothrow_t const&)'
    'operator delete(void*, unsigned long, std::align_val_t)'
    'operator delete[](void*, unsigned long, std::align_val_t)'
  )
  for symbol in "${cpp_patterns[@]}"; do
    if ! grep -Fq " ${symbol}" "${demangled}"; then
      echo "${binary}: required C++ allocator symbol is not defined: ${symbol}" >&2
      return 1
    fi
  done

  for symbol in mmap mmap64 munmap; do
    if awk -v wanted="${symbol}" '$NF == wanted { found = 1 } END { exit !found }' "${symbols}"; then
      echo "${binary}: must not define ${symbol}" >&2
      return 1
    fi
    if ! nm -D "${binary}" | awk -v wanted="${symbol}" \
        '$1 == "U" && ($2 == wanted || index($2, wanted "@") == 1) { found = 1 }
         END { exit !found }'; then
      echo "${binary}: expected an undefined libc reference for ${symbol}" >&2
      return 1
    fi
  done

  echo "${binary}: allocator symbol domain verified"
}

for binary in "$@"; do
  check_one "${binary}"
done
