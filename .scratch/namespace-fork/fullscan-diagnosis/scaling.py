"""Control scanned partitions and rows while varying the full table schema size."""
import argparse
import json
import statistics
import sys
import time
from pathlib import Path
import pymysql
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from fork_service_latency_probe import query

root = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--port', type=int, help='ready diagnostic instance; otherwise use live.json')
parser.add_argument('--reuse', action='store_true', help='reuse the prepared diagnostic tables')
parser.add_argument('--max-amplification', type=float)
parser.add_argument('--rounds', type=int, default=3)
args = parser.parse_args()
port = args.port or json.loads((root / 'live.json').read_text())['port']
parent = pymysql.connect(host='127.0.0.1', port=port, user='root',
                         autocommit=True, read_timeout=300)
query(parent, 'SET ob_query_timeout=300000000')
if not args.reuse:
    for count in (256, 1024):
        query(parent, f'CREATE TABLE scan_diag.t{count}(id INT PRIMARY KEY,v INT) PARTITION BY HASH(id) PARTITIONS {count}')
        with parent.cursor() as cur:
            cur.executemany(f'INSERT INTO scan_diag.t{count} VALUES(%s,%s)', [(i, 10) for i in range(count)])
    query(parent, 'FORK NAMESPACE scaling_child FROM ns1')
child = pymysql.connect(host='127.0.0.1', port=port, user='root@scaling_child',
                        autocommit=True, read_timeout=300)
query(child, 'SET ob_query_timeout=300000000')
measurements = []
for repeat in range(args.rounds):
    for count in (256, 1024, 8000):
        table = 't' if count == 8000 else 't' + str(count)
        sql = f'SELECT COUNT(*),SUM(v) FROM scan_diag.{table} PARTITION(' + ','.join(f'p{i}' for i in range(256)) + ')'
        for name, conn in [('parent', parent), ('child', child)]:
            start = time.perf_counter()
            rows = query(conn, sql)
            elapsed = time.perf_counter() - start
            assert tuple(map(int, rows[0])) == (256, 2560), rows
            sample = dict(round=repeat, namespace=name, table_partitions=count,
                scanned_partitions=256, rows=256, elapsed_s=elapsed)
            measurements.append(sample)
            print(json.dumps(sample), flush=True)
child.close()
parent.close()
amplification = {}
for name in ('parent', 'child'):
    def median(count):
        return statistics.median(s['elapsed_s'] for s in measurements
            if s['namespace'] == name and s['table_partitions'] == count)
    amplification[name] = median(8000) / median(256)
print(json.dumps(dict(amplification=amplification, maximum=args.max_amplification)), flush=True)
if args.max_amplification is not None:
    assert max(amplification.values()) <= args.max_amplification, amplification
