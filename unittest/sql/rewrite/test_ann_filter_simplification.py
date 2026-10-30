"""Native SQL regression for #1535; run against an isolated seekdb instance.

Requires PyMySQL. Set SEEKDB_TEST_SOCKET or SEEKDB_TEST_HOST/SEEKDB_TEST_PORT;
SEEKDB_TEST_DATABASE defaults to test. The suite owns ann_absorption_regression.
"""
import json
import os
import random
import unittest

import pymysql


def balanced(expressions):
    if len(expressions) == 1:
        return expressions[0]
    middle = len(expressions) // 2
    return '(' + balanced(expressions[:middle]) + ') AND (' + balanced(expressions[middle:]) + ')'


class ANNFilterSimplificationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        args = dict(user=os.getenv('SEEKDB_TEST_USER', 'root'),
                    password=os.getenv('SEEKDB_TEST_PASSWORD', ''),
                    database=os.getenv('SEEKDB_TEST_DATABASE', 'test'), charset='utf8mb4',
                    autocommit=True, connect_timeout=5, read_timeout=120)
        if os.getenv('SEEKDB_TEST_SOCKET'):
            args['unix_socket'] = os.environ['SEEKDB_TEST_SOCKET']
        else:
            args.update(host=os.getenv('SEEKDB_TEST_HOST', '127.0.0.1'),
                        port=int(os.getenv('SEEKDB_TEST_PORT', '2881')))
        cls.connection = pymysql.connect(**args)
        with cls.connection.cursor() as cursor:
            cursor.execute('SET ob_query_timeout=100000000')
            cursor.execute('SET ob_hnsw_ef_search=64')
            cursor.execute('DROP TABLE IF EXISTS ann_absorption_regression')
            cursor.execute('CREATE TABLE ann_absorption_regression('
                           'id BIGINT PRIMARY KEY,embedding VECTOR(4),category INT,parity VARCHAR(8),'
                           'KEY idx_category(category)) ORGANIZATION HEAP')
            rng = random.Random(1535)
            for start in range(0, 10000, 500):
                rows = [(i, json.dumps([rng.uniform(-1, 1) for _ in range(4)]),
                         i % 20 + 1, 'even' if i % 2 == 0 else 'odd')
                        for i in range(start, start + 500)]
                cursor.executemany('INSERT INTO ann_absorption_regression VALUES(%s,%s,%s,%s)', rows)
            cursor.execute("INSERT INTO ann_absorption_regression VALUES"
                           "(10000,'[0,0,0,0]',NULL,'even'),(10001,'[1,1,1,1]',NULL,NULL),"
                           "(10002,'[0.1,0.2,0.3,0.4]',1,NULL)")
            cursor.execute('CREATE VECTOR INDEX idx_ann ON ann_absorption_regression(embedding) '
                           'WITH(distance=l2,type=hnsw,lib=vsag,m=16,ef_construction=64,sync_mode=immediate)')
            cursor.execute('ANALYZE TABLE ann_absorption_regression')
            cursor.fetchall()

    @classmethod
    def tearDownClass(cls):
        with cls.connection.cursor() as cursor:
            cursor.execute('DROP TABLE ann_absorption_regression')
        cls.connection.close()

    def ids(self, condition, parameters=(), approximate=False):
        suffix = (" ORDER BY l2_distance(embedding,'[0.1,0.2,0.3,0.4]') APPROXIMATE LIMIT 10"
                  if approximate else ' ORDER BY id')
        with self.connection.cursor() as cursor:
            cursor.execute('SELECT id FROM ann_absorption_regression WHERE ' + condition + suffix, parameters)
            return [row[0] for row in cursor.fetchall()]

    def test_balanced_absorption_is_removed_before_ann_filter(self):
        simple = 'category=1'
        expanded = balanced([simple] + ["(category=1 OR parity='even')"] * 1024)
        eligible = self.ids(simple)
        self.assertEqual(eligible, self.ids(expanded))
        self.assertEqual(self.ids(simple, approximate=True), self.ids(expanded, approximate=True))
        with self.connection.cursor() as cursor:
            cursor.execute('EXPLAIN SELECT id FROM ann_absorption_regression WHERE ' + expanded +
                           " ORDER BY l2_distance(embedding,'[0.1,0.2,0.3,0.4]') APPROXIMATE LIMIT 10")
            plan = '\n'.join(str(row[0]) for row in cursor.fetchall())
        self.assertIn('VECTOR INDEX', plan)
        self.assertFalse('parity' in plan, 'ANN pre-filter retains the redundant OR predicate')

    def test_changed_parameter_values_do_not_reuse_absorption_assumptions(self):
        condition = 'category=%s AND (category=%s OR parity=%s)'
        for parameters in [(1, 1, 'even'), (1, 2, 'odd'), (2, 2, 'even'),
                           (2, 1, 'even'), (1, 1, 'even')]:
            with self.subTest(parameters=parameters):
                eligible = self.ids(condition, parameters)
                actual = self.ids(condition, parameters, approximate=True)
                self.assertEqual(min(10, len(eligible)), len(actual))
                self.assertTrue(set(actual).issubset(eligible))

    def test_absorption_preserves_null_and_non_absorbed_conditions(self):
        for condition in [
            "category=1 AND (category=1 OR parity='even')",
            "category=1 AND (category=2 OR parity='even')",
            "category IS NULL AND (category IS NULL OR parity='odd')",
            "category IS NULL AND (category=2 OR parity='even')",
            '(category=1 OR category IS NULL) AND (category=1 OR category IS NULL OR parity IS NULL)',
        ]:
            with self.subTest(condition=condition):
                eligible = self.ids(condition)
                actual = self.ids(condition, approximate=True)
                self.assertEqual(min(10, len(eligible)), len(actual))
                self.assertTrue(set(actual).issubset(eligible))


if __name__ == '__main__':
    unittest.main()
