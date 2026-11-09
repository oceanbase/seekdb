"""Integration regression for #1535; requires PyMySQL and a running seekdb.

Run against both the original and rebuilt server, for example:
  python unittest/sql/rewrite/test_ann_absorption.py --socket /path/run/sql.sock

Only a uniquely named temporary database is created and dropped. Plan assertions
are intentional: result equality alone passes on the affected server as well.
"""

import argparse
import os
import unittest
import uuid

import pymysql

CONNECTION_OPTIONS = None


def balanced_and(expressions):
    if len(expressions) == 1:
        return expressions[0]
    middle = len(expressions) // 2
    return "({}) AND ({})".format(
        balanced_and(expressions[:middle]), balanced_and(expressions[middle:])
    )


class AnnAbsorptionTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if CONNECTION_OPTIONS is None:
            raise unittest.SkipTest("run this integration test with --socket or --host/--port")
        cls.connection = pymysql.connect(**CONNECTION_OPTIONS, autocommit=True)
        cls.database = "ann_absorption_" + uuid.uuid4().hex
        try:
            with cls.connection.cursor() as cursor:
                cursor.execute("CREATE DATABASE " + cls.database)
                cursor.execute("USE " + cls.database)
                cursor.execute("SET ob_query_timeout = 60000000")
                cursor.execute("SET ob_hnsw_ef_search = 16")
                cursor.execute("""
                    CREATE TABLE t (
                      id INT PRIMARY KEY,
                      category INT,
                      parity VARCHAR(8),
                      embedding VECTOR(3),
                      INDEX idx_category(category),
                      VECTOR INDEX idx_vec(embedding) WITH
                        (distance=l2, type=hnsw, lib=vsag, m=16,
                         ef_construction=128, sync_mode=immediate)
                    ) ORGANIZATION HEAP
                """)
                rows = [
                    (0, 2, "even"), (1, 1, "even"), (2, 1, "odd"),
                    (3, 1, None), (4, 2, "even"), (5, 2, "odd"),
                    (6, None, "even"), (7, None, "odd"), (8, None, None),
                ]
                # Distances deliberately differ from primary-key order.
                coordinates = (8, 3, 1, 2, 4, 5, 6, 7, 0)
                cursor.executemany(
                    "INSERT INTO t VALUES (%s, %s, %s, %s)",
                    [(i, category, parity, f"[{coordinates[i]},0,0]")
                     for i, category, parity in rows],
                )
                cursor.execute("ANALYZE TABLE t")
        except Exception:
            cls.tearDownClass()
            raise

    @classmethod
    def tearDownClass(cls):
        try:
            with cls.connection.cursor() as cursor:
                cursor.execute("DROP DATABASE IF EXISTS " + cls.database)
        finally:
            cls.connection.close()

    def query(self, sql):
        with self.connection.cursor() as cursor:
            cursor.execute(sql)
            return cursor.fetchall()

    def search(self, predicate, approximate=True):
        return (
            "SELECT /*+ INDEX(t idx_vec) */ id FROM t WHERE " + predicate
            + " ORDER BY l2_distance(embedding, '[0,0,0]') "
            + ("APPROXIMATE " if approximate else "") + "LIMIT 10"
        )

    def test_absorption_results_with_nulls(self):
        for branches in (1, 16, 1024):
            with self.subTest(branches=branches):
                predicate = balanced_and(
                    ["category = 1"] + ["(category = 1 OR parity = 'even')"] * branches
                )
                self.assertEqual(self.query(self.search(predicate)), ((2,), (3,), (1,)))
                self.assertEqual(
                    self.query(self.search(predicate, approximate=False)),
                    ((2,), (3,), (1,)),
                )

    def test_ann_plan_removes_absorbed_predicate(self):
        for branches in (1, 1024):
            with self.subTest(branches=branches):
                predicate = balanced_and(
                    ["category = 1"] + ["(category = 1 OR parity = 'even')"] * branches
                )
                plan = "\n".join(str(row[0]) for row in self.query(
                    "EXPLAIN " + self.search(predicate)
                ))
                self.assertIn("VECTOR INDEX", plan.upper(), plan)
                self.assertTrue("parity" not in plan.lower(), plan[:2000])

    def test_different_constants_are_not_absorbed(self):
        predicate = "category = 1 AND (category = 2 OR parity = 'even')"
        self.assertEqual(self.query(self.search(predicate)), ((1,),))

    def test_exact_plan_absorbs_same_expression(self):
        predicate = balanced_and(
            ["category = 1"] + ["(category = 1 OR parity = 'even')"] * 1024
        )
        plan = "\n".join(str(row[0]) for row in self.query(
            "EXPLAIN " + self.search(predicate, approximate=False)
        ))
        self.assertTrue("parity" not in plan.lower(), plan[:2000])

    def test_distance_order_and_limit_are_preserved(self):
        sql = (
            "SELECT /*+ INDEX(t idx_vec) */ id, "
            "l2_distance(embedding, '[0,0,0]') AS distance FROM t "
            "WHERE category = 1 AND (category = 1 OR parity = 'even') "
            "ORDER BY l2_distance(embedding, '[0,0,0]') {} LIMIT 2"
        )
        exact = self.query(sql.format(""))
        self.assertEqual([row[0] for row in exact], [2, 3])
        self.assertEqual(self.query(sql.format("APPROXIMATE")), exact)

    def test_prepared_statement_parameter_constraints(self):
        sql = self.search("category = ? AND (category = ? OR parity = ?)")
        self.query("PREPARE absorption_stmt FROM '" + sql.replace("'", "''") + "'")
        try:
            for p, duplicate, expected in (
                (1, 1, ((2,), (3,), (1,))),
                (1, 2, ((1,),)),
                (2, 2, ((4,), (5,), (0,))),
                (1, 1, ((2,), (3,), (1,))),
            ):
                with self.subTest(p=p, duplicate=duplicate):
                    self.query(f"SET @p = {p}, @duplicate = {duplicate}, @q = 'even'")
                    self.assertEqual(self.query(
                        "EXECUTE absorption_stmt USING @p, @duplicate, @q"
                    ), expected)
        finally:
            self.query("DEALLOCATE PREPARE absorption_stmt")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--socket", default=os.getenv("SEEKDB_TEST_SOCKET"))
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=2881)
    parser.add_argument("--user", default="root")
    args, remaining = parser.parse_known_args()
    CONNECTION_OPTIONS = dict(
        user=args.user, password=os.getenv("SEEKDB_TEST_PASSWORD", ""),
        host=args.host, port=args.port, connect_timeout=10,
        read_timeout=70, write_timeout=70,
    )
    if args.socket:
        CONNECTION_OPTIONS["unix_socket"] = args.socket
    unittest.main(argv=[__file__] + remaining, verbosity=2)
