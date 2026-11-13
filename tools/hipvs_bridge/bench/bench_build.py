#!/usr/bin/env python3
# Copyright (c) 2025 OceanBase.
#
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
"""Reproducible CPU-vs-hipVS HNSW *build* benchmark.

Everything printed here is measured in this process - there are no baked-in
numbers.  On the AMD Radeon Cloud `seekdb-hipvs` template both backends are
available; on a laptop only the CPU baseline runs and the GPU rows are marked
as skipped, which keeps the script useful as a smoke test.

    python3 bench_build.py --n 10000 50000 100000 --dim 128 --k 10
    python3 bench_build.py --n 50000 --dim 256 --repeat 3 --csv results.csv

Backends
    cpu   hnswlib (the same HNSW parameters seekdb exposes as m / ef_construction)
    gpu   hipVS CAGRA (cuvs.neighbors.cagra) optionally projected to HNSW via
          cuvs.neighbors.hnsw.from_cagra, i.e. the exact path the bridge uses
"""

import argparse
import csv
import json
import os
import statistics
import sys
import time

import numpy as np


def _load_backends():
    backends = {}
    try:
        import hnswlib  # noqa: F401

        backends["cpu"] = "hnswlib"
    except Exception:
        backends["cpu"] = None
    try:
        # hipVS ships the cuVS python package; the import name is identical.
        from cuvs.neighbors import cagra  # noqa: F401

        backends["gpu"] = "cuvs"
    except Exception:
        backends["gpu"] = None
    return backends


def make_dataset(n, dim, seed=20261113):
    rng = np.random.default_rng(seed)
    data = rng.standard_normal((n, dim), dtype=np.float32)
    # normalise so l2 and cosine ranking agree and recall numbers are comparable
    data /= np.linalg.norm(data, axis=1, keepdims=True)
    return np.ascontiguousarray(data, dtype=np.float32)


def exact_topk(data, queries, k):
    """Ground truth by blocked brute force (no sklearn dependency)."""
    out = np.empty((queries.shape[0], k), dtype=np.int64)
    for i, q in enumerate(queries):
        d = np.sum((data - q) ** 2, axis=1)
        out[i] = np.argpartition(d, k - 1)[:k][np.argsort(d[np.argpartition(d, k - 1)[:k]])]
    return out


def recall_at_k(got_ids, truth_ids):
    hit = 0
    total = 0
    for got, truth in zip(got_ids, truth_ids):
        truth_set = set(int(x) for x in truth)
        for g in got:
            if int(g) < 0:
                continue
            total += 1
            hit += 1 if int(g) in truth_set else 0
    return hit / total if total else 0.0


def build_cpu(data, m, ef_construction):
    import hnswlib

    index = hnswlib.Index(space="l2", dim=data.shape[1])
    index.init_index(max_elements=data.shape[0], ef_construction=ef_construction, M=m)
    started = time.perf_counter()
    index.add_items(data, np.arange(data.shape[0]))
    elapsed = time.perf_counter() - started
    index.set_ef(max(ef_construction, 64))
    return index, elapsed, lambda q, k: index.knn_query(q, k=k)[0]


def build_gpu(data, m, ef_construction, project_hnsw):
    from cuvs.neighbors import cagra, hnsw

    params = cagra.IndexParams(
        metric="euclidean",
        graph_degree=m,
        intermediate_graph_degree=max(m * 2, 64),
    )
    started = time.perf_counter()
    index = cagra.build(params, data)
    elapsed = time.perf_counter() - started

    exported = None
    if project_hnsw:
        try:
            hp = hnsw.IndexParams(hierarchy="cpu", ef_construction=ef_construction)
            exported = hnsw.from_cagra(hp, index)
        except Exception as exc:  # older hipVS builds lack the projection
            print("  hnsw projection unavailable: %s" % exc)

    def search(q, k):
        sp = cagra.SearchParams(itopk_size=max(k, ef_construction))
        _d, ids = cagra.search(sp, index, q, k)
        return np.asarray(ids)

    return index, elapsed, search, exported


