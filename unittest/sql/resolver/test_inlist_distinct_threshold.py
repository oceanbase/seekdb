"""Native resolver regression for duplicate-heavy integer IN lists (#1512).

Run against a seekdb built from this checkout:
    SEEKDB_PORT=2881 python -m unittest discover -s unittest/sql/resolver
Requires the standard MySQL driver pymysql. Each run owns a temporary schema.
"""

import os
import unittest
import uuid

import pymysql


class DistinctInlistThreshold(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.connection = pymysql.connect(
            host=os.environ.get("SEEKDB_HOST", "127.0.0.1"),
            port=int(os.environ.get("SEEKDB_PORT", "2881")),
            user=os.environ.get("SEEKDB_USER", "root"),
            password=os.environ.get("SEEKDB_PASSWORD", ""),
            autocommit=True,
        )
        cls.schema = "inlist_distinct_" + uuid.uuid4().hex[:12]
        with cls.connection.cursor() as cursor:
            cursor.execute(f"CREATE DATABASE {cls.schema}")
            cursor.execute(f"CREATE TABLE {cls.schema}.items(id INT PRIMARY KEY, value BIGINT, label VARCHAR(16))")
            cursor.executemany(
                f"INSERT INTO {cls.schema}.items VALUES (%s,%s,%s)",
                [(1, 1, "one"), (2, 2, "two"), (3, 3, "three"), (4, 4, "four"), (5, None, None)],
            )

    @classmethod
    def tearDownClass(cls):
        with cls.connection.cursor() as cursor:
            cursor.execute(f"DROP DATABASE {cls.schema}")
        cls.connection.close()

    def statement(self, values, *, operator="IN", column="value"):
        literals = ",".join("NULL" if value is None else str(value) for value in values)
        return (
            "SELECT /*+ OPT_PARAM('inlist_rewrite_threshold' 8) */ id "
            f"FROM {self.schema}.items WHERE {column} {operator} ({literals})"
        )

    def plan(self, statement):
        with self.connection.cursor() as cursor:
            cursor.execute("EXPLAIN " + statement)
            return "\n".join(str(row[0]) for row in cursor.fetchall())

    def ids(self, statement):
        with self.connection.cursor() as cursor:
            cursor.execute(statement + " ORDER BY id")
            return tuple(row[0] for row in cursor.fetchall())

    def test_duplicates_keep_the_native_scalar_predicate(self):
        statement = self.statement([1, 2, 3] * 1024)
        self.assertEqual(self.ids(statement), (1, 2, 3))
        self.assertNotIn("VALUES TABLE ACCESS", self.plan(statement))

    def test_many_distinct_values_retain_the_values_rewrite(self):
        statement = self.statement(range(1, 25))
        self.assertEqual(self.ids(statement), (1, 2, 3, 4))
        self.assertIn("VALUES TABLE ACCESS", self.plan(statement))

    def test_nulls_and_not_in_preserve_three_valued_logic(self):
        self.assertEqual(self.ids(self.statement([1, 2, None] * 1024)), (1, 2))
        self.assertEqual(self.ids(self.statement([1, 2, None] * 1024, operator="NOT IN")), ())
        self.assertEqual(self.ids(self.statement([1, 2] * 1024, operator="NOT IN")), (3, 4))

    def test_parameterized_cache_does_not_freeze_observed_values(self):
        self.assertEqual(self.ids(self.statement([1, 2, 3] * 1024)), (1, 2, 3))
        self.assertEqual(self.ids(self.statement([4] * 3072)), (4,))
        self.assertEqual(self.ids(self.statement(range(1, 3073))), (1, 2, 3, 4))

    def test_string_lists_keep_the_existing_path(self):
        statement = self.statement(["'one'", "'three'"] * 32, column="label")
        self.assertEqual(self.ids(statement), (1, 3))
        self.assertIn("VALUES TABLE ACCESS", self.plan(statement))


if __name__ == "__main__":
    unittest.main()
