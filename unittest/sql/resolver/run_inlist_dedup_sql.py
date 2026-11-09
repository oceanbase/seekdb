#!/usr/bin/env python3
"""Exercise issue #1512 on an acknowledged, caller-owned localhost instance.

Never launches a server, changes global variables, reuses/drops a database, or
logs connection credentials. Requires personal-environment PyMySQL and NumPy.
The issue-shaped profile matches published workload dimensions, not an
unpublished byte-identical dataset; every generated dataset has an evidence hash.
"""

import argparse
import datetime
import hashlib
import json
import math
import os
from pathlib import Path
import random
import re
import statistics
import subprocess
import sys
import time
import uuid


SOURCES = (
    "src/sql/resolver/dml/ob_inlist_resolver.cpp",
    "src/sql/resolver/dml/ob_inlist_resolver.h",
    "src/sql/resolver/expr/ob_raw_expr_resolver_impl.cpp",
    "src/sql/BUILD.bazel",
    "unittest/sql/resolver/BUILD.bazel",
    "unittest/sql/resolver/test_inlist_dedup.cpp",
    "unittest/sql/resolver/run_inlist_dedup_test.py",
    "unittest/sql/resolver/run_inlist_dedup_sql.py",
    "unittest/sql/resolver/README.md",
)


def digest_file(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1048576), b""):
            result.update(chunk)
    return result.hexdigest()


def vector_text(values):
    return "[" + ",".join(format(float(value), ".9g") for value in values) + "]"


def inlist(values, column="category5", negative=False):
    return column + (" NOT IN (" if negative else " IN (") + ",".join(
        "NULL" if value is None else str(value) for value in values) + ")"


def select(predicate, table="inlist_items", vector=None, approximate=False, hint=""):
    result = "SELECT " + ("/*+ " + hint + " */ " if hint else "") + "id FROM " + table
    result += " WHERE " + predicate
    if vector is None:
        result += " ORDER BY id"
    else:
        result += " ORDER BY l2_distance(embedding,'" + vector + "')"
        result += " APPROXIMATE" if approximate else ""
        result += " LIMIT 10"
    return result


def options():
    root = Path(__file__).resolve().parents[3]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--owned-isolated-instance", action="store_true", required=True)
    parser.add_argument("--host", choices=("127.0.0.1", "::1"), default="127.0.0.1")
    parser.add_argument("--port", required=True, type=int)
    parser.add_argument("--user", default="root")
    parser.add_argument("--password-env", default="SEEKDB_TEST_PASSWORD")
    parser.add_argument("--mode", choices=("baseline", "fixed"), required=True)
    parser.add_argument("--continue-baseline-diagnostics", action="store_true",
                        help="baseline only: retain failed checks and nonzero exit while collecting later observations")
    parser.add_argument("--profile", choices=("diagnostic", "issue1512-shaped"), default="diagnostic")
    parser.add_argument("--source-sha", required=True)
    parser.add_argument("--server-binary", required=True, type=Path)
    parser.add_argument("--workspace-root", required=True, type=Path)
    parser.add_argument("--output-root", type=Path, default=root / ".inlist-dedup-validation/sql")
    parser.add_argument("--minimum-recall", type=float, default=0.0,
                        help="record recall always; optional explicit acceptance floor")
    parser.add_argument("--scalar-only", action="store_true", help="skip vector dataset and ANN workloads")
    args = parser.parse_args()
    if args.continue_baseline_diagnostics and args.mode != "baseline":
        parser.error("--continue-baseline-diagnostics requires --mode baseline")
    if not 1024 <= args.port <= 65535 or not 0 <= args.minimum_recall <= 1:
        parser.error("invalid unprivileged port or recall floor")
    if not re.fullmatch("[0-9a-fA-F]{40}", args.source_sha):
        parser.error("--source-sha must be a full commit SHA")
    for field in ("server_binary", "workspace_root", "output_root"):
        setattr(args, field, getattr(args, field).resolve())
    if not root.is_relative_to(args.workspace_root) or not args.server_binary.is_relative_to(args.workspace_root):
        parser.error("checkout and binary must reside within personal --workspace-root")
    if not args.output_root.is_relative_to(root) or args.output_root == root:
        parser.error("output must be a subdirectory of this checkout")
    if not args.server_binary.is_file():
        parser.error("server binary does not exist")
    return root, args


