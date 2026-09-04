#!/usr/bin/env bash

set -Eeuo pipefail

usage()
{
  cat <<'EOF'
Usage: run_gpu_smoke.sh --observer PATH --bridge PATH --base-dir PATH --port PORT
                        --render PATH --evidence PATH
EOF
}

OBSERVER=""
BRIDGE=""
BASE_DIR=""
PORT=""
RENDER=""
EVIDENCE=""
while (($# > 0)); do
  case "$1" in
    --observer) OBSERVER=$2; shift 2 ;;
    --bridge) BRIDGE=$2; shift 2 ;;
    --base-dir) BASE_DIR=$2; shift 2 ;;
    --port) PORT=$2; shift 2 ;;
    --render) RENDER=$2; shift 2 ;;
    --evidence) EVIDENCE=$2; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

[[ -x "$OBSERVER" ]] || { echo "observer is not executable: $OBSERVER" >&2; exit 2; }
[[ -f "$BRIDGE" ]] || { echo "bridge does not exist: $BRIDGE" >&2; exit 2; }
[[ -c /dev/kfd ]] || { echo "/dev/kfd is unavailable" >&2; exit 2; }
[[ -c "$RENDER" ]] || { echo "render node is unavailable: $RENDER" >&2; exit 2; }
[[ "$PORT" =~ ^[0-9]+$ ]] || { echo "invalid port: $PORT" >&2; exit 2; }
[[ -n "$BASE_DIR" && "$BASE_DIR" != "/" ]] || { echo "unsafe base-dir" >&2; exit 2; }
[[ -n "$EVIDENCE" && "$EVIDENCE" != "/" ]] || { echo "unsafe evidence path" >&2; exit 2; }
command -v mysql >/dev/null
command -v python3 >/dev/null
command -v timeout >/dev/null

MAIN_DIR=$BASE_DIR/main
NO_GPU_DIR=$BASE_DIR/no-gpu
DATA_DIR=$EVIDENCE/data
TRACE=$EVIDENCE/obvsag-trace.log
NO_GPU_TRACE=$EVIDENCE/no-gpu-trace.log
CASES=$EVIDENCE/cases.tsv
SUMMARY=$EVIDENCE/summary.json
MAIN_LOG=$EVIDENCE/observer-main.log
NO_GPU_LOG=$EVIDENCE/observer-no-gpu.log
WAIT_FIFO=$BASE_DIR/.wait
MAIN_PID=""
NO_GPU_PID=""

mkdir -p "$BASE_DIR" "$EVIDENCE"
rm -rf "$MAIN_DIR" "$NO_GPU_DIR" "$DATA_DIR"
rm -f "$TRACE" "$NO_GPU_TRACE" "$CASES" "$SUMMARY" "$MAIN_LOG" "$NO_GPU_LOG" "$WAIT_FIFO"
mkdir -p "$MAIN_DIR" "$NO_GPU_DIR" "$DATA_DIR"
mkfifo "$WAIT_FIFO"
exec 9<>"$WAIT_FIFO"
: > "$CASES"

wait_tick()
{
  read -r -t 1 -u 9 _ || true
}

record_case()
{
  printf '%s\t%s\t%s\n' "$1" "$2" "$3" >> "$CASES"
}

fail_case()
{
  record_case "$1" FAIL "$2"
  echo "FAIL [$1] $2" >&2
  exit 1
}

write_summary()
{
  python3 - "$CASES" "$SUMMARY" <<'PY'
import json
import pathlib
import sys

cases_path, summary_path = map(pathlib.Path, sys.argv[1:])
cases = []
if cases_path.exists():
    for line in cases_path.read_text().splitlines():
        if not line:
            continue
        name, status, detail = line.split("\t", 2)
        cases.append({"name": name, "status": status, "detail": detail})
summary = {
    "status": "PASS" if cases and all(case["status"] == "PASS" for case in cases) else "FAIL",
    "cases": cases,
}
summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")
PY
}

