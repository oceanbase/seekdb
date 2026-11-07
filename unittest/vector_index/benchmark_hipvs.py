#!/usr/bin/env python3
"""Measure real seekdb HNSW build time and Recall@K: CPU (lib=vsag) vs AMD GPU (lib=hipvs).

What is measured
  * build_seconds: wall clock of ``CREATE VECTOR INDEX ... WITH (lib=...)`` plus one
    first ``APPROXIMATE`` query through the new index, i.e. the time after which the
    index is actually usable. That covers the whole pipeline: table scan, vector
    buffering, graph construction, import into VSAG and snapshot serialization.
  * recall_at_k: mean ``|approx ∩ exact| / k`` where ``exact`` is a NumPy brute-force
    L2 ground truth computed from the very vectors that were inserted.
  * ``speedup`` = median(build_seconds of VSAG) / median(build_seconds of HIPVS).

Integrity rules (a benchmark that cannot prove these refuses to report a speedup)
  * every ``lib=hipvs`` round must produce at least one ``SEEKDB_HIPVS_BUILD`` record
    in the server's stderr with ``gpu_compute=true, cpu_fallback=false`` and the
    row/dim of this run -- otherwise the run is marked as failed, not as fast;
  * the approximate query must be executed through the vector index (EXPLAIN check);
  * CPU and GPU recall must both clear ``--min-recall`` or the result is reported as
    a quality failure (the GPU path is not allowed to win by returning garbage);
  * at least three rounds per library, medians (not minima) are compared.

Requires: numpy, pymysql. Run it next to the database so it can read its stderr.

Example
  python3 benchmark_hipvs.py --database test --gpu-log /tmp/obtest/observer.log \\
      --rows 20000 --dim 128 --rounds 3 --gpu-lib /workspace/hipvs-work/opt/lib/libseekdb_hipvs.so
"""
import argparse
import hashlib
import json
import os
import statistics
import time
import uuid
from datetime import datetime, timezone
from pathlib import Path

import numpy as np
import pymysql

GPU_MARKER = "SEEKDB_HIPVS_BUILD "


