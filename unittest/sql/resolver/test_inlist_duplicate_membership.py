#!/usr/bin/env python3
"""SQL regression for #1512. Requires a running dedicated seekdb instance.

Run with --baseline to record the old plan without requiring the optimized plan.
The default mode also rejects VALUES/subquery materialization for duplicate-only
growth of a scalar integer IN list. All result assertions run in both modes.
"""
import argparse
import json
import os
import random
import time

import pymysql


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline", action="store_true")
    args = parser.parse_args()
    connection = pymysql.connect(
        host=os.getenv("MYSQL_HOST", "127.0.0.1"),
        port=int(os.getenv("MYSQL_PORT", "2881")),
        user=os.getenv("MYSQL_USER", "root"),
        password=os.getenv("MYSQL_PASSWORD", ""),
        autocommit=True,
    )
    with connection.cursor() as cursor:
        cursor.execute("CREATE DATABASE IF NOT EXISTS inlist_1512_regression")
        cursor.execute("USE inlist_1512_regression")
        cursor.execute("DROP TABLE IF EXISTS memberships")
        cursor.execute("CREATE TABLE memberships(id INT PRIMARY KEY, category5 INT)")
        cursor.executemany("INSERT INTO memberships VALUES (%s,%s)",
                           [(i, i % 5 + 1) for i in range(1000)] + [(1000, None)])
        try:
            values = [1, 2, 3] * 1024
            random.Random(1024).shuffle(values)
            expanded = ",".join(map(str, values))

            def rows(predicate):
                cursor.execute("SELECT id FROM memberships WHERE " + predicate + " ORDER BY id")
                return cursor.fetchall()

            for operator in ("IN", "NOT IN"):
                assert rows(f"category5 {operator} (1,2,3)") == rows(f"category5 {operator} ({expanded})")
            # NULL and mixed-type lists must preserve SQL three-valued logic.
            for suffix in (",NULL", ",'3'", ",3.0"):
                for operator in ("IN", "NOT IN"):
                    assert rows(f"category5 {operator} (1,2,3{suffix})") == rows(
                        f"category5 {operator} ({expanded}{suffix})")

            cursor.execute("EXPLAIN SELECT id FROM memberships WHERE category5 IN (" + expanded + ")")
            plan = "\n".join(str(cell) for row in cursor.fetchall() for cell in row)
            materialized = "VALUES TABLE" in plan.upper() or "SUBPLAN FILTER" in plan.upper()
            # Explicit prepared statements must continue honoring every binding.
            placeholders = ",".join("?" for _ in range(24))
            cursor.execute("PREPARE membership_stmt FROM %s", (
                f"SELECT COUNT(*) FROM memberships WHERE category5 IN ({placeholders})",))
            variables = ",".join(f"@member{i}" for i in range(24))
            for distinct, expected in (([1], 200), ([1,2,3,4,5], 1000), ([None], 0)):
                for i in range(24):
                    cursor.execute(f"SET @member{i}=%s", (distinct[i % len(distinct)],))
                cursor.execute("EXECUTE membership_stmt USING " + variables)
                assert cursor.fetchone()[0] == expected
            cursor.execute("DEALLOCATE PREPARE membership_stmt")
            timings = {}
            for label, members in (("compact", "1,2,3"), ("duplicated", expanded)):
                started = time.perf_counter()
                for _ in range(30):
                    cursor.execute("SELECT COUNT(*) FROM memberships WHERE category5 IN (" + members + ")")
                    assert cursor.fetchone()[0] == 600
                timings[label + "_avg_ms"] = (time.perf_counter() - started) * 1000 / 30
            report = {"baseline_mode": args.baseline, "materialized_values": materialized,
                      "correctness": "passed", "timings": timings, "plan": plan}
            print(json.dumps(report, ensure_ascii=False, indent=2))
            if not args.baseline:
                assert not materialized, "duplicate integer membership was unnecessarily materialized"
        finally:
            cursor.execute("DROP TABLE memberships")
    connection.close()


if __name__ == "__main__":
    main()