stop_observer()
{
  local pid=$1
  local expected_dir=$2
  [[ -n "$pid" && -d "/proc/$pid" ]] || return 0
  local state cmd
  state=$(awk '/^State:/{print $2}' "/proc/$pid/status" 2>/dev/null || true)
  [[ "$state" == "Z" ]] && return 0
  cmd=$(tr '\0' ' ' < "/proc/$pid/cmdline" 2>/dev/null || true)
  if [[ "$cmd" != *"$OBSERVER"* || "$cmd" != *"--base-dir $expected_dir"* ]]; then
    echo "refusing to stop pid=$pid: $cmd" >&2
    return 1
  fi
  kill -TERM "$pid" 2>/dev/null || true
  for _ in 1 2 3 4 5; do
    [[ ! -d "/proc/$pid" ]] && break
    state=$(awk '/^State:/{print $2}' "/proc/$pid/status" 2>/dev/null || true)
    [[ "$state" == "Z" ]] && break
    wait_tick
  done
  if [[ -d "/proc/$pid" ]]; then
    state=$(awk '/^State:/{print $2}' "/proc/$pid/status" 2>/dev/null || true)
    if [[ "$state" != "Z" ]]; then
      cmd=$(tr '\0' ' ' < "/proc/$pid/cmdline" 2>/dev/null || true)
      if [[ "$cmd" == *"$OBSERVER"* && "$cmd" == *"--base-dir $expected_dir"* ]]; then
        kill -KILL "$pid" 2>/dev/null || true
      fi
    fi
  fi
  wait "$pid" 2>/dev/null || true
}

on_exit()
{
  local rc=$?
  trap - EXIT INT TERM
  stop_observer "$NO_GPU_PID" "$NO_GPU_DIR" || true
  stop_observer "$MAIN_PID" "$MAIN_DIR" || true
  write_summary
  exec 9>&- || true
  rm -f "$WAIT_FIFO"
  exit "$rc"
}
trap on_exit EXIT INT TERM

mysql_cmd()
{
  timeout 300 mysql --connect-timeout=5 --protocol=TCP -h127.0.0.1 -P"$1" -uroot -A -N -B "${@:2}"
}

wait_ready()
{
  local port=$1
  local pid=$2
  for _ in $(seq 1 120); do
    [[ -d "/proc/$pid" ]] || return 1
    if mysql_cmd "$port" -e 'select 1' >/dev/null 2>&1; then
      return 0
    fi
    wait_tick
  done
  return 1
}

start_observer()
{
  local run_dir=$1
  local port=$2
  local trace=$3
  local log=$4
  local hide_gpu=$5
  local pid_var=$6
  local -a env_args=(
    "LD_LIBRARY_PATH=$(dirname "$BRIDGE"):/opt/hipvs/lib:/opt/rocm/lib"
    "OB_VSAG_TRACE=1"
    "OB_VSAG_TRACE_FILE=$trace"
  )
  if [[ "$hide_gpu" == "1" ]]; then
    env_args+=("HIP_VISIBLE_DEVICES=-1" "ROCR_VISIBLE_DEVICES=-1")
  fi
  ulimit -n 65535
  env -u OB_VSAG_USE_CUVS "${env_args[@]}" "$OBSERVER" \
    --nodaemon \
    --port "$port" \
    --data-dir "$run_dir/store" \
    --base-dir "$run_dir" \
    --parameter __min_full_resource_pool_memory=2147483648 \
    --parameter memory_budget=2G \
    --parameter datafile_size=20G \
    --parameter cpu_count=24 \
    --parameter local_ip=127.0.0.1 \
    >"$log" 2>&1 &
  local pid=$!
  printf -v "$pid_var" '%s' "$pid"
  wait_ready "$port" "$pid"
}

trace_offset()
{
  local trace=$1
  local lines=0
  [[ -f "$trace" ]] && lines=$(wc -l < "$trace")
  echo $((lines + 1))
}

