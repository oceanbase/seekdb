#!/usr/bin/env bash
# Copyright (c) 2026 OceanBase.
# Licensed under the Apache License, Version 2.0.

set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 ]]; then
  echo "usage: $0 BUILD_DIR [OUTPUT_DIR]" >&2
  exit 2
fi

build_dir=$1
output_dir=${2:-${build_dir}/quota-benchmark-results}
benchmark=${build_dir}/unittest/benchmark/memory_quota_benchmark
warmup_ms=${WARMUP_MS:-2000}
duration_ms=${DURATION_MS:-10000}
repeats=${REPEATS:-5}
thread_counts=${THREAD_COUNTS:-"1 8 32"}
candidate_sha=${CANDIDATE_SHA:-$(git rev-parse HEAD)}
base_sha=${BASE_SHA:-$(git merge-base HEAD upstream/master 2>/dev/null || git rev-parse HEAD^)}
candidate_diff_sha256=$({
  git diff --binary HEAD
  while IFS= read -r -d '' path; do
    printf 'untracked\0%s\0' "${path}"
    sha256sum "${path}"
  done < <(git ls-files --others --exclude-standard -z | sort -z)
} | sha256sum | awk '{print $1}')

make -C "${build_dir}" memory_quota_benchmark
mkdir -p "${output_dir}"
allowed_cpus=$(taskset -pc $$ | sed 's/.*: //')
benchmark_cpus=${BENCHMARK_CPUS:-${allowed_cpus}}

expand_cpu_list()
{
  local item first last cpu
  IFS=',' read -ra items <<<"$1"
  for item in "${items[@]}"; do
    if [[ ${item} == *-* ]]; then
      first=${item%-*}
      last=${item#*-}
      for ((cpu = first; cpu <= last; ++cpu)); do
        echo "${cpu}"
      done
    else
      echo "${item}"
    fi
  done
}

mapfile -t benchmark_cpu_ids < <(expand_cpu_list "${benchmark_cpus}")
for threads in ${thread_counts}; do
  if (( threads < 1 || ${#benchmark_cpu_ids[@]} < threads )); then
    echo "quota benchmark cannot bind ${threads} threads to ${benchmark_cpus}" >&2
    exit 2
  fi
done

cpu_set_for_threads()
{
  local threads=$1 cpu_set
  cpu_set=$(IFS=,; echo "${benchmark_cpu_ids[*]:0:${threads}}")
  echo "${cpu_set}"
}

metadata=${output_dir}/metadata.txt
{
  echo "candidate_sha=${candidate_sha}"
  echo "candidate_diff_sha256=${candidate_diff_sha256}"
  echo "base_sha=${base_sha}"
  echo "base_mode=legacy_sum_and_ctx_hold_precheck_seq_cst_admission_release_control"
  echo "base_identity=algorithmic_control_extracted_from_base_sha"
  echo "candidate_mode=memory_quota_reserve_reconcile_release"
  echo "benchmark_sha256=$(sha256sum "${benchmark}" | awk '{print $1}')"
  echo "allowed_cpus=${allowed_cpus}"
  echo "benchmark_cpus=${benchmark_cpus}"
  echo "cpu_binding=first_N_cpus_for_N_threads"
  echo "execution_order=paired_interleaved_alternating"
  echo "thread_counts=${thread_counts}"
  echo "warmup_ms=${warmup_ms}"
  echo "duration_ms=${duration_ms}"
  echo "repeats=${repeats}"
  uname -a
  lscpu | sed -n '1,24p'
} >"${metadata}"

results=${output_dir}/results.tsv
printf 'mode\tthreads\trepeat\toutput\n' >"${results}"
for threads in ${thread_counts}; do
  cpu_set=$(cpu_set_for_threads "${threads}")
  for repeat in $(seq 1 "${repeats}"); do
    if (( repeat % 2 == 1 )); then
      modes=(legacy_hold quota)
    else
      modes=(quota legacy_hold)
    fi
    for mode in "${modes[@]}"; do
      output=$(taskset -c "${cpu_set}" "${benchmark}" \
        "${mode}" "${threads}" "${warmup_ms}" "${duration_ms}")
      printf '%s\t%s\t%s\t%s\n' \
        "${mode}" "${threads}" "${repeat}" "${output}" >>"${results}"
    done
  done
done

perf_results=${output_dir}/perf-cache-misses.tsv
printf 'mode\tthreads\tcache_misses\n' >"${perf_results}"
if command -v perf >/dev/null 2>&1; then
  perf_available=true
  for mode in legacy_hold quota; do
    for threads in ${thread_counts}; do
      cpu_set=$(cpu_set_for_threads "${threads}")
      perf_output=$(mktemp)
      if perf stat -x, -e cache-misses -o "${perf_output}" -- \
          taskset -c "${cpu_set}" "${benchmark}" "${mode}" "${threads}" \
          "${warmup_ms}" "${duration_ms}" >/dev/null 2>&1; then
        cache_misses=$(awk -F, '$3 ~ /^cache-misses/ { gsub(/ /, "", $1); print $1 }' \
          "${perf_output}")
        if [[ -z ${cache_misses} || ${cache_misses} == *"not supported"* ]]; then
          cache_misses=unavailable
          perf_available=false
        fi
        printf '%s\t%s\t%s\n' "${mode}" "${threads}" "${cache_misses}" \
          >>"${perf_results}"
      else
        perf_available=false
      fi
      rm -f "${perf_output}"
    done
  done
  if [[ ${perf_available} != true ]]; then
    echo "# perf stat unavailable or not permitted for one or more runs" \
      >>"${perf_results}"
  fi
else
  echo "# perf executable unavailable" >>"${perf_results}"
fi

echo "quota benchmark results: ${output_dir}"
