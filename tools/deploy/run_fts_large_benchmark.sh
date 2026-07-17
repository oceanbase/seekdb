#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build_debug"
BINARY="${BUILD_DIR}/src/observer/seekdb"
OBCLIENT="${REPO_ROOT}/deps/3rd/u01/obclient/bin/obclient"
BENCHMARK="${REPO_ROOT}/tools/benchmark/fts_large_bench.sh"
SCORER="${REPO_ROOT}/tools/benchmark/fts_large_bench_score.py"
RUN_DIR="$(mktemp -d /tmp/seekdb-fts-large-bench.XXXXXX)"
REPORT="${REPORT:-/tmp/seekdb-fts-large-$(date +%Y%m%d-%H%M%S).txt}"
STARTED=0
DO_BUILD=1

cleanup()
{
  local status=$?
  local pid=""
  trap - EXIT

  if [[ "${STARTED}" -eq 1 && -f "${RUN_DIR}/run/seekdb.pid" ]]; then
    pid="$(<"${RUN_DIR}/run/seekdb.pid")"
    if [[ "${pid}" =~ ^[0-9]+$ ]] && kill -0 "${pid}" 2>/dev/null; then
      echo "[cleanup] stopping temporary seekdb (pid ${pid})" >&2
      kill "${pid}" 2>/dev/null || true
      for _ in {1..20}; do
        if ! kill -0 "${pid}" 2>/dev/null; then
          break
        fi
        sleep 1
      done
    fi
  fi

  if [[ "${status}" -eq 0 && "${RUN_DIR}" == /tmp/seekdb-fts-large-bench.* ]]; then
    rm -rf -- "${RUN_DIR}"
  elif [[ "${status}" -ne 0 ]]; then
    echo "[failed] temporary files and logs retained at: ${RUN_DIR}" >&2
  fi
  exit "${status}"
}

trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

if [[ "${1:-}" == "--no-build" ]]; then
  DO_BUILD=0
  shift
fi
if [[ $# -ne 0 ]]; then
  echo "Usage: tools/deploy/run_fts_large_benchmark.sh [--no-build]" >&2
  exit 2
fi

if [[ ! -x "${OBCLIENT}" ]]; then
  echo "Missing obclient: ${OBCLIENT}" >&2
  exit 1
fi
if "${OBCLIENT}" -h127.0.0.1 -P2881 -uroot -A -e 'SELECT 1' >/dev/null 2>&1; then
  echo "Port 2881 is already serving a database. Stop it before running this script." >&2
  exit 1
fi

if [[ "${DO_BUILD}" -eq 1 ]]; then
  echo "[1/4] building seekdb" >&2
  cmake --build "${BUILD_DIR}" --target seekdb -j2
fi
if [[ ! -x "${BINARY}" ]]; then
  echo "seekdb binary not found: ${BINARY}" >&2
  exit 1
fi

echo "[2/4] starting isolated seekdb" >&2
if ! ln "${BINARY}" "${RUN_DIR}/seekdb" 2>/dev/null; then
  cp "${BINARY}" "${RUN_DIR}/seekdb"
fi
chmod +x "${RUN_DIR}/seekdb"
(
  cd "${RUN_DIR}"
  ./seekdb
)
STARTED=1

ready=0
for _ in {1..60}; do
  if "${OBCLIENT}" -h127.0.0.1 -P2881 -uroot -A -e 'SELECT 1' >/dev/null 2>&1; then
    ready=1
    break
  fi
  sleep 2
done
if [[ "${ready}" -ne 1 ]]; then
  echo "seekdb did not become ready in 120 seconds" >&2
  tail -n 100 "${RUN_DIR}/log/seekdb.log" 2>/dev/null || true
  exit 1
fi

echo "[3/4] running official FTS large benchmark" >&2
MYSQL="${OBCLIENT} -h127.0.0.1 -P2881 -uroot -A -N -s --default-character-set=utf8mb4" \
MYSQL_VERBOSE="${OBCLIENT} -h127.0.0.1 -P2881 -uroot -A --default-character-set=utf8mb4" \
LABEL="${LABEL:-local}" \
OUTPUT="${REPORT}" \
bash "${BENCHMARK}"

echo "[4/4] score" >&2
python3 "${SCORER}" "${REPORT}"
echo "Report: ${REPORT}" >&2