trace_count()
{
  local trace=$1
  local offset=$2
  local marker=$3
  tail -n +"$offset" "$trace" 2>/dev/null | awk -v marker="$marker" '$1 == marker {count++} END {print count+0}'
}

trace_cuvs_count()
{
  local trace=$1
  local offset=$2
  tail -n +"$offset" "$trace" 2>/dev/null | awk '$1 ~ /^cuvs_/ {count++} END {print count+0}'
}

ann_ids()
{
  local port=$1
  local table=$2
  local query=$3
  local where_clause=${4:-}
  local mode=$5
  local approximate=""
  [[ "$mode" == "approx" ]] && approximate="APPROXIMATE"
  mysql_cmd "$port" -e "use hipvs_gate; select group_concat(id order by id) from (select id from $table $where_clause order by l2_distance(v,$query) $approximate limit 10) x;"
}

python3 - "$DATA_DIR" <<'PY'
import pathlib
import sys

try:
    import numpy as np
except ImportError as exc:
    raise SystemExit("numpy is required by run_gpu_smoke.sh") from exc

out = pathlib.Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
rng = np.random.default_rng(1314)
n, dim, nq, topk = 10_000, 128, 100, 10
base = rng.normal(size=(n, dim)).astype(np.float32)
base /= np.linalg.norm(base, axis=1, keepdims=True)
probe_rows = np.linspace(0, n - 1, nq, dtype=np.int64)
queries = base[probe_rows] + rng.normal(scale=0.002, size=(nq, dim)).astype(np.float32)
queries /= np.linalg.norm(queries, axis=1, keepdims=True)
gt = np.empty((nq, topk), dtype=np.int32)
for query_id, query in enumerate(queries):
    distances = np.sum((base - query) ** 2, axis=1)
    gt[query_id] = np.argsort(distances)[:topk]
base.tofile(out / "base.f32")
queries.tofile(out / "query.f32")
gt.tofile(out / "gt.i32")
(out / "query0.txt").write_text("[" + ",".join(f"{value:.7f}" for value in queries[0]) + "]")

def vector(values):
    return "[" + ",".join(f"{value:.7f}" for value in values) + "]"

def write_insert(path, table, ids, vectors, chunk=250):
    with path.open("w") as sql:
        sql.write("use hipvs_gate;\n")
        for start in range(0, len(ids), chunk):
            rows = [f"({int(row_id)},'{vector(row)}')" for row_id, row in zip(ids[start:start+chunk], vectors[start:start+chunk])]
            sql.write(f"insert into {table} values " + ",".join(rows) + ";\n")

write_insert(out / "cuvs-0.sql", "t_cuvs", np.arange(0, 300), base[0:300])
write_insert(out / "cuvs-1.sql", "t_cuvs", np.arange(300, 400), base[300:400])
write_insert(out / "cuvs-2.sql", "t_cuvs", np.arange(400, 700), base[400:700])
write_insert(out / "vsag.sql", "t_vsag", np.arange(0, 300), base[0:300])
write_insert(out / "no-gpu.sql", "t_cuvs", np.arange(0, 300), base[0:300])
write_insert(out / "batch-base.sql", "batch_base", np.arange(n), base)
write_insert(out / "batch-probes.sql", "batch_probes", 100_000 + np.arange(nq), queries)
PY

QUERY=$(cat "$DATA_DIR/query0.txt")
NO_GPU_PORT=$((PORT + 10))
for smoke_port in "$PORT" "$NO_GPU_PORT"; do
  if mysql_cmd "$smoke_port" -e 'select 1' >/dev/null 2>&1; then
    fail_case port_preflight "port $smoke_port is already serving MySQL"
  fi
done

