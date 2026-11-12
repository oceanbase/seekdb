#!/usr/bin/env bash
set -euo pipefail

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$repo"

binary="$repo/build/bin/src/observer/seekdb"
client="$repo/deps/3rd/u01/obclient/bin/obclient"
port="${SEEKDB_TEST_PORT:-10154}"
trials="${SEEKDB_TEST_TRIALS:-3}"

[[ -x "$binary" ]] || { echo "缺少编译产物: $binary"; exit 1; }
[[ -x "$client" ]] || { echo "缺少客户端: $client"; exit 1; }

run_root=$(mktemp -d "$repo/build/issue1544.XXXXXX")
active_base=""

cleanup() {
  if [[ -n "$active_base" ]]; then
    python3 tools/deploy/sdb.py stop --base-dir "$active_base" || true
  fi
}
trap cleanup EXIT

start_and_wait() {
  python3 tools/deploy/sdb.py start \
    --binary "$binary" \
    --base-dir "$active_base" \
    --port "$port" \
    --parameter memory_budget=1G \
    --parameter log_disk_size=2G \
    --parameter datafile_size=512M

  python3 tools/deploy/sdb.py wait-ready \
    --client "$client" \
    --base-dir "$active_base" \
    --port "$port" \
    --timeout 240
}

sql() {
  "$client" -h127.0.0.1 -P"$port" -uroot -N -B -e "$1"
}

echo "测试目录: $run_root"

for ((trial = 1; trial <= trials; trial++)); do
  active_base="$run_root/trial-$trial"
  echo "第 $trial 轮：首次启动"
  start_and_wait

  sql "CREATE DATABASE issue1544;
       CREATE TABLE issue1544.t (id INT PRIMARY KEY, v VARCHAR(20));
       INSERT INTO issue1544.t VALUES (1, 'persist');"

  [[ "$(sql "SELECT COUNT(*) FROM issue1544.t WHERE id=1 AND v='persist'")" == "1" ]]
  python3 tools/deploy/sdb.py stop --base-dir "$active_base"

  echo "第 $trial 轮：重开并检查持久化"
  start_and_wait
  [[ "$(sql "SELECT COUNT(*) FROM issue1544.t WHERE id=1 AND v='persist'")" == "1" ]]

  python3 tools/deploy/sdb.py stop --base-dir "$active_base"
  active_base=""
done

echo "PASS: $trials 轮启动与持久化测试通过；日志保留在 $run_root"
