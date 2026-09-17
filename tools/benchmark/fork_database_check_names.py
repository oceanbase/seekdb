#!/usr/bin/env python3
"""Check whether FORK DATABASE preserves explicitly named CHECK constraints.

Each size creates a unique source database with N named CHECK constraints,
times FORK DATABASE, and compares the source and target constraint names.
Only bench_check_names_* databases created by this script are removed.
"""

import argparse
import json
import subprocess
import sys
import time
import uuid
from pathlib import Path


def sql(client, port, statement):
    result = subprocess.run(
        [str(client), "-h127.0.0.1", f"-P{port}", "-uroot", "-Doceanbase",
         "-A", "-N", "-B", "-e", statement],
        capture_output=True, text=True, timeout=180, check=False)
    if result.returncode:
        raise RuntimeError(result.stderr.strip() or result.stdout.strip())
    return result.stdout.strip()


def constraint_names(client, port, database):
    output = sql(client, port,
                 "SELECT CONSTRAINT_NAME FROM information_schema.TABLE_CONSTRAINTS "
                 f"WHERE TABLE_SCHEMA='{database}' AND CONSTRAINT_TYPE='CHECK' "
                 "ORDER BY CONSTRAINT_NAME")
    return output.splitlines() if output else []


def wait_for_fork(client, port, target, deadline):
    # This benchmark runs on an isolated instance, so no unrelated fork tasks
    # should contribute to this count.
    while time.monotonic() < deadline:
        active = sql(client, port,
                     "SELECT COUNT(*) FROM oceanbase.__all_ddl_task_status "
                     "WHERE ddl_type=10009")
        if active == "0" and sql(client, port,
                                 f"SELECT COUNT(*) FROM {target}.t000") == "1":
            return
        time.sleep(0.1)
    raise TimeoutError(f"fork {target} did not become readable")


def measure(client, port, token, count):
    source = f"bench_check_names_{token}_{count}_src"
    target = f"bench_check_names_{token}_{count}_dst"
    try:
        sql(client, port, f"CREATE DATABASE {source}")
        for index in range(count):
            sql(client, port,
                f"CREATE TABLE {source}.t{index:03d} "
                f"(id INT PRIMARY KEY, value INT, "
                f"CONSTRAINT ck_{index:03d} CHECK (value > 0)); "
                f"INSERT INTO {source}.t{index:03d} VALUES (1, 1)")
        source_names = constraint_names(client, port, source)
        if source_names != [f"ck_{index:03d}" for index in range(count)]:
            raise AssertionError(f"source names differ: {source_names!r}")

        started = time.perf_counter_ns()
        sql(client, port, f"FORK DATABASE {source} TO {target}")
        elapsed_ms = (time.perf_counter_ns() - started) / 1e6
        wait_for_fork(client, port, target, time.monotonic() + 120)
        ready_ms = (time.perf_counter_ns() - started) / 1e6
        target_names = constraint_names(client, port, target)
        result = dict(checks=count, fork_ms=round(elapsed_ms, 3),
                      ready_ms=round(ready_ms, 3),
                      preserved_names=len(set(source_names) & set(target_names)),
                      target_names=target_names)
        return result
    finally:
        sql(client, port, f"DROP DATABASE IF EXISTS {target}")
        sql(client, port, f"DROP DATABASE IF EXISTS {source}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", type=Path,
                        default=Path("deps/3rd/u01/obclient/bin/obclient"))
    parser.add_argument("--port", type=int, default=2882)
    parser.add_argument("--table-counts", default="1,8,32")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    client = args.client.resolve()
    if not client.is_file():
        parser.error(f"client not found: {client}")
    try:
        counts = [int(value) for value in args.table_counts.split(",")]
    except ValueError as error:
        parser.error(str(error))
    if not counts or any(count < 1 or count > 128 for count in counts):
        parser.error("table counts must be between 1 and 128")

    sql(client, args.port, "SELECT 1")
    token = uuid.uuid4().hex[:8]
    results = []
    for count in counts:
        result = measure(client, args.port, token, count)
        results.append(result)
        print(json.dumps(result, sort_keys=True), flush=True)
    if args.output:
        args.output.write_text(json.dumps(results, indent=2) + "\n")
    return 0 if all(row["preserved_names"] == row["checks"] for row in results) else 1


if __name__ == "__main__":
    sys.exit(main())
