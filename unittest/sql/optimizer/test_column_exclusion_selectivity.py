"""SQL integration regression for #1511 (requires PyMySQL and a running seekdb).

Run: python unittest/sql/optimizer/test_column_exclusion_selectivity.py
Connection: SEEKDB_TEST_HOST/PORT/USER/PASSWORD (defaults: localhost:2881/root).
Uses a unique disposable database; never modifies an existing database.
The selectivity assertions fail against the unpatched seekdb 1.4.0 image.
"""
import os
import re
import unittest
import uuid

import pymysql


class ColumnExclusionSelectivityTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.connection = pymysql.connect(
            host=os.getenv('SEEKDB_TEST_HOST', '127.0.0.1'),
            port=int(os.getenv('SEEKDB_TEST_PORT', '2881')),
            user=os.getenv('SEEKDB_TEST_USER', 'root'),
            password=os.getenv('SEEKDB_TEST_PASSWORD', ''),
            charset='utf8mb4', autocommit=True, read_timeout=120,
        )
        cls.cursor = cls.connection.cursor()
        cls.database = 'test_exclusion_' + uuid.uuid4().hex
        cls.cursor.execute('CREATE DATABASE ' + cls.database)
        cls.cursor.execute('USE ' + cls.database)
        cls.cursor.execute('SET ob_query_timeout=120000000')
        cls.cursor.execute('''CREATE TABLE exclusions (
            id INT PRIMARY KEY, category INT NOT NULL, nullable_category INT,
            label VARCHAR(8) COLLATE utf8mb4_general_ci NOT NULL, skew INT NOT NULL)''')
        for start in range(0, 6000, 200):
            cls.cursor.executemany('INSERT INTO exclusions VALUES (%s,%s,%s,%s,%s)', [
                (i, i % 5 + 1, i % 6 + 1 if i % 6 < 5 else None,
                 chr(ord('a') + i % 5), 1 if i % 10 < 6 else 2 if i % 10 < 8 else 3 if i % 10 < 9 else 4)
                for i in range(start, start + 200)
            ])
        cls.cursor.execute('ANALYZE TABLE exclusions')
        cls.cursor.fetchall()
        # Explicit full-sample histograms make the skew and NULL expectations
        # independent of the server's automatic histogram collection policy.
        cls.cursor.execute("CALL DBMS_STATS.GATHER_TABLE_STATS(NULL, 'exclusions', "
                           "method_opt=>'FOR ALL COLUMNS SIZE 254', estimate_percent=>100)")
        cls.cursor.fetchall()

    @classmethod
    def tearDownClass(cls):
        cls.cursor.execute('DROP DATABASE ' + cls.database)
        cls.connection.close()

    def estimated_rows(self, condition):
        self.cursor.execute('EXPLAIN SELECT /*+ NO_REWRITE FULL(e) */ id '
                            'FROM exclusions e WHERE ' + condition)
        plan = '\n'.join(str(row[0]) for row in self.cursor.fetchall())
        # The first operator is the root output cardinality, not its cost.
        match = re.search(r'\|\s*0\s*\|[^|]+\|[^|]*\|\s*([0-9.]+)\s*\|', plan)
        self.assertIsNotNone(match, plan)
        return float(match.group(1))

    def ids(self, condition):
        self.cursor.execute('SELECT id FROM exclusions WHERE ' + condition + ' ORDER BY id')
        return [row[0] for row in self.cursor.fetchall()]

    def assert_equivalent_estimates(self, exclusions, reference):
        expected_ids = self.ids(reference)
        self.assertEqual(self.ids(exclusions), expected_ids)
        expected = self.estimated_rows(reference)
        actual = self.estimated_rows(exclusions)
        tolerance = max(2, expected * .02)
        self.assertAlmostEqual(actual, expected, delta=tolerance)
        self.assertAlmostEqual(actual, len(expected_ids), delta=max(2, len(expected_ids) * .02))

    def test_negated_equalities(self):
        self.assert_equivalent_estimates(
            'NOT(category=1) AND NOT(category=2) AND NOT(category=3) AND NOT(category=4)',
            'NOT(category IN (1,2,3,4))')

    def test_not_equal_and_reversed_operands(self):
        self.assert_equivalent_estimates(
            'category<>1 AND 2<>category AND NOT(3=category) AND category!=4',
            'NOT(category IN (1,2,3,4))')

    def test_nested_conjunction(self):
        self.assert_equivalent_estimates(
            '(NOT(category=1) AND NOT(category=2)) AND (NOT(category=3) AND NOT(category=4))',
            'NOT(category IN (1,2,3,4))')

    def test_skewed_histogram(self):
        self.assert_equivalent_estimates(
            'NOT(skew=1) AND skew<>2', 'NOT(skew IN (1,2))')

    def test_duplicate_exclusions(self):
        self.assert_equivalent_estimates(
            'category<>1 AND NOT(category=1) AND category<>2 AND NOT(2=category)',
            'NOT(category IN (1,2))')

    def test_overlapping_not_in_and_equalities(self):
        self.assert_equivalent_estimates(
            'NOT(category IN (1,2)) AND category NOT IN (2,3) AND NOT(category=4)',
            'NOT(category IN (1,2,3,4))')

    def test_nullable_column(self):
        self.assert_equivalent_estimates(
            'NOT(nullable_category=1) AND nullable_category<>2 AND NOT(nullable_category=3)',
            'NOT(nullable_category IN (1,2,3))')

    def test_exhausted_domain(self):
        self.assertEqual(self.ids('category<>1 AND category<>2 AND category<>3 '
                                 'AND category<>4 AND category<>5'), [])
        self.assertLessEqual(self.estimated_rows(
            'category<>1 AND category<>2 AND category<>3 AND category<>4 AND category<>5'), 2)

    def test_case_insensitive_duplicates(self):
        self.assert_equivalent_estimates(
            "label<>'a' AND NOT(label='A') AND label<>'b'",
            "NOT(label IN ('a','b'))")

    def test_null_exclusion(self):
        self.assertEqual(self.ids('category<>1 AND NOT(category=NULL)'), [])
        self.assertLessEqual(self.estimated_rows('category<>1 AND NOT(category=NULL)'), 2)

    def test_prepared_parameters(self):
        sql = ('EXPLAIN SELECT /*+ NO_REWRITE FULL(e) */ id FROM exclusions e '
               'WHERE NOT(category=?) AND NOT(category=?) AND NOT(category=?) AND NOT(category=?)')
        self.cursor.execute('PREPARE exclusion_plan FROM %s', (sql,))
        try:
            for values in [(1, 2, 3, 4), (1, 1, 2, 2)]:
                for name, value in zip(('a', 'b', 'c', 'd'), values):
                    self.cursor.execute('SET @' + name + '=%s', (value,))
                self.cursor.execute('EXECUTE exclusion_plan USING @a,@b,@c,@d')
                plan = '\n'.join(str(row[0]) for row in self.cursor.fetchall())
                match = re.search(r'\|\s*0\s*\|[^|]+\|[^|]*\|\s*([0-9.]+)\s*\|', plan)
                self.assertIsNotNone(match, plan)
                expected = 6000 * (5 - len(set(values))) / 5
                self.assertAlmostEqual(float(match.group(1)), expected, delta=max(2, expected*.02))
        finally:
            self.cursor.execute('DEALLOCATE PREPARE exclusion_plan')

    def test_unsupported_expressions_keep_execution_semantics(self):
        for condition, reference in [
            ('NOT(category+1=2) AND NOT(category+1=3)', 'NOT(category+1 IN (2,3))'),
            ("category<>'1' AND category<>2.5", "NOT(category IN ('1',2.5))"),
            ('NOT(category=1) OR NOT(category=2)', 'category IS NOT NULL'),
        ]:
            with self.subTest(condition=condition):
                self.assertEqual(self.ids(condition), self.ids(reference))


if __name__ == '__main__':
    unittest.main(verbosity=2)