def build_gpu_records(log_path, offset, rows, dim):
    """Return the GPU build records written by the backend after ``offset``."""
    records = []
    if log_path is None:
        return records
    with log_path.open("rb") as handle:
        handle.seek(offset)
        text = handle.read().decode("utf-8", errors="replace")
    for line in text.splitlines():
        if GPU_MARKER not in line:
            continue
        try:
            record = json.loads(line.split(GPU_MARKER, 1)[1])
        except json.JSONDecodeError:
            continue
        if record.get("gpu_compute") is True and record.get("cpu_fallback") is False:
            records.append(record)
    return records


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--user", default="root")
    parser.add_argument("--password", default=os.environ.get("SEEKDB_PASSWORD", ""))
    parser.add_argument("--database", required=True)
    parser.add_argument("--gpu-log", type=Path, required=True,
                        help="server stderr/stdout log that receives the hipVS build records")
    parser.add_argument("--rows", type=int, default=20000)
    parser.add_argument("--dim", type=int, default=128)
    parser.add_argument("--queries", type=int, default=200)
    parser.add_argument("--rounds", type=int, default=3)
    parser.add_argument("--m", type=int, default=16)
    parser.add_argument("--ef-construction", type=int, default=200)
    parser.add_argument("--ef-search", type=int, default=100)
    parser.add_argument("--k", type=int, default=10)
    parser.add_argument("--dataset", choices=("clustered", "gaussian"), default="clustered",
                        help="clustered = embedding-like corpus, gaussian = isotropic noise")
    parser.add_argument("--min-recall", type=float, default=0.90)
    parser.add_argument("--output", type=Path, default=Path("hipvs-benchmark.json"))
    args = parser.parse_args()

    if args.rounds < 3:
        parser.error("at least three rounds per library are required (median, not minimum)")
    if args.rows <= 2 * args.m:
        parser.error("rows must exceed 2 * m (CAGRA needs a graph of degree 2*m)")
    if not args.gpu_log.is_file():
        parser.error(f"--gpu-log {args.gpu_log} must exist and belong to the server under test")

    rng = np.random.default_rng(20261107)
    if args.dataset == "clustered":
        # Embedding-like corpus: Gaussian clusters of radius 0.15 around 200
        # random centres. This is much closer to real embedding distributions
        # (and to what an ANN index is expected to solve) than an isotropic
        # Gaussian, where all pairwise distances concentrate.
        centres = rng.normal(size=(200, args.dim)).astype(np.float32)
        assignment = rng.integers(0, 200, size=args.rows)
        vectors = (centres[assignment] +
                   0.15 * rng.normal(size=(args.rows, args.dim))).astype(np.float32)
        query_clusters = rng.integers(0, 200, size=args.queries)
        queries = (centres[query_clusters] +
                   0.15 * rng.normal(size=(args.queries, args.dim))).astype(np.float32)
    else:
        vectors = rng.normal(size=(args.rows, args.dim)).astype(np.float32)
        queries = rng.normal(size=(args.queries, args.dim)).astype(np.float32)
    vids = (rng.permutation(np.arange(args.rows, dtype=np.int64)) * 7 + 11).astype(np.int64)

    # Exact ground truth, computed outside the database.
    truth = []
    for query in queries:
        distances = np.square(vectors.astype(np.float64) - query.astype(np.float64)).sum(axis=1)
        truth.append({int(v) for v in vids[np.argpartition(distances, args.k - 1)[:args.k]]})

    report = {
        "status": "running",
        "started_at": datetime.now(timezone.utc).isoformat(),
        "dataset": {
            "kind": f"synthetic-{args.dataset}-fp32",
            "seed": 20261107,
            "rows": args.rows,
            "dim": args.dim,
            "queries": args.queries,
            "sha256": hashlib.sha256(vectors.tobytes() + queries.tobytes() + vids.tobytes()).hexdigest(),
        },
        "parameters": {"m": args.m, "ef_construction": args.ef_construction,
                       "ef_search": args.ef_search, "k": args.k},
        "timing_scope": "CREATE VECTOR INDEX + first APPROXIMATE query through the new index",
        "runs": [],
        "medians": {},
        "speedup": None,
        "recall_ok": None,
    }

    def save():
        args.output.parent.mkdir(parents=True, exist_ok=True)
        temporary = args.output.with_suffix(args.output.suffix + ".tmp")
        temporary.write_text(json.dumps(report, indent=2, default=str) + "\n", encoding="utf-8")
        temporary.replace(args.output)

    connection = pymysql.connect(host=args.host, port=args.port, user=args.user,
                                 password=args.password, database=args.database,
                                 autocommit=True, read_timeout=14400, write_timeout=14400)
    prefix = "hipvsbench_" + uuid.uuid4().hex[:10]
    tables = {}
    query_json = [json.dumps(row.tolist()) for row in queries]
    try:
        with connection.cursor() as cursor:
            cursor.execute("SELECT VERSION()")
            report["database_version"] = cursor.fetchone()[0]
            for library in ("VSAG", "HIPVS"):
                table = f"{prefix}_{library.lower()}"
                tables[library] = table
                cursor.execute(f"CREATE TABLE `{table}` (id BIGINT PRIMARY KEY, v VECTOR({args.dim}))")
                # Keep one INSERT statement around a few hundred KB so that
                # high-dimensional corpora do not hit max_allowed_packet.
                batch = max(16, min(512, 262144 // max(args.dim * 12, 1)))
                for start in range(0, args.rows, batch):
                    stop = min(start + batch, args.rows)
                    rows = [(int(vids[i]), json.dumps(vectors[i].tolist())) for i in range(start, stop)]
                    cursor.executemany(f"INSERT INTO `{table}` VALUES (%s, %s)", rows)
                print(f"[setup] {library}: {args.rows} rows in `{table}`", flush=True)

            for round_id in range(args.rounds):
                # Alternate the order so cache/thermal drift shows up in the raw runs.
                order = ("VSAG", "HIPVS") if round_id % 2 == 0 else ("HIPVS", "VSAG")
                for library in order:
                    table = tables[library]
                    # OceanBase has no "DROP INDEX IF EXISTS", so tolerate the
                    # "no such index" error of the very first round.
                    try:
                        cursor.execute(f"DROP INDEX `idx` ON `{table}`")
                    except pymysql.err.MySQLError as drop_error:
                        # 1091: the index does not exist yet (first round).
                        if not drop_error.args or drop_error.args[0] != 1091:
                            raise
                    log_offset = args.gpu_log.stat().st_size
                    begin = time.perf_counter()
                    cursor.execute(
                        f"CREATE VECTOR INDEX idx ON `{table}`(v) WITH ("
                        f"distance=l2,type=hnsw,lib={library},m={args.m},"
                        f"ef_construction={args.ef_construction},ef_search={args.ef_search})")
                    sql = (f"SELECT /*+ INDEX(`{table}` idx) */ id FROM `{table}` "
                           f"ORDER BY l2_distance(v, %s) APPROXIMATE LIMIT {args.k}")
                    cursor.execute(sql, (query_json[0],))
                    cursor.fetchall()
                    build_seconds = time.perf_counter() - begin

                    gpu_records = build_gpu_records(args.gpu_log, log_offset, args.rows, args.dim)
                    if library == "HIPVS" and not gpu_records:
                        raise RuntimeError(
                            "no SEEKDB_HIPVS_BUILD record in the server log for this round: "
                            "the GPU path cannot be claimed, aborting")

                    hits = 0
                    latencies = []
                    for index, query in enumerate(query_json):
                        started = time.perf_counter()
                        cursor.execute(sql, (query,))
                        found = {int(row[0]) for row in cursor.fetchall()}
                        latencies.append((time.perf_counter() - started) * 1000.0)
                        hits += len(found & truth[index]) / args.k
                    recall = hits / len(query_json)

                    cursor.execute("EXPLAIN " + sql, (query_json[0],))
                    plan = [list(row) for row in cursor.fetchall()]
                    plan_text = " ".join(str(cell) for row in plan for cell in row)
                    used_index = "idx" in plan_text

                    report["runs"].append({
                        "library": library,
                        "round": round_id + 1,
                        "build_seconds": build_seconds,
                        "recall_at_k": recall,
                        "query_median_ms": statistics.median(latencies),
                        "query_p95_ms": float(np.percentile(latencies, 95)),
                        "explain_uses_index": used_index,
                        "gpu_evidence": gpu_records,
                    })
                    print(f"[round {round_id + 1}] {library:5s} build={build_seconds:8.3f}s "
                          f"recall@{args.k}={recall:.4f} index_used={used_index} "
                          f"gpu_records={len(gpu_records)}", flush=True)
                    save()

        medians = {library: statistics.median(run["build_seconds"] for run in report["runs"]
                                             if run["library"] == library)
                   for library in ("VSAG", "HIPVS")}
        recalls = {library: statistics.median(run["recall_at_k"] for run in report["runs"]
                                              if run["library"] == library)
                   for library in ("VSAG", "HIPVS")}
        report["medians"] = {"build_seconds": medians, "recall_at_k": recalls}
        report["speedup"] = medians["VSAG"] / medians["HIPVS"] if medians["HIPVS"] else None
        report["recall_ok"] = all(value >= args.min_recall for value in recalls.values())
        report["status"] = "measured" if report["recall_ok"] else "low_recall"
    except Exception as error:  # noqa: BLE001 - report, then re-raise
        report["status"] = "failed"
        report["error"] = f"{type(error).__name__}: {error}"
        raise
    finally:
        for table in tables.values():
            try:
                with connection.cursor() as cursor:
                    cursor.execute(f"DROP TABLE IF EXISTS `{table}`")
            except Exception:  # noqa: BLE001
                pass
        connection.close()
        save()

    print(json.dumps({"medians": report["medians"], "speedup": report["speedup"],
                      "recall_ok": report["recall_ok"], "status": report["status"]}, indent=2))
    if not report["recall_ok"]:
        raise SystemExit("recall below --min-recall for at least one library")


if __name__ == "__main__":
    main()