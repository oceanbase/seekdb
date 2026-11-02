"""Live SQL regression for #1489; run against a server built from this branch.

Requires PyMySQL. SEEKDB_PORT defaults to 2881; the connection uses local root.
Creates and removes a uniquely named test table in the test database.

Run: python3 -m pip install pymysql
     python3 unittest/sql/test_large_boolean_duplicates.py
"""
import os
import re
import unittest
import uuid

import pymysql


class LargeBooleanDuplicates(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.connection = pymysql.connect(
            host="127.0.0.1", port=int(os.getenv("SEEKDB_PORT", "2881")),
            user=os.getenv("SEEKDB_USER", "root"),
            password=os.getenv("SEEKDB_PASSWORD", ""), database="test",
            autocommit=True,
        )
        cls.addClassCleanup(cls.connection.close)
        cls.cursor = cls.connection.cursor()
        cls.addClassCleanup(cls.cursor.close)
        cls.table = "boolean_duplicates_" + uuid.uuid4().hex
        cls.cursor.execute(f"CREATE TABLE {cls.table} "
                           "(id INT PRIMARY KEY, age INT, category INT, "
                           "embedding VECTOR(3)) ORGANIZATION HEAP")
        cls.addClassCleanup(cls.cursor.execute, f"DROP TABLE {cls.table}")
        cls.cursor.executemany(f"INSERT INTO {cls.table} VALUES (%s,%s,%s,%s)",
                              [(1, 10, 1, '[1,0,0]'), (2, 20, 2, '[2,0,0]'),
                               (3, None, 2, '[3,0,0]'), (4, 2001, 2, '[4,0,0]'),
                               (5, 2001, None, '[5,0,0]'), (6, None, None, '[6,0,0]')])
        cls.cursor.execute(f"CREATE VECTOR INDEX duplicates_hnsw ON {cls.table}(embedding) "
                           "WITH (distance=l2, type=hnsw, lib=vsag, m=16, "
                           "ef_construction=128, sync_mode=immediate)")

    def test_duplicate_branches_at_size_boundary(self):
        for operator in ("OR", "AND"):
            compact = f"category = 1 {operator} age > 1000"
            for count in (99, 100, 101, 129, 1025):
                with self.subTest(operator=operator, branches=count):
                    expanded = f"category = 1 {operator} " + f" {operator} ".join(
                        ["age > 1000"] * (count - 1))
                    # Compare the full three-valued truth result, including NULL.
                    self.cursor.execute(
                        f"SELECT id, ({expanded}) <=> ({compact}) "
                        f"FROM {self.table} ORDER BY id")
                    self.assertTrue(all(row[1] == 1 for row in self.cursor.fetchall()))
                    for approximate in (False, True):
                        with self.subTest(approximate=approximate):
                            suffix = "ORDER BY l2_distance(embedding, '[0,0,0]') "
                            suffix += "APPROXIMATE LIMIT 10" if approximate else "LIMIT 10"
                            query = ("SELECT /*+ NO_USE_PLAN_CACHE */ id "
                                     f"FROM {self.table} WHERE {expanded} {suffix}")
                            self.cursor.execute(query)
                            actual = self.cursor.fetchall()
                            self.cursor.execute(query.replace(expanded, compact))
                            self.assertEqual(self.cursor.fetchall(), actual)
                            self.cursor.execute("EXPLAIN EXTENDED " + query)
                            plan = "\n".join(str(row[0]) for row in self.cursor.fetchall())
                            plan = re.sub(r"\([0-9A-Fa-f]{8,16}\)", "", plan)
                            self.assertEqual(1, len(re.findall(r"\.age\s*>\s*1000", plan)), plan)

    def test_plan_cache_parameter_constraints(self):
        # Equal literal values may be deduplicated only with the corresponding
        # parameter-equality constraints; a later execution can change one value.
        for approximate in (False, True):
            suffix = "ORDER BY l2_distance(embedding, '[0,0,0]') "
            suffix += "APPROXIMATE LIMIT 10" if approximate else "LIMIT 10"
            for changed in (None, 0, 127, None):
                values = [1000] * 128
                if changed is not None:
                    values[changed] = 10
                predicate = "category = 1 OR " + " OR ".join(
                    f"age > {value}" for value in values)
                self.cursor.execute(f"SELECT id FROM {self.table} WHERE {predicate} {suffix}")
                expected = ((1,), (4,), (5,)) if changed is None else ((1,), (2,), (4,), (5,))
                self.assertEqual(expected, self.cursor.fetchall())


if __name__ == "__main__":
    unittest.main()
