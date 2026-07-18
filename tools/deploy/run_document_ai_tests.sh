#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build_debug"
BINARY="${BUILD_DIR}/src/observer/seekdb"
OBCLIENT="${REPO_ROOT}/deps/3rd/u01/obclient/bin/obclient"
MYSQLTEST="${REPO_ROOT}/deps/3rd/u01/obclient/bin/mysqltest"
RUN_DIR="$(mktemp -d /tmp/seekdb-document-ai-test.XXXXXX)"
MYSQL_TMP_DIR="${RUN_DIR}/mysqltmp"
STARTED=0
DO_BUILD=1

usage()
{
  cat <<'EOF'
Usage: tools/deploy/run_document_ai_tests.sh [--no-build]

Build seekdb, start an isolated temporary instance, run the two official
Document AI mysqltests, and stop the instance automatically.

  --no-build  Reuse build_debug/src/observer/seekdb without rebuilding it.
EOF
}

cleanup()
{
  local status=$?
  local pid=""
  local stopped=1
  trap - EXIT

  if [[ "${STARTED}" -eq 1 && -f "${RUN_DIR}/run/seekdb.pid" ]]; then
    pid="$(<"${RUN_DIR}/run/seekdb.pid")"
    if [[ "${pid}" =~ ^[0-9]+$ ]] && kill -0 "${pid}" 2>/dev/null; then
      echo "[cleanup] stopping temporary seekdb (pid ${pid})"
      kill "${pid}" 2>/dev/null || true
      for _ in {1..20}; do
        if ! kill -0 "${pid}" 2>/dev/null; then
          stopped=0
          break
        fi
        sleep 1
      done
    else
      stopped=0
    fi
  else
    stopped=0
  fi

  if [[ "${status}" -eq 0 && "${stopped}" -eq 0
        && "${RUN_DIR}" == /tmp/seekdb-document-ai-test.* ]]; then
    rm -rf -- "${RUN_DIR}"
  elif [[ "${status}" -ne 0 ]]; then
    echo "[failed] temporary files and logs retained at: ${RUN_DIR}" >&2
  else
    echo "[warning] seekdb may still be running; files retained at: ${RUN_DIR}" >&2
  fi

  exit "${status}"
}

trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

while [[ $# -gt 0 ]]; do
  case "$1" in
    --no-build)
      DO_BUILD=0
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if [[ ! -x "${OBCLIENT}" || ! -x "${MYSQLTEST}" ]]; then
  echo "Missing obclient or mysqltest under deps/3rd/u01/obclient/bin" >&2
  exit 1
fi

if "${OBCLIENT}" -h127.0.0.1 -P2881 -uroot -A -e 'SELECT 1' >/dev/null 2>&1; then
  echo "Port 2881 is already serving a database. Stop it before running this script." >&2
  exit 1
fi

if [[ "${DO_BUILD}" -eq 1 ]]; then
  echo "[1/4] building seekdb"
  cmake --build "${BUILD_DIR}" --target seekdb -j2
fi

if [[ ! -x "${BINARY}" ]]; then
  echo "seekdb binary not found: ${BINARY}" >&2
  echo "Run without --no-build first." >&2
  exit 1
fi

echo "[2/4] starting isolated seekdb"
mkdir -p "${MYSQL_TMP_DIR}"
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

"${OBCLIENT}" -h127.0.0.1 -P2881 -uroot -A \
  -e 'CREATE DATABASE IF NOT EXISTS test;'

run_mysqltest()
{
  local case_name=$1
  echo "[test] ${case_name}"
  MYSQL_TMP_DIR="${MYSQL_TMP_DIR}" \
  OBMYSQL_MS0=127.0.0.1 \
  OBMYSQL_PORT=2881 \
  OBMYSQL_PWD= \
  "${MYSQLTEST}" \
    --host=127.0.0.1 \
    --port=2881 \
    --user=root \
    --database=test \
    --test-file="${REPO_ROOT}/tools/deploy/mysql_test/test_suite/ai_funcs/t/${case_name}.test" \
    --result-file="${REPO_ROOT}/tools/deploy/mysql_test/test_suite/ai_funcs/r/${case_name}.result"
}

echo "[3/4] running official Document AI tests"
run_mysqltest load_file
run_mysqltest ai_split_document

echo "[4/4] PASS: load_file and ai_split_document"
