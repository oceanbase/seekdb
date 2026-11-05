# Copyright (c) 2026 OceanBase.
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

"""Small structural reproduction of oceanbase/seekdb#1512, not its benchmark.

Creates a uniquely named database and leaves it intact for inspection.
Run from the AMD Notebook kernel or on a host that can reach seekdb.
"""
import argparse
import json
import math
from pathlib import Path
import random
import time
import uuid

import pymysql


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=2881)
    parser.add_argument("--user", default="root")
    parser.add_argument("--rows", type=int, default=2000)
    parser.add_argument("--expect-compact", action="store_true",
                        help="Require the repeated list to keep a direct IN plan on a patched server")
    args = parser.parse_args(argv)
    if not 100 <= args.rows <= 20000:
        parser.error("--rows must be between 100 and 20000")

    database = "ob_practice_1512_" + uuid.uuid4().hex[:10]
    output = Path(database + "_report.json")
    rng = random.Random(20261028)
    dimension = 16
    def vector():
        values = [rng.gauss(0, 1) for _ in range(dimension)]
        norm = math.sqrt(sum(x * x for x in values))
        return json.dumps([x / norm for x in values], separators=(",", ":"))

    compact_values = [1, 2, 3]
    repeated_values = compact_values * 1024
    random.Random(1024).shuffle(repeated_values)
    filters = {
        "compact": "category5 IN (1,2,3)",
        "repeated": "category5 IN (" + ",".join(map(str, repeated_values)) + ")",
    }
    report = {
        "issue": "https://github.com/oceanbase/seekdb/issues/1512",
        "purpose": "Small structural/semantic reproduction; not the published performance workload",
        "database": database,
        "rows": args.rows,
        "dimension": dimension,
        "inlist_elements": {"compact": 3, "repeated": len(repeated_values)},
        "distinct_inlist_values": compact_values,
        "passed": False,
    }
    connection = None
    try:
        connection = pymysql.connect(
            host=args.host, port=args.port, user=args.user,
            charset="utf8mb4", autocommit=True,
            connect_timeout=10, read_timeout=300, write_timeout=300,
        )
        with connection.cursor() as cursor:
            cursor.execute("SELECT VERSION()")
            report["version"] = cursor.fetchone()[0]
            print("Connected:", report["version"], flush=True)
            cursor.execute("SHOW VARIABLES LIKE '%inlist%'")
            report["inlist_variables"] = cursor.fetchall()
            cursor.execute("CREATE DATABASE `" + database + "`")
            connection.select_db(database)
            cursor.execute(
                "CREATE TABLE samples (id BIGINT PRIMARY KEY, "
                "embedding VECTOR(16) NOT NULL, category5 INT NOT NULL, "
                "KEY idx_category5(category5)) ORGANIZATION HEAP"
            )
            print("Created isolated database:", database, flush=True)
            for start in range(0, args.rows, 100):
                cursor.executemany(
                    "INSERT INTO samples(id,embedding,category5) VALUES (%s,%s,%s)",
                    [(i, vector(), i % 5 + 1) for i in range(start, min(start + 100, args.rows))],
                )
            print("Inserted", args.rows, "rows; creating vector index", flush=True)
            cursor.execute(
                "CREATE VECTOR INDEX idx_embedding ON samples(embedding) "
                "WITH (distance=l2,type=hnsw,lib=vsag,m=16,"
                "ef_construction=64,sync_mode=immediate)"
            )
            cursor.execute("ANALYZE TABLE samples")
            cursor.fetchall()
            cursor.execute("SET ob_hnsw_ef_search=64")
            query_vector = vector()
            report["query_vector"] = query_vector
            results = {}
            for name, predicate in filters.items():
                cursor.execute("SELECT id FROM samples WHERE " + predicate + " ORDER BY id")
                eligible = [row[0] for row in cursor.fetchall()]
                cursor.execute(
                    "SELECT id FROM samples WHERE " + predicate +
                    " ORDER BY l2_distance(embedding,%s),id LIMIT 10", (query_vector,),
                )
                exact = [row[0] for row in cursor.fetchall()]
                ann = ("SELECT id FROM samples WHERE " + predicate +
                       " ORDER BY l2_distance(embedding,%s) APPROXIMATE LIMIT 10")
                cursor.execute("EXPLAIN " + ann, (query_vector,))
                plan = [[str(value) for value in row] for row in cursor.fetchall()]
                cursor.execute(ann, (query_vector,))
                approximate = [row[0] for row in cursor.fetchall()]
                plan_text = "\n".join(" ".join(row) for row in plan).upper()
                results[name] = {
                    "eligible_ids": eligible, "exact_top10": exact,
                    "ann_top10": approximate, "plan": plan,
                    "operators": {operator: operator in plan_text for operator in (
                        "VALUES TABLE ACCESS", "HASH DISTINCT", "SUBPLAN FILTER", "TOP-N SORT",
                    )},
                }
                print(name, "eligible rows:", len(eligible),
                      "operators:", results[name]["operators"], flush=True)
            report["results"] = results
            report["eligible_sets_equal"] = results["compact"]["eligible_ids"] == results["repeated"]["eligible_ids"]
            report["exact_top10_equal"] = results["compact"]["exact_top10"] == results["repeated"]["exact_top10"]
            report["repeated_introduces_values_table"] = (
                not results["compact"]["operators"]["VALUES TABLE ACCESS"]
                and results["repeated"]["operators"]["VALUES TABLE ACCESS"]
            )
            report["passed"] = report["eligible_sets_equal"] and report["exact_top10_equal"]
            if not report["passed"]:
                raise RuntimeError("Equivalent predicates did not preserve exact results")

            # Reuse literal shapes with changed values, including after a
            # duplicate-heavy query, to exercise parameterized plan reuse.
            cases = [
                ("duplicates", "id IN (1,2,1,2,1,2)", [1, 2]),
                ("new_distinct_values", "id IN (1,2,3,4,5,6)", [1, 2, 3, 4, 5, 6]),
                ("duplicates_again", "id IN (1,2,1,2,1,2)", [1, 2]),
                ("negative", "id IN (-1,2,-1,2,-1,2)", [2]),
                ("null_in", "id IN (1,NULL,2,NULL,1,NULL)", [1, 2]),
                ("null_not_in", "id NOT IN (1,NULL,2,NULL,1,NULL)", []),
                ("string_coercion", "id IN ('1','2','1','2','1','2')", [1, 2]),
                ("unsigned", "id IN (18446744073709551615,18446744073709551615,"
                 "18446744073709551615,18446744073709551615)", []),
                ("tuple", "(category5,id) IN ((2,1),(3,2),(2,1),(3,2))", [1, 2]),
                ("at_threshold", "id IN (1,2,3,4)", [1, 2, 3, 4]),
            ]
            report["regressions"] = {}
            for name, predicate, expected in cases:
                statement = ("SELECT /*+ OPT_PARAM('inlist_rewrite_threshold' 4) */ id "
                             "FROM samples WHERE " + predicate + " ORDER BY id")
                cursor.execute(statement)
                actual = [row[0] for row in cursor.fetchall()]
                report["regressions"][name] = {"expected": expected, "actual": actual}
                if actual != expected:
                    report["passed"] = False
                    raise RuntimeError("Semantic regression: " + name)

            prepared_sql = ("SELECT /*+ OPT_PARAM('inlist_rewrite_threshold' 4) */ id "
                            "FROM samples WHERE id IN (?,?,?,?,?,?) ORDER BY id")
            cursor.execute("PREPARE bug1512_ps FROM %s", (prepared_sql,))
            try:
                for name, values in [
                    ("prepared_duplicates", [1, 2, 1, 2, 1, 2]),
                    ("prepared_rebind", [1, 2, 3, 4, 5, 6]),
                    ("prepared_null", [1, None, 2, None, 1, None]),
                ]:
                    cursor.execute("SET @a=%s,@b=%s,@c=%s,@d=%s,@e=%s,@f=%s", values)
                    cursor.execute("EXECUTE bug1512_ps USING @a,@b,@c,@d,@e,@f")
                    actual = [row[0] for row in cursor.fetchall()]
                    expected = sorted({value for value in values if value is not None})
                    report["regressions"][name] = {"expected": expected, "actual": actual}
                    if actual != expected:
                        report["passed"] = False
                        raise RuntimeError("Prepared-statement regression: " + name)
            finally:
                cursor.execute("DEALLOCATE PREPARE bug1512_ps")

            print("Semantic and prepared-statement regressions passed:", len(report["regressions"]), flush=True)
            report["expected_compact_plan"] = args.expect_compact
            if args.expect_compact and results["repeated"]["operators"]["VALUES TABLE ACCESS"]:
                report["passed"] = False
                raise RuntimeError("Patched-server expectation failed: repeated IN still uses a VALUES table")
    except Exception as error:
        report["passed"] = False
        report["error"] = str(error)
        raise
    finally:
        if connection is not None:
            connection.close()
        report["checked_at_utc"] = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
        output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print("Report:", str(output.resolve()), flush=True)
        print("Database retained for inspection:", database, flush=True)
    print("Exact result equivalence:", report["passed"], flush=True)
    print("Extra VALUES plan reproduced:", report["repeated_introduces_values_table"], flush=True)
    return report


if __name__ == "__main__":
    main()
