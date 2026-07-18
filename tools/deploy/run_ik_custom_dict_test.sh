#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build_debug"
BINARY="${BUILD_DIR}/src/observer/seekdb"
OBCLIENT="${REPO_ROOT}/deps/3rd/u01/obclient/bin/obclient"
MYSQLTEST="${REPO_ROOT}/deps/3rd/u01/obclient/bin/mysqltest"
RUN_DIR="$(mktemp -d /tmp/seekdb-ik-custom-dict-test.XXXXXX)"
MYSQL_TMP_DIR="${RUN_DIR}/mysqltmp"
STARTED=0
DO_BUILD=1

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
        && "${RUN_DIR}" == /tmp/seekdb-ik-custom-dict-test.* ]]; then
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

if [[ "${1:-}" == "--no-build" ]]; then
  DO_BUILD=0
  shift
fi
if [[ $# -ne 0 ]]; then
  echo "Usage: tools/deploy/run_ik_custom_dict_test.sh [--no-build]" >&2
  exit 2
fi

if [[ ! -x "${OBCLIENT}" || ! -x "${MYSQLTEST}" ]]; then
  echo "Missing obclient or mysqltest under deps/3rd/u01/obclient/bin" >&2
  exit 1
fi
if "${OBCLIENT}" -h127.0.0.1 -P2881 -uroot -A -e 'SELECT 1' >/dev/null 2>&1; then
  echo "Port 2881 is already serving a database. Stop it before running this script." >&2
  exit 1
fi

if [[ "${DO_BUILD}" -eq 1 ]]; then
  echo "[1/5] building seekdb"
  cmake --build "${BUILD_DIR}" --target seekdb -j2
fi
if [[ ! -x "${BINARY}" ]]; then
  echo "seekdb binary not found: ${BINARY}" >&2
  exit 1
fi

echo "[2/5] starting isolated seekdb"
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

"${OBCLIENT}" -h127.0.0.1 -P2881 -uroot -A -e 'CREATE DATABASE IF NOT EXISTS test;'

echo "[3/5] running official ik_custom_dict mysqltest"
MYSQL_TMP_DIR="${MYSQL_TMP_DIR}" \
OBMYSQL_MS0=127.0.0.1 \
OBMYSQL_PORT=2881 \
OBMYSQL_PWD= \
"${MYSQLTEST}" \
  --host=127.0.0.1 \
  --port=2881 \
  --user=root \
  --database=test \
  --test-file="${REPO_ROOT}/tools/deploy/mysql_test/test_suite/ai_funcs/t/ik_custom_dict.test" \
  --result-file="${REPO_ROOT}/tools/deploy/mysql_test/test_suite/ai_funcs/r/ik_custom_dict.result"

echo "[4/5] checking refresh-gated cache semantics"
"${OBCLIENT}" -h127.0.0.1 -P2881 -uroot -A -e '
  DROP DATABASE IF EXISTS ik_refresh_test;
  CREATE DATABASE ik_refresh_test;
  USE ik_refresh_test;
  CREATE TABLE refresh_dict (word varchar(100) primary key)
    ORGANIZATION INDEX DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci FULLTEXT_DICT="Y";
  INSERT INTO refresh_dict VALUES ("旧词");
  CREATE TABLE docs (
    id int primary key auto_increment,
    content varchar(100),
    FULLTEXT INDEX ft_content(content) WITH PARSER ik
      PARSER_PROPERTIES=(dict_table="refresh_dict")
  ) DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_bin;
  INSERT INTO docs(content) VALUES ("旧词");
  INSERT INTO refresh_dict VALUES ("新词汇");
  INSERT INTO docs(content) VALUES ("新词汇");
'

before_refresh=$("${OBCLIENT}" -h127.0.0.1 -P2881 -uroot -A -N -s \
  -e 'SELECT COUNT(*) FROM ik_refresh_test.docs
      WHERE MATCH(content) AGAINST("新词汇" IN BOOLEAN MODE);')
if [[ "${before_refresh}" != "0" ]]; then
  echo "Expected custom dictionary update to remain invisible before REFRESH; got ${before_refresh}" >&2
  exit 1
fi

"${OBCLIENT}" -h127.0.0.1 -P2881 -uroot -A -e '
  USE ik_refresh_test;
  ALTER SYSTEM REFRESH FULLTEXT DICT refresh_dict;
  ALTER SYSTEM REFRESH FULLTEXT DICT "ik_refresh_test.refresh_dict";
  ALTER SYSTEM REFRESH FULLTEXT DICT "refresh_dict";
  INSERT INTO docs(content) VALUES ("新词汇");
'
after_refresh=$("${OBCLIENT}" -h127.0.0.1 -P2881 -uroot -A -N -s \
  -e 'SELECT COUNT(*) FROM ik_refresh_test.docs
      WHERE MATCH(content) AGAINST("新词汇" IN BOOLEAN MODE);')
if [[ "${after_refresh}" != "1" ]]; then
  echo "Expected exactly one row indexed after REFRESH; got ${after_refresh}" >&2
  exit 1
fi
"${OBCLIENT}" -h127.0.0.1 -P2881 -uroot -A -e 'DROP DATABASE ik_refresh_test;'

echo "[5/5] PASS: ik_custom_dict and refresh cache semantics"
