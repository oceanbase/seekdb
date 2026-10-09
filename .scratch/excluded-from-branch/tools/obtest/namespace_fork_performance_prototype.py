#!/usr/bin/env python3
"""PROTOTYPE: measure root capture, cold first access and hot access independently.

SEEKDB_FORK_PROTOTYPE_TEST_ROOT=/tmp python3 tools/obtest/namespace_fork_performance_prototype.py --binary build_release/src/observer/seekdb
Small real-engine benchmark, not a production throughput or capacity claim.
"""
import argparse
import hashlib
import math
from pathlib import Path
import statistics
import time
import threading
from concurrent.futures import ThreadPoolExecutor

import pymysql

from namespace_lineage_prototype import LineageExperiment


class PerformanceExperiment(LineageExperiment):
    def start(self):
        super().start()
        self.sql('ALTER SYSTEM SET max_syslog_file_count=0')

    def sample(self, source, label, repeats, during_gc=False):
        measurements = []
        for i in range(repeats):
            name = label + '_probe_' + str(i)
            pages = self.sql("SELECT COUNT(*) FROM __fork_proto_meta.pages", log=False)[0][0]
            physical = self.physical()
            start = time.perf_counter()
            self.sql('FORK DATABASE ' + source + ' TO ' + name, log=False)
            fork_ms = (time.perf_counter() - start) * 1000
            ns = self.root(name)[0]
            if not during_gc:
                assert self.sql("SELECT COUNT(*) FROM __fork_proto_meta.pages", log=False)[0][0] == pages
            else:
                parent, child = self.root(source), self.root(name)
                assert parent[2] == child[2] and parent[4] == child[4]
            assert self.physical() == physical
            query = 'SELECT id,v FROM ' + self.table(ns, 'db1.t1') + ' ORDER BY id'
            start = time.perf_counter()
            assert self.sql(query, log=False) == ((1,10),(2,20))
            cold_ms = (time.perf_counter() - start) * 1000
            start = time.perf_counter()
            assert self.sql(query, log=False) == ((1,10),(2,20))
            hot_ms = (time.perf_counter() - start) * 1000
            measurements.append((fork_ms,cold_ms,hot_ms))
            self.drop(name)
        metrics = {}
        for n,key in enumerate(('fork','cold_first_query','hot_query')):
            values = sorted(row[n] for row in measurements)
            metrics[key] = dict(median_ms=statistics.median(values),
                                p95_ms=values[math.ceil(len(values)*0.95)-1])
        self.record('performance', label=label, samples=repeats, metrics=metrics,
                    raw_ms=measurements, snapshots=len(self.graph()))

    def sample_with_gc(self, repeats):
        stop = threading.Event()
        def collect():
            con = self.connect()
            times, conflicts = [],0
            try:
                while not stop.is_set():
                    start = time.perf_counter()
                    try:
                        self.sql('FORK DATABASE __gc__ TO __gc__',con,log=False)
                        times.append((time.perf_counter()-start)*1000)
                    except pymysql.MySQLError as error:
                        assert error.args[0] in (1205,4012), error.args
                        conflicts += 1
                    stop.wait(.05)
                return times,conflicts
            finally:
                con.close()
        with ThreadPoolExecutor(max_workers=1) as pool:
            gc = pool.submit(collect)
            try:
                self.sample('b','tables_10_during_gc',repeats,during_gc=True)
            finally:
                stop.set()
            times,conflicts = gc.result(timeout=35)
            assert times
            self.record('concurrent_gc_cost',completed=len(times),retryable_conflicts=conflicts,raw_ms=times)

    def run_performance(self, tables, repeats, with_gc):
        self.setup_lineage(tables=tables//2)
        self.sample('b', 'tables_'+str(tables), repeats)
        if tables != 10:
            return
        if with_gc:
            self.sample_with_gc(repeats)
            return
        siblings = []
        for count in (16,64):
            while len(siblings) < count:
                name = 'sibling_' + str(len(siblings))
                self.sql('FORK DATABASE b TO ' + name, log=False)
                siblings.append(name)
            self.sample('b', 'siblings_'+str(count), repeats)
        for name in siblings:
            self.drop(name)
        parent, depth = 'b', 1
        for target_depth in (8,32):
            while depth < target_depth:
                child = 'depth_' + str(depth+1)
                self.sql('FORK DATABASE ' + parent + ' TO ' + child, log=False)
                parent,depth = child,depth+1
            self.sample(parent, 'parent_depth_'+str(depth), repeats)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True)
    parser.add_argument('--mode', type=int, choices=(5,6), default=5)
    parser.add_argument('--samples', type=int, default=20)
    parser.add_argument('--with-gc', action='store_true')
    parser.add_argument('--tables', type=int, choices=(10,100), nargs='+', default=(10,100))
    args = parser.parse_args()
    assert args.samples > 0
    assert not args.with_gc or args.mode == 6
    digest = hashlib.sha256()
    with Path(args.binary).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024*1024), b''):
            digest.update(chunk)
    for tables in args.tables:
        exp = PerformanceExperiment(args.binary, 'performance_v9_'+str(tables), prototype=args.mode)
        try:
            exp.record('benchmark_binary', sha256=digest.hexdigest(), mode=args.mode)
            exp.start()
            exp.run_performance(tables, args.samples, args.with_gc)
            exp.record('PASS', case='namespace_performance', tables=tables)
        except BaseException as error:
            exp.record('FAIL', error=repr(error), base=exp.base)
            raise
        finally:
            exp.close()


if __name__ == '__main__':
    main()
