# Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
"""Real-server regressions for duplicate-heavy membership rewrites (#1512)."""
import json
import math
import os
from pathlib import Path
import random
import uuid

import pytest

pymysql = pytest.importorskip('pymysql')
pytestmark = pytest.mark.skipif('SEEKDB_INLIST_TEST_CONFIG' not in os.environ,
    reason='requires a disposable seekdb server selected explicitly')


@pytest.fixture(scope='module')
def connection():
    config = json.loads(Path(os.environ['SEEKDB_INLIST_TEST_CONFIG']).read_text())
    socket = Path(config['unix_socket']).resolve()
    assert socket.parent.parent.name.startswith(('seekdb-pr-', 'seekdb-fts-'))
    db = 'pr1512_' + uuid.uuid4().hex[:12]
    connection = pymysql.connect(user=config['user'], password=config['password'],
        unix_socket=str(socket), charset='utf8mb4', autocommit=True, read_timeout=120)
    try:
        with connection.cursor() as cursor:
            cursor.execute('SET ob_query_timeout=120000000')
            cursor.execute('SET ob_trx_timeout=120000000')
            cursor.execute(f'CREATE DATABASE {db}')
            cursor.execute(f'USE {db}')
            cursor.execute('CREATE TABLE t(id INT PRIMARY KEY,n BIGINT,s VARCHAR(20))')
            values = [None, -(2**63), -1, 0, 1, 2, 3, 4, 5, 2**63-1]
            cursor.executemany('INSERT INTO t VALUES(%s,%s,%s)',
                               [(i, value, str(value)) for i, value in enumerate(values)])
        yield connection
    finally:
        with connection.cursor() as cursor:
            cursor.execute(f'DROP DATABASE IF EXISTS {db}')
        connection.close()


def sql(predicate, threshold=4):
    return f"SELECT /*+ opt_param('inlist_rewrite_threshold',{threshold}) */ id FROM t WHERE {predicate}"


def run(connection, query):
    with connection.cursor() as cursor:
        cursor.execute(query)
        return cursor.fetchall()


def explain(connection, query):
    return '\n'.join(str(value) for row in run(connection, 'EXPLAIN ' + query) for value in row)


def test_duplicate_heavy_auto_parameters_keep_inline_plan(connection):
    values = [1, 2, 3] * 1024
    random.Random(1024).shuffle(values)
    expanded = 'n IN (' + ','.join(map(str, values)) + ')'
    compact = 'n IN (1,2,3)'
    assert run(connection, sql(expanded) + ' ORDER BY id') == run(connection, sql(compact) + ' ORDER BY id')
    assert 'VALUES TABLE ACCESS' not in explain(connection, sql(expanded))


def test_distinct_threshold_and_forced_rewrite_still_apply(connection):
    assert 'VALUES TABLE ACCESS' in explain(connection, sql('n IN (0,1,2,3,4,5,6,7)'))
    assert 'VALUES TABLE ACCESS' in explain(connection, sql('n IN (1,2,1,2,1,2)', threshold=1))


@pytest.mark.parametrize('compact,expanded', [
    ('n IN (1,2,NULL)', 'n IN (1,2,NULL,1,2,NULL)'),
    ('n NOT IN (1,2,NULL)', 'n NOT IN (1,2,NULL,1,2,NULL)'),
    ('n NOT IN (1,2)', 'n NOT IN (1,2,1,2,1,2)'),
    ('n IN (-9223372036854775808,9223372036854775807)',
     'n IN (-9223372036854775808,9223372036854775807,-9223372036854775808,9223372036854775807)'),
    ("s IN ('1','2')", "s IN ('1','2','1','2','1','2')"),
    ("n IN (1,'2')", "n IN (1,'2',1,'2',1,'2')"),
    ('(id,n) IN ((4,1),(5,2))', '(id,n) IN ((4,1),(5,2),(4,1),(5,2))'),
])
def test_null_negation_boundaries_and_unsupported_types(connection, compact, expanded):
    assert run(connection, sql(compact) + ' ORDER BY id') == run(connection, sql(expanded) + ' ORDER BY id')


def test_prepared_statement_keeps_all_parameter_positions(connection):
    with connection.cursor() as cursor:
        cursor.execute('PREPARE inlist_stmt FROM %s', (sql('n IN (?,?,?,?,?,?,?,?)') + ' ORDER BY id',))
        try:
            for values in ([1] * 8, list(range(8)), [2] * 8, [None] * 8):
                cursor.execute('SET ' + ','.join(f'@p{i}=%s' for i in range(8)), values)
                cursor.execute('EXECUTE inlist_stmt USING ' + ','.join(f'@p{i}' for i in range(8)))
                actual = cursor.fetchall()
                cursor.execute('SELECT id FROM t WHERE n IN (' + ','.join(['%s']*8) + ') ORDER BY id', values)
                assert actual == cursor.fetchall()
        finally:
            cursor.execute('DEALLOCATE PREPARE inlist_stmt')


def test_ann_plan_and_membership_equivalence(connection):
    rng = random.Random(20261002)
    rows = []
    for i in range(2000):
        vector = [rng.gauss(0, 1) for _ in range(4)]
        norm = math.sqrt(sum(v*v for v in vector))
        rows.append((i, i % 5, json.dumps([v/norm for v in vector])))
    with connection.cursor() as cursor:
        cursor.execute('CREATE TABLE ann(id BIGINT PRIMARY KEY,category5 INT NOT NULL,embedding VECTOR(4)) ORGANIZATION HEAP')
        cursor.executemany('INSERT INTO ann VALUES(%s,%s,%s)', rows)
        cursor.execute('CREATE VECTOR INDEX ann_hnsw ON ann(embedding) '
                       'WITH(distance=l2,type=hnsw,lib=vsag,m=16,ef_construction=128,sync_mode=immediate)')
        cursor.execute('ANALYZE TABLE ann')
        cursor.fetchall()
        cursor.execute('SET ob_hnsw_ef_search=16')
    values = [1, 2, 3] * 1024
    random.Random(1024).shuffle(values)
    compact = 'category5 IN (1,2,3)'
    expanded = 'category5 IN (' + ','.join(map(str, values)) + ')'
    reference_ids = run(connection, 'SELECT id FROM ann WHERE ' + compact + ' ORDER BY id')
    assert len(reference_ids) == 1200
    assert run(connection, 'SELECT id FROM ann WHERE ' + expanded + ' ORDER BY id') == reference_ids
    query = "SELECT id FROM ann WHERE {} ORDER BY l2_distance(embedding,'[0.1,0.2,0.3,0.4]') {} LIMIT 10"
    for approximate in ('', 'APPROXIMATE'):
        short = query.format(compact, approximate)
        long = query.format(expanded, approximate)
        assert run(connection, short) == run(connection, long)
        if approximate:
            plan = explain(connection, long)
            assert 'VECTOR INDEX' in plan
            assert 'VALUES TABLE ACCESS' not in plan
