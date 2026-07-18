#!/usr/bin/env python3
"""Aggregate repeated FTS benchmark reports and reuse the official scorer."""

from __future__ import annotations

import argparse
import json
from copy import deepcopy
from pathlib import Path
from statistics import mean, stdev
from typing import Any

from fts_large_bench_score import (
    DEFAULT_BASELINE,
    calculate_score,
    check_config,
    load_baseline,
    load_report,
)


REPORT_METRICS = (
    "select1_avg_ms",
    "raw_load_sec",
    "build_ik_all_sec",
    "build_ik_content_sec",
    "build_beng_en_sec",
    "build_total_sec",
    "tokenize_ik_avg_ms",
    "tokenize_beng_avg_ms",
    "query_cn_avg_ms",
    "query_beng_avg_ms",
    "query_mixed_avg_ms",
    "query_limit_avg_ms",
)
HIT_METRICS = (
    "query_cn_hits",
    "query_beng_hits",
    "query_mixed_hits",
    "query_limit_hits",
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Compare repeated before/after fts_large_bench.sh reports. "
            "Scoring is delegated to fts_large_bench_score.py."
        )
    )
    parser.add_argument("--before", type=Path, nargs="+", required=True)
    parser.add_argument("--after", type=Path, nargs="+", required=True)
    parser.add_argument("--baseline", type=Path, default=DEFAULT_BASELINE)
    parser.add_argument("--json-out", type=Path)
    return parser.parse_args()


def load_group(paths: list[Path], baseline: dict[str, Any]) -> list[dict[str, float]]:
    reports = []
    for path in paths:
        report = load_report(path)
        check_config(report, baseline, allow_config_mismatch=False)
        missing = [metric for metric in REPORT_METRICS + HIT_METRICS if metric not in report]
        if missing:
            raise SystemExit(f"{path}: missing metrics: {', '.join(missing)}")
        reports.append(report)
    return reports


def summarize(reports: list[dict[str, float]]) -> dict[str, dict[str, float]]:
    result = {}
    for metric in REPORT_METRICS + HIT_METRICS:
        values = [report[metric] for report in reports]
        result[metric] = {
            "mean": mean(values),
            "stdev": stdev(values) if len(values) > 1 else 0.0,
        }
    return result


def aggregate_report(
    reports: list[dict[str, float]], summary: dict[str, dict[str, float]]
) -> dict[str, float]:
    aggregate = {
        key: reports[0][key]
        for key in ("rows", "batch", "rounds", "query_rounds", "samples", "warmup", "skip_load")
    }
    aggregate.update({metric: values["mean"] for metric, values in summary.items()})
    return aggregate


def local_baseline(
    official: dict[str, Any], before_aggregate: dict[str, float]
) -> dict[str, Any]:
    baseline = deepcopy(official)
    baseline["name"] = "fts_large_bench_local_before_mean"
    baseline["description"] = "Five-run same-machine before mean; local performance proxy only."
    baseline["source_run_count"] = 5
    for metric in baseline["baseline_metrics"]:
        if metric in before_aggregate:
            baseline["baseline_metrics"][metric] = before_aggregate[metric]
    return baseline


def main() -> int:
    args = parse_args()
    official = load_baseline(args.baseline)
    before_reports = load_group(args.before, official)
    after_reports = load_group(args.after, official)
    before_summary = summarize(before_reports)
    after_summary = summarize(after_reports)
    before_aggregate = aggregate_report(before_reports, before_summary)
    after_aggregate = aggregate_report(after_reports, after_summary)

    official_before = calculate_score(before_aggregate, official)
    official_after = calculate_score(after_aggregate, official)
    proxy = calculate_score(after_aggregate, local_baseline(official, before_aggregate))

    output = {
        "before_files": [str(path) for path in args.before],
        "after_files": [str(path) for path in args.after],
        "raw_runs": {
            "before": [
                {metric: report[metric] for metric in REPORT_METRICS + HIT_METRICS}
                for report in before_reports
            ],
            "after": [
                {metric: report[metric] for metric in REPORT_METRICS + HIT_METRICS}
                for report in after_reports
            ],
        },
        "metrics": {},
        "official_ci_baseline_score": {
            "before": official_before,
            "after": official_after,
        },
        "same_machine_performance_proxy": proxy,
    }
    for metric in REPORT_METRICS + HIT_METRICS:
        before = before_summary[metric]
        after = after_summary[metric]
        output["metrics"][metric] = {
            "before": before,
            "after": after,
            "improvement": (before["mean"] - after["mean"]) / before["mean"],
        }

    print("metric,before_mean,before_stdev,after_mean,after_stdev,improvement_percent")
    for metric, values in output["metrics"].items():
        print(
            f"{metric},{values['before']['mean']:.6f},{values['before']['stdev']:.6f},"
            f"{values['after']['mean']:.6f},{values['after']['stdev']:.6f},"
            f"{values['improvement'] * 100:.4f}"
        )
    print(f"official_ci_baseline_score_before,{official_before['score']:.4f}")
    print(f"official_ci_baseline_score_after,{official_after['score']:.4f}")
    print(f"same_machine_performance_proxy,{proxy['score']:.4f}")

    if args.json_out:
        args.json_out.write_text(json.dumps(output, indent=2, sort_keys=True) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
