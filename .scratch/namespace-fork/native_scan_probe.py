#!/usr/bin/env python3
"""Native scan reuse: mixed inherited/owned partitions, projections, NLJ and LOBs."""
import argparse
from decimal import Decimal
import resource
from fork_parent_truncate_probe import BootstrapExperiment, connect


def run(binary):
    exp = BootstrapExperiment(binary, 'native_scan', prototype=6)
    exp.extra_parameters = [('ob_compaction_schedule_interval', '5m')]
    try:
        exp.start()
        exp.sql('CREATE DATABASE native_scan')
        exp.sql('CREATE TABLE native_scan.t(id INT PRIMARY KEY,v INT,d DECIMAL(12,2),'
                'c CHAR(5),body LONGTEXT) PARTITION BY HASH(id) PARTITIONS 8')
        exp.sql('CREATE INDEX by_v ON native_scan.t(v)')
        exp.sql("INSERT INTO native_scan.t SELECT seq,seq*10,seq+0.25,'abc',"
                "REPEAT(CHAR(65+seq),12000+seq) FROM "
                "(SELECT 0 seq UNION ALL SELECT 1 UNION ALL SELECT 2 UNION ALL SELECT 3 "
                "UNION ALL SELECT 4 UNION ALL SELECT 5 UNION ALL SELECT 6 UNION ALL SELECT 7) x")
        exp.sql('CREATE TABLE native_scan.keys_t(k INT PRIMARY KEY)')
        exp.sql('INSERT INTO native_scan.keys_t VALUES(0),(1),(2),(3),(4),(5),(6),(7)')
        exp.sql('FORK NAMESPACE native_scan_child FROM ns1')
        exp.sql('UPDATE native_scan.t SET v=v+1000')
        with connect(exp, 'root@native_scan_child') as child:
            assert exp.sql('SELECT COUNT(*),SUM(v) FROM native_scan.t', child) == ((8,280),)
            # Materialize selected tablets. A partition switch must not carry
            # the inherited fork cap into an owned tablet and hide these writes.
            exp.sql('BEGIN', child)
            exp.sql('UPDATE native_scan.t SET v=111 WHERE id=1', child)
            exp.sql('DELETE FROM native_scan.t WHERE id=2', child)
            exp.sql("INSERT INTO native_scan.t VALUES(9,99,9.25,'xyz',REPEAT('z',16000))", child)
            expected = ((0,0),(1,111),(3,30),(4,40),(5,50),(6,60),(7,70),(9,99))
            for hint in ('', '/*+ PARALLEL(2) */'):
                assert exp.sql(f'SELECT {hint} id,v FROM native_scan.t ORDER BY id', child) == expected
                assert exp.sql(f'SELECT {hint} COUNT(*),SUM(v) FROM native_scan.t', child) == ((8,460),)
            exp.sql('COMMIT', child)
            # Plan-owned column descriptors include decimal scale and CHAR/LOB metadata.
            assert exp.sql('SELECT id,d,c,LENGTH(body) FROM native_scan.t WHERE id IN(1,9) ORDER BY id', child) == (
                (1,Decimal('1.25'),'abc',12001),(9,Decimal('9.25'),'xyz',16000))
            assert exp.sql('SELECT id,SUBSTR(body,1,3) FROM native_scan.t FORCE INDEX(by_v) '
                           'WHERE v>=60 ORDER BY id', child) == ((1,'BBB'),(6,'GGG'),(7,'HHH'),(9,'zzz'))
            # Repeated parameterized rescans under a nested loop join.
            statement = ('SELECT /*+ LEADING(k t) USE_NL(t) NO_USE_HASH(t) NO_USE_MERGE(t) */ '
                         'k.k,t.v FROM native_scan.keys_t k JOIN native_scan.t t ON t.id=k.k ORDER BY k.k')
            plan = exp.sql('EXPLAIN ' + statement, child)
            assert 'NESTED-LOOP' in str(plan).upper(), plan
            assert exp.sql(statement, child) == expected[:-1]
            exp.sql("PREPARE scan_ps FROM 'SELECT id,v FROM native_scan.t WHERE id=?'", child)
            for key, value in expected:
                exp.sql(f'SET @scan_key={key}', child)
                assert exp.sql('EXECUTE scan_ps USING @scan_key', child) == ((key,value),)
            exp.sql('DEALLOCATE PREPARE scan_ps', child)
            assert exp.sql('SELECT COUNT(*) FROM information_schema.columns '
                           "WHERE table_schema='native_scan' AND table_name='t'", child) == ((5,),)
            exp.sql('ALTER TABLE native_scan.t ADD COLUMN extra INT DEFAULT 17', child)
            assert exp.sql('SELECT id,extra FROM native_scan.t ORDER BY id', child) == tuple((k,17) for k,_ in expected)
            assert exp.sql('SELECT id,v FROM native_scan.t ORDER BY id', child) == expected
            exp.record('PASS', case='native_scan', partition_switch=True, own_writes=True,
                       native_descriptors=True, local_index_lookup=True, nlj=True, lob=True, ddl=True)
        assert exp.sql('SELECT SUM(v) FROM native_scan.t') == ((8280,),)
    finally:
        exp.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True)
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    run(args.binary)
