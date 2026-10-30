#!/usr/bin/env python3
"""Integration regression for seekdb #1535; requires a running seekdb server.

Uses the mysql CLI and the Python standard library. Credentials may be supplied
through a mysql defaults file; no password is accepted on the command line.
This checks predicate elimination and NULL semantics, not latency claims.
"""

import argparse
import json
from pathlib import Path
import subprocess
import uuid


def balanced(expressions):
    if len(expressions) == 1:
        return expressions[0]
    mid = len(expressions) // 2
    return "(" + balanced(expressions[:mid]) + ") AND (" + balanced(expressions[mid:]) + ")"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mysql", default="mysql")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=2881)
    parser.add_argument("--user", default="root")
    parser.add_argument("--defaults-extra-file", type=Path)
    parser.add_argument("--output", type=Path, default=Path("ann_absorption_results"))
    parser.add_argument("--observe-baseline", action="store_true",
                        help="Record an unpatched plan without requiring predicate elimination")
    args = parser.parse_args()

    command = [args.mysql]
    if args.defaults_extra_file:
        command.append("--defaults-extra-file=" + str(args.defaults_extra_file.resolve()))
    command += ["--protocol=tcp", "--host=" + args.host, "--port=" + str(args.port),
                "--user=" + args.user, "--batch", "--raw", "--skip-column-names",
                "--default-character-set=utf8mb4"]

    def sql(statement):
        result = subprocess.run(command, input=statement, text=True, encoding="utf-8",
                                capture_output=True, timeout=120, check=False)
        if result.returncode:
            raise RuntimeError(result.stderr.strip())
        return result.stdout

    suffix = uuid.uuid4().hex[:12]
    database = "issue1535_" + suffix
    output = args.output / suffix
    output.mkdir(parents=True, exist_ok=False)
    summary = {"database": database, "observe_baseline": args.observe_baseline,
               "output": str(output.resolve()), "checks": {}}
    created = False
    error = None
    try:
        summary["version"] = sql("SELECT VERSION();").strip()
        sql("CREATE DATABASE " + database + ";")
        created = True
        table = database + ".ann_absorption"
        sql("CREATE TABLE " + table + " (id BIGINT PRIMARY KEY, category INT, "
            "parity VARCHAR(8), embedding VECTOR(3), KEY idx_category(category), "
            "VECTOR INDEX idx_vec(embedding) WITH (distance=l2,lib=vsag,type=hnsw,"
            "m=16,ef_construction=128,sync_mode=immediate)) ORGANIZATION HEAP;")
        rows = []
        for i in range(32):
            category = "NULL" if i % 11 == 0 else str(i % 3)
            parity = "NULL" if i % 7 == 0 else ("'even'" if i % 2 == 0 else "'odd'")
            rows.append("(%d,%s,%s,'[%d,0,0]')" % (i, category, parity, i + 1))
        sql("INSERT INTO " + table + " VALUES " + ",".join(rows) + ";")
        sql("ANALYZE TABLE " + table + ";")

        p = "category = 1"
        q = "parity = 'even'"
        predicates = {
            "original": p,
            "and_absorption": p + " AND (" + p + " OR " + q + ")",
            "expanded1024": balanced([p] + ["(" + p + " OR " + q + ")"] * 1024),
            "or_absorption": p + " OR (" + p + " AND " + q + ")",
        }
        expected = [str(i) for i in range(32) if i % 11 != 0 and i % 3 == 1]
        for name, predicate in predicates.items():
            eligible = sql("SELECT id FROM " + table + " WHERE " + predicate + " ORDER BY id;").splitlines()
            if eligible != expected:
                raise AssertionError(name + ": eligible rows differ (including NULL cases)")
            # There are at most 10 eligible rows and all distances are distinct.
            # Every eligible row must therefore be returned, in increasing id order.
            query = ("SELECT id FROM " + table + " WHERE " + predicate +
                     " ORDER BY l2_distance(embedding,'[0,0,0]') APPROXIMATE LIMIT 10")
            prefix = "SET SESSION ob_hnsw_ef_search=64;\n"
            actual = sql(prefix + query + ";").splitlines()
            if actual != expected:
                raise AssertionError(name + ": ANN output differs from exact eligible rows")
            plan = sql(prefix + "EXPLAIN " + query + ";")
            (output / (name + ".plan.txt")).write_text(plan, encoding="utf-8")
            if "VECTOR" not in plan.upper():
                raise AssertionError(name + ": plan does not show a vector scan")
            marker = "Outputs & filters:"
            if marker not in plan:
                raise AssertionError(name + ": unrecognized EXPLAIN format; inspect saved plan")
            parity_mentions = plan.split(marker, 1)[1].lower().count("parity")
            summary["checks"][name] = {"eligible_ids": eligible, "ann_ids": actual,
                                       "parity_mentions_in_plan": parity_mentions}
            if parity_mentions and not args.observe_baseline:
                raise AssertionError(name + ": redundant parity predicate remains in plan")
        summary["status"] = "observed" if args.observe_baseline else "passed"
    except Exception as exc:
        error = exc
        summary["status"] = "failed"
        summary["error"] = str(exc)
    finally:
        if created:
            try:
                # Only remove the unique database created by this invocation.
                sql("DROP DATABASE " + database + ";")
                summary["database_removed"] = True
            except Exception as exc:
                summary["database_removed"] = False
                summary["cleanup_error"] = str(exc)
                summary["status"] = "failed"
                error = error or exc
        (output / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(json.dumps(summary, indent=2))
    return 1 if error else 0


if __name__ == "__main__":
    raise SystemExit(main())
