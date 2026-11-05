"""SQL regression for issue #1512 against a running, patched seekdb server.

Install PyMySQL, set SEEKDB_TEST_HOST (and optionally SEEKDB_TEST_PORT,
SEEKDB_TEST_USER, SEEKDB_TEST_PASSWORD), then run this file with Python 3.
"""

import os
import random
import unittest
import uuid


@unittest.skipUnless(os.environ.get("SEEKDB_TEST_HOST"), "set SEEKDB_TEST_HOST to run SQL regressions")
class InlistRewriteTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        import pymysql

        cls.connection = pymysql.connect(
            host=os.environ["SEEKDB_TEST_HOST"],
            port=int(os.environ.get("SEEKDB_TEST_PORT", "2881")),
            user=os.environ.get("SEEKDB_TEST_USER", "root"),
            password=os.environ.get("SEEKDB_TEST_PASSWORD", ""),
            autocommit=True,
        )
        cls.database = "inlist_1512_" + uuid.uuid4().hex
        try:
            cls.query("CREATE DATABASE " + cls.database)
            cls.connection.select_db(cls.database)
            cls.query("CREATE TABLE inlist_1512 (v INT)")
            cls.query("INSERT INTO inlist_1512 VALUES (1),(2),(3),(4),(5),(NULL)")
        except Exception:
            cls.query("DROP DATABASE IF EXISTS " + cls.database)
            cls.connection.close()
            raise

    @classmethod
    def tearDownClass(cls):
        try:
            cls.query("DROP DATABASE " + cls.database)
        finally:
            cls.connection.close()

    @classmethod
    def query(cls, sql):
        with cls.connection.cursor() as cursor:
            cursor.execute(sql)
            return cursor.fetchall()

    def statement(self, values, op="IN", threshold=4):
        return (
            f"SELECT /*+ NO_REWRITE OPT_PARAM('inlist_rewrite_threshold', {threshold}) */ v "
            f"FROM inlist_1512 WHERE v {op} ({values}) ORDER BY v"
        )

    def assert_values_table(self, statement, expected):
        plan = "\n".join(str(cell) for row in self.query("EXPLAIN " + statement) for cell in row)
        self.assertEqual("VALUES TABLE ACCESS" in plan.upper(), expected, plan)

    def test_shuffled_3072_values_keep_scalar_in(self):
        values = [1, 2, 3] * 1024
        random.Random(1024).shuffle(values)
        statement = self.statement(",".join(map(str, values)), threshold=1000)
        self.assertEqual(self.query(statement), ((1,), (2,), (3,)))
        self.assert_values_table(statement, False)

    def test_distinct_threshold_boundary(self):
        below = self.statement("1,2,3,1,2,3,1,2,3")
        at = self.statement("1,2,3,4,1,2,3,4")
        self.assertEqual(self.query(below), ((1,), (2,), (3,)))
        self.assertEqual(self.query(at), ((1,), (2,), (3,), (4,)))
        self.assert_values_table(below, False)
        self.assert_values_table(at, True)

    def test_not_in_and_null_semantics(self):
        self.assertEqual(self.query(self.statement("1,2,3,1,2,3", "NOT IN")), ((4,), (5,)))
        self.assertEqual(self.query(self.statement("1,2,3,NULL,1,2,3", "NOT IN")), ())
        self.assertEqual(self.query(self.statement("1,2,3,NULL,1,2,3")), ((1,), (2,), (3,)))

    def test_string_values_use_existing_rewrite(self):
        statement = self.statement("'1','2','3','1','2','3'")
        self.assertEqual(self.query(statement), ((1,), (2,), (3,)))
        self.assert_values_table(statement, True)

    def test_cached_plan_keeps_every_operand(self):
        # Same parameterized SQL shape; values cease to be duplicates on reuse.
        self.assertEqual(self.query(self.statement("1,1,1,1,1,1")), ((1,),))
        self.assertEqual(self.query(self.statement("1,2,3,4,5,5")), ((1,), (2,), (3,), (4,), (5,)))

    def test_prepare_keeps_unbound_parameters(self):
        self.query("SET @a=1, @b=1, @c=1, @d=1")
        self.query("PREPARE inlist_1512_stmt FROM '" + self.statement("?,?,?,?").replace("'", "''") + "'")
        try:
            self.assertEqual(self.query("EXECUTE inlist_1512_stmt USING @a,@b,@c,@d"), ((1,),))
            self.query("SET @b=2, @c=3, @d=4")
            self.assertEqual(
                self.query("EXECUTE inlist_1512_stmt USING @a,@b,@c,@d"),
                ((1,), (2,), (3,), (4,)),
            )
        finally:
            self.query("DEALLOCATE PREPARE inlist_1512_stmt")


if __name__ == "__main__":
    unittest.main()
