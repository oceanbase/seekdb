"""Reproduce duplicate-IN vector plan overhead on a real disposable database.

Requires numpy and pymysql. Uses the dimensions, row count, filter, and balanced
query schedule from issue #1512. Synthetic metadata/vector generation is defined
here because the issue does not publish its complete generator. This is an
independent reproduction, not the reporter's exact data or hardware benchmark.
"""
import argparse
import hashlib
import json
import random
import statistics
import time
import uuid
from pathlib import Path

import numpy as np
import pymysql

p = argparse.ArgumentParser()
p.add_argument('--port', type=int, default=2881)
p.add_argument('--rows', type=int, default=200000)
p.add_argument('--expect-compact', action='store_true')
p.add_argument('--output', required=True)
a = p.parse_args()
assert a.rows >= 1000
conn = pymysql.connect(host='127.0.0.1', port=a.port, user='root',
                       autocommit=True, read_timeout=900, write_timeout=900)
cur = conn.cursor()
db = 'inlist_ann_' + uuid.uuid4().hex[:12]
report = {'rows': a.rows, 'dim': 128, 'data_seed': 20261002,
          'query_seed': 20261003, 'warmup_pairs': 4, 'measured_pairs': 12,
          'queries': [], 'status': 'running'}

def run(sql):
    cur.execute(sql)
    return cur.fetchall()

def vector(v):
    return '[' + ','.join(format(float(x), '.8g') for x in v) + ']'

def unit(v):
    return v / np.linalg.norm(v, axis=-1, keepdims=True)

def ids(q):
    return [int(r[0]) for r in run(q)]

run('CREATE DATABASE ' + db)
try:
    run('USE ' + db)
    run('SET ob_query_timeout=600000000')
    run('SET ob_trx_timeout=600000000')
    run('SET ob_hnsw_ef_search=16')
    run('CREATE TABLE docs (id BIGINT PRIMARY KEY, embedding VECTOR(128), '
        'tenant INT NOT NULL, level INT NOT NULL, category INT NOT NULL, '
        'category5 INT NOT NULL, age INT NOT NULL, parity INT NOT NULL, '
        'KEY tenant_idx(tenant), KEY category5_idx(category5))')
    rng = np.random.default_rng(20261002)
    centers = unit(rng.normal(size=(100, 128))).astype(np.float32)
    tenants = rng.integers(0, 100, a.rows)
    levels = rng.integers(1, 6, a.rows)
    categories = rng.integers(1, 21, a.rows)
    ages = rng.integers(18, 81, a.rows)
    started = time.perf_counter()
    digest = hashlib.sha256()
    for start in range(0, a.rows, 200):
        end = min(a.rows, start + 200)
        noise = unit(rng.normal(size=(end-start, 128))).astype(np.float32)
        embeddings = unit(.85 * centers[tenants[start:end]] + np.sqrt(1-.85**2) * noise)
        rows = [(i, vector(embeddings[i-start]), int(tenants[i]), int(levels[i]),
                 int(categories[i]), int(tenants[i] % 5 + 1), int(ages[i]), i % 2)
                for i in range(start, end)]
        digest.update(json.dumps(rows, separators=(',', ':')).encode())
        cur.executemany('INSERT INTO docs VALUES (%s,%s,%s,%s,%s,%s,%s,%s)', rows)
    report['load_seconds'] = time.perf_counter() - started
    report['dataset_sha256'] = digest.hexdigest()
    started = time.perf_counter()
    run('CREATE VECTOR INDEX docs_hnsw ON docs(embedding) WITH '
        '(distance=l2,type=hnsw,lib=vsag,m=16,ef_construction=128,sync_mode=immediate)')
    report['index_seconds'] = time.perf_counter() - started
    run('ANALYZE TABLE docs')
    repeated = [1, 2, 3] * 1024
    random.Random(1024).shuffle(repeated)
    filters = {'compact': 'category5 IN (1,2,3)',
               'duplicate': 'category5 IN (' + ','.join(map(str, repeated)) + ')'}
    eligible = {name: ids('SELECT id FROM docs WHERE ' + f + ' ORDER BY id')
                for name, f in filters.items()}
    assert eligible['compact'] == eligible['duplicate'], 'Eligible IDs differ'
    report['eligible_count'] = len(eligible['compact'])
    report['eligible_sha256'] = hashlib.sha256(json.dumps(eligible['compact']).encode()).hexdigest()
    eligible_set = set(eligible['compact'])
    query_rng = np.random.default_rng(20261003)
    queries = unit(query_rng.normal(size=(8, 128))).astype(np.float32)
    timings = {name: [] for name in filters}
    for index, v in enumerate(queries):
        literal = vector(v)
        sqls = {name: "SELECT id FROM docs WHERE " + f +
                " ORDER BY l2_distance(embedding,'" + literal + "') APPROXIMATE LIMIT 10"
                for name, f in filters.items()}
        plans = {name: '\n'.join(' '.join(str(c) for c in row) for row in run('EXPLAIN ' + q))
                 for name, q in sqls.items()}
        if a.expect_compact:
            assert 'VALUES TABLE ACCESS' not in plans['duplicate'], 'Duplicate domain materialized'
        exact = {name: ids(q.replace(' APPROXIMATE', '')) for name, q in sqls.items()}
        assert exact['compact'] == exact['duplicate'], 'Exact Top-K changed'
        per_query = {'index': index, 'plans': plans, 'exact_ids': exact['compact'],
                     'recall': {}, 'last_ann_ids': {}}
        report['queries'].append(per_query)
        for pair in range(16):
            order = ('compact', 'duplicate') if (pair + index) % 2 == 0 else ('duplicate', 'compact')
            for name in order:
                start = time.perf_counter_ns()
                result = ids(sqls[name])
                elapsed = (time.perf_counter_ns() - start) / 1e6
                per_query['last_ann_ids'][name] = result
                assert len(result) <= 10 and len(set(result)) == len(result)
                # The unpatched duplicate plan can filter after taking top-k
                # and underfill the result. Preserve that baseline evidence.
                if name == 'compact' or a.expect_compact:
                    assert len(result) == 10, (index, pair, name, result)
                assert all(i in eligible_set for i in result)
                if pair >= 4:
                    timings[name].append(elapsed)
                per_query['last_ann_ids'][name] = result
                per_query['recall'][name] = len(set(result) & set(exact[name])) / 10
    report['timings_ms'] = timings
    report['median_ms'] = {name: statistics.median(t) for name, t in timings.items()}
    report['duplicate_latency_ratio'] = report['median_ms']['duplicate'] / report['median_ms']['compact']
    report['recall_mean'] = {name: statistics.mean(q['recall'][name] for q in report['queries'])
                             for name in filters}
    report['status'] = 'passed'
finally:
    try:
        run('DROP DATABASE ' + db)
    finally:
        conn.close()
        Path(a.output).write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps({k: report[k] for k in ('status','eligible_count','median_ms',
                                       'duplicate_latency_ratio','recall_mean')}))
