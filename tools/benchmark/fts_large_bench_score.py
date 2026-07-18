#!/usr/bin/env python3
"""Score one fts_large_bench.sh report against the checked-in CI baseline."""

import argparse
import json
import re
import sys
from pathlib import Path
from statistics import mean
from typing import Any, Dict, List


METRIC_RE = re.compile(r"^\s*([A-Za-z0-9_]+):\s*([-+]?(?:\d+(?:\.\d*)?|\.\d+))\s*$")
DEFAULT_BASELINE = Path(__file__).with_name("fts_large_bench_baseline.json")


def load_report(path: Path) -> Dict[str, float]:
    metrics: Dict[str, float] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        match = METRIC_RE.match(line)
        if match:
            metrics[match.group(1)] = float(match.group(2))
    return metrics


def load_baseline(path: Path) -> Dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def check_config(
    report: Dict[str, float],
    baseline: Dict[str, Any],
    allow_config_mismatch: bool,
) -> List[str]:
    mismatches = []
    for key, expected in baseline["benchmark_config"].items():
        actual = report.get(key)
        if actual is None or actual != float(expected):
            mismatches.append(f"{key}: expected {expected}, got {actual}")
    if mismatches and not allow_config_mismatch:
        raise SystemExit(
            "benchmark config mismatch; rerun fts_large_bench.sh with baseline settings:\n"
            + "\n".join(f"  - {item}" for item in mismatches)
        )
    return mismatches


def improvement(baseline_value: float, current_value: float) -> float:
    if baseline_value <= 0:
        raise ValueError(f"baseline value must be positive, got {baseline_value}")
    return (baseline_value - current_value) / baseline_value


def calculate_score(report: Dict[str, float], baseline: Dict[str, Any]) -> Dict[str, Any]:
    baseline_metrics = baseline["baseline_metrics"]
    categories = baseline["score_categories"]
    full_score_improvement = float(baseline["score_rule"]["full_score_improvement"])
    max_score = float(baseline["score_rule"]["max_score"])

    category_results: Dict[str, Any] = {}
    for category, metric_names in categories.items():
        metric_results = []
        for metric in metric_names:
            if metric not in report:
                raise SystemExit(f"missing metric in report: {metric}")
            base = float(baseline_metrics[metric])
            current = float(report[metric])
            metric_results.append(
                {
                    "metric": metric,
                    "baseline": base,
                    "current": current,
                    "improvement": improvement(base, current),
                }
            )
        category_results[category] = {
            "improvement": mean(item["improvement"] for item in metric_results),
            "metrics": metric_results,
        }

    mean_category_improvement = mean(
        item["improvement"] for item in category_results.values()
    )
    normalized = max(0.0, min(mean_category_improvement, full_score_improvement))
    score = normalized / full_score_improvement * max_score
    return {
        "score": score,
        "mean_improvement": mean_category_improvement,
        "full_score_improvement": full_score_improvement,
        "categories": category_results,
    }


def format_percent(value: float) -> str:
    return f"{value * 100:.2f}%"


def print_human(result: Dict[str, Any], config_mismatches: List[str]) -> None:
    print("FTS Large Benchmark Score")
    print("=========================")
    print(f"score: {result['score']:.2f} / 100")
    print(f"mean_improvement: {format_percent(result['mean_improvement'])}")
    print(f"full_score_improvement: {format_percent(result['full_score_improvement'])}")
    if config_mismatches:
        print("config_mismatches:")
        for mismatch in config_mismatches:
            print(f"  - {mismatch}")
    print()
    for category, category_result in result["categories"].items():
        print(f"{category}_improvement: {format_percent(category_result['improvement'])}")
        for metric in category_result["metrics"]:
            print(
                "  "
                f"{metric['metric']}: "
                f"baseline={metric['baseline']:.6g}, "
                f"current={metric['current']:.6g}, "
                f"improvement={format_percent(metric['improvement'])}"
            )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Score fts_large_bench.sh output against a fixed baseline."
    )
    parser.add_argument("report", type=Path, help="Path to fts_large_bench.sh report")
    parser.add_argument(
        "--baseline",
        type=Path,
        default=DEFAULT_BASELINE,
        help=f"Baseline JSON path (default: {DEFAULT_BASELINE})",
    )
    parser.add_argument(
        "--allow-config-mismatch",
        action="store_true",
        help="Do not fail if rows/rounds/query_rounds/etc differ from the baseline.",
    )
    parser.add_argument("--json", action="store_true", help="Print machine-readable JSON")
    args = parser.parse_args()

    report = load_report(args.report)
    baseline = load_baseline(args.baseline)
    config_mismatches = check_config(report, baseline, args.allow_config_mismatch)
    result = calculate_score(report, baseline)

    if args.json:
        result["config_mismatches"] = config_mismatches
        print(json.dumps(result, indent=2, sort_keys=True))
    else:
        print_human(result, config_mismatches)
    return 0


if __name__ == "__main__":
    sys.exit(main())
