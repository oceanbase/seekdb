#!/usr/bin/env bash
# Copyright (c) 2026 OceanBase.
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)

# Override these with environment variables when testing another build.
BASE_DIR=${1:-${GIS_TEST_BASE:-/data/wangzelin.wzl/test/gis_online}}
PORT=${2:-${GIS_TEST_PORT:-2931}}
MYSQL_USER=${MYSQL_USER:-root}
MYSQL_HOST=${MYSQL_HOST:-127.0.0.1}
MYSQL_PASSWORD=${MYSQL_PASSWORD:-}
SEEKDB_BIN=${SEEKDB_BIN:-$REPO_ROOT/build_release/src/observer/seekdb}
GIS_SO=${GIS_SO:-$REPO_ROOT/build_release/plugins/gis/seekdb_gis.so}
GIS_SOURCE=${GIS_SOURCE:-$REPO_ROOT/plugins/gis}
DB_NAME=${GIS_TEST_DB:-gis_live_$(date +%Y%m%d%H%M%S)}

PLUGIN_DIR="$BASE_DIR/plugins/gis"
EXT_DIR="$BASE_DIR/share/seekdb/extension"
LOG_DIR="$BASE_DIR/log"
CONSOLE_LOG="$LOG_DIR/gis-live-console.log"
PID_FILE="$BASE_DIR/run/gis-live.pid"

die() { echo "ERROR: $*" >&2; exit 1; }

command -v mysql >/dev/null 2>&1 || die "mysql client not found"
[[ -x "$SEEKDB_BIN" ]] || die "seekdb binary not found or not executable: $SEEKDB_BIN"
[[ -f "$GIS_SO" ]] || die "GIS plugin not found: $GIS_SO"
[[ -f "$GIS_SOURCE/plugin.toml" ]] || die "GIS plugin.toml not found: $GIS_SOURCE/plugin.toml"
[[ -f "$GIS_SOURCE/sql/gis.control" ]] || die "GIS control file not found: $GIS_SOURCE/sql/gis.control"
[[ -f "$GIS_SOURCE/sql/gis--1.0.sql" ]] || die "GIS SQL package not found: $GIS_SOURCE/sql/gis--1.0.sql"

mkdir -p "$PLUGIN_DIR" "$EXT_DIR" "$LOG_DIR" "$BASE_DIR/run"
cp -f "$SEEKDB_BIN" "$BASE_DIR/seekdb"
cp -f "$GIS_SO" "$PLUGIN_DIR/seekdb_gis.so"
cp -f "$GIS_SOURCE/plugin.toml" "$PLUGIN_DIR/plugin.toml"
cp -f "$GIS_SOURCE/sql/gis.control" "$GIS_SOURCE/sql/gis--1.0.sql" "$EXT_DIR/"
chmod +x "$BASE_DIR/seekdb"

# A previously initialized base directory persists the original MySQL port;
# seekdb intentionally ignores a new --port value on subsequent starts.
if [[ -f "$BASE_DIR/run/telemetry.json" ]]; then
  persisted_port=$(sed -n 's/.*"port"[[:space:]]*:[[:space:]]*\([0-9][0-9]*\).*/\1/p' \
    "$BASE_DIR/run/telemetry.json" | head -1)
  if [[ -n "$persisted_port" && "$persisted_port" != "$PORT" ]]; then
    die "base directory is initialized for port $persisted_port, but requested $PORT; use $persisted_port or a fresh BASE_DIR"
  fi
fi

mysql_args=(-h"$MYSQL_HOST" -P"$PORT" -u"$MYSQL_USER" --connect-timeout=2)
if [[ -n "$MYSQL_PASSWORD" ]]; then
  mysql_args+=(-p"$MYSQL_PASSWORD")
fi

if mysql "${mysql_args[@]}" -e 'SELECT 1' >/dev/null 2>&1; then
  die "port $PORT is already serving MySQL; choose another port or stop the existing instance"
fi

echo "Starting seekdb: base=$BASE_DIR port=$PORT"
server_command=("$BASE_DIR/seekdb"
  --base-dir="$BASE_DIR"
  --extension-dir="$EXT_DIR"
  --port="$PORT"
  --nodaemon
  --log-level="${GIS_LOG_LEVEL:-WDIAG}")
if [[ "${GIS_GDB:-0}" == 1 ]]; then
  command -v gdb >/dev/null 2>&1 || die "GIS_GDB=1 requires gdb"
  echo "GIS_GDB=1: crash backtrace will be appended to $CONSOLE_LOG"
  gdb -q -batch -ex run -ex 'thread apply all bt 40' -ex quit --args "${server_command[@]}" \
    >"$CONSOLE_LOG" 2>&1 &
else
  "${server_command[@]}" >"$CONSOLE_LOG" 2>&1 &
fi
server_pid=$!
printf '%s\n' "$server_pid" > "$PID_FILE"

cleanup_on_start_failure() {
  if kill -0 "$server_pid" 2>/dev/null; then
    kill "$server_pid" 2>/dev/null || true
  fi
}
trap cleanup_on_start_failure EXIT

ready=0
for _ in $(seq 1 90); do
  if ! kill -0 "$server_pid" 2>/dev/null; then
    echo "seekdb exited before MySQL became ready" >&2
    tail -80 "$CONSOLE_LOG" >&2 || true
    exit 1
  fi
  if mysql "${mysql_args[@]}" -e 'SELECT 1' >/dev/null 2>&1; then
    ready=1
    break
  fi
  sleep 1
done
[[ "$ready" == 1 ]] || { tail -80 "$CONSOLE_LOG" >&2 || true; die "MySQL was not ready after 90 seconds"; }

echo "MySQL is ready; installing GIS plugin and extension in database $DB_NAME"
if mysql "${mysql_args[@]}" -N -B -e 'SHOW PLUGINS' | awk -F '\t' '$1 == "org.seekdb.gis" { found=1 } END { exit(found ? 0 : 1) }'; then
  echo "GIS plugin is already installed; continuing"
else
  mysql "${mysql_args[@]}" -e \
    "INSTALL PLUGIN \`org.seekdb.gis\` SONAME 'gis/seekdb_gis.so';"
fi

if ! mysql "${mysql_args[@]}" <<SQL
CREATE DATABASE \`$DB_NAME\`;
USE \`$DB_NAME\`;
CREATE EXTENSION gis;
SELECT ST_X(POINT(1, 2)) AS x, ST_Y(POINT(1, 2)) AS y;
SELECT ST_Distance(POINT(0, 0), POINT(3, 4)) AS distance;
SELECT ST_Area(ST_GeomFromText('POLYGON((0 0,4 0,4 3,0 3,0 0))')) AS area;
SQL
then
  echo "GIS SQL test failed; recent server output:" >&2
  tail -120 "$CONSOLE_LOG" >&2 || true
  if [[ -f "$BASE_DIR/log/seekdb.log" ]]; then
    echo "Recent seekdb diagnostics:" >&2
    grep -n -E '5542|1305|does not exist|native routine binding|CREATE EXTENSION|extension' \
      "$BASE_DIR/log/seekdb.log" | tail -80 >&2 || true
  fi
  exit 1
fi

trap - EXIT
echo "PASS: GIS live plugin and SQL extension test"
echo "Server PID: $server_pid"
echo "Console log: $CONSOLE_LOG"
echo "Database retained for diagnosis: $DB_NAME"
echo "Stop server with: kill $server_pid"
