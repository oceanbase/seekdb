#!/usr/bin/env python3
"""Issue #1512 SQL regressions against a running seekdb built from this checkout.

Uses the mysql CLI, creates a uniquely named table, and drops only that table.
Example:
  python3 unittest/sql/test_inlist_plan.py --defaults-extra-file /path/client.cnf --database test
The two duplicate-list plan checks must FAIL against the unpatched 1.4.0 binary.
This small fixture checks plan shape and SQL semantics, not ANN latency.
"""

import argparse
import random
import subprocess
import sys
import unittest
import uuid


class InListPlanTests(unittest.TestCase):
    command = []

    @classmethod
    def sql(cls, statement):
        completed = subprocess.run(cls.command, input=statement, text=True,
                                   capture_output=True, timeout=120)
        if completed.returncode:
            raise RuntimeError(completed.stderr.strip())
        return completed.stdout.strip()

    @classmethod
    def setUpClass(cls):
        cls.table = "inlist_1512_" + uuid.uuid4().hex[:12]
        values = [1, 2, 3] * 1024
        random.Random(1024).shuffle(values)
        cls.long_list = ",".join(map(str, values))
        cls.sql(f"CREATE TABLE {cls.table} (id INT PRIMARY KEY, category5 INT, "
                "embedding VECTOR(3)) ORGANIZATION HEAP;")
        cls.addClassCleanup(cls.sql, f"DROP TABLE {cls.table};")
        cls.sql(f"INSERT INTO {cls.table} VALUES "
                "(1,1,'[1,0,0]'),(2,2,'[2,0,0]'),(3,3,'[3,0,0]'),"
                "(4,4,'[4,0,0]'),(5,5,'[5,0,0]'),(6,NULL,'[6,0,0]');")
        cls.sql(f"CREATE VECTOR INDEX inlist_1512_hnsw ON {cls.table}(embedding) "
                "WITH (distance=l2,type=hnsw,lib=vsag,sync_mode=immediate);")
        cls.sql(f"ANALYZE TABLE {cls.table};")

    def query(self, values, negative=False):
        op = "NOT IN" if negative else "IN"
        return f"SELECT id FROM {self.table} WHERE category5 {op} ({values})"

    def assert_direct_plan(self, query):
        plan = self.sql("EXPLAIN " + query + ";")
        self.assertNotIn("VALUES TABLE ACCESS", plan, plan)
        self.assertNotIn("HASH DISTINCT", plan, plan)
        return plan

    def test_duplicate_scalar_plan(self):
        self.assert_direct_plan(self.query("1,2,3"))
        self.assert_direct_plan(self.query(self.long_list))

    def test_duplicate_ann_plan(self):
        suffix = " ORDER BY l2_distance(embedding,'[1,0,0]') APPROXIMATE LIMIT 3"
        compact = self.query("1,2,3") + suffix
        expanded = self.query(self.long_list) + suffix
        self.assertIn("VECTOR INDEX", self.sql("EXPLAIN " + compact + ";"))
        self.assert_direct_plan(expanded)
        self.assertEqual(self.sql(compact + ";"), self.sql(expanded + ";"))

    def test_membership_and_null_semantics(self):
        for values, negative, expected in [
            (self.long_list, False, "1\n2\n3"),
            (self.long_list, True, "4\n5"),
            (self.long_list + ",NULL", False, "1\n2\n3"),
            (self.long_list + ",NULL", True, ""),
        ]:
            with self.subTest(negative=negative, has_null=values.endswith("NULL")):
                self.assertEqual(expected, self.sql(self.query(values, negative) + " ORDER BY id;"))

    def test_changing_auto_parameterized_values(self):
        # Same SQL shape with different values. A plan must retain ALL parameter
        # positions, including those equal to another position on the first run.
        first = [1] * 3072
        second = first.copy()
        second[-1] = 5
        for values, expected in [(first, "1"), (second, "1\n5"), (first, "1")]:
            query = self.query(",".join(map(str, values)))
            self.assertEqual(expected, self.sql(query + " ORDER BY id;"))

    def test_explicit_prepare_rebind(self):
        statement = (self.query("?,?,?,?") + " ORDER BY id").replace(
            "SELECT", "SELECT /*+ OPT_PARAM('inlist_rewrite_threshold', 4) */", 1
        ).replace("'", "''")
        result = self.sql(
            f"PREPARE inlist_ps FROM '{statement}';"
            "SET @a=1,@b=1,@c=1,@d=1;"
            "EXECUTE inlist_ps USING @a,@b,@c,@d;"
            "SET @d=5;"
            "EXECUTE inlist_ps USING @a,@b,@c,@d;"
            "DEALLOCATE PREPARE inlist_ps;"
        )
        self.assertEqual("1\n1\n5", result)

    def test_distinct_list_retains_values_rewrite(self):
        values = ",".join(map(str, range(1, 3073)))
        plan = self.sql("EXPLAIN " + self.query(values) + ";")
        self.assertIn("VALUES TABLE ACCESS", plan, plan)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mysql", default="mysql")
    parser.add_argument("--defaults-extra-file", required=True)
    parser.add_argument("--database", default="test")
    args = parser.parse_args()
    InListPlanTests.command = [args.mysql, "--defaults-extra-file=" + args.defaults_extra_file,
                               "--batch", "--raw", "--skip-column-names", args.database]
    unittest.main(argv=[sys.argv[0]], verbosity=2)
