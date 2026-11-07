#!/usr/bin/env python3
"""Recall@K comparison of the CPU (lib=vsag) and AMD GPU (lib=hipvs) HNSW builds.

The build-time benchmark answers "how long"; this script answers "is the graph as
good". For a fixed corpus it builds the index with each library at several
``ef_search`` values and reports recall@k against a NumPy brute-force ground
truth, so the GPU graph can be compared with the CPU graph at the same search
effort (the only fair comparison: a different ef is a different operating point).

Example:
  python3 recall_sweep.py --port 2881 --database test --rows 20000 --dim 768 \\
      --ef 200 800 --queries 50
"""
import argparse
import json
import statistics
import sys
import time
import uuid
from pathlib import Path

import numpy as np
import pymysql


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--user", default="root")
    parser.add_argument("--password", default="")
    parser.add_argument("--database", required=True)
    parser.add_argument("--rows", type=int, default=20000)
    parser.add_argument("--dim", type=int, default=768)
    parser.add_argument("--queries", type=int, default=50)
    parser.add_argument("--m", type=int, default=16)
    parser.add_argument("--ef-construction", type=int, default=200)
    parser.add_argument("--ef", type=int, nargs="+", default=[200, 800])
    parser.add_argument("--k", type=int, default=10)
    parser.add_argument("--output", type=Path, default=Path("recall-sweep.json"))
    args = parser.parse_args()

    rng = np.random.default_rng(20261107)
    vectors = rng.normal(size=(args.rows, args.dim)).astype(np.float32)
    queries = rng.normal(size=(args.queries, args.dim)).astype(np.float32)
    vids = (rng.permutation(np.arange(args.rows, dtype=np.int64)) * 7 + 11).astype(np.int64)

    truth = []
    for query in queries:
        distances = np.square(vectors.astype(np.float64) - query.astype(np.float64)).sum(axis=1)
        truth.append({int(v) for v in vids[np.argpartition(distances, args.k - 1)[:args.k]]})
    query_json = [json.dumps(row.tolist()) for row in queries]

    connection = pymysql.connect(host=args.host, port=args.port, user=args.user,
                                 password=args.password, database=args.database,
                                 autocommit=True, read_timeout=14400, write_timeout=14400)
    table = "recallsweep_" + uuid.uuid4().hex[:10]
    report = {"dataset": {"rows": args.rows, "dim": args.dim, "kind": "synthetic-normal-fp32"},
              "parameters": {"m": args.m, "ef_construction": args.ef_construction, "k": args.k},
              "results": []}
    try:
        with connection.cursor() as cursor:
            cursor.execute(f"CREATE TABLE `{table}` (id BIGINT PRIMARY KEY, v VECTOR({args.dim}))")
            batch = max(16, min(512, 262144 // max(args.dim * 12, 1)))
            for start in range(0, args.rows, batch):
                stop = min(start + batch, args.rows)
                rows = [(int(vids[i]), json.dumps(vectors[i].tolist())) for i in range(start, stop)]
                cursor.executemany(f"INSERT INTO `{table}` VALUES (%s, %s)", rows)
            print(f"[setup] {args.rows} rows, dim {args.dim}", flush=True)

            for library in ("VSAG", "HIPVS"):
                for ef in args.ef:
                    try:
                        cursor.execute(f"DROP INDEX `idx` ON `{table}`")
                    except pymysql.err.MySQLError as drop_error:
                        if not drop_error.args or drop_error.args[0] != 1091:
                            raise
                    started = time.perf_counter()
                    cursor.execute(
                        f"CREATE VECTOR INDEX idx ON `{table}`(v) WITH ("
                        f"distance=l2,type=hnsw,lib={library},m={args.m},"
                        f"ef_construction={args.ef_construction},ef_search={ef})")
                    build_seconds = time.perf_counter() - started
                    sql = (f"SELECT /*+ INDEX(`{table}` idx) */ id FROM `{table}` "
                           f"ORDER BY l2_distance(v, %s) APPROXIMATE LIMIT {args.k}")
                    hits = 0
                    latencies = []
                    for index, query in enumerate(query_json):
                        begin = time.perf_counter()
                        cursor.execute(sql, (query,))
                        found = {int(row[0]) for row in cursor.fetchall()}
                        latencies.append((time.perf_counter() - begin) * 1000.0)
                        hits += len(found & truth[index]) / args.k
                    recall = hits / len(query_json)
                    entry = {"library": library, "ef_search": ef, "build_seconds": build_seconds,
                             "recall_at_k": recall,
                             "query_median_ms": statistics.median(latencies)}
                    report["results"].append(entry)
                    print(f"[{library:5s}] ef_search={ef:5d} build={build_seconds:7.3f}s "
                          f"recall@{args.k}={recall:.4f} query_median={entry['query_median_ms']:.2f}ms",
                          flush=True)
                    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    finally:
        try:
            with connection.cursor() as cursor:
                cursor.execute(f"DROP TABLE IF EXISTS `{table}`")
        except Exception:  # noqa: BLE001
            pass
        connection.close()

    pairs = {}
    for entry in report["results"]:
        pairs.setdefault(entry["ef_search"], {})[entry["library"]] = entry["recall_at_k"]
    worst_gap = 0.0
    for ef, values in sorted(pairs.items()):
        if "VSAG" in values and "HIPVS" in values:
            worst_gap = max(worst_gap, values["VSAG"] - values["HIPVS"])
            print(f"ef_search={ef}: cpu={values['VSAG']:.4f} gpu={values['HIPVS']:.4f} "
                  f"delta={values['VSAG'] - values['HIPVS']:+.4f}")
    report["worst_recall_gap"] = worst_gap
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())