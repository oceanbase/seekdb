#!/usr/bin/env python3
"""SQL regressions for issue #1512 against a running disposable seekdb.

Run with SEEKDB_MYSQL_COMMAND='mysql -h127.0.0.1 -P2881 -uroot' and,
if needed, MYSQL_PWD in the environment. Only the uniquely named database
created by this test is removed. No third-party Python packages are needed.
"""

import os
import random
import shlex
import subprocess
import unittest
import uuid


class InlistRewriteTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.client = shlex.split(os.environ.get("SEEKDB_MYSQL_COMMAND", "mysql"))
        cls.client += ["--batch", "--raw", "--skip-column-names"]
        cls.database = "inlist_1512_" + uuid.uuid4().hex[:12]
        cls.sql("CREATE DATABASE " + cls.database)
        cls.addClassCleanup(cls.sql, "DROP DATABASE " + cls.database)
        cls.table = cls.database + ".items"
        cls.sql("CREATE TABLE " + cls.table + " (id INT PRIMARY KEY, category INT, "
                "embedding VECTOR(3)) ORGANIZATION HEAP")
        rows = ["(%d,%s,'[%d,0,0]')" % (i, "NULL" if i == 7 else i, i)
                for i in range(1, 8)]
        cls.sql("INSERT INTO " + cls.table + " VALUES " + ",".join(rows))
        cls.sql("CREATE VECTOR INDEX items_vec ON " + cls.table + "(embedding) "
                "WITH (distance=l2,type=hnsw,lib=vsag,m=8,"
                "ef_construction=40,sync_mode=immediate)")

    @classmethod
    def sql(cls, statement):
        result = subprocess.run(cls.client + ["-e", statement], text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                timeout=120)
        if result.returncode:
            raise AssertionError(result.stderr)
        return result.stdout.strip()

    @staticmethod
    def repeated(values):
        values = list(values) * 1024
        random.Random(1512).shuffle(values)
        return ",".join(map(str, values))

    @classmethod
    def ann(cls, values, hint=""):
        return ("SELECT " + hint + " id FROM " + cls.table +
                " WHERE category IN (" + values + ") "
                "ORDER BY l2_distance(embedding,'[0,0,0]') APPROXIMATE LIMIT 10")

    def test_duplicate_domain_keeps_scalar_ann_plan(self):
        compact = self.ann("1,2,3")
        duplicate = self.ann(self.repeated([1, 2, 3]))
        self.assertEqual(self.sql(compact), self.sql(duplicate))
        plan = self.sql("EXPLAIN " + duplicate)
        self.assertIn("VECTOR INDEX", plan)
        self.assertNotIn("VALUES TABLE ACCESS", plan)
        self.assertNotIn("SUBPLAN FILTER", plan)

    def test_changed_literals_are_not_discarded_by_cached_plan(self):
        values = [1, 2, 3] * 1024
        first = self.ann(",".join(map(str, values)))
        self.assertEqual("1\n2\n3", self.sql(first))
        # The last occurrence, not the first representative, now supplies 6.
        values[-1] = 6
        self.assertEqual("1\n2\n3\n6", self.sql(self.ann(",".join(map(str, values)))))
        self.assertEqual("4\n5\n6", self.sql(self.ann(self.repeated([4, 5, 6]))))

    def test_not_in_and_null_semantics(self):
        values = self.repeated([1, 2, 3])
        self.assertEqual("4\n5\n6", self.sql(
            "SELECT id FROM " + self.table + " WHERE category NOT IN (" +
            values + ") ORDER BY id"))
        self.assertEqual("", self.sql(
            "SELECT id FROM " + self.table + " WHERE category NOT IN (" +
            values + ",NULL) ORDER BY id"))
        self.assertEqual("1\n2\n3", self.sql(
            "SELECT id FROM " + self.table + " WHERE category IN (" +
            values + ",NULL) ORDER BY id"))

    def test_explicit_lower_threshold_is_respected(self):
        query = self.ann(self.repeated([1, 2, 3]),
                         "/*+ OPT_PARAM('inlist_rewrite_threshold' 2) */")
        self.assertEqual("1\n2\n3", self.sql(query))
        self.assertIn("VALUES TABLE ACCESS", self.sql("EXPLAIN " + query))

    def test_high_cardinality_still_uses_values_rewrite(self):
        query = self.ann(",".join(map(str, range(1, 3073))))
        self.assertEqual("1\n2\n3\n4\n5\n6", self.sql(query))
        self.assertIn("VALUES TABLE ACCESS", self.sql("EXPLAIN " + query))

    def test_bounded_domain_and_signed_values(self):
        query = self.ann(self.repeated([-1, 0, 1]))
        self.assertEqual("1", self.sql(query))
        self.assertNotIn("VALUES TABLE ACCESS", self.sql("EXPLAIN " + query))
        at_limit = self.ann(",".join(map(str, list(range(1, 65)) * 16)))
        over_limit = self.ann(",".join(map(str, list(range(1, 66)) * 16)))
        self.assertNotIn("VALUES TABLE ACCESS", self.sql("EXPLAIN " + at_limit))
        self.assertIn("VALUES TABLE ACCESS", self.sql("EXPLAIN " + over_limit))

    def test_string_inputs_keep_existing_rewrite(self):
        values = self.repeated(["'1'", "'2'", "'3'"])
        query = self.ann(values)
        self.assertEqual("1\n2\n3", self.sql(query))
        self.assertIn("VALUES TABLE ACCESS", self.sql("EXPLAIN " + query))

    def test_prepared_parameters_remain_independent(self):
        count = 210
        query = "SELECT id FROM " + self.table + " WHERE category IN (" + ",".join(
            ["?"] * count) + ") ORDER BY id"
        variables = ",".join(["@a", "@b", "@c"] * (count // 3))
        output = self.sql(
            "PREPARE p FROM '" + query + "'; SET @a=1,@b=2,@c=3; "
            "EXECUTE p USING " + variables + "; SET @a=4,@b=5,@c=6; "
            "EXECUTE p USING " + variables + "; DEALLOCATE PREPARE p")
        self.assertEqual("1\n2\n3\n4\n5\n6", output)


if __name__ == "__main__":
    unittest.main(verbosity=2)
