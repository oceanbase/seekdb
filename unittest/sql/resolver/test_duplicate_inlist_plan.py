"""Regression for competition issue #1512 against a running seekdb.

Requires PyMySQL. Run with SEEKDB_TEST_PORT=2881 python3 <this file> -v.
Only a uniquely named fixture database is created and removed.
"""
import json
import os
import random
import unittest
import uuid

import pymysql


class DuplicateInListTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.connection = pymysql.connect(
            host=os.getenv("SEEKDB_TEST_HOST", "127.0.0.1"),
            port=int(os.getenv("SEEKDB_TEST_PORT", "2881")),
            user=os.getenv("SEEKDB_TEST_USER", "root"),
            password=os.getenv("SEEKDB_TEST_PASSWORD", ""),
            autocommit=True, charset="utf8mb4", read_timeout=30,
        )
        cls.database = "inlist_ci_" + uuid.uuid4().hex[:12]
        cls.addClassCleanup(cls.cleanup)
        cls.execute("CREATE DATABASE `" + cls.database + "`")
        cls.execute("USE `" + cls.database + "`")
        cls.execute("CREATE TABLE docs(id INT PRIMARY KEY,category INT NOT NULL,name VARCHAR(20) COLLATE utf8mb4_general_ci)")
        for i in range(100):
            cls.execute("INSERT INTO docs VALUES(%s,%s,%s)", (i, i % 10, "value-%d" % (i % 10)))
        cls.execute("ANALYZE TABLE docs")

    @classmethod
    def execute(cls, sql, args=None):
        with cls.connection.cursor() as cursor:
            cursor.execute(sql, args)
            return cursor.fetchall()

    @classmethod
    def cleanup(cls):
        try:
            cls.execute("DROP DATABASE IF EXISTS `" + cls.database + "`")
        finally:
            cls.connection.close()

    @staticmethod
    def repeated(values):
        expanded = list(values) * 1024
        random.Random(1024).shuffle(expanded)
        return expanded

    def query(self, values, operation="IN", explain=False, column="category"):
        marks = ",".join(["%s"] * len(values))
        return self.execute(("EXPLAIN " if explain else "") + "SELECT id FROM docs WHERE "
                            + column + " " + operation + " (" + marks + ") ORDER BY id", tuple(values))

    def test_duplicate_membership_avoids_values_subquery(self):
        values = self.repeated([1, 2, 3])
        self.assertEqual(self.query([1, 2, 3]), self.query(values))
        plan = "\n".join(str(column) for row in self.query(values, explain=True) for column in row)
        self.assertNotIn("VALUES TABLE ACCESS", plan.upper(), plan)
        self.assertNotIn("SUBPLAN FILTER", plan.upper(), plan)

    def test_not_in_null_semantics(self):
        for operation in ("IN", "NOT IN"):
            for values in ([1, 2, 3], [None, 1, 2, 3], [None]):
                with self.subTest(operation=operation, values=values):
                    self.assertEqual(self.query(values, operation), self.query(self.repeated(values), operation))

    def test_cached_plan_preserves_independently_bound_values(self):
        for values in ([1, 2, 3], [4, 5, 6], list(range(10)), [9, 9, 9]):
            with self.subTest(values=values):
                result = self.query(self.repeated(values))
                expected = tuple((i,) for i in range(100) if i % 10 in values)
                self.assertEqual(expected, result)

    def test_case_insensitive_string_membership(self):
        values = ["VALUE-1", "value-2", "Value-3"]
        self.assertEqual(self.query(values, column="name"), self.query(self.repeated(values), column="name"))

    def test_vector_membership_keeps_filter_in_vector_scan(self):
        self.execute("CREATE TABLE vector_docs(id INT PRIMARY KEY,category INT NOT NULL,embedding VECTOR(8),"
                     "VECTOR INDEX vec_idx(embedding) WITH(distance=l2,type=hnsw,lib=vsag,sync_mode=immediate))")
        try:
            rng = random.Random(1512)
            for i in range(256):
                vector = json.dumps([rng.random() for _ in range(8)])
                self.execute("INSERT INTO vector_docs VALUES(%s,%s,%s)", (i, i % 10, vector))
            self.execute("ANALYZE TABLE vector_docs")
            vector = json.dumps([rng.random() for _ in range(8)])
            values = self.repeated([1, 2, 3])

            def query(members, approximate=False, explain=False):
                marks = ",".join(["%s"] * len(members))
                sql = ("EXPLAIN " if explain else "") + "SELECT id,category FROM vector_docs WHERE category IN ("
                sql += marks + ") ORDER BY l2_distance(embedding,%s) "
                sql += ("APPROXIMATE " if approximate else "") + "LIMIT 10"
                return self.execute(sql, tuple(members) + (vector,))

            self.assertEqual(query([1, 2, 3]), query(values))
            for members in ([1, 2, 3], values):
                rows = query(members, approximate=True)
                self.assertTrue(0 < len(rows) <= 10)
                self.assertTrue(all(category in (1, 2, 3) for _, category in rows))
            plan = "\n".join(str(cell) for row in query(values, approximate=True, explain=True) for cell in row)
            self.assertIn("VECTOR INDEX", plan.upper(), plan)
            self.assertNotIn("VALUES TABLE ACCESS", plan.upper(), plan)
            self.assertNotIn("SUBPLAN FILTER", plan.upper(), plan)
        finally:
            self.execute("DROP TABLE vector_docs")


if __name__ == "__main__":
    unittest.main()
