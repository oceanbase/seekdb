#!/usr/bin/env python3
"""Check recorded hipVS evidence without running SQL or claiming new GPU timings."""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import re
import statistics
import subprocess


def require(condition, message):
    if not condition:
        raise ValueError(message)


def verify_quality(result, floor, queries):
    values = result.get("per_query_recall", [])
    require(result.get("queries") == queries and len(values) == queries and queries > 0,
            "recall query count mismatch")
    require(all(isinstance(v, (int, float)) and math.isfinite(v) and 0 <= v <= 1 for v in values),
            "nonfinite or out-of-range per-query recall")
    recall = statistics.mean(values)
    require(math.isclose(recall, result["recall_at_10"], rel_tol=0, abs_tol=1e-12),
            "aggregate recall disagrees with per-query recalls")
    require(recall >= floor and result.get("exact_sql_crosscheck") is True,
            "quality floor or exact SQL cross-check failed")


def verify_runs(report):
    require(report.get("completed") is True and not report.get("error"),
            "campaign is incomplete or has a recorded error")
    cfg = report["configuration"]
    rounds, queries, floor = cfg["rounds"], cfg["queries"], cfg["min_recall"]
    require(rounds >= 3 and math.isfinite(floor) and 0 <= floor <= 1,
            "invalid rounds or recall floor")
    expected = [(r, backend) for r in range(1, rounds + 1)
                for backend in (("vsag", "hipvs") if r % 2 else ("hipvs", "vsag"))]
    require([(r["round"], r["backend"]) for r in report["runs"]] == expected,
            "missing, duplicated, or out-of-order build rounds")
    for run in report["runs"]:
        require(run["status"] == "passed", "build run did not pass")
        seconds, ddl = run["build_seconds"], run["ddl_seconds"]
        require(math.isfinite(seconds) and math.isfinite(ddl) and seconds >= ddl > 0,
                "invalid end-to-end or DDL timing")
        ids = run["availability_ids"]
        require(len(ids) == 10 and len(set(ids)) == 10, "invalid availability IDs")
        verify_quality(run, floor, queries)
        if run["backend"] == "hipvs":
            events = run["gpu_events"]
            require(len(events) == 1 and "complete gpu=true fallback=false" in events[0],
                    "GPU run has missing, duplicated, failed, or fallback evidence")
            for field, value in (("rows", cfg["rows"]), ("dim", cfg["dim"]), ("max_degree", cfg["m"])):
                require(re.search(rf"\b{field}={value}(?=[,\s)])", events[0]),
                        f"GPU event does not match dataset parameter {field}")
    medians = {backend: statistics.median(r["build_seconds"] for r in report["runs"]
                                         if r["backend"] == backend)
               for backend in ("vsag", "hipvs")}
    for backend, value in medians.items():
        require(math.isclose(value, report["medians_seconds"][backend], rel_tol=1e-12),
                "recorded median disagrees with raw runs")
    speedup = medians["vsag"] / medians["hipvs"]
    require(math.isclose(speedup, report["speedup"], rel_tol=1e-12),
            "recorded speedup disagrees with medians")
    dml = report["dml_and_filter"]
    require(all(dml.get(k) is True for k in ("update", "delete", "reinsert", "all_filter_ids_valid")),
            "DML or filter IDs did not pass")
    require(math.isfinite(dml["filter_recall_at_10"]) and floor <= dml["filter_recall_at_10"] <= 1,
            "filtered recall is below the local floor")
    restart = report["restart_verification"]
    require(restart.get("status", "passed") == "passed", "restart verification did not pass")
    verify_quality(restart, floor, queries)
    return medians, speedup


def verify_evidence(root, source_ref=None, results_dir=None):
    root = root.resolve()
    folder = Path(results_dir).resolve() if results_dir else root / "docs/gpu-hnsw-build/results"
    load = lambda name: json.loads((folder / name).read_text(encoding="utf-8"))
    report = load("sql-50000x128.json")
    medians, speedup = verify_runs(report)
    manifest = load("source-sha256.json")
    if source_ref:
        source_ref = subprocess.check_output(
            ["git", "rev-parse", "--verify", source_ref + "^{commit}"], cwd=root,
            text=True).strip()
        subprocess.run(["git", "merge-base", "--is-ancestor", load("final-environment.json")["base_revision"],
                        source_ref], cwd=root, check=True)
    for path, digest in manifest.items():
        target = (root / path).resolve()
        require(not Path(path).is_absolute() and target.is_relative_to(root),
                "source manifest escapes repository")
        content = (subprocess.check_output(["git", "show", f"{source_ref}:{path}"], cwd=root)
                   if source_ref else target.read_bytes().replace(b"\r\n", b"\n"))
        require(hashlib.sha256(content).hexdigest() == digest,
                f"measured source differs: {path}; select the measured --source-ref for historical evidence")
    profile = load("kernel-trace-50000x128-command.json")
    require(profile["returncode"] == 0 and profile["outside_sql_timed_runs"] is True,
            "profiling failed or was mixed into SQL timing")
    for key in ("rows", "dim", "m", "ef_construction"):
        require(profile[key] == report["configuration"][key], "profile parameters differ from SQL run")
    hashes = load("final-environment.json")["sha256"]
    require(any(path.endswith("/seekdb_hipvs_hnsw_worker") and digest == profile["worker_sha256"]
                for path, digest in hashes.items()), "profile worker differs from measured worker")
    with (folder / "kernel-trace-50000x128/hipvs-build_kernel_trace.csv").open(encoding="utf-8", newline="") as stream:
        kernels = list(csv.DictReader(stream))
    require(len(kernels) == profile["kernel_dispatches"] and len(kernels) > 0,
            "kernel dispatch count mismatch")
    nn_descent = sum("nn_descent" in row["Kernel_Name"].lower() for row in kernels)
    require(nn_descent == profile["nn_descent_dispatches"] and nn_descent > 0,
            "NN-descent dispatch count mismatch")
    return {"verified": True, "scope": "archived evidence consistency only; no SQL or GPU execution",
            "source_ref": source_ref or "working tree (LF normalized)", "source_files": len(manifest),
            "build_runs": len(report["runs"]), "medians_seconds": medians, "speedup": speedup,
            "kernel_dispatches": len(kernels), "nn_descent_dispatches": nn_descent}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument("--source-ref", help="commit used to produce the archived measurements")
    parser.add_argument("--results-dir", type=Path, help="directory containing a separate measurement campaign")
    args = parser.parse_args()
    try:
        result = verify_evidence(Path(__file__).resolve().parents[2], args.source_ref, args.results_dir)
    except (KeyError, OSError, ValueError, TypeError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Evidence verification failed: {error}\n")
    print(json.dumps(result, indent=2))
