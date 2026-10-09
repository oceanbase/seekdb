#!/usr/bin/env python3
"""Keep the child alive after native GC removes source tablets, then crash/restart."""
import argparse
import hashlib
import os
from pathlib import Path
import resource
import time

from fork_parent_truncate_probe import (
    BootstrapExperiment, connect, metadata, namespace_id, physical_id, physical_state,
)


def wait_for(fn, seconds, label):
    end = time.monotonic() + seconds
    last = None
    while time.monotonic() < end:
        last = fn()
        if last:
            return last
        time.sleep(.5)
    raise AssertionError((label, last))


def run(binary, mixed_source=False):
    exp = BootstrapExperiment(binary, 'detached_restart', prototype=6)
    exp.extra_parameters = [('ob_compaction_schedule_interval', '3s')]
    child = None
    expected = {}
    try:
        exp.start()
        exp.sql('CREATE DATABASE detach_repro')
        for table in ('plain', 'lob'):
            payload = ', body LONGTEXT' if table == 'lob' else ''
            exp.sql(f'CREATE TABLE detach_repro.{table}(id INT PRIMARY KEY,v INT{payload},KEY idx_v(v))')
            values = []
            for i in range(1, 9):
                suffix = f",REPEAT('payload-{i}-',4096)" if table == 'lob' else ''
                values.append(f'({i},{i * 10}{suffix})')
            exp.sql(f'INSERT INTO detach_repro.{table} VALUES' + ','.join(values))
        schemas = exp.sql(
            "SELECT table_name,table_id,tablet_id FROM oceanbase.__all_table WHERE database_id="
            "(SELECT database_id FROM oceanbase.__all_database WHERE database_name='detach_repro') "
            "AND tablet_id>0 ORDER BY table_id")
        logical = [int(row[2]) for row in schemas]
        sources = [physical_id(1, tablet) for tablet in logical]
        assert len(logical) >= 6, schemas
        exp.sql('ALTER SYSTEM MINOR FREEZE')
        main_sources = [physical_id(1, int(row[2])) for row in schemas if row[0] in ('plain', 'lob')]
        def source_dumped():
            rows = exp.sql('SELECT tablet_id,table_type FROM oceanbase.V$OB_SSTABLES WHERE tablet_id IN ('
                           + ','.join(map(str, main_sources)) + ')', log=False)
            return rows if all(any(int(t) == source and kind in ('MINOR', 'MINI') for t, kind in rows)
                               for source in main_sources) else None
        exp.record('source_dumped', sstables=wait_for(source_dumped, 90, 'source dump'))
        if mixed_source:
            for table in ('plain', 'lob'):
                exp.sql(f'UPDATE detach_repro.{table} SET v=800 WHERE id=8')
        exp.sql('FORK NAMESPACE detach_child FROM ns1')
        ns = namespace_id(exp, 'detach_child')
        record = next(value for key, value in metadata(exp, 1) if int(key['namespace_id']) == ns)
        snapshot = int(record['fork_cap'])
        if mixed_source:
            for table in ('plain', 'lob'):
                exp.sql(f'UPDATE detach_repro.{table} SET v=8000 WHERE id=8')
        child = connect(exp, 'root@detach_child')
        children = [physical_id(ns, tablet) for tablet in logical]
        assert not physical_state(exp, children)
        for table in ('plain', 'lob'):
            exp.sql(f'UPDATE detach_repro.{table} SET v=v+1', child)
        exp.sql("UPDATE detach_repro.lob SET body=REPEAT('child-replacement-',4096) WHERE id=2", child)
        for i in range(1, 9):
            body = 'child-replacement-' * 4096 if i == 2 else f'payload-{i}-' * 4096
            expected[i] = (len(body), hashlib.md5(body.encode()).hexdigest())

        def verify(connection):
            wanted = tuple((i, 801 if mixed_source and i == 8 else i * 10 + 1)
                           for i in range(1, 9))
            assert exp.sql('SELECT id,v FROM detach_repro.plain ORDER BY id', connection, log=False) == wanted
            for table in ('plain', 'lob'):
                got = exp.sql(f'SELECT id,v FROM detach_repro.{table} FORCE INDEX(idx_v) '
                              'WHERE v>=0 ORDER BY id', connection, log=False)
                assert got == wanted, (table, got)
            got = exp.sql('SELECT id,LENGTH(body),MD5(body) FROM detach_repro.lob ORDER BY id', connection, log=False)
            assert got == tuple((i, *expected[i]) for i in range(1, 9)), got

        verify(child)
        exp.record('child_materialized', schemas=schemas, physical=physical_state(exp, sources + children))
        exp.sql('DROP TABLE detach_repro.plain')
        exp.sql('DROP TABLE detach_repro.lob')
        exp.sql('ALTER SYSTEM MINOR FREEZE')
        last = None
        def sources_gone():
            nonlocal last
            verify(child)
            rows = physical_state(exp, sources)
            if rows != last:
                exp.record('source_gc_progress', physical=rows)
                last = rows
            return not rows
        wait_for(sources_gone, 150, 'source physical tablets not fully reclaimed')
        pins = [key for key, _ in metadata(exp, 8) if int(key['snapshot_id']) == snapshot]
        if mixed_source:
            rewrite_lines = []
            for path in (exp.base / 'log').glob('seekdb.log*'):
                if path.is_file():
                    rewrite_lines.extend(line for line in path.read_text(errors='replace').splitlines()
                                         if 'fork rewrite: successfully created sstable' in line
                                         and any(f'id:{tablet}' in line for tablet in children))
            assert rewrite_lines, 'mixed-source case did not exercise successful SSTable rewrite'
            exp.record('rewrite_path_observed', count=len(rewrite_lines), evidence=rewrite_lines[:2])
        exp.record('sources_absent_child_alive', physical=physical_state(exp, sources),
                   child_physical=physical_state(exp, children), retained_pin=pins)
        child.close()
        child = None
        exp.connection.close()
        exp.connection = None
        exp.proc.kill()
        exp.proc.wait(timeout=15)
        exp.record('crash_after_source_reclamation', returncode=exp.proc.returncode)
        exp.start()
        assert not physical_state(exp, sources), physical_state(exp, sources)
        child = connect(exp, 'root@detach_child')
        verify(child)
        exp.sql("UPDATE detach_repro.lob SET body=CONCAT(body,'-after-restart') WHERE id=1", child)
        body = 'payload-1-' * 4096 + '-after-restart'
        expected[1] = (len(body), hashlib.md5(body.encode()).hexdigest())
        verify(child)
        exp.record('PASS', case='detached_child_restart_plain_index_lob',
                   physical_sources=[], child_alive=True, post_restart_write=True,
                   pin_retained=bool(pins), mixed_source=mixed_source)
    except Exception as exc:
        exp.record('FAIL', error=repr(exc))
        raise
    finally:
        if child is not None and child.open:
            child.close()
        exp.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True)
    parser.add_argument('--mixed-source', action='store_true')
    args = parser.parse_args()
    root = Path('/data/1/tmp/seekdb-ns-probes')
    root.mkdir(parents=True, exist_ok=True)
    os.environ['SEEKDB_FORK_PROTOTYPE_TEST_ROOT'] = str(root)
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    run(args.binary, args.mixed_source)
