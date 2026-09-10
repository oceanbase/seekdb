#!/usr/bin/env python3
# Copyright (c) 2026 OceanBase.
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy at http://www.apache.org/licenses/LICENSE-2.0
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Run P0 against a disposable seekdb schema; emit evidence, never record goldens.

Requires PyMySQL. Credentials only come from GRAPH_P0_* environment variables.
The C++ driver is a separate process using the same environment.
"""
import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import platform
import random
import statistics
import subprocess
import threading
import time
import uuid

import pymysql

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "tools/deploy/mysql_test/test_suite/graph/fixture.sql"
NAMES = ("out", "in", "filtered", "chain", "trail", "converge", "works")
SERVER_PID = None


class ProcessSample:
    """Whole-server measurements, including background work; not operator counters."""
    def __enter__(self):
        self.result = {}
        self.stop = threading.Event()
        self.peak = 0
        self.error = None
        if SERVER_PID:
            self.start = self.read()
            self.thread = threading.Thread(target=self.poll)
            self.thread.start()
        return self

    def read(self):
        root = Path("/proc") / str(SERVER_PID)
        stat = (root / "stat").read_text().rsplit(")", 1)[1].split()
        ticks = int(stat[11]) + int(stat[12])
        rss = int(stat[21]) * os.sysconf("SC_PAGE_SIZE")
        self.peak = max(self.peak, rss)
        return ticks, rss

    def poll(self):
        while not self.stop.wait(.01):
            try:
                self.read()
            except OSError as exc:
                self.error = str(exc)
                return

    def __exit__(self, *_):
        if SERVER_PID:
            self.stop.set()
            self.thread.join()
            end = self.read()
            self.result = {"server_cpu_ms": (end[0] - self.start[0]) * 1000 / os.sysconf("SC_CLK_TCK"),
                           "server_rss_start_bytes": self.start[1], "server_rss_peak_sampled_bytes": self.peak,
                           "sample_interval_ms": 10, "sampling_error": self.error}
ONE = """SELECT s.tenant,s.id,e.tenant,e.id,t.tenant,t.id
FROM gp_vertex s JOIN gp_edge e ON s.tenant=e.src_tenant AND s.id=e.src
JOIN gp_vertex t ON t.tenant=e.dst_tenant AND t.id=e.dst"""
ASSOCIATION = """SELECT b.binding_id,e.tenant,e.id,t.tenant,t.id
FROM gp_binding b JOIN gp_vertex s ON s.tenant=b.tenant AND s.id=b.id
JOIN gp_edge e ON e.src_tenant=s.tenant AND e.src=s.id
JOIN gp_vertex t ON t.tenant=e.dst_tenant AND t.id=e.dst"""


def connect(database=None):
    return pymysql.connect(host=os.getenv("GRAPH_P0_HOST", "127.0.0.1"),
                           port=int(os.getenv("GRAPH_P0_PORT", "2881")),
                           user=os.getenv("GRAPH_P0_USER", "root"),
                           password=os.getenv("GRAPH_P0_PASSWORD", ""),
                           database=database, autocommit=True,
                           connect_timeout=10, read_timeout=30)


def query(conn, sql, params=None):
    with conn.cursor() as cur:
        cur.execute(sql, params)
        return tuple(cur.fetchall())


def require_equal(actual, expected, context):
    if Counter(actual) != Counter(expected):
        missing = Counter(expected) - Counter(actual)
        extra = Counter(actual) - Counter(expected)
        raise AssertionError("{}: missing={}, extra={}".format(context, missing, extra))


def snapshot(conn):
    vertices = {(t, i) for t, i in query(conn, "SELECT tenant,id FROM gp_vertex")}
    companies = {(t, i) for t, i in query(conn, "SELECT tenant,id FROM gp_company")}
    edges = query(conn, "SELECT tenant,id,src_tenant,src,dst_tenant,dst,weight FROM gp_edge")
    works = query(conn, "SELECT tenant,id,src_tenant,src,dst_tenant,dst FROM gp_works")
    return vertices, companies, edges, works


def enumerate_pattern(data, name):
    """Independent bag oracle: enumerate identities, no generated SQL/IR reuse."""
    vertices, companies, edges, works = data
    relation = works if name == "works" else edges
    valid = [e for e in relation if (e[2], e[3]) in vertices
             and (e[4], e[5]) in (companies if name == "works" else vertices)]
    rows = []
    # Deliberately exhaustive for correctness graphs, not the performance data.
    for e in valid:
        start, end = e[2:4], e[4:6]
        if name == "in":
            start, end = end, start
        if name == "filtered" and e[6] != 1:
            continue
        row = start + e[:2] + end
        if name in ("chain", "trail", "converge"):
            for f in valid:
                if name == "trail" and e[:2] == f[:2]:
                    continue
                if name == "converge":
                    if e[4:6] == f[4:6]:
                        rows.append(row + f[:2] + f[2:4])
                elif e[4:6] == f[2:4]:
                    rows.append(row + f[:2] + f[4:6])
        else:
            rows.append(row)
    return rows


def expand_expected(conn, direction):
    vertices, _, edges, _ = snapshot(conn)
    bindings = query(conn, "SELECT binding_id,tenant,id FROM gp_binding")
    rows = []
    for binding, tenant, key in bindings:
        if (tenant, key) not in vertices:
            continue
        for e in edges:
            source, target = (e[2:4], e[4:6]) if direction == "out" else (e[4:6], e[2:4])
            if source == (tenant, key) and target in vertices:
                rows.append((binding,) + e[:2] + target)
    return rows


def expand(binary, db, direction, page, budget=1048576):
    child_env = dict(os.environ, GRAPH_P0_DATABASE=db)
    before = time.perf_counter()
    with ProcessSample() as resource:
        result = subprocess.run([str(binary), "expand", direction, str(page), str(budget)],
                                env=child_env, text=True, capture_output=True, timeout=65)
    elapsed = (time.perf_counter() - before) * 1000
    # Partial stdout from a failed process must never be accepted as a result.
    if result.returncode:
        raise RuntimeError(result.stderr.strip())
    rows = [tuple(int(v) for v in line.split("\t")) for line in result.stdout.splitlines()]
    return rows, dict(json.loads(result.stderr), wall_ms=elapsed, process=resource.result)


def insert_random(conn, seed):
    rng = random.Random(seed)
    query(conn, "DELETE FROM gp_edge")
    rows = [(1, i, 1, rng.randrange(1, 8), 1, rng.randrange(1, 8), rng.randrange(1, 3))
            for i in range(1, 25)]
    with conn.cursor() as cur:
        cur.executemany("INSERT INTO gp_edge VALUES (%s,%s,%s,%s,%s,%s,%s)", rows)


def functional(conn, db, binary, sqls):
    evidence = {"fixed_patterns": {}, "expansion": [], "random_seeds": []}
    for name in NAMES:
        actual = query(conn, sqls[name])
        expected = enumerate_pattern(snapshot(conn), name)
        require_equal(actual, expected, name)
        evidence["fixed_patterns"][name] = len(actual)
    for direction in ("out", "in"):
        expected = expand_expected(conn, direction)
        require_equal(query(conn, sqls["batch_" + direction]), expected, "single-statement batch " + direction)
        for page in (1, 2, 64):
            actual, metrics = expand(binary, db, direction, page)
            require_equal(actual, expected, "{} page {}".format(direction, page))
            evidence["expansion"].append(dict(metrics, direction=direction, page=page))
    # Fixed seed list makes failures reproducible without a third-party graph DB.
    for seed in range(10):
        insert_random(conn, seed)
        data = snapshot(conn)
        for name in NAMES:
            require_equal(query(conn, sqls[name]), enumerate_pattern(data, name), "seed {} {}".format(seed, name))
        for direction in ("out", "in"):
            require_equal(query(conn, sqls["batch_" + direction]), expand_expected(conn, direction),
                          "seed {} single-statement {}".format(seed, direction))
            actual, _ = expand(binary, db, direction, 2)
            require_equal(actual, expand_expected(conn, direction), "seed {} expand {}".format(seed, direction))
        evidence["random_seeds"].append(seed)
    query(conn, "DELETE FROM gp_binding")
    actual, _ = expand(binary, db, "out", 2)
    require_equal(actual, [], "empty bindings")
    return evidence


def transaction_tests(conn, db):
    # The first read is an explicit barrier before the second connection commits.
    other = connect(db)
    try:
        query(conn, "SET SESSION TRANSACTION ISOLATION LEVEL REPEATABLE READ")
        query(conn, "START TRANSACTION WITH CONSISTENT SNAPSHOT")
        initial = query(conn, ONE)
        query(other, "DELETE FROM gp_vertex WHERE tenant=1 AND id=3")
        require_equal(query(conn, ONE), initial, "snapshot across committed delete")
        query(conn, "ROLLBACK")
        if Counter(query(conn, ONE)) == Counter(initial):
            raise AssertionError("fresh read did not observe committed delete")
        query(other, "INSERT INTO gp_vertex VALUES (1,3,'C')")
        query(conn, "START TRANSACTION")
        query(conn, "DELETE FROM gp_vertex WHERE tenant=1 AND id=3")
        if any(row[-2:] == (1, 3) for row in query(conn, ONE)):
            raise AssertionError("own delete invisible")
        query(conn, "ROLLBACK")
        require_equal(query(conn, ONE), initial, "rollback")
        return {"repeatable_read": "pass", "own_write": "pass", "rollback": "pass"}
    finally:
        query(conn, "ROLLBACK")
        other.close()


def cancellation_test(db, sql):
    """Cancel a real graph-shaped statement at a server-observed running barrier."""
    worker, controller = connect(db), connect(db)
    query(worker, "SET SESSION ob_query_timeout=45000000")
    connection_id = query(worker, "SELECT CONNECTION_ID()")[0][0]
    marker = "graph_p0_cancel_" + uuid.uuid4().hex
    delayed = "/*" + marker + "*/ " + sql.replace("SELECT b.binding_id", "SELECT SLEEP(30),b.binding_id")
    errors = []
    def execute():
        try:
            query(worker, delayed)
        except pymysql.Error as exc:
            errors.append(exc.args[0])
    thread = threading.Thread(target=execute)
    thread.start()
    try:
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            rows = query(controller, "SHOW FULL PROCESSLIST")
            if any(str(row[0]) == str(connection_id) and any(marker in str(value) for value in row) for row in rows):
                break
            if not thread.is_alive():
                raise AssertionError("query ended before cancellation barrier: {}".format(errors))
            time.sleep(.01)
        else:
            raise AssertionError("query did not reach running barrier")
        start = time.monotonic()
        query(controller, "KILL QUERY " + str(int(connection_id)))
        thread.join(10)
        if thread.is_alive() or errors != [1317]:
            raise AssertionError("expected query interruption, got {}".format(errors))
        require_equal(query(worker, "SELECT 1"), [(1,)], "connection usable after cancellation")
        return {"error_code": errors[0], "cancel_response_ms": (time.monotonic() - start) * 1000}
    finally:
        if thread.is_alive():
            query(controller, "KILL QUERY " + str(int(connection_id)))
            thread.join(10)
        worker.close()
        controller.close()


def scale_data(conn, nodes, shape, seed):
    rng = random.Random(seed)
    for table in ("gp_binding", "gp_edge", "gp_vertex"):
        query(conn, "DELETE FROM " + table)
    with conn.cursor() as cur:
        cur.executemany("INSERT INTO gp_vertex VALUES (1,%s,'v')", [(i,) for i in range(1, nodes + 1)])
        for base in range(0, nodes * 10, 1000):
            rows = []
            for i in range(base, min(base + 1000, nodes * 10)):
                src = 1 if shape == "hub" and rng.random() < .5 else rng.randrange(1, nodes + 1)
                dst = 1 if shape == "converge" else rng.randrange(1, nodes + 1)
                rows.append((1, i + 1, 1, src, 1, dst, 1))
            cur.executemany("INSERT INTO gp_edge VALUES (%s,%s,%s,%s,%s,%s,%s)", rows)
        cur.executemany("INSERT INTO gp_binding VALUES (%s,1,%s)", [(i, i % 16 + 1) for i in range(64)])
    query(conn, "ANALYZE TABLE gp_vertex,gp_edge,gp_binding")


def benchmark(conn, db, binary, sqls, nodes, runs):
    results = []
    for shape in ("uniform", "hub", "converge"):
        scale_data(conn, nodes, shape, 20260909)
        expected = query(conn, ASSOCIATION)
        require_equal(query(conn, sqls["out"]), query(conn, ONE), "scale one-hop lowering")
        require_equal(query(conn, sqls["batch_out"]), expected, "scale single-statement batch")
        modes = {"ordinary_sql": ONE, "lowered_sql": sqls["out"], "binding_join": ASSOCIATION,
                 "single_statement_batch": sqls["batch_out"]}
        for mode, sql in modes.items():
            query(conn, sql)  # one explicitly warm run
            times, resources = [], []
            for _ in range(runs):
                start = time.perf_counter()
                with ProcessSample() as resource:
                    rows = query(conn, sql)
                times.append((time.perf_counter() - start) * 1000)
                resources.append(resource.result)
            results.append({"shape": shape, "mode": mode, "nodes": nodes, "edges": nodes * 10,
                            "runs": runs, "median_ms": statistics.median(times), "max_ms": max(times),
                            "output_rows": len(rows), "process_samples": resources,
                            "explain": query(conn, "EXPLAIN " + sql)})
        for page in (64, 256):
            times, samples = [], []
            for _ in range(runs):
                rows, metrics = expand(binary, db, "out", page)
                require_equal(rows, expected, "scale association {}".format(shape))
                times.append(metrics["wall_ms"])
                samples.append(metrics)
            results.append({"shape": shape, "mode": "sql_access_controller", "page": page,
                            "nodes": nodes, "edges": nodes * 10, "runs": runs,
                            "median_ms": statistics.median(times), "max_ms": max(times), "samples": samples})
    return results


def load_fixture(conn):
    for table in ("gp_binding", "gp_works", "gp_company", "gp_edge", "gp_vertex"):
        query(conn, "DROP TABLE IF EXISTS " + table)
    for statement in FIXTURE.read_text().split(";"):
        if statement.strip():
            query(conn, statement)


def main():
    global SERVER_PID
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--nodes", type=int, default=1000)
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--skip-benchmark", action="store_true")
    parser.add_argument("--server-pid", type=int, help="optional dedicated server PID for CPU/RSS sampling")
    parser.add_argument("--server-binary", type=Path, help="record this server binary's version and SHA256")
    args = parser.parse_args()
    SERVER_PID = args.server_pid
    if not 16 <= args.nodes <= 100000 or not 1 <= args.runs <= 100:
        parser.error("nodes must be 16..100000; runs must be 1..100")
    db = "graph_p0_" + uuid.uuid4().hex[:12]
    binary = args.binary.resolve()
    sqls = {name: subprocess.check_output([str(binary), "sql", name], text=True).strip().rstrip(";")
            for name in NAMES + ("batch_out", "batch_in")}
    report = {"database": db, "platform": platform.platform(), "sql": sqls,
              "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
              "server_revision_note": "SELECT VERSION does not establish a source revision; record server --version separately",
              "limitations": ["SQL-backed access, not an in-process DAS adapter",
                              "RR transaction snapshot, not internal statement snapshot propagation",
                              "edge_rows_returned is not storage rows examined",
                              "controller_budget_charge excludes server and client-library allocations",
                              "warm local latency only; CPU/RSS samples include background server activity",
                              "RSS samples can miss short peaks and are not query memory allocation"]}
    if args.server_binary:
        report["server_binary_sha256"] = hashlib.sha256(args.server_binary.read_bytes()).hexdigest()
        report["server_binary_version"] = subprocess.check_output(
            [str(args.server_binary), "--version"], text=True, stderr=subprocess.STDOUT)
    admin = connect()
    conn = None
    created = False
    try:
        query(admin, "CREATE DATABASE " + db)
        created = True
        conn = connect(db)
        report["server_version"] = query(conn, "SELECT VERSION()")
        report["memory_budget"] = query(conn, "SHOW PARAMETERS LIKE 'memory_budget'")
        load_fixture(conn)
        report["transactions"] = transaction_tests(conn, db)
        report["cancellation"] = cancellation_test(db, sqls["batch_out"])
        report["functional"] = functional(conn, db, binary, sqls)
        if not args.skip_benchmark:
            report["benchmark"] = benchmark(conn, db, binary, sqls, args.nodes, args.runs)
        report["status"] = "passed"
    except Exception as exc:
        report["status"] = "failed"
        report["error"] = str(exc)
        raise
    finally:
        if conn:
            conn.close()
        try:
            if created:
                query(admin, "DROP DATABASE " + db)
        finally:
            admin.close()
            args.report.parent.mkdir(parents=True, exist_ok=True)
            args.report.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n")
    print("PASS: graph P0 integration; report=" + str(args.report))


if __name__ == "__main__":
    main()
