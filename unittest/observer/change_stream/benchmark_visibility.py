import argparse
import json
import math
import os
import random
import re
import time
import threading
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import numpy as np
import psutil
import pymysql


def percentile(values, fraction):
    return sorted(values)[max(0, math.ceil(len(values) * fraction) - 1)]


def distribution(values):
    return {"count": len(values), "p50": percentile(values, .5),
            "p95": percentile(values, .95), "p99": percentile(values, .99),
            "min": min(values), "max": max(values)}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--pid", type=int, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--rows", type=int, default=2000)
    parser.add_argument("--dim", type=int, default=16)
    parser.add_argument("--samples", type=int, default=100)
    parser.add_argument("--idle-seconds", type=float, default=10)
    parser.add_argument("--poll-ms", type=float, default=2)
    parser.add_argument("--gap-ms", type=float, default=200)
    parser.add_argument("--throughput-rows", type=int, default=0)
    parser.add_argument("--clients", type=int, default=4)
    parser.add_argument("--throughput-kind", choices=["vector", "plain"], default="vector")
    parser.add_argument("--log-file", type=Path)
    parser.add_argument("--database")
    parser.add_argument("--seed", type=int, default=937)
    args = parser.parse_args()
    database = args.database or "visibility_" + str(time.time_ns())
    result = {"args": vars(args).copy(), "database": database,
              "load_start": os.getloadavg(), "samples": [], "errors": []}
    result["args"]["output"] = str(args.output)
    result["args"]["log_file"] = str(args.log_file) if args.log_file else None
    log_offset = args.log_file.stat().st_size if args.log_file else 0
    result["started_at_unix_s"] = time.time()
    proc = psutil.Process(args.pid)
    result["binary"] = proc.exe()
    result["affinity"] = proc.cpu_affinity()
    connection = pymysql.connect(host="127.0.0.1", port=args.port, user="root",
                                 autocommit=True, read_timeout=600,
                                 write_timeout=60, connect_timeout=5)
    rng = np.random.RandomState(args.seed)
    gap_rng = random.Random(args.seed)

    def vector():
        return "[" + ",".join("%.6f" % x for x in rng.normal(size=args.dim)) + "]"

    def query(cursor, sql, params=None):
        cursor.execute(sql, params)
        return cursor.fetchall()

    try:
        with connection.cursor() as cursor:
            result["version"] = query(cursor, "SELECT VERSION()")
            query(cursor, "SET ob_query_timeout=600000000")
            if not args.database:
                query(cursor, "CREATE DATABASE " + database)
                query(cursor, "USE " + database)
                query(cursor, "CREATE TABLE vectors (id BIGINT PRIMARY KEY, v VECTOR(%d)) ORGANIZATION HEAP" % args.dim)
                for start in range(0, args.rows, 100):
                    cursor.executemany("INSERT INTO vectors VALUES (%s,%s)",
                                       [(i, vector()) for i in range(start, min(start + 100, args.rows))])
                query(cursor, "CREATE VECTOR INDEX idx ON vectors(v) WITH (distance=l2,type=hnsw_sq,m=16,ef_construction=200,sync_mode=async)")
            else:
                assert re.fullmatch(r"[A-Za-z0-9_]+", database)
                query(cursor, "USE " + database)
            initial_count, next_id = query(cursor, "SELECT COUNT(*), COALESCE(MAX(id)+1,0) FROM vectors")[0]
            result["initial_row_count"] = initial_count
            query(cursor, "SET ob_hnsw_ef_search=256")
            ann = "SELECT /*+ INDEX(vectors idx) */ id FROM vectors ORDER BY l2_distance(v,%s) APPROXIMATE LIMIT 10"
            result["ddl"] = query(cursor, "SHOW CREATE TABLE vectors")
            result["plan"] = query(cursor, "EXPLAIN " + ann, (vector(),))
            assert "VECTOR INDEX SCAN" in str(result["plan"]), "query did not use VECTOR INDEX SCAN"
            assert "SYNC_MODE=ASYNC" in str(result["ddl"]).upper(), result["ddl"]
            for i in range(20):
                query(cursor, ann, (vector(),))
            cpu_start = proc.cpu_times()
            idle_start = time.perf_counter()
            time.sleep(args.idle_seconds)
            cpu_end = proc.cpu_times()
            result["idle"] = {"wall_s": time.perf_counter() - idle_start,
                              "cpu_s": cpu_end.user + cpu_end.system - cpu_start.user - cpu_start.system}
            samples = [vector() for _ in range(args.samples)]
            for i, vec in enumerate(samples):
                time.sleep(gap_rng.uniform(0, args.gap_ms / 1000))
                row_id = next_id + i
                started = time.perf_counter()
                query(cursor, "INSERT INTO vectors VALUES (%s,%s)", (row_id, vec))
                committed = time.perf_counter()
                assert query(cursor, "SELECT id FROM vectors WHERE id=%s", (row_id,)) == ((row_id,),)
                pk_visible = time.perf_counter()
                attempts = 0
                found = False
                while time.perf_counter() - committed < 5:
                    ids = query(cursor, ann, (vec,))
                    attempts += 1
                    if (row_id,) in ids:
                        found = True
                        break
                    time.sleep(args.poll_ms / 1000)
                finished = time.perf_counter()
                sample = {"id": row_id, "write_ms": (committed-started)*1000,
                          "pk_ms": (pk_visible-committed)*1000,
                          "visible_ms": (finished-committed)*1000,
                          "end_to_end_ms": (finished-started)*1000,
                          "attempts": attempts, "found": found}
                result["samples"].append(sample)
                if not found:
                    result["errors"].append({"id": row_id, "error": "ANN visibility timeout"})
            result["row_count"] = query(cursor, "SELECT COUNT(*) FROM vectors")[0][0]
            assert result["row_count"] == initial_count + args.samples
            result["visible_ms"] = distribution([s["visible_ms"] for s in result["samples"]])
            result["write_ms"] = distribution([s["write_ms"] for s in result["samples"]])
            result["pk_ms"] = distribution([s["pk_ms"] for s in result["samples"]])
            if args.throughput_rows:
                write_table = "vectors"
                if args.throughput_kind == "plain":
                    write_table = "log_writes_" + str(time.time_ns())
                    query(cursor, "CREATE TABLE " + write_table + "(id BIGINT PRIMARY KEY, payload LONGTEXT) ORGANIZATION HEAP")
                rows = [(next_id + args.samples + i, vector())
                        for i in range(args.throughput_rows)]
                barrier = threading.Barrier(args.clients + 1)

                def write_partition(partition):
                    conn = pymysql.connect(host="127.0.0.1", port=args.port, user="root",
                                           database=database, autocommit=True,
                                           read_timeout=60, write_timeout=60)
                    latencies = []
                    failures = []
                    try:
                        with conn.cursor() as cur:
                            barrier.wait(timeout=30)
                            for row in partition:
                                start = time.perf_counter()
                                try:
                                    cur.execute("INSERT INTO " + write_table + " VALUES (%s,%s)", row)
                                    latencies.append((time.perf_counter()-start)*1000)
                                except Exception as error:
                                    failures.append({"id": row[0], "error": repr(error)})
                    finally:
                        conn.close()
                    return latencies, failures

                with ThreadPoolExecutor(max_workers=args.clients) as pool:
                    futures = [pool.submit(write_partition, rows[i::args.clients])
                               for i in range(args.clients)]
                    cpu_start = proc.cpu_times()
                    client_cpu_start = time.process_time()
                    start = time.perf_counter()
                    barrier.wait(timeout=30)
                    outcomes = [future.result() for future in futures]
                    elapsed = time.perf_counter()-start
                    cpu_end = proc.cpu_times()
                latencies = [x for latency, _ in outcomes for x in latency]
                result["errors"].extend(x for _, errors in outcomes for x in errors)
                result["throughput"] = {
                    "kind": args.throughput_kind,
                    "wall_s": elapsed, "completed": len(latencies),
                    "writes_per_s": len(latencies)/elapsed,
                    "write_ms": distribution(latencies) if latencies else None,
                    "server_cpu_s": cpu_end.user + cpu_end.system - cpu_start.user - cpu_start.system,
                    "client_cpu_s": time.process_time() - client_cpu_start,
                }
                written_count = query(cursor, "SELECT COUNT(*) FROM " + write_table)[0][0]
                expected_count = args.throughput_rows
                if args.throughput_kind == "vector":
                    expected_count += initial_count + args.samples
                assert written_count == expected_count
                query(cursor, "CALL dbms_index_manager.refresh()")
                indexed_wall = time.perf_counter() - start
                indexed_cpu = proc.cpu_times()
                result["throughput"]["caught_up_wall_s"] = indexed_wall
                result["throughput"]["caught_up_writes_per_s"] = len(latencies) / indexed_wall
                result["throughput"]["server_cpu_to_caught_up_s"] = (
                    indexed_cpu.user + indexed_cpu.system - cpu_start.user - cpu_start.system)
                missing = []
                drain_deadline = time.perf_counter() + 60
                if args.throughput_kind == "vector":
                    for row_id, vec in rows:
                        while (row_id,) not in query(cursor, ann, (vec,)):
                            if time.perf_counter() >= drain_deadline:
                                missing.append(row_id)
                                break
                            time.sleep(.01)
                    result["throughput"]["indexed_verified"] = len(rows) - len(missing)
                else:
                    actual = query(cursor, "SELECT SUM(id), SUM(OCTET_LENGTH(payload)) FROM " + write_table)[0]
                    assert actual == (sum(x[0] for x in rows), sum(len(x[1]) for x in rows))
                    result["throughput"]["plain_rows_verified"] = len(rows)
                result["throughput"]["validation_wall_s"] = time.perf_counter()-start-elapsed
                if missing:
                    result["errors"].append({"error": "ANN visibility timeout", "ids": missing})
    except Exception as error:
        result["errors"].append({"error": repr(error)})
        raise
    finally:
        result["load_end"] = os.getloadavg()
        result["finished_at_unix_s"] = time.time()
        if args.log_file:
            with args.log_file.open(errors="replace") as log:
                log.seek(log_offset)
                lines = log.read().splitlines()
            result["palf_fs_callback"] = {
                "stats": [{"groups": int(m.group(1)), "average_us": int(m.group(2))}
                          for line in lines
                          for m in [re.search(r"PALF STAT FS CB EXCUTE COST TIME.*cur_stat_count=(\d+).*avg \(count/cost\)=(\d+)", line)]
                          if m],
                "slow_calls": [line for line in lines if "fs_cb->update_end_lsn() cost too much time" in line],
            }
        args.output.write_text(json.dumps(result, indent=2))
        connection.close()
        print(json.dumps({k: result.get(k) for k in ["visible_ms", "write_ms", "pk_ms", "idle", "throughput", "errors"]}), flush=True)
    return int(bool(result["errors"]))


if __name__ == "__main__":
    raise SystemExit(main())