start_observer "$MAIN_DIR" "$PORT" "$TRACE" "$MAIN_LOG" 0 MAIN_PID || fail_case observer_start "TRACE observer did not become SQL-ready"
mysql_cmd "$PORT" -e 'alter system set ob_vector_memory_limit_percentage=30; create database if not exists hipvs_gate;' >/dev/null
mysql_cmd "$PORT" -e "use hipvs_gate;
  create table t_cuvs(id int primary key, v vector(128), vector index idx_c(v) with (distance=l2,type=hnsw,lib=cuvs,m=16,ef_construction=200,ef_search=200));
  create table t_vsag(id int primary key, v vector(128), vector index idx_v(v) with (distance=l2,type=hnsw,lib=vsag,m=16,ef_construction=200,ef_search=200));" >/dev/null
mysql_cmd "$PORT" < "$DATA_DIR/cuvs-0.sql" >/dev/null
mysql_cmd "$PORT" < "$DATA_DIR/vsag.sql" >/dev/null

offset=$(trace_offset "$TRACE")
first_approx=$(ann_ids "$PORT" t_cuvs "$QUERY" "" approx)
first_exact=$(ann_ids "$PORT" t_cuvs "$QUERY" "" exact)
builds=$(trace_count "$TRACE" "$offset" cuvs_build)
serves=$(trace_count "$TRACE" "$offset" cuvs_serve)
[[ "$builds" -ge 1 && "$serves" -ge 1 ]] || fail_case cuvs_l2_route "build=$builds serve=$serves"
[[ -n "$first_approx" && -n "$first_exact" ]] || fail_case cuvs_l2_route "empty ANN result"
record_case cuvs_l2_route PASS "build=$builds serve=$serves"

offset=$(trace_offset "$TRACE")
vsag_approx=$(ann_ids "$PORT" t_vsag "$QUERY" "" approx)
vsag_cuvs=$(trace_cuvs_count "$TRACE" "$offset")
[[ -n "$vsag_approx" && "$vsag_cuvs" -eq 0 ]] || fail_case vsag_cpu_route "cuvs_markers=$vsag_cuvs"
record_case vsag_cpu_route PASS "cuvs_markers=0"

if mysql_cmd "$PORT" -e 'use hipvs_gate; create table bad_cos(id int primary key, v vector(128), vector index idx(v) with (distance=cosine,type=hnsw,lib=cuvs));' >"$EVIDENCE/ddl-cosine.log" 2>&1; then
  fail_case ddl_non_l2 "lib=cuvs cosine unexpectedly succeeded"
fi
if mysql_cmd "$PORT" -e 'use hipvs_gate; create table bad_ip(id int primary key, v vector(128), vector index idx(v) with (distance=inner_product,type=hnsw,lib=cuvs));' >"$EVIDENCE/ddl-inner-product.log" 2>&1; then
  fail_case ddl_non_l2 "lib=cuvs inner_product unexpectedly succeeded"
fi
grep -q 'lib=cuvs only supports distance=l2' "$EVIDENCE/ddl-cosine.log" || fail_case ddl_non_l2 "cosine error text mismatch"
grep -q 'lib=cuvs only supports distance=l2' "$EVIDENCE/ddl-inner-product.log" || fail_case ddl_non_l2 "inner_product error text mismatch"
record_case ddl_non_l2 PASS "cosine and inner_product rejected"

mysql_cmd "$PORT" < "$DATA_DIR/cuvs-1.sql" >/dev/null
offset=$(trace_offset "$TRACE")
fresh_approx=$(ann_ids "$PORT" t_cuvs "$QUERY" "" approx)
fresh_exact=$(ann_ids "$PORT" t_cuvs "$QUERY" "" exact)
fresh_serve=$(trace_count "$TRACE" "$offset" cuvs_serve)
[[ "$fresh_serve" -eq 0 && "$fresh_approx" == "$fresh_exact" ]] || fail_case freshness_fallback "serve=$fresh_serve approx=$fresh_approx exact=$fresh_exact"
record_case freshness_fallback PASS "n=400 serve=0 exact_match=1"

