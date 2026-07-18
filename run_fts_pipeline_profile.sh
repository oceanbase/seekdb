#!/usr/bin/env bash
# Rebuild/deploy the FTS benchmark instance, run the large FTS benchmark, and
# save the live DAG_ROOT FTS pipeline counters from the DDL monitor virtual table.
#
# Usage:
#   ./run_fts_pipeline_profile.sh
#   REBUILD=0 LABEL=profile MYSQL_PORT=10000 ./run_fts_pipeline_profile.sh
#   ROWS=50000 BUILD_JOBS=8 ./run_fts_pipeline_profile.sh
#   FUNCTIONAL_TESTS='ai_split_document ik_custom_dict' ./run_fts_pipeline_profile.sh

set -euo pipefail

CONTAINER="${CONTAINER:-seekdb-dev}"
WORKDIR="${WORKDIR:-/workspace/seekdb}"
MYSQL_PORT="${MYSQL_PORT:-10000}"
# A full debug build touches large unity translation units.  16 is a safer
# default than serial-ish builds while avoiding the host contention that an
# unconditional nproc (64 in the dev container) can cause.
BUILD_JOBS="${BUILD_JOBS:-16}"
REBUILD="${REBUILD:-1}"
LABEL="${LABEL:-fts_pipeline_profile}"
MONITOR_INTERVAL_SEC="${MONITOR_INTERVAL_SEC:-1}"
RUN_FUNCTIONAL_TESTS="${RUN_FUNCTIONAL_TESTS:-1}"
# These cover the Document AI splitter and custom IK dictionary paths that
# share FTS parser state with the benchmark. Set RUN_FUNCTIONAL_TESTS=0 to
# skip them, or override this list when investigating one case.
FUNCTIONAL_TESTS="${FUNCTIONAL_TESTS:-ai_split_document ik_custom_dict}"

command -v docker >/dev/null 2>&1 || { echo 'ERROR: docker is required.' >&2; exit 1; }
docker container inspect "${CONTAINER}" >/dev/null || { echo "ERROR: missing container ${CONTAINER}" >&2; exit 1; }

if [[ "${REBUILD}" == "1" ]]; then
  # This builds inside seekdb-dev and recreates only the ftsbench instance.
  BUILD_JOBS="${BUILD_JOBS}" CONTAINER="${CONTAINER}" WORKDIR="${WORKDIR}" ./refresh_fts_bench_env.sh
fi

docker exec --interactive --user "$(id -u):$(id -g)" --workdir "${WORKDIR}" \
  --env "MYSQL_PORT=${MYSQL_PORT}" \
  --env "LABEL=${LABEL}" \
  --env "MONITOR_INTERVAL_SEC=${MONITOR_INTERVAL_SEC}" \
  --env "RUN_FUNCTIONAL_TESTS=${RUN_FUNCTIONAL_TESTS:-1}" \
  --env "FUNCTIONAL_TESTS=${FUNCTIONAL_TESTS:-ai_split_document ik_custom_dict}" \
  --env "ROWS=${ROWS:-20000}" --env "BATCH=${BATCH:-500}" \
  --env "ROUNDS=${ROUNDS:-3000}" --env "QUERY_ROUNDS=${QUERY_ROUNDS:-200}" \
  --env "SAMPLES=${SAMPLES:-3}" --env "WARMUP=${WARMUP:-30}" \
  --env BASH_ENV= \
  "${CONTAINER}" bash -s <<'IN_CONTAINER'
set -euo pipefail

MYSQL_CMD="mysql -h127.0.0.1 -P${MYSQL_PORT} -uroot -N -s --default-character-set=utf8mb4"
RESULT_DIR="tools/benchmark/results"
STAMP="$(date +%Y%m%d_%H%M%S)"
BENCH_REPORT="${RESULT_DIR}/fts_pipeline_${STAMP}.txt"
MONITOR_REPORT="${RESULT_DIR}/fts_pipeline_monitor_${STAMP}.tsv"
mkdir -p "${RESULT_DIR}"

monitor_pid=''
monitor_available=0
cleanup() {
  if [[ -n "${monitor_pid}" ]]; then
    kill "${monitor_pid}" 2>/dev/null || true
    wait "${monitor_pid}" 2>/dev/null || true
  fi
}
trap cleanup EXIT

