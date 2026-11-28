"""ANN integer-negation SQL integration regression (requires PyMySQL).

Run this script against a running seekdb. Connection settings use
SEEKDB_TEST_HOST/PORT/USER/PASSWORD (defaults: localhost:2881/root).
Creates and drops a unique test database; does not modify existing data.
"""
import os
import unittest
import uuid
import pymysql


class AnnIntegerNegationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.connection = pymysql.connect(host=os.getenv('SEEKDB_TEST_HOST','127.0.0.1'),
            port=int(os.getenv('SEEKDB_TEST_PORT','2881')),
            user=os.getenv('SEEKDB_TEST_USER','root'),
            password=os.getenv('SEEKDB_TEST_PASSWORD',''),
            autocommit=True,read_timeout=180,charset='utf8mb4')
        cls.cursor = cls.connection.cursor()
        cls.database = 'test_ann_negation_'+uuid.uuid4().hex
        cls.cursor.execute('CREATE DATABASE '+cls.database)
        cls.cursor.execute('USE '+cls.database)
        cls.cursor.execute('SET ob_query_timeout=180000000')
        cls.cursor.execute('SET ob_hnsw_ef_search=1000')
        cls.cursor.execute('''CREATE TABLE t (
            id BIGINT PRIMARY KEY, embedding VECTOR(16) NOT NULL,
            category INT, boundary BIGINT, unsigned_boundary BIGINT UNSIGNED,
            label VARCHAR(8) COLLATE utf8mb4_general_ci)''')
        records = []
        for i in range(180):
            vector = '['+','.join(str((i+1)*(j+1)/10000) for j in range(16))+']'
            records.append((i,vector, None if i%6==0 else i%5+1,
                [-(2**63),2**63-1,0][i%3], [0,2**64-1,2**63][i%3],
                ['a','A','b',None][i%4]))
        cls.cursor.executemany('INSERT INTO t VALUES (%s,%s,%s,%s,%s,%s)',records)
        cls.cursor.execute('''CREATE VECTOR INDEX ann_idx ON t(embedding)
            WITH (DISTANCE=L2,TYPE=HNSW,LIB=VSAG,M=16,EF_CONSTRUCTION=128,SYNC_MODE=IMMEDIATE)''')
        cls.cursor.execute('ANALYZE TABLE t')
        cls.cursor.fetchall()
        cls.vector = "'["+','.join(['0']*16)+"]'"

    @classmethod
    def tearDownClass(cls):
        cls.cursor.execute('DROP DATABASE '+cls.database)
        cls.connection.close()

    def check_condition(self, condition):
        self.cursor.execute('SELECT id FROM t WHERE '+condition+' ORDER BY id')
        expected = [int(row[0]) for row in self.cursor.fetchall()]
        sql = ('SELECT /*+ INDEX(e PRIMARY) */ id FROM t e WHERE '+condition+
               ' ORDER BY l2_distance(embedding,'+self.vector+') APPROXIMATE LIMIT 180')
        self.cursor.execute(sql)
        actual = [int(row[0]) for row in self.cursor.fetchall()]
        self.assertEqual(sorted(actual),expected,condition)

    def test_integer_conditions(self):
        conditions = [
            'NOT(category=1)', 'NOT(1=category)',
            'NOT(category=1) AND NOT(category=2)',
            '(NOT(category=1) AND (NOT(category=2) AND NOT(category=3)))',
            'NOT(category=1) AND NOT(category=1)',
            'NOT(category=NULL)',
            'NOT(category=1) AND category IS NULL',
            'NOT(boundary=-9223372036854775808)',
            'NOT(boundary=9223372036854775807)',
            'NOT(unsigned_boundary=18446744073709551615)',
            'NOT(unsigned_boundary=-1)',
        ]
        for condition in conditions:
            with self.subTest(condition=condition):
                self.check_condition(condition)

    def test_fallback_conditions(self):
        for condition in [
            "NOT(label='a')", 'NOT(category=1.5)',
            'NOT(category=1 OR category=2)', 'NOT(category <=> 1)',
            'NOT(category=boundary)', 'NOT(category IN (1,2,NULL))',
            "NOT(category=1) AND NOT(label='a')",
            'NOT(category=1) OR category IS NULL',
        ]:
            with self.subTest(condition=condition):
                self.check_condition(condition)

    def test_prepared_parameters(self):
        sql = ('SELECT /*+ INDEX(e PRIMARY) */ id FROM t e WHERE NOT(category=?) '
               'ORDER BY l2_distance(embedding,'+self.vector+') APPROXIMATE LIMIT 180')
        self.cursor.execute('PREPARE negation_stmt FROM '+self.connection.escape(sql))
        try:
            for value in [1,2,None,'a']:
                with self.subTest(value=value):
                    self.cursor.execute('SET @negation_value=%s',(value,))
                    self.cursor.execute('SELECT id FROM t WHERE NOT(category=@negation_value) ORDER BY id')
                    expected = [int(row[0]) for row in self.cursor.fetchall()]
                    self.cursor.execute('EXECUTE negation_stmt USING @negation_value')
                    self.assertEqual(sorted(int(row[0]) for row in self.cursor.fetchall()),expected)
        finally:
            self.cursor.execute('DEALLOCATE PREPARE negation_stmt')

    def test_expected_plan_shape(self):
        sql = ('SELECT /*+ INDEX(e PRIMARY) */ id FROM t e WHERE NOT(category=1) '
               'ORDER BY l2_distance(embedding,'+self.vector+') APPROXIMATE LIMIT 10')
        self.cursor.execute('EXPLAIN '+sql)
        plan = '\n'.join(str(row[0]) for row in self.cursor.fetchall())
        if os.getenv('SEEKDB_EXPECT_NEGATION_REWRITE','1')=='1':
            self.assertNotIn('T_OP_NOT,',plan)
            self.assertIn('!= 1',plan)
            self.cursor.execute('EXPLAIN '+sql.replace('INDEX(e PRIMARY)', 'INDEX(e PRIMARY) NO_REWRITE'))
            disabled_plan = '\n'.join(str(row[0]) for row in self.cursor.fetchall())
            self.assertIn('T_OP_NOT,',disabled_plan)
            for condition in ["NOT(label='a')", 'NOT(category=1 OR category=2)']:
                with self.subTest(condition=condition):
                    fallback_sql = sql.replace('NOT(category=1)',condition)
                    self.cursor.execute('EXPLAIN '+fallback_sql)
                    fallback_plan = '\n'.join(str(row[0]) for row in self.cursor.fetchall())
                    self.assertIn('T_OP_NOT,',fallback_plan)

    def test_multiple_exclusions(self):
        cases = [
            'category<>1 AND category<>2 AND category<>3',
            'NOT(category=1) AND NOT(category=2) AND NOT(category=3)',
            '1<>category AND 2<>category AND 3<>category',
            'category<>1 AND category<>1 AND category<>1',
            'category<>1 AND category<>2 AND category<>NULL',
            'category<>1 AND category<>2 AND category<>3 AND category IS NULL',
            '(category<>1 AND (category<>2 AND category<>3))',
            'category<>1 AND category<>2 AND category<>3 AND boundary<>0',
            'category<>1 AND category<>2 AND category<>3 AND boundary<>1 AND boundary<>2 AND boundary<>3',
            "category<>1 AND category<>2 AND category<>3 AND label<>'a'",
            'category<>1 AND category<>2 AND category<>3 AND category<>2.5',
            'boundary<>-9223372036854775808 AND boundary<>0 AND boundary<>9223372036854775807',
            'unsigned_boundary<>0 AND unsigned_boundary<>9223372036854775808 AND unsigned_boundary<>18446744073709551615',
            'unsigned_boundary<>9223372036854775808 AND unsigned_boundary<>9223372036854775809 AND unsigned_boundary<>18446744073709551615',
            'unsigned_boundary<>9223372036854775808 AND unsigned_boundary<>9223372036854775809 AND unsigned_boundary<>NULL',
            'unsigned_boundary<>-1 AND unsigned_boundary<>0 AND unsigned_boundary<>1',
            'category<>1 OR category<>2 OR category<>3',
            'category<>1 AND category<>2 AND category<>boundary',
            ' AND '.join(f'category<>{v}' for v in range(1,33)),
            ' AND '.join(f'category<>{v}' for v in range(1,34)),
        ]
        for condition in cases:
            with self.subTest(condition=condition):
                self.check_condition(condition)

    def test_fusion_plan(self):
        condition = 'category<>1 AND category<>2 AND category<>3'
        sql = ('SELECT /*+ INDEX(e PRIMARY) */ id FROM t e WHERE '+condition+
               ' ORDER BY l2_distance(embedding,'+self.vector+') APPROXIMATE LIMIT 10')
        def plan(statement):
            self.cursor.execute('EXPLAIN '+statement)
            return '\n'.join(str(row[0]) for row in self.cursor.fetchall())
        if os.getenv('SEEKDB_EXPECT_FUSION','1')=='1':
            self.assertIn('T_OP_NOT_IN',plan(sql))
            self.assertIn('T_OP_NOT_IN',plan(sql.replace(condition,
                ' AND '.join(f'category<>{v}' for v in range(1,33)))))
        self.assertNotIn('T_OP_NOT_IN',plan(sql.replace('INDEX(e PRIMARY)','INDEX(e PRIMARY) NO_REWRITE')))
        for unchanged in [
            'category<>1 AND category<>2',
            'category<>1 AND category<>2 AND category<>2.5',
            'category<>1 OR category<>2 OR category<>3',
            ' AND '.join(f'category<>{v}' for v in range(1,34)),
        ]:
            with self.subTest(condition=unchanged):
                self.assertNotIn('T_OP_NOT_IN',plan(sql.replace(condition,unchanged)))

    def test_fused_prepared_parameters(self):
        sql = ('SELECT /*+ INDEX(e PRIMARY) */ id FROM t e '
               'WHERE category<>? AND category<>? AND category<>? '
               'ORDER BY l2_distance(embedding,'+self.vector+') APPROXIMATE LIMIT 180')
        self.cursor.execute('PREPARE fusion_stmt FROM '+self.connection.escape(sql))
        try:
            for values in [(1,2,3), (None,2,3), ('1',2,3), ('a',2,3),
                           (1.5,2.5,3.5), (2**64-1,1,2), (1,2,3)]:
                with self.subTest(values=values):
                    self.cursor.execute('SET @f1=%s,@f2=%s,@f3=%s',values)
                    self.cursor.execute('SELECT id FROM t WHERE category<>@f1 AND category<>@f2 AND category<>@f3 ORDER BY id')
                    expected = [int(row[0]) for row in self.cursor.fetchall()]
                    self.cursor.execute('EXECUTE fusion_stmt USING @f1,@f2,@f3')
                    self.assertEqual(sorted(int(row[0]) for row in self.cursor.fetchall()),expected)
        finally:
            self.cursor.execute('DEALLOCATE PREPARE fusion_stmt')


if __name__ == '__main__':
    unittest.main(verbosity=2)