mysql_cmd "$PORT" < "$DATA_DIR/cuvs-2.sql" >/dev/null
offset=$(trace_offset "$TRACE")
ann_ids "$PORT" t_cuvs "$QUERY" "" approx >/dev/null
rebuilds=$(trace_count "$TRACE" "$offset" cuvs_build)
resumed=$(trace_count "$TRACE" "$offset" cuvs_serve)
[[ "$rebuilds" -ge 1 && "$resumed" -ge 1 ]] || fail_case freshness_rebuild "build=$rebuilds serve=$resumed"
record_case freshness_rebuild PASS "n=700 build=$rebuilds serve=$resumed"

offset=$(trace_offset "$TRACE")
filter_approx=$(ann_ids "$PORT" t_cuvs "$QUERY" "where id >= 600" approx)
filter_exact=$(ann_ids "$PORT" t_cuvs "$QUERY" "where id >= 600" exact)
filter_serve=$(trace_count "$TRACE" "$offset" cuvs_serve)
[[ "$filter_serve" -eq 0 && "$filter_approx" == "$filter_exact" ]] || fail_case filter_fallback "serve=$filter_serve approx=$filter_approx exact=$filter_exact"
record_case filter_fallback PASS "serve=0 exact_match=1"

mysql_cmd "$PORT" -e 'use hipvs_gate; delete from t_cuvs where id=0;' >/dev/null
offset=$(trace_offset "$TRACE")
delete_approx=$(ann_ids "$PORT" t_cuvs "$QUERY" "" approx)
delete_exact=$(ann_ids "$PORT" t_cuvs "$QUERY" "" exact)
delete_serve=$(trace_count "$TRACE" "$offset" cuvs_serve)
[[ "$delete_serve" -eq 0 && "$delete_approx" == "$delete_exact" && ",${delete_approx}," != *",0,"* ]] || fail_case delete_fallback "serve=$delete_serve approx=$delete_approx exact=$delete_exact"
record_case delete_fallback PASS "serve=0 exact_match=1 deleted_absent=1"

mysql_cmd "$PORT" -e 'use hipvs_gate;
  create table batch_base(id bigint primary key, v vector(128));
  create table batch_probes(id bigint primary key, v vector(128));
  create table batch_out(probe_id bigint, neighbor_id bigint, distance float, rk int);' >/dev/null
mysql_cmd "$PORT" < "$DATA_DIR/batch-base.sql" >/dev/null
mysql_cmd "$PORT" < "$DATA_DIR/batch-probes.sql" >/dev/null
offset=$(trace_offset "$TRACE")
mysql_cmd "$PORT" -e 'use hipvs_gate; call dbms_vector.batch_knn("batch_base","batch_probes",10,"batch_out");' >"$EVIDENCE/batch-call.log" 2>&1
raw_batch=$(trace_count "$TRACE" "$offset" cuvs_raw_batch)
batch_rows=$(mysql_cmd "$PORT" -e 'use hipvs_gate; select count(*) from batch_out;')
probe_count=$(mysql_cmd "$PORT" -e 'use hipvs_gate; select count(distinct probe_id) from batch_out;')
bad_groups=$(mysql_cmd "$PORT" -e 'use hipvs_gate; select count(*) from (select probe_id,count(*) c from batch_out group by probe_id having c<>10) x;')
mysql_cmd "$PORT" -e 'use hipvs_gate; select probe_id,neighbor_id,rk from batch_out order by probe_id,rk;' > "$EVIDENCE/batch-output.tsv"
[[ "$raw_batch" -eq 1 && "$batch_rows" -eq 1000 && "$probe_count" -eq 100 && "$bad_groups" -eq 0 ]] || fail_case batch_knn "trace=$raw_batch rows=$batch_rows probes=$probe_count bad_groups=$bad_groups"
batch_recall=$(python3 - "$DATA_DIR/gt.i32" "$EVIDENCE/batch-output.tsv" <<'PY'
import pathlib
import struct
import sys

gt_path, result_path = map(pathlib.Path, sys.argv[1:])
gt = struct.unpack("<1000i", gt_path.read_bytes())
rows = [line.split("\t") for line in result_path.read_text().splitlines() if line]
got = {}
for probe_id, neighbor_id, rank in rows:
    got.setdefault(int(probe_id) - 100_000, []).append((int(rank), int(neighbor_id)))
hits = 0
for query_id in range(100):
    predicted = {neighbor for _, neighbor in sorted(got.get(query_id, []))[:10]}
    truth = set(gt[query_id * 10:(query_id + 1) * 10])
    hits += len(predicted & truth)
print(f"{hits / 1000.0:.4f}")
PY
)
python3 - "$batch_recall" <<'PY' || fail_case batch_knn "recall=$batch_recall"
import sys
raise SystemExit(0 if float(sys.argv[1]) >= 0.75 else 1)
PY
record_case batch_knn PASS "trace=1 rows=1000 recall=$batch_recall"

