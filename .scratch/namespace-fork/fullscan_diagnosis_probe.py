#!/usr/bin/env python3
"""Disposable parent/child full scan diagnosis; JSONL evidence, no production changes."""
import argparse
import json
import resource
import subprocess
import sys
import time
from pathlib import Path
import pymysql
from fork_parent_truncate_probe import BootstrapExperiment
from fork_service_latency_probe import query


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True)
    parser.add_argument('--partitions', type=int, default=8000)
    parser.add_argument('--hold', type=int, default=0)
    parser.add_argument('--scaling', action='store_true', help='also scan 256 partitions of differently sized tables')
    parser.add_argument('--max-amplification', type=float,
                        help='optional scaling budget for 8000 vs 256 total partitions, with 256 scanned')
    parser.add_argument('--max-scan-seconds', type=float,
                        help='optional performance budget; raises on a slow full scan')
    args = parser.parse_args()
    if args.scaling and args.partitions != 8000:
        parser.error('--scaling requires --partitions 8000')
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    exp = BootstrapExperiment(args.binary, 'fullscan_diagnosis', prototype=6)
    exp.extra_parameters = [('memory_budget', '8G'), ('datafile_size', '512M'),
                            ('datafile_maxsize', '2G'), ('ob_compaction_schedule_interval', '5m')]
    child = None
    full_scan_seconds = []
    evidence = Path(__file__).with_name('fullscan-diagnosis')
    evidence.mkdir(exist_ok=True)
    try:
        exp.start()
        exp.connection._read_timeout = 300
        exp.sql('SET ob_query_timeout=300000000')
        exp.sql('SET ob_trx_timeout=300000000')
        exp.sql('CREATE DATABASE scan_diag')
        exp.sql(f'CREATE TABLE scan_diag.t(id INT PRIMARY KEY,v INT) PARTITION BY HASH(id) PARTITIONS {args.partitions}')
        with exp.connection.cursor() as cur:
            for offset in range(0, args.partitions, 500):
                cur.executemany('INSERT INTO scan_diag.t VALUES(%s,%s)',
                    [(i, 10) for i in range(offset, min(offset + 500, args.partitions))])
        exp.sql('FORK NAMESPACE scan_child FROM ns1')
        child = pymysql.connect(host='127.0.0.1', port=exp.port, user='root@scan_child',
                                autocommit=True, read_timeout=300)
        query(child, 'SET ob_query_timeout=300000000')
        state = dict(pid=exp.proc.pid, port=exp.port, base=str(exp.base), partitions=args.partitions)
        (evidence / 'live.json').write_text(json.dumps(state))
        exp.record('fixture_ready', **state)
        for name, conn in [('parent', exp.connection), ('child', child)]:
            exp.record('explain', namespace=name, plan=query(conn, 'EXPLAIN SELECT COUNT(*),SUM(v) FROM scan_diag.t'))
        for count in sorted({n for n in (64, 256, 1024, args.partitions) if n <= args.partitions}):
            suffix = '' if count == args.partitions else ' PARTITION(' + ','.join('p'+str(i) for i in range(count)) + ')'
            sql = 'SELECT /* scan_diag_' + str(count) + ' */ COUNT(*),SUM(v) FROM scan_diag.t' + suffix
            for name, conn in [('parent', exp.connection), ('child', child)]:
                exp.record('scan_begin', namespace=name, partitions=count)
                begin = time.perf_counter()
                rows = query(conn, sql)
                elapsed = time.perf_counter() - begin
                assert rows[0][1] == rows[0][0] * 10, rows
                if count == args.partitions:
                    assert rows[0][0] == args.partitions, rows
                    full_scan_seconds.append(elapsed)
                exp.record('scan_end', namespace=name, partitions=count, elapsed_s=elapsed, rows=rows,
                           slow=elapsed > max(1.0, count * .001))
        exp.record('PASS', case='fullscan_result_correctness', full_scan_seconds=full_scan_seconds)
        if args.scaling:
            command = [sys.executable, str(evidence / 'scaling.py'), '--port', str(exp.port)]
            if args.max_amplification is not None:
                command.extend(['--max-amplification', str(args.max_amplification)])
            subprocess.run(command, check=True)
            exp.record('PASS', case='fullscan_schema_scaling')
        if args.hold:
            exp.record('hold_for_diagnostics', seconds=args.hold)
            time.sleep(args.hold)
        if args.max_scan_seconds is not None:
            assert max(full_scan_seconds) <= args.max_scan_seconds, full_scan_seconds
    finally:
        if child is not None: child.close()
        exp.close()


if __name__ == '__main__':
    main()