def main():
    root, args = options()
    import pymysql
    profile = ({"rows": 2000, "dimensions": 16, "queries": 2, "warmup": 1, "measured": 4, "exact": 1}
               if args.profile == "diagnostic" else
               {"rows": 200000, "dimensions": 128, "queries": 8, "warmup": 4, "measured": 12, "exact": 4})
    run_id = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "_" + uuid.uuid4().hex[:10]
    database = "inlist_dedup_" + run_id.lower()
    output = args.output_root / run_id
    output.mkdir(parents=True, exist_ok=False)
    path = output / "evidence.json"
    evidence = {"status": "running", "run_id": run_id, "database": database,
                "mode": args.mode, "profile": args.profile, "workload": profile,
                "continue_baseline_diagnostics": args.continue_baseline_diagnostics,
                "declared_server_source_sha": args.source_sha,
                "server_binary_sha256": digest_file(args.server_binary),
                "runner_head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
                "source_sha256": {name: digest_file(root / name) for name in SOURCES},
                "identity_note": "Caller-declared source SHA and binary hash are identities, not build provenance.",
                "data_note": "Published issue omits full RNG call order and tenant-vector generation code. This is an explicit reference-shaped workload, not byte-identical official data.",
                "plans": {}, "checks": {}, "timings": [], "recall": [], "statements": [],
                "queries": [], "exact_results": [], "ann_results": [],
                "plan_cache": {"status": "unverified", "snapshots": [], "texts": {},
                               "schema_source": "src/share/inner_table/ob_inner_table_schema_def.py:7183-7199",
                               "schema_source_sha256": digest_file(root / "src/share/inner_table/ob_inner_table_schema_def.py")}}
    connection = None

    def save():
        path.write_text(json.dumps(evidence, ensure_ascii=False, indent=2) + "\n")

    def check(name, passed, detail=None):
        evidence["checks"][name] = {"passed": bool(passed), "detail": detail}
        if not passed:
            evidence["status"] = "failed"
            evidence.setdefault("failure_type", "AssertionError")
            evidence.setdefault("first_failure", {"type": "AssertionError", "check": name, "detail": detail})
        save()
        if not passed and not args.continue_baseline_diagnostics:
            raise AssertionError(name)

    try:
        connection = pymysql.connect(host=args.host, port=args.port, user=args.user,
                                     password=os.environ.get(args.password_env, ""), autocommit=True,
                                     connect_timeout=10, read_timeout=300, charset="utf8mb4")
        cursor = connection.cursor()

        def execute(sql, parameters=None, record=True):
            if record and parameters is None:
                evidence["statements"].append(sql)
            cursor.execute(sql, parameters)
            return cursor.fetchall() if cursor.description else ()

        def plan(name, sql):
            text = "\n".join("\t".join(str(value) for value in row)
                             for row in execute("EXPLAIN EXTENDED " + sql))
            evidence["plans"][name] = {"sql": sql, "text": text}
            save()
            return text.upper()

        evidence["server_version"] = execute("SELECT VERSION()")
        execute("CREATE DATABASE " + database)
        execute("USE " + database)
        execute("SET SESSION ob_query_timeout=300000000")
        execute("CREATE TABLE scalar_items(id INT PRIMARY KEY, category5 BIGINT NULL, u BIGINT UNSIGNED NULL)")
        scalar = [(0, None, None), (1, -1, 0), (2, 0, 1), (3, 1, 2), (4, 2, 3), (5, 3, 4),
                  (6, -9223372036854775808, 18446744073709551615), (7, 9223372036854775807, 9223372036854775808)]
        cursor.executemany("INSERT INTO scalar_items VALUES(%s,%s,%s)", scalar)
        duplicate = [1, 2, 3] * 1024
        random.Random(1024).shuffle(duplicate)
        evidence["membership_sha256"] = hashlib.sha256(json.dumps(duplicate).encode()).hexdigest()
        cases = [([1, 2, 1], False), (duplicate, False), ([1, None, 1, None], False),
                 ([1, None, 1, None], True), ([1, 2, 1], True),
                 ([-1, -9223372036854775808, 9223372036854775807] * 2, False)]
        for index, (values, negative) in enumerate(cases):
            expected = tuple((row[0],) for row in scalar if row[1] is not None and
                             ((row[1] not in values and None not in values) if negative else row[1] in values))
            check("scalar_truth_" + str(index), execute(select(inlist(values, negative=negative), "scalar_items")) == expected)
        check("unsigned_boundary", execute(select(inlist([18446744073709551615] * 2, column="u"), "scalar_items")) == ((6,),))
        for name, original, reference in (
                ("mixed_signed_unsigned", "category5 IN (-1,18446744073709551615,-1)", "category5 IN (-1,18446744073709551615)"),
                ("strings", "category5 IN ('1','1','2')", "category5 IN ('1','2')"),
                ("decimal", "category5 IN (1.0,1.0,2.0)", "category5 IN (1.0,2.0)"),
                ("expression", "category5 IN (1+0,1+0,2)", "category5 IN (1+0,2)"),
                ("literal_precision", "category5 IN (1,0001,1)", "category5 IN (1,0001)")):
            check("fallback_" + name, execute(select(original, "scalar_items")) == execute(select(reference, "scalar_items")))
        threshold_hint = "OPT_PARAM('inlist_rewrite_threshold' 3)"
        below = plan("threshold_below", select(inlist([1, 2, 1, 2]), "scalar_items", hint=threshold_hint))
        at = plan("threshold_at", select(inlist([1, 2, 3, 1]), "scalar_items", hint=threshold_hint))
        if args.mode == "fixed":
            check("below_threshold_avoids_values", "VALUES TABLE ACCESS" not in below)
        check("at_threshold_keeps_values", "VALUES TABLE ACCESS" in at)
        check("threshold_contiguous_param_results", execute(select(inlist([1, 2, 3, 1]), "scalar_items", hint=threshold_hint)) == ((3,), (4,), (5,)))
        # Every last position below was removed from the initial all-equal AST.
        # Use a unique table name, so cache-view queries cannot observe another
        # workload or accidentally match themselves through their LIKE argument.
        cache_table = "cachet" + uuid.uuid4().hex
        execute("CREATE TABLE " + cache_table + " LIKE scalar_items")
        execute("INSERT INTO " + cache_table + " SELECT * FROM scalar_items")
        cache_view_sql = ("SELECT PLAN_ID,SQL_ID,HIT_COUNT,EXECUTIONS,STATEMENT,QUERY_SQL,PARAM_INFOS "
                          "FROM oceanbase.`V$OB_PLAN_CACHE_PLAN_STAT` WHERE STATEMENT LIKE %s "
                          "OR QUERY_SQL LIKE %s")
        evidence["plan_cache"]["query"] = cache_view_sql
        cache_view_available = True

        def cache_text(value):
            value = str(value)
            digest = hashlib.sha256(value.encode()).hexdigest()
            evidence["plan_cache"]["texts"][digest] = value
            return digest

        def cache_snapshot(prefix):
            nonlocal cache_view_available
            if not cache_view_available:
                return None
            try:
                rows = execute(cache_view_sql, (prefix + "%", prefix + "%"), record=False)
                return [{"plan_id": int(row[0]), "sql_id": str(row[1]),
                         "hit_count": int(row[2]), "executions": int(row[3]),
                         "statement_sha256": cache_text(row[4]), "query_sql_sha256": cache_text(row[5]),
                         "param_infos_sha256": cache_text(row[6])} for row in rows]
            except pymysql.MySQLError as error:
                cache_view_available = False
                evidence["plan_cache"]["unverified_reason"] = {
                    "type": type(error).__name__, "mysql_error_code": error.args[0] if error.args else None,
                    "detail": "Supported cache view unavailable or unauthorized; cache hits are not verified."}
                save()
                return None

        # Expected IDs are explicit SQL-semantic expectations. In particular,
        # uint64 max must add row 6 for u; ignoring that removed bind is visible.
        cache_steps = (
            ("equal_initial", "1", (3,), (2,)),
            ("unequal", "2", (3, 4), (2, 3)),
            ("equal_after_unequal", "1", (3,), (2,)),
            ("decimal_same", "1.0", (3,), (2,)),
            ("decimal_other", "2.0", (3, 4), (2, 3)),
            ("decimal_fraction", "2.5", (3,), (2,)),
            ("equal_after_decimal", "1", (3,), (2,)),
            ("string_same", "'1'", (3,), (2,)),
            ("string_other", "'2'", (3, 4), (2, 3)),
            ("string_fraction", "'2.5'", (3,), (2,)),
            ("equal_after_string", "1", (3,), (2,)),
            ("uint64_other", "18446744073709551615", (3,), (2, 6)),
            ("equal_after_uint64", "1", (3,), (2,)),
            ("null", "NULL", (3,), (2,)),
            ("equal_after_null", "1", (3,), (2,)),
            ("third_value", "3", (3, 5), (2, 4)),
        )
        cache_groups = {}
        for column, expected_offset in (("category5", 2), ("u", 3)):
            for count in (4, 3072):
                prefix = "SELECT id FROM " + cache_table + " WHERE " + column + " IN ("
                group_name = column + "/" + str(count)
                cache_groups[group_name] = []
                for step in cache_steps:
                    name, token = step[:2]
                    sql = prefix + "1," * (count - 1) + token + ") ORDER BY id"
                    expected = tuple((value,) for value in step[expected_offset])
                    for retry in range(2):
                        before = cache_snapshot(prefix)
                        check("auto_cache_%s_%d_%s_%d" % (column, count, name, retry), execute(sql) == expected)
                        after = cache_snapshot(prefix)
                        deltas = []
                        if before is not None and after is not None:
                            previous = {(row["plan_id"], row["sql_id"]): row for row in before}
                            for row in after:
                                key = (row["plan_id"], row["sql_id"])
                                if key in previous and row["hit_count"] > previous[key]["hit_count"]:
                                    deltas.append({"plan_id": key[0], "sql_id": key[1],
                                                   "hit_count_delta": row["hit_count"] - previous[key]["hit_count"]})
                        observation = {"group": group_name, "step": name, "last_token": token,
                                       "retry": retry, "before": before, "after": after, "hits": deltas}
                        evidence["plan_cache"]["snapshots"].append(observation)
                        cache_groups[group_name].append(observation)
                        save()
        evidence["plan_cache"]["groups"] = {}
        for group_name, observations in cache_groups.items():
            seeds = {(hit["plan_id"], hit["sql_id"]) for observation in observations
                     if observation["step"] == "equal_initial" and observation["retry"] == 1
                     for hit in observation["hits"]}
            returned = {observation["step"]: any((hit["plan_id"], hit["sql_id"]) in seeds
                        for hit in observation["hits"]) for observation in observations
                        if observation["step"].startswith("equal_after_") and observation["retry"] == 0}
            changed_seed_hits = [observation["step"] for observation in observations
                                 if observation["step"] in ("unequal", "decimal_same", "decimal_other",
                                                             "decimal_fraction", "string_same", "string_other",
                                                             "string_fraction", "uint64_other", "null", "third_value")
                                 and observation["retry"] == 0
                                 and any((hit["plan_id"], hit["sql_id"]) in seeds for hit in observation["hits"])]
            verified = bool(seeds) and bool(returned) and all(returned.values())
            evidence["plan_cache"]["groups"][group_name] = {
                "status": "observed" if verified else "unverified",
                "initial_equal_plan_ids": sorted(plan_id for plan_id, _ in seeds),
                "return_to_equal_reused_initial_plan": returned,
                "changed_values_hitting_initial_plan": changed_seed_hits}
            if args.mode == "fixed" and seeds:
                check("cache_removed_value_invalidates_" + group_name, not changed_seed_hits)
        if cache_view_available and all(group["status"] == "observed"
                                        for group in evidence["plan_cache"]["groups"].values()):
            evidence["plan_cache"]["status"] = "observed"
        elif "unverified_reason" not in evidence["plan_cache"]:
            evidence["plan_cache"]["unverified_reason"] = {
                "detail": "Cache view was readable, but initial/return-to-equal HIT_COUNT deltas were not all observed."}
        save()
        # NULL in NOT IN must change the result, unlike positive IN. This proves
        # the removed last position remains semantically visible on cache reuse.
        for count in (4, 3072):
            for step, token, expected_ids in (("equal", "1", (1, 2, 4, 5, 6, 7)),
                                               ("unequal", "2", (1, 2, 5, 6, 7)),
                                               ("equal_again", "1", (1, 2, 4, 5, 6, 7)),
                                               ("null", "NULL", ())):
                sql = ("SELECT id FROM " + cache_table + " WHERE category5 NOT IN (" +
                       "1," * (count - 1) + token + ") ORDER BY id")
                for retry in range(2):
                    check("auto_cache_not_in_%d_%s_%d" % (count, step, retry),
                          execute(sql) == tuple((value,) for value in expected_ids))
        prepared = select("category5 IN (?,?,?)", "scalar_items")
        execute("PREPARE inlist_guard FROM %s", (prepared,))
        evidence["prepared_sql"] = prepared
        try:
            for index, values in enumerate(((1, 1, 1), (1, 2, 1), (1, 1, 1), (None, 1, None), (None, None, None))):
                execute("SET @a=%s,@b=%s,@c=%s", values)
                actual = execute("EXECUTE inlist_guard USING @a,@b,@c")
                expected = tuple((row[0],) for row in scalar if row[1] is not None and row[1] in values)
                check("prepared_cache_" + str(index), actual == expected)
        finally:
            execute("DEALLOCATE PREPARE inlist_guard")
        # USE_PLAN_CACHE(NONE) disables plan-cache reuse for these compile-sensitive
        # samples. Compare across separately built baseline/fixed servers.
        # EXPLAIN timings are client end-to-end planning controls, not CPU time.
        planning_lists = {"small": [1, 2, 3], "duplicate3072": duplicate,
                          "mostly_distinct3072": list(range(3071)) + [0]}
        for name, values in planning_lists.items():
            sql = "EXPLAIN " + select(inlist(values), "scalar_items", hint="USE_PLAN_CACHE(NONE)")
            for index in range(8):
                started = time.perf_counter_ns()
                execute(sql, record=index == 0)
                evidence["timings"].append({"phase": "planning", "variant": name,
                                             "milliseconds": (time.perf_counter_ns() - started) / 1000000})
        if not args.scalar_only:
            import numpy as np
            dimensions = profile["dimensions"]
            generator = np.random.default_rng(20261002)
            centers = generator.normal(size=(100, dimensions))
            centers /= np.linalg.norm(centers, axis=1, keepdims=True)
            execute("SET SESSION ob_hnsw_ef_search=16")
            execute("CREATE TABLE inlist_items(id BIGINT PRIMARY KEY, embedding VECTOR(" + str(dimensions) + "), "
                    "tenant INT NOT NULL, level INT NOT NULL, category INT NOT NULL, category5 INT NOT NULL, "
                    "age INT NOT NULL, parity VARCHAR(8) NOT NULL, INDEX idx_category5(category5), INDEX idx_tenant(tenant)) ORGANIZATION HEAP")
            digest = hashlib.sha256()
            for start in range(0, profile["rows"], 100):
                count = min(100, profile["rows"] - start)
                tenants = generator.integers(0, 100, size=count)
                levels = generator.integers(0, 32, size=count)
                ages = generator.integers(1, 1001, size=count)
                noise = generator.normal(size=(count, dimensions))
                noise /= np.linalg.norm(noise, axis=1, keepdims=True)
                vectors = .85 * centers[tenants] + .15 * noise
                vectors /= np.linalg.norm(vectors, axis=1, keepdims=True)
                batch = [(start + i, vector_text(vectors[i]), int(tenants[i]), int(levels[i]), int(tenants[i]) + 1,
                          int(tenants[i]) % 5 + 1, int(ages[i]), "even" if int(tenants[i]) % 2 == 0 else "odd") for i in range(count)]
                digest.update((json.dumps(batch, separators=(",", ":")) + "\n").encode())
                cursor.executemany("INSERT INTO inlist_items VALUES(%s,%s,%s,%s,%s,%s,%s,%s)", batch)
            evidence["dataset_sha256"] = digest.hexdigest()
            execute("CREATE VECTOR INDEX idx_inlist_vec ON inlist_items(embedding) WITH "
                    "(distance=l2,type=hnsw,lib=vsag,m=16,ef_construction=128,sync_mode=immediate)")
            execute("ANALYZE TABLE inlist_items")
            evidence["indexes"] = execute("SHOW INDEX FROM inlist_items")
            compact = inlist([1, 2, 3])
            expanded = inlist(duplicate)
            eligible = {row[0] for row in execute(select(compact))}
            check("complete_eligibility_equality", eligible == {row[0] for row in execute(select(expanded))})
            evidence["eligible_count"] = len(eligible)
            evidence["eligible_ids"] = sorted(eligible)
            evidence["eligible_ids_sha256"] = hashlib.sha256(
                json.dumps(evidence["eligible_ids"], separators=(",", ":")).encode()).hexdigest()
            query_generator = np.random.default_rng(20261003)
            queries = query_generator.normal(size=(profile["queries"], dimensions))
            queries /= np.linalg.norm(queries, axis=1, keepdims=True)
            q0 = vector_text(queries[0])
            for name, predicate in (("compact_ann", compact), ("expanded_ann", expanded)):
                text = plan(name, select(predicate, vector=q0, approximate=True))
                check(name + "_vector_operator", "VECTOR INDEX" in text)
                if args.mode == "fixed":
                    check(name + "_no_values_subplan", "VALUES TABLE ACCESS" not in text and "SUBPLAN FILTER" not in text)
            # A deterministic geometry makes an equal -> unequal cache error
            # visible: all category2 vectors are nearer than category1 vectors.
            execute("CREATE TABLE guard_items(id INT PRIMARY KEY, category5 INT, embedding VECTOR(" + str(dimensions) + "))")
            guard_rows = []
            for category in (1, 2):
                for index in range(32):
                    values = [0.0] * dimensions
                    values[0] = 2 + index / 100 if category == 1 else (index + 1) / 1000
                    guard_rows.append((category * 100 + index, category, vector_text(values)))
            cursor.executemany("INSERT INTO guard_items VALUES(%s,%s,%s)", guard_rows)
            execute("CREATE VECTOR INDEX idx_guard_vec ON guard_items(embedding) WITH "
                    "(distance=l2,type=hnsw,lib=vsag,m=16,ef_construction=128,sync_mode=immediate)")
            execute("ANALYZE TABLE guard_items")
            zero = vector_text([0.0] * dimensions)
            for index, last in enumerate((1, 2, 1)):
                values = [1] * 3071 + [last]
                actual = execute(select(inlist(values), "guard_items", vector=zero, approximate=True))
                allowed = {row[0] for row in guard_rows if row[1] in values}
                reference = execute(select("category5 = 1" if last == 1 else "category5 = 1 OR category5 = 2",
                                           "guard_items", vector=zero))
                actual_ids, exact_ids = {row[0] for row in actual}, {row[0] for row in reference}
                check("ann_cache_guard_" + str(index), len(actual) == 10 and len(actual_ids) == 10 and
                      actual_ids <= allowed and (last == 1 or all(row[0] // 100 == 2 for row in actual)))
                evidence["recall"].append({"phase": "cache_guard", "step": index,
                                            "recall_at_10": len(actual_ids & exact_ids) / 10})
            for number, query in enumerate(queries):
                vector = vector_text(query)
                variants = {"compact": select(compact, vector=vector, approximate=True),
                            "expanded": select(expanded, vector=vector, approximate=True)}
                exact_sql = {name: select(predicate, vector=vector)
                             for name, predicate in (("compact", compact), ("expanded", expanded))}
                evidence["queries"].append({"query": number, "vector": vector,
                                             "vector_sha256": hashlib.sha256(vector.encode()).hexdigest(),
                                             "ann_sql": variants, "exact_sql": exact_sql,
                                             "sql_sha256": {name: hashlib.sha256(sql.encode()).hexdigest()
                                                            for name, sql in {**{"ann_" + key: value for key, value in variants.items()},
                                                                              **{"exact_" + key: value for key, value in exact_sql.items()}}.items()}})
                exact = {}
                for name, predicate in (("compact", compact), ("expanded", expanded)):
                    for iteration in range(profile["exact"]):
                        started = time.perf_counter_ns()
                        rows = execute(exact_sql[name], record=iteration == 0)
                        evidence["timings"].append({"phase": "exact", "variant": name, "query": number,
                                                     "milliseconds": (time.perf_counter_ns() - started) / 1000000})
                        exact[name] = rows
                        evidence["exact_results"].append({"query": number, "variant": name,
                                                           "iteration": iteration, "ordered_ids": [row[0] for row in rows]})
                check("exact_topk_equal_" + str(number), exact["compact"] == exact["expanded"])
                exact_ids = {row[0] for row in exact["compact"]}
                for phase, repeats in (("warmup", profile["warmup"]), ("measured", profile["measured"])):
                    for pair in range(repeats):
                        for name in (("compact", "expanded") if pair % 2 == 0 else ("expanded", "compact")):
                            started = time.perf_counter_ns()
                            actual = execute(variants[name], record=pair == 0)
                            elapsed = (time.perf_counter_ns() - started) / 1000000
                            ids = {row[0] for row in actual}
                            recall = len(ids & exact_ids) / len(exact_ids) if exact_ids else 1.0
                            check("ann_%s_%d_%d_%s" % (phase, number, pair, name),
                                  len(actual) == min(10, len(eligible)) and len(ids) == len(actual) and ids <= eligible and recall >= args.minimum_recall)
                            evidence["timings"].append({"phase": phase, "variant": name, "query": number, "milliseconds": elapsed})
                            evidence["recall"].append({"phase": phase, "variant": name, "query": number, "recall_at_10": recall})
                            evidence["ann_results"].append({"phase": phase, "pair": pair, "variant": name,
                                                          "query": number, "ordered_ids": [row[0] for row in actual]})
        evidence["summary_ms"] = {}
        for phase in ("planning", "exact", "measured"):
            names = sorted({item["variant"] for item in evidence["timings"] if item["phase"] == phase})
            for name in names:
                samples = [item["milliseconds"] for item in evidence["timings"] if item["phase"] == phase and item["variant"] == name]
                evidence["summary_ms"][phase + "/" + name] = {"count": len(samples), "median": statistics.median(samples),
                                                                  "min": min(samples), "max": max(samples)}
        failed_checks = [name for name, result in evidence["checks"].items() if not result["passed"]]
        if failed_checks:
            evidence["status"] = "failed"
            save()
            print("Baseline diagnostics retained", len(failed_checks), "failed checks; evidence:", path, file=sys.stderr)
            return 1
        evidence["status"] = ("passed" if evidence["plan_cache"]["status"] == "observed"
                              else "passed_with_unverified_cache_observation")
        save()
        print("Functional checks passed; cache observation:", evidence["plan_cache"]["status"],
              "; retained isolated database:", database, "; evidence:", path)
        return 0
    except Exception as error:
        evidence["status"] = "failed"
        evidence.setdefault("failure_type", type(error).__name__)
        evidence.setdefault("first_failure", {"type": type(error).__name__})
        evidence["terminal_failure_type"] = type(error).__name__
        save()
        print("Validation failed (" + type(error).__name__ + "); evidence:", path, file=sys.stderr)
        return 1
    finally:
        if connection is not None:
            connection.close()


if __name__ == "__main__":
    sys.exit(main())
