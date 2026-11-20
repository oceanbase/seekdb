import time
import numpy as np
import pymysql

rng = np.random.default_rng(20262003)
vectors = rng.normal(size=(20000,128)).astype(np.float32)
vectors /= np.linalg.norm(vectors, axis=1)[:,None]
queries = rng.normal(size=(12,128)).astype(np.float32)
queries /= np.linalg.norm(queries, axis=1)[:,None]
def fmt(v): return '[' + ','.join(format(float(x), '.7g') for x in v) + ']'
c = pymysql.connect(host='127.0.0.1', port=2881, user='root', autocommit=True, read_timeout=600, write_timeout=600)
with c.cursor() as q:
    q.execute('CREATE DATABASE IF NOT EXISTS codex_1471_repro')
    q.execute('USE codex_1471_repro')
    q.execute('CREATE TABLE IF NOT EXISTS vectors (id INT PRIMARY KEY, category5 INT NOT NULL, embedding VECTOR(128) NOT NULL) ORGANIZATION HEAP')
    q.execute('SELECT COUNT(*) FROM vectors')
    count=q.fetchone()[0]
    if count not in (0,20000): raise RuntimeError('Unexpected row count: %s' % count)
    if count == 0:
        for start in range(0, 20000, 200):
            q.executemany('INSERT INTO vectors (id,category5,embedding) VALUES (%s,%s,%s)', [(i,i%5+1,fmt(vectors[i])) for i in range(start,start+200)])
            if start % 2000 == 0: print('loaded',start+200,flush=True)
    q.execute('SHOW INDEX FROM vectors')
    if not any(row[2]=='v_idx' for row in q.fetchall()):
        q.execute('CREATE VECTOR INDEX v_idx ON vectors(embedding) WITH (distance=cosine,type=hnsw,lib=vsag,m=16,ef_construction=128,sync_mode=immediate)')
    q.execute('ANALYZE TABLE vectors')
    q.execute('SET ob_hnsw_ef_search=16')
    a=[]; b=[]
    for v in queries:
        vec=fmt(v)
        for k in range(14):
            for filt, out in ([(False,a),(True,b)] if k%2==0 else [(True,b),(False,a)]):
                sql='SELECT id FROM vectors '+('WHERE category5 BETWEEN 1 AND 5 ' if filt else '')+"ORDER BY cosine_distance(embedding, %s) APPROXIMATE LIMIT 10"
                start=time.perf_counter(); q.execute(sql,(vec,)); q.fetchall(); elapsed=(time.perf_counter()-start)*1000
                if k>=4: out.append(elapsed)
    print('p50 ms unfiltered=',np.median(a),'filtered=',np.median(b),'ratio=',np.median(b)/np.median(a),flush=True)
c.close()

