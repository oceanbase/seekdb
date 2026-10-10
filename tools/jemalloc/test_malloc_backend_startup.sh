#!/usr/bin/env bash

# Copyright (c) 2026 OceanBase.
# Licensed under the Apache License, Version 2.0.

set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 ]]; then
  echo "usage: $0 <seekdb-binary> [bundled|system]" >&2
  exit 2
fi

candidate=$1
mode=${2:-bundled}
if [[ ! -x ${candidate} ]]; then
  echo "not an executable seekdb binary: ${candidate}" >&2
  exit 2
fi
if [[ ${mode} != bundled && ${mode} != system ]]; then
  echo "allocator mode must be bundled or system" >&2
  exit 2
fi

result_dir=$(mktemp -d)
trap 'rm -rf -- "${result_dir}"' EXIT

run_case()
{
  local name=$1
  local backend=$2
  local expected_rc=$3
  local output=${result_dir}/${name}.log
  local actual_rc=0
  if [[ ${backend} == __UNSET__ ]]; then
    env -u MALLOC_BACKEND "${candidate}" --help >"${output}" 2>&1 || actual_rc=$?
  else
    MALLOC_BACKEND=${backend} "${candidate}" --help >"${output}" 2>&1 || actual_rc=$?
  fi
  if [[ ${actual_rc} -ne ${expected_rc} ]]; then
    echo "${name}: expected exit ${expected_rc}, got ${actual_rc}" >&2
    tail -n 20 "${output}" >&2
    exit 1
  fi
}

run_case unset __UNSET__ 0
if [[ ${mode} == bundled ]]; then
  run_case jemalloc jemalloc 0
  if [[ $(grep -c "deprecated and has no effect" "${result_dir}/jemalloc.log") -ne 1 ]]; then
    echo "jemalloc: expected exactly one deprecation warning" >&2
    exit 1
  fi
else
  run_case jemalloc jemalloc 127
fi
run_case obmalloc obmalloc 127
run_case unknown unknown 127

echo "MALLOC_BACKEND startup matrix passed (${mode})"