ls -l "/proc/$MAIN_PID/fd" > "$EVIDENCE/gpu-fds.txt"
if grep -Fq "$RENDER" "$EVIDENCE/gpu-fds.txt" && grep -Fq '/dev/kfd' "$EVIDENCE/gpu-fds.txt"; then
  record_case gpu_fd PASS "observer opened $RENDER and /dev/kfd"
else
  fail_case gpu_fd "expected GPU fds not found for pid=$MAIN_PID"
fi

stop_observer "$MAIN_PID" "$MAIN_DIR"
MAIN_PID=""
if mysql_cmd "$PORT" -e 'select 1' >/dev/null 2>&1; then
  fail_case cleanup "main observer port $PORT is still open"
fi

start_observer "$NO_GPU_DIR" "$NO_GPU_PORT" "$NO_GPU_TRACE" "$NO_GPU_LOG" 1 NO_GPU_PID || fail_case gpu_unavailable_fallback "observer did not become SQL-ready"
mysql_cmd "$NO_GPU_PORT" -e 'alter system set ob_vector_memory_limit_percentage=30; create database if not exists hipvs_gate; use hipvs_gate; create table t_cuvs(id int primary key, v vector(128), vector index idx(v) with (distance=l2,type=hnsw,lib=cuvs,m=16,ef_construction=200,ef_search=200));' >/dev/null
mysql_cmd "$NO_GPU_PORT" < "$DATA_DIR/no-gpu.sql" >/dev/null
offset=$(trace_offset "$NO_GPU_TRACE")
no_gpu_approx=$(ann_ids "$NO_GPU_PORT" t_cuvs "$QUERY" "" approx)
no_gpu_exact=$(ann_ids "$NO_GPU_PORT" t_cuvs "$QUERY" "" exact)
no_gpu_serve=$(trace_count "$NO_GPU_TRACE" "$offset" cuvs_serve)
[[ "$no_gpu_serve" -eq 0 && "$no_gpu_approx" == "$no_gpu_exact" && -d "/proc/$NO_GPU_PID" ]] || fail_case gpu_unavailable_fallback "serve=$no_gpu_serve approx=$no_gpu_approx exact=$no_gpu_exact"
record_case gpu_unavailable_fallback PASS "serve=0 exact_match=1 observer_alive=1"

stop_observer "$NO_GPU_PID" "$NO_GPU_DIR"
NO_GPU_PID=""
if mysql_cmd "$NO_GPU_PORT" -e 'select 1' >/dev/null 2>&1; then
  fail_case cleanup "no-GPU observer port $NO_GPU_PORT is still open"
fi
record_case cleanup PASS "all observers stopped and ports closed"

write_summary
grep -q '"status": "PASS"' "$SUMMARY"
echo "GPU smoke PASS: $SUMMARY"