run_functional_tests() {
  if [[ "${RUN_FUNCTIONAL_TESTS}" != "1" ]]; then
    echo 'Skipping functional tests (RUN_FUNCTIONAL_TESTS is not 1).'
    return
  fi

  local mysqltest='' candidate
  for candidate in \
    "$(dirname "$(command -v mysql)")/mysqltest" \
    "deps/3rd/u01/obclient/bin/mysqltest" \
    "rpm/.dep_create/var/u01/obclient/bin/mysqltest"; do
    if [[ -x "${candidate}" ]]; then
      mysqltest="${candidate}"
      break
    fi
  done
  if [[ -z "${mysqltest}" ]]; then
    candidate="$(find deps/3rd rpm/.dep_create/var -maxdepth 8 -path '*/bin/mysqltest' -type f -print -quit 2>/dev/null || true)"
    [[ -n "${candidate}" ]] && mysqltest="${candidate}"
  fi
  [[ -n "${mysqltest}" && -x "${mysqltest}" ]] \
    || { echo 'ERROR: mysqltest was not found.' >&2; exit 1; }

  MYSQL_TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/fts-pipeline-mtr.XXXXXX")"
  export MYSQL_TMP_DIR
  trap 'rm -rf "${MYSQL_TMP_DIR}"; cleanup' EXIT

  ${MYSQL_CMD} -e 'CREATE DATABASE IF NOT EXISTS test;'
  echo "=== Functional tests: ${FUNCTIONAL_TESTS} ==="
  for test_name in ${FUNCTIONAL_TESTS}; do
    local test_file="tools/deploy/mysql_test/test_suite/ai_funcs/t/${test_name}.test"
    local result_file="tools/deploy/mysql_test/test_suite/ai_funcs/r/${test_name}.result"
    local test_tmp_dir="${MYSQL_TMP_DIR}/${test_name}"
    [[ -f "${test_file}" && -f "${result_file}" ]] \
      || { echo "ERROR: missing mysqltest case '${test_name}'." >&2; exit 1; }
    mkdir -p "${test_tmp_dir}"
    cp "${test_file}" "${test_tmp_dir}/${test_name}.test"
    cp "${result_file}" "${test_tmp_dir}/${test_name}.result"
    echo "===================== ai_funcs/${test_name} ====================="
    "${mysqltest}" --host=127.0.0.1 --port="${MYSQL_PORT}" --user=root --database=test \
      --test-file="${test_tmp_dir}/${test_name}.test" \
      --result-file="${test_tmp_dir}/${test_name}.result"
    echo ">>> ${test_name}: PASS"
  done
}

run_functional_tests

if [[ "$(${MYSQL_CMD} -e "SELECT COUNT(*) FROM information_schema.tables
    WHERE table_schema = 'oceanbase' AND table_name = '__all_virtual_ddl_dag_monitor';" 2>/dev/null || true)" == "1" ]]; then
  monitor_available=1
  echo -e 'captured_at\tdag_id\ttrace_id\tcreate_time\tfinish_time\tmessage' > "${MONITOR_REPORT}"
  (
    while true; do
      # DAG_ROOT contains the aggregate FTS counters added by the pipeline change.
      ${MYSQL_CMD} -e "SELECT NOW(6), dag_id, trace_id, create_time, finish_time, message
        FROM oceanbase.__all_virtual_ddl_dag_monitor
        WHERE task_info = 'DAG_ROOT' AND message LIKE '%fts_tokenized_word_cnt%';" \
        >> "${MONITOR_REPORT}" 2>/dev/null || true
      sleep "${MONITOR_INTERVAL_SEC}"
    done
  ) &
  monitor_pid="$!"
else
  echo "DDL DAG monitor is unavailable in this SeekDB version; skipping pipeline-counter capture."
fi

echo "Benchmark report: ${BENCH_REPORT}"
if [[ "${monitor_available}" == 1 ]]; then
  echo "Monitor snapshots: ${MONITOR_REPORT}"
fi
MYSQL="${MYSQL_CMD}" \
MYSQL_VERBOSE="mysql -h127.0.0.1 -P${MYSQL_PORT} -uroot --default-character-set=utf8mb4" \
LABEL="${LABEL}" OUTPUT="${BENCH_REPORT}" \
ROWS="${ROWS}" BATCH="${BATCH}" ROUNDS="${ROUNDS}" QUERY_ROUNDS="${QUERY_ROUNDS}" \
SAMPLES="${SAMPLES}" WARMUP="${WARMUP}" \
./tools/benchmark/fts_large_bench.sh

if [[ "${monitor_available}" == 1 ]]; then
  echo
  echo 'Final FTS doc-word stage counters (DAG_ROOT):'
  ${MYSQL_CMD} -e "SELECT create_time, finish_time, message
    FROM oceanbase.__all_virtual_ddl_dag_monitor
    WHERE task_info = 'DAG_ROOT'
      AND message LIKE '%\"is_fts_doc_word_build\": 1%'
    ORDER BY create_time DESC LIMIT 10;"
fi
IN_CONTAINER
