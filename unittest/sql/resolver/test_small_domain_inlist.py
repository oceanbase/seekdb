"""Live SQL regression for repeated IN domains (issue #1512).

Run with SEEKDB_MYSQL set to a MySQL client command, then execute this file.
The test creates and removes its own database. It does not modify shared data.
"""
import os
import random
import shlex
import subprocess
import unittest
import uuid


@unittest.skipUnless(os.environ.get("SEEKDB_MYSQL"), "requires a live seekdb client command")
class SmallDomainInlistTest(unittest.TestCase):
    @classmethod
    def sql(cls, query):
        proc = subprocess.run(cls.client + ["-e", query], capture_output=True, text=True)
        if proc.returncode:
            raise AssertionError(proc.stderr)
        return proc.stdout.strip()

    @classmethod
    def setUpClass(cls):
        cls.client = shlex.split(os.environ["SEEKDB_MYSQL"]) + ["-N", "-s", "-A"]
        cls.database = "inlist_regression_" + uuid.uuid4().hex
        cls.sql("CREATE DATABASE " + cls.database)
        cls.client.append(cls.database)
        try:
            cls.sql("CREATE TABLE t (id BIGINT PRIMARY KEY, category BIGINT, embedding VECTOR(3))")
            rows = []
            for i in range(1000):
                category = "NULL" if i % 10 == 0 else str(i % 5 + 1)
                rows.append("(%d,%s,'[1,0,0]')" % (i, category))
            cls.sql("INSERT INTO t VALUES " + ",".join(rows))
            cls.sql("CREATE VECTOR INDEX t_hnsw ON t(embedding) "
                    "WITH(distance=l2,type=hnsw,lib=vsag,m=16,"
                    "ef_construction=128,sync_mode=immediate)")
            cls.sql("ANALYZE TABLE t")
        except Exception:
            cls.sql("DROP DATABASE " + cls.database)
            raise

    @classmethod
    def tearDownClass(cls):
        cls.sql("DROP DATABASE " + cls.database)

    def test_repeated_domain_stays_in_the_vector_filter(self):
        values = ["1", "2", "3"] * 1024
        random.Random(1024).shuffle(values)
        predicate = "category IN (" + ",".join(values) + ")"
        self.assertEqual(self.sql("SELECT COUNT(*) FROM t WHERE " + predicate),
                         self.sql("SELECT COUNT(*) FROM t WHERE category IN (1,2,3)"))
        plan = self.sql("EXPLAIN SELECT id FROM t WHERE " + predicate +
                        " ORDER BY l2_distance(embedding,'[1,0,0]') APPROXIMATE LIMIT 10")
        self.assertIn("VECTOR INDEX", plan)
        self.assertNotIn("VALUES TABLE ACCESS", plan)
        self.assertNotIn("SUBPLAN FILTER", plan)

    def test_larger_domains_keep_the_existing_materialization(self):
        values = ",".join(["1", "2", "3"] * 2048)
        self.assertEqual(self.sql("SELECT COUNT(*) FROM t WHERE category IN (" + values + ")"),
                         self.sql("SELECT COUNT(*) FROM t WHERE category IN (1,2,3)"))
        plan = self.sql("EXPLAIN SELECT id FROM t WHERE category IN (" + values +
                        ") ORDER BY l2_distance(embedding,'[1,0,0]') APPROXIMATE LIMIT 10")
        self.assertIn("VALUES TABLE ACCESS", plan)

    def test_unsigned_integer_boundaries(self):
        self.sql("CREATE TABLE unsigned_values (v BIGINT UNSIGNED)")
        self.sql("INSERT INTO unsigned_values VALUES (0),(9223372036854775808),"
                 "(18446744073709551615),(NULL)")
        for compact in ["0,9223372036854775808", "18446744073709551615,NULL"]:
            repeated = ",".join([compact] * 1024)
            for operator in ["IN", "NOT IN"]:
                prefix = "SELECT COUNT(*) FROM unsigned_values WHERE v " + operator + " ("
                self.assertEqual(self.sql(prefix + compact + ")"),
                                 self.sql(prefix + repeated + ")"))

    def test_null_and_not_in_semantics(self):
        for compact in ["1,2,NULL", "NULL", "1,2,3"]:
            repeated = ",".join([compact] * 1024)
            for operator in ["IN", "NOT IN"]:
                prefix = "SELECT COUNT(*) FROM t WHERE category " + operator + " ("
                self.assertEqual(self.sql(prefix + compact + ")"),
                                 self.sql(prefix + repeated + ")"))

    def test_parameterized_plan_can_change_membership(self):
        placeholders = ",".join(["?"] * 3072)
        bindings = ",".join(["@a", "@b", "@c"] * 1024)
        result = self.sql("PREPARE p FROM 'SELECT COUNT(*) FROM t WHERE category IN (" +
                          placeholders + ")'; SET @a=1,@b=2,@c=3; EXECUTE p USING " +
                          bindings + "; SET @a=4,@b=5,@c=NULL; EXECUTE p USING " +
                          bindings + "; DEALLOCATE PREPARE p")
        expected = [self.sql("SELECT COUNT(*) FROM t WHERE category IN (1,2,3)"),
                    self.sql("SELECT COUNT(*) FROM t WHERE category IN (4,5,NULL)")]
        self.assertEqual(result.splitlines(), expected)

    def test_cached_literal_plan_does_not_remove_input_values(self):
        for domain in [[1, 2, 3], [3, 4, 5], list(range(1, 100))]:
            values = [str(domain[i % len(domain)]) for i in range(3072)]
            self.assertEqual(self.sql("SELECT COUNT(*) FROM t WHERE category IN (" +
                                      ",".join(values) + ")"),
                             self.sql("SELECT COUNT(*) FROM t WHERE category IN (" +
                                      ",".join(map(str, domain)) + ")"))


if __name__ == "__main__":
    unittest.main()
