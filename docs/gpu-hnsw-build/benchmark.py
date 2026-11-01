#!/usr/bin/env python3
"""Measure fresh SQL HNSW builds, including the first availability query.

Requires numpy and a mysql CLI. Uses a dedicated, explicitly named database.
Environment startup and original data generation/import are outside build time.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import statistics
import subprocess
import time

def validate_filter_result(got, expected, valid_ids, min_recall):
    """Fail the campaign when filtering returns bad IDs or loses recall."""
    if not expected or len(got) != len(expected) or len(set(got)) != len(got):
        raise RuntimeError("filtered query returned missing or duplicate IDs")
    got = set(got)
    if not got <= valid_ids or any(v % 3 != 0 for v in got):
        raise RuntimeError("filtered query returned an invalid or unfiltered ID")
    recall = len(got & expected) / len(expected)
    if recall < min_recall:
        raise RuntimeError(f"filtered Recall@10 {recall} is below the local floor {min_recall}")
    return recall


def begin_restart_verification(report, digest):
    if report["configuration"]["data_sha256"] != digest:
        raise RuntimeError("restart verification uses a different dataset")
    if not report.get("build_completed", report.get("completed", False)):
        raise RuntimeError("restart verification requires a successful build campaign")
    # Retain the historical construction result separately, so a failed
    # restart can be retried without presenting the latest attempt as passed.
    report["build_completed"] = True
    report["completed"] = False
    report.pop("error", None)
    report["restart_verification"] = {"status": "running"}


def record_failure(report, error, restart=False):
    report["completed"] = False
    report["error"] = str(error)
    if restart:
        report["restart_verification"] = {"status": "failed", "error": str(error)}


def main():
    p = argparse.ArgumentParser(__doc__)
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=2881)
    p.add_argument("--user", default="root")
    p.add_argument("--client", default="mysql")
    p.add_argument("--database", default="hipvs_build_benchmark")
    p.add_argument("--rows", type=int, default=50000)
    p.add_argument("--dim", type=int, default=128)
    p.add_argument("--queries", type=int, default=100)
    p.add_argument("--rounds", type=int, default=3)
    p.add_argument("--m", type=int, choices=(16, 32, 64), default=32)
    p.add_argument("--gpu-lib", choices=("hipvs", "cuvs"), default="hipvs",
                   help="AMD construction spelling; cuvs matches the course SQL alias")
    p.add_argument("--ef-construction", type=int, default=200)
    p.add_argument("--ef-search", type=int, default=1000)
    p.add_argument("--min-recall", type=float, default=0.99,
                   help="local quality floor, not an official competition threshold")
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--server-log", type=Path, required=True)
    p.add_argument("--verify-existing", action="store_true")
    a = p.parse_args()
    if not re.fullmatch(r"[a-zA-Z][a-zA-Z0-9_]*", a.database):
        p.error("database must be a simple SQL identifier")
    if a.rounds < 3 or a.rows <= 4 * a.m or min(a.dim, a.queries) <= 0:
        p.error("at least three rounds and enough rows/dimensions/queries are required")
    if not 0 <= a.min_recall <= 1 or a.ef_construction <= a.m or a.ef_search < 10:
        p.error("invalid quality or HNSW parameters")
    import numpy as np
    a.output.parent.mkdir(parents=True, exist_ok=True)
    client = [a.client, "--protocol=TCP", f"--host={a.host}", f"--port={a.port}",
              f"--user={a.user}", "--batch", "--raw", "--skip-column-names",
              "--connect-timeout=10", "--max-allowed-packet=67108864"]

    def sql(statement, database=True):
        prefix = f"SET ob_query_timeout=600000000; SET ob_hnsw_ef_search={a.ef_search};"
        cmd = client + ([a.database] if database else [])
        result = subprocess.run(cmd, input=prefix + statement, text=True,
                                capture_output=True, timeout=650)
        if result.returncode:
            raise RuntimeError(result.stderr.strip() + " SQL=" + statement[:160])
        return [line.split("\t") for line in result.stdout.splitlines() if line]

    def vec(v):
        # Nine significant digits round-trip all finite float32 inputs.
        return "'[" + ",".join(format(float(x), ".9g") for x in v) + "]'"

    rng = np.random.default_rng(20261030)
    x = rng.standard_normal((a.rows, a.dim), dtype=np.float32)
    ids = 1000000007 + 17 * rng.permutation(a.rows).astype(np.int64)
    queries = rng.standard_normal((a.queries, a.dim), dtype=np.float32)
    digest = hashlib.sha256(x.tobytes() + ids.tobytes() + queries.tobytes()).hexdigest()
    config = {k: v for k, v in vars(a).items() if k not in ("output", "server_log")}
    config.update(seed=20261030, dtype="float32", distance="l2", data_sha256=digest)
    report = {"configuration": config, "runs": [], "official_target_known": False,
              "completed": False, "build_completed": False,
              "timing": "CREATE VECTOR INDEX through completion of the same first availability query"}
    if a.verify_existing:
        report = json.loads(a.output.read_text(encoding="utf-8"))
        begin_restart_verification(report, digest)

    def save():
        tmp = a.output.with_suffix(".tmp")
        tmp.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        tmp.replace(a.output)

    def neighbors(q, approximate=True, where="", limit=10):
        rows = sql(f"SELECT id,l2_distance(vec,{vec(q)}) FROM corpus {where} "
                   f"ORDER BY l2_distance(vec,{vec(q)}) "
                   f"{'APPROXIMATE' if approximate else ''} LIMIT {limit}")
        got = [int(row[0]) for row in rows]
        if len(got) != limit or len(set(got)) != limit:
            raise RuntimeError(f"query returned missing or duplicate IDs: expected={limit}, rows={rows}")
        if any(not math.isfinite(float(row[1])) or float(row[1]) < 0 for row in rows):
            raise RuntimeError("query returned an invalid distance")
        return got

    start = time.perf_counter()
    # Compute independent exact ground truth from the imported float32 values.
    x64 = x.astype(np.float64)
    truth = []
    for q in queries:
        distances = np.einsum("ij,ij->i", x64 - q, x64 - q)
        truth.append(set(int(v) for v in ids[np.argsort(distances)[:10]]))
    report["ground_truth_seconds"] = time.perf_counter() - start
    valid_ids = set(int(v) for v in ids)

    def quality():
        recalls = []
        for q, expected in zip(queries, truth):
            got = set(neighbors(q))
            if not got <= valid_ids:
                raise RuntimeError("query returned an ID outside the imported corpus")
            recalls.append(len(got & expected) / 10)
        for q, expected in list(zip(queries, truth))[:3]:
            if set(neighbors(q, approximate=False)) != expected:
                raise RuntimeError("numpy and exact SQL disagree")
        recall = statistics.mean(recalls)
        if recall < a.min_recall:
            raise RuntimeError(f"Recall@10 {recall} is below the local floor {a.min_recall}")
        return {"recall_at_10": recall, "queries": len(recalls),
                "per_query_recall": recalls, "exact_sql_crosscheck": True}

    try:
        if a.verify_existing:
            save()
            if int(sql("SELECT count(*) FROM corpus")[0][0]) != a.rows:
                raise RuntimeError("restart lost rows")
            report["restart_verification"] = {"status": "passed", **quality()}
            report["completed"] = True
            save()
            print(json.dumps(report["restart_verification"]), flush=True)
            return
        sql(f"CREATE DATABASE IF NOT EXISTS {a.database}", database=False)
        sql("DROP TABLE IF EXISTS corpus;CREATE TABLE corpus(id BIGINT PRIMARY KEY,"
            f"vec VECTOR({a.dim}))")
        report["server_version"] = sql("SELECT version()")
        report["server_settings"] = sql("SHOW PARAMETERS WHERE name IN "
            "('cpu_count','memory_limit','vector_memory_limit')")
        start = time.perf_counter()
        for begin in range(0, a.rows, 500):
            values = ",".join(f"({int(ids[i])},{vec(x[i])})"
                              for i in range(begin, min(begin + 500, a.rows)))
            sql("INSERT INTO corpus VALUES " + values)
        report["import_seconds"] = time.perf_counter() - start
        if int(sql("SELECT count(*) FROM corpus")[0][0]) != a.rows:
            raise RuntimeError("incomplete import")
        save()
        for round_no in range(1, a.rounds + 1):
            # Alternate which backend runs first, retaining a fresh index each time.
            order = ("vsag", "hipvs") if round_no % 2 else ("hipvs", "vsag")
            for backend in order:
                run = {"round": round_no, "backend": backend, "status": "building"}
                report["runs"].append(run)
                save()
                log_paths = lambda: list(a.server_log.parent.glob(a.server_log.name + "*"))
                old_inodes = {path.stat().st_ino for path in log_paths() if path.is_file()}
                # Keep the original inode open across rotation; reading a new
                # file at the old file's offset can silently lose GPU evidence.
                with a.server_log.open("rb") as log:
                    log.seek(0, os.SEEK_END)
                    start = time.perf_counter()
                    lib = a.gpu_lib if backend == "hipvs" else backend
                    sql(f"CREATE VECTOR INDEX idx ON corpus(vec) WITH (distance=l2,type=hnsw,"
                        f"lib={lib},m={a.m},ef_construction={a.ef_construction},ef_search={a.ef_search})")
                    run["ddl_seconds"] = time.perf_counter() - start
                    first = neighbors(queries[0])
                    elapsed = time.perf_counter() - start
                    log_text = log.read().decode(errors="replace")
                for path in log_paths():
                    if path.is_file() and not path.name.endswith(".gz") and path.stat().st_ino not in old_inodes:
                        log_text += path.read_bytes().decode(errors="replace")
                events = [line for line in log_text.splitlines() if "[HIPVS_BUILD]" in line]
                run.update(build_seconds=elapsed, availability_ids=first, gpu_events=events)
                save()
                if backend == "hipvs":
                    if not any("complete gpu=true fallback=false" in e for e in events):
                        raise RuntimeError("GPU build has no successful actual GPU-path evidence")
                    if any("fallback=true" in e or "[HIPVS_BUILD] failed" in e for e in events):
                        raise RuntimeError("GPU performance run contained fallback or an error")
                run.update(quality())
                run["explain"] = sql(f"EXPLAIN SELECT id FROM corpus ORDER BY "
                                     f"l2_distance(vec,{vec(queries[0])}) APPROXIMATE LIMIT 10")
                run["status"] = "passed"
                print(json.dumps({k: v for k, v in run.items()
                                  if k not in ("explain", "per_query_recall", "gpu_events")}), flush=True)
                save()
                sql("DROP INDEX idx ON corpus")
        medians = {backend: statistics.median(run["build_seconds"] for run in report["runs"]
                                             if run["backend"] == backend)
                   for backend in ("vsag", "hipvs")}
        report["medians_seconds"] = medians
        report["speedup"] = medians["vsag"] / medians["hipvs"]
        # Leave a newly built GPU-produced native index for restart and DML checks.
        sql(f"CREATE VECTOR INDEX idx ON corpus(vec) WITH (distance=l2,type=hnsw,lib={a.gpu_lib},"
            f"m={a.m},ef_construction={a.ef_construction},ef_search={a.ef_search})")
        target = int(ids[0])
        zero = np.zeros(a.dim, dtype=np.float32)
        sql(f"UPDATE corpus SET vec={vec(zero)} WHERE id={target}")
        if neighbors(zero, limit=1) != [target]:
            raise RuntimeError("post-build update query failed")
        sql(f"DELETE FROM corpus WHERE id={target}")
        if target in neighbors(zero):
            raise RuntimeError("deleted snapshot ID leaked into results")
        sql(f"INSERT INTO corpus VALUES ({target},{vec(x[0])})")
        if neighbors(x[0], limit=1) != [target]:
            raise RuntimeError("post-build reinsertion query failed")
        mask = ids % 3 == 0
        filtered_dist = np.einsum("ij,ij->i", x64[mask] - queries[0], x64[mask] - queries[0])
        expected = set(int(v) for v in ids[mask][np.argsort(filtered_dist)[:10]])
        got = neighbors(queries[0], where="WHERE id % 3 = 0")
        filter_recall = validate_filter_result(got, expected, valid_ids, a.min_recall)
        report["dml_and_filter"] = {"update": True, "delete": True, "reinsert": True,
                                    "filter_recall_at_10": filter_recall,
                                    "all_filter_ids_valid": True}
        report["build_completed"] = True
        report["completed"] = True
        save()
        print(json.dumps({"medians_seconds": medians, "speedup": report["speedup"],
                          "dml_and_filter": report["dml_and_filter"]}), flush=True)
    except Exception as exc:
        record_failure(report, exc, restart=a.verify_existing)
        save()
        raise


if __name__ == "__main__":
    main()
