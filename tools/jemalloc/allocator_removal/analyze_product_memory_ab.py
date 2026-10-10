#!/usr/bin/env python3
# Copyright (c) 2026 OceanBase.
# Licensed under the Apache License, Version 2.0.

"""Summarize product allocator A/B JSON results and enforce the 5% gate."""

import argparse
import glob
import json
import math
import os
import re
import statistics
import sys


def coefficient_of_variation(values):
    if not values:
        return math.inf
    mean = statistics.fmean(values)
    if mean == 0:
        return math.inf
    return statistics.pstdev(values) / mean * 100.0


def load_results(root):
    groups = {}
    pattern = os.path.join(root, "*", "*", "results", "*.json")
    for path in sorted(glob.glob(pattern)):
        parts = os.path.normpath(path).split(os.sep)
        variant = parts[-4]
        budget = parts[-3]
        with open(path, encoding="utf-8") as result_file:
            result = json.load(result_file)
        workload = result["workload"]
        groups.setdefault((budget, workload, variant), []).append(result)
    return groups


def load_runtime_metrics(root):
    metrics = {}
    for variant_dir in sorted(glob.glob(os.path.join(root, "*", "*"))):
        if not os.path.isdir(variant_dir):
            continue
        variant = os.path.basename(os.path.dirname(variant_dir))
        budget = os.path.basename(variant_dir)
        memory_path = os.path.join(variant_dir, "memory-summary.txt")
        samples_path = os.path.join(variant_dir, "process-and-components.tsv")
        jemalloc_path = os.path.join(variant_dir, "jemalloc-summary.txt")
        item = {"components": {}}
        if os.path.exists(memory_path):
            with open(memory_path, encoding="utf-8") as memory_file:
                for line in memory_file:
                    key, value = line.strip().split("=", 1)
                    item[key] = int(value or 0)
        if os.path.exists(samples_path):
            with open(samples_path, encoding="utf-8") as samples_file:
                next(samples_file, None)
                for line in samples_file:
                    fields = line.rstrip("\n").split("\t")
                    if len(fields) != 9:
                        continue
                    component = fields[3]
                    component_item = item["components"].setdefault(
                        component,
                        {"max_committed_bytes": 0, "max_reserved_bytes": 0,
                         "max_reject_count": 0, "max_reclaim_count": 0},
                    )
                    component_item["max_committed_bytes"] = max(
                        component_item["max_committed_bytes"], int(fields[5]))
                    component_item["max_reserved_bytes"] = max(
                        component_item["max_reserved_bytes"], int(fields[6]))
                    component_item["max_reject_count"] = max(
                        component_item["max_reject_count"], int(fields[7]))
                    component_item["max_reclaim_count"] = max(
                        component_item["max_reclaim_count"], int(fields[8]))
        if os.path.exists(jemalloc_path):
            with open(jemalloc_path, encoding="utf-8") as jemalloc_file:
                lines = [line.rstrip("\n") for line in jemalloc_file]
            samples = []
            for line in lines:
                sample = {}
                for key in ("allocated", "active", "allocator_resident", "mapped",
                            "virtual_memory", "process_resident"):
                    match = re.search(rf"(?:^|[, (]){key}=([0-9]+)", line)
                    if match:
                        sample[key] = int(match.group(1))
                if sample:
                    samples.append(sample)
            item["jemalloc_samples"] = samples
            if samples:
                for key in ("allocated", "active", "allocator_resident", "mapped"):
                    item[f"max_jemalloc_{key}_bytes"] = max(
                        sample.get(key, 0) for sample in samples
                    )
        metrics[f"{budget}/{variant}"] = item
    return metrics


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("result_dir")
    parser.add_argument("--max-cv-percent", type=float, default=3.0)
    parser.add_argument("--max-regression-percent", type=float, default=5.0)
    args = parser.parse_args()

    groups = load_results(args.result_dir)
    report = {
        "groups": {},
        "runtime_metrics": load_runtime_metrics(args.result_dir),
        "failures": [],
        "noisy": [],
    }
    for key, results in sorted(groups.items()):
        budget, workload, variant = key
        throughput = [result["operations_per_second"] for result in results]
        p99 = [result["latency_us"]["p99"] for result in results]
        identity = f"{budget}/{workload}/{variant}"
        report["groups"][identity] = {
            "rounds": len(results),
            "throughput_median": statistics.median(throughput),
            "throughput_cv_percent": coefficient_of_variation(throughput),
            "p99_us_median": statistics.median(p99),
            "p99_cv_percent": coefficient_of_variation(p99),
        }
        if (coefficient_of_variation(throughput) > args.max_cv_percent
                or coefficient_of_variation(p99) > args.max_cv_percent):
            report["noisy"].append(identity)

    for budget, workload in sorted({(key[0], key[1]) for key in groups}):
        base = report["groups"].get(f"{budget}/{workload}/base")
        candidate = report["groups"].get(f"{budget}/{workload}/candidate")
        if base is None or candidate is None:
            report["failures"].append(f"missing A/B pair for {budget}/{workload}")
            continue
        throughput_delta = (
            candidate["throughput_median"] / base["throughput_median"] - 1.0
        ) * 100.0
        p99_delta = (candidate["p99_us_median"] / base["p99_us_median"] - 1.0) * 100.0
        comparison = {
            "throughput_delta_percent": throughput_delta,
            "p99_delta_percent": p99_delta,
        }
        report.setdefault("comparisons", {})[f"{budget}/{workload}"] = comparison
        if throughput_delta < -args.max_regression_percent:
            report["failures"].append(
                f"{budget}/{workload} throughput regressed {throughput_delta:.2f}%"
            )
        if p99_delta > args.max_regression_percent:
            report["failures"].append(
                f"{budget}/{workload} p99 regressed {p99_delta:.2f}%"
            )

    for budget in sorted({key[0] for key in groups}):
        for variant in ("base", "candidate"):
            identity = f"{budget}/{variant}"
            runtime = report["runtime_metrics"].get(identity)
            if runtime is None:
                report["failures"].append(f"missing runtime metrics for {identity}")
            elif not runtime.get("jemalloc_samples"):
                report["failures"].append(f"missing jemalloc samples for {identity}")
        candidate_runtime = report["runtime_metrics"].get(f"{budget}/candidate", {})
        components = candidate_runtime.get("components", {})
        expected_components = {"KV_CACHE", "SQL_WORKAREA", "VECTOR", "META_OBJECT"}
        missing_components = sorted(expected_components.difference(components))
        if missing_components:
            report["failures"].append(
                f"missing candidate component samples for {budget}: {missing_components}"
            )

    report_path = os.path.join(args.result_dir, "summary.json")
    with open(report_path, "w", encoding="utf-8") as report_file:
        json.dump(report, report_file, indent=2, sort_keys=True)
        report_file.write("\n")
    print(json.dumps(report, indent=2, sort_keys=True))
    if report["noisy"]:
        print("result is too noisy to pass; rerun listed groups", file=sys.stderr)
        return 2
    if report["failures"]:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
