#!/usr/bin/env python3
# Copyright (c) 2026 OceanBase.
# Licensed under the Apache License, Version 2.0.

"""Deterministic SQL, KV-cache, and vector workloads for allocator A/B tests."""

import argparse
import concurrent.futures
import hashlib
import json
import math
import random
import statistics
import sys
import threading
import time

import pymysql


DATABASE = "allocator_removal_ab"


def connect(args, database=None):
    return pymysql.connect(
        host=args.host,
        port=args.port,
        user=args.user,
        password=args.password,
        database=database,
        charset="utf8mb4",
        autocommit=True,
        connect_timeout=10,
        read_timeout=60,
        write_timeout=60,
    )


def batches(total, size):
    for begin in range(1, total + 1, size):
        yield begin, min(total + 1, begin + size)


def deterministic_payload(row_id, size):
    seed = hashlib.sha256(str(row_id).encode("ascii")).digest()
    repeats = (size + len(seed) - 1) // len(seed)
    return (seed * repeats)[:size]


def vector_text(row_id, dimensions):
    rng = random.Random(0x5EED0000 + row_id)
    return "[" + ",".join(f"{rng.uniform(-1.0, 1.0):.6f}" for _ in range(dimensions)) + "]"


def prepare(args):
    selected = set(args.workloads.split(","))
    unknown = selected.difference({"sql", "kv", "vector"})
    if unknown:
        raise ValueError(f"unknown workloads: {sorted(unknown)}")

    with connect(args) as connection:
        with connection.cursor() as cursor:
            cursor.execute(f"DROP DATABASE IF EXISTS {DATABASE}")
            cursor.execute(f"CREATE DATABASE {DATABASE}")

    with connect(args, DATABASE) as connection:
        with connection.cursor() as cursor:
            if "sql" in selected:
                cursor.execute(
                    "CREATE TABLE sql_workload ("
                    "id BIGINT PRIMARY KEY, value BIGINT NOT NULL, payload VARCHAR(256) NOT NULL)"
                )
                statement = "INSERT INTO sql_workload(id, value, payload) VALUES(%s, 0, %s)"
                for begin, end in batches(args.sql_rows, args.batch_size):
                    rows = [(row_id, f"row-{row_id:012d}" * 8) for row_id in range(begin, end)]
                    cursor.executemany(statement, rows)

            if "kv" in selected:
                cursor.execute(
                    "CREATE TABLE kv_workload ("
                    "id BIGINT PRIMARY KEY, payload VARBINARY(1024) NOT NULL)"
                )
                statement = "INSERT INTO kv_workload(id, payload) VALUES(%s, %s)"
                for begin, end in batches(args.kv_rows, args.batch_size):
                    rows = [
                        (row_id, deterministic_payload(row_id, args.kv_payload_bytes))
                        for row_id in range(begin, end)
                    ]
                    cursor.executemany(statement, rows)

            if "vector" in selected:
                cursor.execute(
                    "CREATE TABLE vector_workload ("
                    f"id BIGINT PRIMARY KEY, embedding VECTOR({args.vector_dimensions}))"
                )
                statement = "INSERT INTO vector_workload(id, embedding) VALUES(%s, %s)"
                vector_batch = min(args.batch_size, 200)
                for begin, end in batches(args.vector_rows, vector_batch):
                    rows = [
                        (row_id, vector_text(row_id, args.vector_dimensions))
                        for row_id in range(begin, end)
                    ]
                    cursor.executemany(statement, rows)
                if not args.skip_vector_index:
                    cursor.execute(
                        "CREATE VECTOR INDEX idx_allocator_ab_vector "
                        "ON vector_workload(embedding) WITH (distance=l2, type=hnsw)"
                    )

            counts = {}
            for workload, table in (
                ("sql", "sql_workload"),
                ("kv", "kv_workload"),
                ("vector", "vector_workload"),
            ):
                if workload in selected:
                    cursor.execute(f"SELECT COUNT(*), COALESCE(SUM(id), 0) FROM {table}")
                    count, id_sum = cursor.fetchone()
                    counts[workload] = {"rows": int(count), "id_sum": int(id_sum)}

    print(json.dumps({"database": DATABASE, "prepared": counts}, sort_keys=True))


class Reservoir:
    def __init__(self, capacity, seed):
        self.capacity = capacity
        self.values = []
        self.seen = 0
        self.random = random.Random(seed)

    def add(self, value):
        self.seen += 1
        if len(self.values) < self.capacity:
            self.values.append(value)
        else:
            position = self.random.randrange(self.seen)
            if position < self.capacity:
                self.values[position] = value


def percentile(values, quantile):
    if not values:
        return 0.0
    ordered = sorted(values)
    position = (len(ordered) - 1) * quantile
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return ordered[lower]
    return ordered[lower] + (ordered[upper] - ordered[lower]) * (position - lower)


def execute_operation(cursor, workload, rng, args, queries):
    if workload == "sql":
        row_id = rng.randint(1, args.sql_rows)
        if rng.random() < 0.80:
            cursor.execute("SELECT value, payload FROM sql_workload WHERE id=%s", (row_id,))
            row = cursor.fetchone()
            if row is None:
                raise RuntimeError(f"missing sql row {row_id}")
        else:
            cursor.execute("UPDATE sql_workload SET value=value+1 WHERE id=%s", (row_id,))
    elif workload == "kv":
        row_id = rng.randint(1, args.kv_rows)
        cursor.execute("SELECT payload FROM kv_workload WHERE id=%s", (row_id,))
        row = cursor.fetchone()
        if row is None:
            raise RuntimeError(f"missing kv row {row_id}")
    elif workload == "vector":
        query = queries[rng.randrange(len(queries))]
        cursor.execute(
            "SELECT id FROM vector_workload "
            "ORDER BY l2_distance(embedding, %s) APPROXIMATE LIMIT 10",
            (query,),
        )
        rows = cursor.fetchall()
        if not rows:
            raise RuntimeError("vector query returned no rows")
    else:
        raise ValueError(f"unknown workload {workload}")


