"""Collect real seekdb CPU/HIPVS DDL latency and Recall@K; never synthesize scores.

Requires numpy and pymysql. Run beside the database so --gpu-log can read its
stderr. Every table has a fresh random name; existing tables are never dropped.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import statistics
import time
import uuid

import numpy as np
import pymysql


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=2881)
    parser.add_argument("--user", default="root")
    parser.add_argument("--database", required=True, help="Existing disposable benchmark database")
    parser.add_argument("--gpu-log", required=True, type=Path, help="Database stderr with SEEKDB_HIPVS records")
    parser.add_argument("--rows", type=int, default=10000)
    parser.add_argument("--dim", type=int, default=128)
    parser.add_argument("--queries", type=int, default=100)
    parser.add_argument("--rounds", type=int, default=3)
    parser.add_argument("--m", type=int, default=16)
    parser.add_argument("--ef-construction", type=int, default=200)
    parser.add_argument("--ef-search", type=int, default=100)
    parser.add_argument("--k", type=int, default=10)
    parser.add_argument("--output", type=Path, default=Path("hipvs-results.json"))
    args = parser.parse_args()
    if (args.rows <= 2 * args.m or args.dim < 1 or args.queries < 1 or args.rounds < 3 or
            not 1 <= args.k <= args.rows or not 5 <= args.m <= 128 or
            not args.m < args.ef_construction <= 1000 or not 1 <= args.ef_search <= 160000):
        parser.error("invalid dataset/HNSW parameters, or fewer than three rounds")
    if not args.gpu_log.is_file():
        parser.error("--gpu-log must already exist and belong to the server under measurement")
    rng = np.random.default_rng(20261101)
    vectors = rng.normal(size=(args.rows, args.dim)).astype(np.float32)
    queries = rng.normal(size=(args.queries, args.dim)).astype(np.float32)
    ids = rng.permutation(np.arange(args.rows, dtype=np.int64) * 103 + 17)
    truth = []
    for query in queries:
        distances = np.square(vectors.astype(np.float64) - query.astype(np.float64)).sum(axis=1)
        truth.append(set(ids[np.argpartition(distances, args.k - 1)[:args.k]].tolist()))
    report = {
        "status": "running", "started_at": datetime.now(timezone.utc).isoformat(),
        "dataset": {"kind": "synthetic-normal-fp32", "seed": 20261101,
                    "rows": args.rows, "dim": args.dim, "queries": args.queries,
                    "sha256": hashlib.sha256(vectors.tobytes() + queries.tobytes() + ids.tobytes()).hexdigest()},
        "parameters": {"m": args.m, "ef_construction": args.ef_construction,
                       "ef_search": args.ef_search, "k": args.k},
        "runs": [], "speedup": None,
        "timing_scope": "CREATE VECTOR INDEX through successful first forced-index query",
        "memory_peak_bytes": None,
    }

    def save():
        args.output.parent.mkdir(parents=True, exist_ok=True)
        temporary = args.output.with_suffix(args.output.suffix + ".tmp")
        temporary.write_text(json.dumps(report, indent=2, ensure_ascii=False, default=str) + "\n", encoding="utf-8")
        temporary.replace(args.output)

    connection = pymysql.connect(host=args.host, port=args.port, user=args.user,
                                 password=os.environ.get("SEEKDB_PASSWORD", ""),
                                 database=args.database, autocommit=True, read_timeout=7200)
    prefix = "hipvs_" + uuid.uuid4().hex[:12]
    try:
        with connection.cursor() as cursor:
            cursor.execute("SELECT VERSION()")
            report["database_version"] = cursor.fetchone()[0]
            for round_id in range(args.rounds):
                # Alternate order to expose cache/thermal effects in the raw runs.
                libraries = ("VSAG", "HIPVS") if round_id % 2 == 0 else ("HIPVS", "VSAG")
                for library in libraries:
                    table = f"{prefix}_{round_id}_{library.lower()}"
                    cursor.execute(f"CREATE TABLE `{table}` (id BIGINT PRIMARY KEY, v VECTOR({args.dim}))")
                    try:
                        for start in range(0, args.rows, 256):
                            batch = [(int(ids[i]), json.dumps(vectors[i].tolist()))
                                     for i in range(start, min(start + 256, args.rows))]
                            cursor.executemany(f"INSERT INTO `{table}` VALUES (%s,%s)", batch)
                        log_offset = args.gpu_log.stat().st_size
                        begin = time.perf_counter()
                        cursor.execute(f"CREATE VECTOR INDEX idx ON `{table}`(v) WITH ("
                                       f"distance=l2,type=hnsw,lib={library},m={args.m},"
                                       f"ef_construction={args.ef_construction},ef_search={args.ef_search})")
                        sql = (f"SELECT /*+ INDEX({table} idx) */ id FROM `{table}` "
                               f"ORDER BY l2_distance(v,%s) APPROXIMATE LIMIT {args.k}")
                        cursor.execute(sql, (json.dumps(queries[0].tolist()),))
                        cursor.fetchall()
                        build_seconds = time.perf_counter() - begin
                        gpu_records = []
                        if library == "HIPVS":
                            with args.gpu_log.open("rb") as log:
                                log.seek(log_offset)
                                for line in log.read().decode("utf-8", errors="replace").splitlines():
                                    if "SEEKDB_HIPVS " in line:
                                        record = json.loads(line.split("SEEKDB_HIPVS ", 1)[1])
                                        if (record.get("gpu_compute") is True and record.get("cpu_fallback") is False
                                                and record.get("rows") == args.rows and record.get("dim") == args.dim):
                                            gpu_records.append(record)
                            if not gpu_records:
                                raise RuntimeError("No matching real GPU build evidence; refusing to report HIPVS speedup")
                        latencies, recalls = [], []
                        for query, expected in zip(queries, truth):
                            begin = time.perf_counter()
                            cursor.execute(sql, (json.dumps(query.tolist()),))
                            found = {int(row[0]) for row in cursor.fetchall()}
                            latencies.append((time.perf_counter() - begin) * 1000)
                            recalls.append(len(found & expected) / args.k)
                        cursor.execute("EXPLAIN " + sql, (json.dumps(queries[0].tolist()),))
                        plan = [list(row) for row in cursor.fetchall()]
                        report["runs"].append({"library": library, "round": round_id + 1,
                            "build_seconds": build_seconds, "recall_at_k": statistics.mean(recalls),
                            "query_median_ms": statistics.median(latencies),
                            "query_p95_ms": float(np.percentile(latencies, 95)),
                            "gpu_evidence": gpu_records, "explain": plan})
                        save()
                    finally:
                        cursor.execute(f"DROP TABLE `{table}`")
        medians = {library: statistics.median(r["build_seconds"] for r in report["runs"]
                                              if r["library"] == library) for library in ("VSAG", "HIPVS")}
        report.update(status="measured", median_build_seconds=medians,
                      speedup=medians["VSAG"] / medians["HIPVS"])
    except Exception as error:
        report.update(status="failed", error_type=type(error).__name__)
        raise
    finally:
        connection.close()
        save()


if __name__ == "__main__":
    main()