def fmt(x):
    return "n/a" if x is None else ("%.2f" % x)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--n", type=int, nargs="+", default=[10000, 50000, 100000])
    ap.add_argument("--dim", type=int, default=128)
    ap.add_argument("--k", type=int, default=10)
    ap.add_argument("--m", type=int, default=16, help="HNSW m / CAGRA graph_degree")
    ap.add_argument("--ef-construction", type=int, default=200)
    ap.add_argument("--repeat", type=int, default=1, help="take the median of N runs")
    ap.add_argument("--queries", type=int, default=200)
    ap.add_argument("--csv", default=None)
    ap.add_argument("--json", default=None)
    args = ap.parse_args()

    backends = _load_backends()
    print("backends:", json.dumps(backends, ensure_ascii=False))
    if not backends["cpu"] and not backends["gpu"]:
        print("no backend available - on the AMD template run inside the seekdb-hipvs image")
        return 1

    rows = []
    for n in args.n:
        data = make_dataset(n, args.dim)
        queries = make_dataset(max(args.queries, 1), args.dim, seed=7)
        truth = exact_topk(data, queries, args.k)

        row = {"n": n, "dim": args.dim, "k": args.k, "m": args.m,
               "ef_construction": args.ef_construction}

        if backends["cpu"]:
            times, recs = [], []
            for _ in range(args.repeat):
                index, secs, search = build_cpu(data, args.m, args.ef_construction)
                got = search(queries, args.k)
                times.append(secs)
                recs.append(recall_at_k(got, truth))
            row.update(cpu_build_s=statistics.median(times),
                       cpu_recall=statistics.median(recs))
        else:
            row.update(cpu_build_s=None, cpu_recall=None)

        if backends["gpu"]:
            times, recs = [], []
            for _ in range(args.repeat):
                _idx, secs, search, _exported = build_gpu(
                    data, args.m, args.ef_construction, project_hnsw=True)
                got = search(queries, args.k)
                times.append(secs)
                recs.append(recall_at_k(got, truth))
            row.update(gpu_build_s=statistics.median(times),
                       gpu_recall=statistics.median(recs))
        else:
            row.update(gpu_build_s=None, gpu_recall=None)

        if row.get("cpu_build_s") and row.get("gpu_build_s"):
            row["speedup"] = row["cpu_build_s"] / row["gpu_build_s"]
        else:
            row["speedup"] = None
        rows.append(row)

        print("n=%-8d cpu_build=%-9s gpu_build=%-9s speedup=%-7s "
              "recall cpu=%-7s gpu=%-7s" % (
                  row["n"], fmt(row["cpu_build_s"]), fmt(row["gpu_build_s"]),
                  fmt(row["speedup"]), fmt(row["cpu_recall"]), fmt(row["gpu_recall"])))

    print()
    print("| N | dim | CPU build (s) | hipVS build (s) | speedup | recall@%d CPU | recall@%d hipVS |"
          % (args.k, args.k))
    print("|---:|---:|---:|---:|---:|---:|---:|")
    for r in rows:
        print("| %d | %d | %s | %s | %s | %s | %s |" % (
            r["n"], r["dim"], fmt(r.get("cpu_build_s")), fmt(r.get("gpu_build_s")),
            fmt(r.get("speedup")), fmt(r.get("cpu_recall")), fmt(r.get("gpu_recall"))))

    if args.csv:
        os.makedirs(os.path.dirname(args.csv) or ".", exist_ok=True)
        with open(args.csv, "w", newline="", encoding="utf-8") as fh:
            writer = csv.DictWriter(fh, fieldnames=list(rows[0].keys()))
            writer.writeheader()
            writer.writerows(rows)
        print("\nwrote", args.csv)
    if args.json:
        with open(args.json, "w", encoding="utf-8") as fh:
            json.dump({"backends": backends, "rows": rows}, fh, indent=2)
        print("wrote", args.json)
    return 0


if __name__ == "__main__":
    sys.exit(main())
