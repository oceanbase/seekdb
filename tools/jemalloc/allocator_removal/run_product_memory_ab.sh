#!/usr/bin/env bash
# Copyright (c) 2026 OceanBase.
# Licensed under the Apache License, Version 2.0.

set -euo pipefail

usage()
{
  cat <<'EOF'
usage: run_product_memory_ab.sh [--smoke] BASE_BIN CANDIDATE_BIN OUTPUT_DIR

Full defaults: 5 rounds; 300-second warmup and 600-second sample for SQL,
KV-cache (384 MiB logical working set with 256 MiB cache limit), and 100k x
128 vector workloads, under default and 2G memory budgets.

Environment overrides: SERVER_CPUS, CLIENT_CPUS, THREADS, ROUNDS, WARMUP_SECONDS,
SAMPLE_SECONDS, SQL_ROWS, KV_ROWS, KV_PAYLOAD_BYTES, VECTOR_ROWS, VECTOR_DIMENSIONS.
EOF
}

smoke=false
if [[ ${1:-} == --smoke ]]; then
  smoke=true
  shift
fi
if [[ $# -ne 3 ]]; then
  usage >&2
  exit 2
fi

base_bin=$(realpath "$1")
candidate_bin=$(realpath "$2")
output_dir=$(realpath -m "$3")
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
workload=${script_dir}/product_workload.py
analyzer=${script_dir}/analyze_product_memory_ab.py

for executable in "${base_bin}" "${candidate_bin}"; do
  if [[ ! -x ${executable} ]]; then
    echo "not an executable seekdb binary: ${executable}" >&2
    exit 2
  fi
done
if [[ -e ${output_dir} && -n $(find "${output_dir}" -mindepth 1 -maxdepth 1 -print -quit) ]]; then
  echo "output directory must not exist or must be empty: ${output_dir}" >&2
  exit 2
fi

server_cpus=${SERVER_CPUS:-0-7}
client_cpus=${CLIENT_CPUS:-8-15}
threads=${THREADS:-8}
rounds=${ROUNDS:-5}
warmup_seconds=${WARMUP_SECONDS:-300}
sample_seconds=${SAMPLE_SECONDS:-600}
sql_rows=${SQL_ROWS:-100000}
kv_rows=${KV_ROWS:-786432}
kv_payload_bytes=${KV_PAYLOAD_BYTES:-512}
vector_rows=${VECTOR_ROWS:-100000}
vector_dimensions=${VECTOR_DIMENSIONS:-128}
port_base=${PORT_BASE:-45200}
if [[ ${smoke} == true ]]; then
  rounds=${ROUNDS:-1}
  warmup_seconds=${WARMUP_SECONDS:-2}
  sample_seconds=${SAMPLE_SECONDS:-5}
  sql_rows=${SQL_ROWS:-2000}
  kv_rows=${KV_ROWS:-4000}
  kv_payload_bytes=${KV_PAYLOAD_BYTES:-256}
  vector_rows=${VECTOR_ROWS:-500}
fi

mkdir -p "${output_dir}"
current_pid=
monitor_pid=

stop_server()
{
  if [[ -n ${monitor_pid} ]]; then
    kill "${monitor_pid}" 2>/dev/null || true
    wait "${monitor_pid}" 2>/dev/null || true
    monitor_pid=
  fi
  if [[ -n ${current_pid} ]] && kill -0 "${current_pid}" 2>/dev/null; then
    kill -USR1 "${current_pid}" 2>/dev/null || true
    for _ in $(seq 1 60); do
      if ! kill -0 "${current_pid}" 2>/dev/null; then
        break
      fi
      sleep 1
    done
    if kill -0 "${current_pid}" 2>/dev/null; then
      echo "seekdb did not stop after SIGUSR1: pid=${current_pid}" >&2
      return 1
    fi
    wait "${current_pid}" 2>/dev/null || true
  fi
  current_pid=
}
trap stop_server EXIT INT TERM

wait_ready()
{
  local port=$1
  local ready=0
  for _ in $(seq 1 180); do
    if mysql --protocol=tcp -h127.0.0.1 -P"${port}" -uroot -Nse \
        'SELECT START_SERVICE_TIME > 0 FROM oceanbase.V$OB_SERVER_STAT LIMIT 1' \
        2>/dev/null | grep -qx 1; then
      ready=1
      break
    fi
    if ! kill -0 "${current_pid}" 2>/dev/null; then
      echo "seekdb exited before readiness: pid=${current_pid}" >&2
      return 1
    fi
    sleep 1
  done
  if [[ ${ready} -ne 1 ]]; then
    echo "seekdb readiness timed out on port ${port}" >&2
    return 1
  fi
}

sample_process()
{
  local pid=$1
  local port=$2
  local destination=$3
  printf 'timestamp\trss_kb\tpss_kb\tcomponent\tlimit_bytes\tcommitted_bytes\treserved_bytes\treject_count\treclaim_count\n' >"${destination}"
  while kill -0 "${pid}" 2>/dev/null; do
    local timestamp rss pss rows
    timestamp=$(date +%s)
    rss=$(awk '/^Rss:/ {print $2}' "/proc/${pid}/smaps_rollup" 2>/dev/null || true)
    pss=$(awk '/^Pss:/ {print $2}' "/proc/${pid}/smaps_rollup" 2>/dev/null || true)
    rows=$(mysql --protocol=tcp -h127.0.0.1 -P"${port}" -uroot -Nse \
      'SELECT COMPONENT_NAME,LIMIT_BYTES,COMMITTED_BYTES,RESERVED_BYTES,REJECT_COUNT,RECLAIM_COUNT FROM oceanbase.V$OB_COMPONENT_MEMORY ORDER BY COMPONENT_NAME' \
      2>/dev/null || true)
    if [[ -n ${rows} ]]; then
      while IFS=$'\t' read -r component limit committed reserved reject reclaim; do
        printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
          "${timestamp}" "${rss:-0}" "${pss:-0}" "${component}" "${limit}" \
          "${committed}" "${reserved}" "${reject}" "${reclaim}" >>"${destination}"
      done <<<"${rows}"
    else
      printf '%s\t%s\t%s\tunavailable\t0\t0\t0\t0\t0\n' \
        "${timestamp}" "${rss:-0}" "${pss:-0}" >>"${destination}"
    fi
    sleep 1
  done
}

run_variant()
{
  local variant=$1
  local budget=$2
  local binary=$3
  local port=$4
  local run_dir=${output_dir}/${variant}/${budget}
  local server_dir=${run_dir}/server
  mkdir -p "${server_dir}/store/redo" "${run_dir}/results"

  local parameters=(
    --parameter datafile_size=2G
    --parameter datafile_maxsize=8G
    --parameter log_disk_size=2G
    --parameter cpu_count=8
    --parameter kvcache_memory_limit=256M
  )
  if [[ ${budget} == two_gb ]]; then
    parameters+=(--parameter memory_budget=2G)
  fi

  JE_MALLOC_CONF=stats_print:true taskset -c "${server_cpus}" "${binary}" \
    --nodaemon --port "${port}" --base-dir "${server_dir}" \
    --data-dir "${server_dir}/store" --redo-dir "${server_dir}/store/redo" \
    "${parameters[@]}" >"${run_dir}/server.console" 2>&1 &
  current_pid=$!
  printf '%s\n' "${current_pid}" >"${run_dir}/server.pid"
  wait_ready "${port}"

  taskset -c "${client_cpus}" python3 "${workload}" prepare --port "${port}" \
    --sql-rows "${sql_rows}" --kv-rows "${kv_rows}" \
    --kv-payload-bytes "${kv_payload_bytes}" --vector-rows "${vector_rows}" \
    --vector-dimensions "${vector_dimensions}" >"${run_dir}/prepare.json"
  mysql --protocol=tcp -h127.0.0.1 -P"${port}" -uroot -Nse \
    "SELECT 'sql',COUNT(*),SUM(id) FROM allocator_removal_ab.sql_workload UNION ALL SELECT 'kv',COUNT(*),SUM(id) FROM allocator_removal_ab.kv_workload UNION ALL SELECT 'vector',COUNT(*),SUM(id) FROM allocator_removal_ab.vector_workload" \
    >"${run_dir}/data-identity.tsv"

  sample_process "${current_pid}" "${port}" "${run_dir}/process-and-components.tsv" &
  monitor_pid=$!
  for workload_name in sql kv vector; do
    for round in $(seq 1 "${rounds}"); do
      taskset -c "${client_cpus}" python3 "${workload}" run --port "${port}" \
        --workload "${workload_name}" --threads "${threads}" \
        --warmup "${warmup_seconds}" --duration "${sample_seconds}" \
        --sql-rows "${sql_rows}" --kv-rows "${kv_rows}" \
        --vector-dimensions "${vector_dimensions}" \
        >"${run_dir}/results/${workload_name}-round-${round}.json"
      kill -49 "${current_pid}"
      sleep 0.2
    done
  done
  stop_server

  awk -F'\t' 'NR > 1 {if ($2 > rss) rss=$2; if ($3 > pss) pss=$3} END {print "peak_rss_kb=" rss "\npeak_pss_kb=" pss}' \
    "${run_dir}/process-and-components.tsv" >"${run_dir}/memory-summary.txt"
  grep 'allocator process memory' "${server_dir}/log/seekdb.log" \
    >"${run_dir}/jemalloc-summary.txt" || true
  sha256sum "${binary}" >"${run_dir}/binary.sha256"
}

{
  echo "base_source=${BASE_SOURCE:-not-specified}"
  echo "candidate_source=${CANDIDATE_SOURCE:-not-specified}"
  echo "base_bin=${base_bin}"
  echo "candidate_bin=${candidate_bin}"
  echo "base_sha256=$(sha256sum "${base_bin}" | awk '{print $1}')"
  echo "candidate_sha256=$(sha256sum "${candidate_bin}" | awk '{print $1}')"
  echo "server_cpus=${server_cpus}"
  echo "client_cpus=${client_cpus}"
  echo "threads=${threads}"
  echo "rounds=${rounds}"
  echo "warmup_seconds=${warmup_seconds}"
  echo "sample_seconds=${sample_seconds}"
  echo "sql_rows=${sql_rows}"
  echo "kv_rows=${kv_rows}"
  echo "kv_payload_bytes=${kv_payload_bytes}"
  echo "kv_logical_working_set_bytes=$((kv_rows * kv_payload_bytes))"
  echo "kvcache_memory_limit_bytes=$((256 * 1024 * 1024))"
  echo "vector_rows=${vector_rows}"
  echo "vector_dimensions=${vector_dimensions}"
  echo "allowed_cpus=$(taskset -pc $$ | sed 's/.*: //')"
  echo "cgroup=$(awk -F: '$1 == "0" {print $3}' /proc/self/cgroup)"
  uname -a
  lscpu | sed -n '1,24p'
  if command -v numactl >/dev/null 2>&1; then
    numactl --hardware
  fi
} >"${output_dir}/metadata.txt"

port=${port_base}
for budget in default two_gb; do
  run_variant base "${budget}" "${base_bin}" "${port}"
  port=$((port + 10))
  run_variant candidate "${budget}" "${candidate_bin}" "${port}"
  port=$((port + 10))
done

set +e
python3 "${analyzer}" "${output_dir}" >"${output_dir}/analysis.stdout" 2>"${output_dir}/analysis.stderr"
analysis_rc=$?
set -e
echo "product allocator A/B results: ${output_dir}"
if [[ ${smoke} == true ]]; then
  printf '%s\n' "${analysis_rc}" >"${output_dir}/smoke-analysis-exit-code.txt"
  echo "smoke completed; performance gate intentionally not evaluated from smoke data"
  exit 0
fi
exit "${analysis_rc}"