def run_worker(worker_id, ready_barrier, start_barrier, timing, args, queries):
    rng = random.Random(args.seed + worker_id * 104729)
    reservoir = Reservoir(args.latency_samples, args.seed ^ worker_id)
    operations = 0
    errors = []
    connection = connect(args, DATABASE)
    try:
        with connection.cursor() as cursor:
            ready_barrier.wait(timeout=60)
            start_barrier.wait(timeout=10)
            sample_at = timing["sample_at"]
            stop_at = timing["stop_at"]
            while True:
                now = time.monotonic()
                if now >= stop_at:
                    break
                started = time.perf_counter_ns()
                try:
                    execute_operation(cursor, args.workload, rng, args, queries)
                except Exception as error:  # Propagate a concise error after all workers join.
                    errors.append(f"{type(error).__name__}: {error}")
                    break
                finished = time.perf_counter_ns()
                if time.monotonic() >= sample_at:
                    operations += 1
                    reservoir.add((finished - started) / 1000.0)
    finally:
        connection.close()
    return {"operations": operations, "latencies_us": reservoir.values, "errors": errors}


def run(args):
    if args.threads <= 0 or args.duration <= 0 or args.warmup < 0:
        raise ValueError("threads and duration must be positive; warmup must be non-negative")
    queries = [vector_text(args.seed + index, args.vector_dimensions) for index in range(64)]
    ready_barrier = threading.Barrier(args.threads + 1)
    start_barrier = threading.Barrier(args.threads + 1)
    timing = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.threads) as executor:
        futures = [
            executor.submit(
                run_worker,
                worker,
                ready_barrier,
                start_barrier,
                timing,
                args,
                queries,
            )
            for worker in range(args.threads)
        ]
        ready_barrier.wait(timeout=60)
        started_at = time.monotonic()
        timing["sample_at"] = started_at + args.warmup
        timing["stop_at"] = timing["sample_at"] + args.duration
        start_barrier.wait(timeout=10)
        results = [future.result() for future in futures]
    completed_at = time.monotonic()

    errors = [error for result in results for error in result["errors"]]
    operations = sum(result["operations"] for result in results)
    latencies = [latency for result in results for latency in result["latencies_us"]]
    measured_seconds = args.duration
    output = {
        "workload": args.workload,
        "threads": args.threads,
        "seed": args.seed,
        "warmup_seconds": args.warmup,
        "requested_duration_seconds": args.duration,
        "measured_seconds": measured_seconds,
        "wall_seconds_including_warmup": completed_at - started_at,
        "operations": operations,
        "operations_per_second": operations / measured_seconds,
        "latency_sample_count": len(latencies),
        "latency_us": {
            "mean": statistics.fmean(latencies) if latencies else 0.0,
            "p50": percentile(latencies, 0.50),
            "p95": percentile(latencies, 0.95),
            "p99": percentile(latencies, 0.99),
            "max": max(latencies, default=0.0),
        },
        "errors": errors,
    }
    print(json.dumps(output, sort_keys=True))
    if errors:
        return 1
    return 0


def add_connection_arguments(parser):
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--user", default="root")
    parser.add_argument("--password", default="")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    prepare_parser = subparsers.add_parser("prepare")
    add_connection_arguments(prepare_parser)
    prepare_parser.add_argument("--workloads", default="sql,kv,vector")
    prepare_parser.add_argument("--sql-rows", type=int, default=100000)
    prepare_parser.add_argument("--kv-rows", type=int, default=786432)
    prepare_parser.add_argument("--kv-payload-bytes", type=int, default=512)
    prepare_parser.add_argument("--vector-rows", type=int, default=100000)
    prepare_parser.add_argument("--vector-dimensions", type=int, default=128)
    prepare_parser.add_argument("--batch-size", type=int, default=1000)
    prepare_parser.add_argument("--skip-vector-index", action="store_true")
    prepare_parser.set_defaults(function=prepare)

    run_parser = subparsers.add_parser("run")
    add_connection_arguments(run_parser)
    run_parser.add_argument("--workload", choices=("sql", "kv", "vector"), required=True)
    run_parser.add_argument("--threads", type=int, default=8)
    run_parser.add_argument("--warmup", type=float, default=300.0)
    run_parser.add_argument("--duration", type=float, default=600.0)
    run_parser.add_argument("--seed", type=int, default=20260917)
    run_parser.add_argument("--latency-samples", type=int, default=200000)
    run_parser.add_argument("--sql-rows", type=int, default=100000)
    run_parser.add_argument("--kv-rows", type=int, default=786432)
    run_parser.add_argument("--vector-dimensions", type=int, default=128)
    run_parser.set_defaults(function=run)

    arguments = parser.parse_args()
    try:
        result = arguments.function(arguments)
    except Exception as error:
        print(f"{type(error).__name__}: {error}", file=sys.stderr)
        return 1
    return result if isinstance(result, int) else 0


if __name__ == "__main__":
    sys.exit(main())
