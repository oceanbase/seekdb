#!/usr/bin/env python3
"""Real mini and fork installation must preserve layout provenance and completeness."""
import argparse
import os
from pathlib import Path
import re
import resource
import tempfile

from fork_parent_truncate_probe import BootstrapExperiment, connect, namespace_id, physical_id
from physical_merge_layout_probe import wait_for


class Trace:
    def __init__(self, exp):
        self.exp, self.positions, self.rows = exp, {}, []

    def read(self, kind, tablet):
        for path in (self.exp.base / 'log').glob('seekdb.log*'):
            if path.suffix == '.wf':
                continue
            inode = path.stat().st_ino
            with path.open('rb') as stream:
                stream.seek(self.positions.get(inode, 0))
                while True:
                    pos = stream.tell()
                    line = stream.readline()
                    if not line or not line.endswith(b'\n'):
                        stream.seek(pos)
                        break
                    if b'LOCAL_SCHEMA_' in line:
                        text = line.decode(errors='replace')
                        pairs = {key: int(value) for key, value in re.findall(r'(\w+)=(-?\d+)', text)}
                        self.rows.append((text, pairs))
                self.positions[inode] = stream.tell()
        return [row for text, row in self.rows if kind in text and row.get('tablet') == tablet]


def definition(exp, conn=None):
    rows = exp.sql("SELECT tablet_id,schema_version FROM oceanbase.__all_table "
                   "WHERE database_id=(SELECT database_id FROM oceanbase.__all_database "
                   "WHERE database_name='local_schema') AND table_name='t'", conn, log=False)
    assert len(rows) == 1, rows
    return tuple(map(int, rows[0]))


def run(binary, case):
    with tempfile.TemporaryDirectory(prefix='local-schema-pause-') as directory:
        pause = Path(directory) / 'baseline'
        os.environ['SEEKDB_BASELINE_PAUSE'] = str(pause)
        os.environ['SEEKDB_LOCAL_SCHEMA_INSTALL_PROBE'] = '1'
        exp = BootstrapExperiment(binary, 'local_schema_' + case, prototype=6)
        exp.extra_parameters = [('ob_compaction_schedule_interval', '3s')]
        try:
            exp.start()
            if case != 'mini':
                pause.write_text('0\n')
            trace = Trace(exp)
            exp.sql('CREATE DATABASE local_schema')
            exp.sql('CREATE TABLE local_schema.t(id INT PRIMARY KEY,v INT DEFAULT 3)')
            exp.sql('INSERT INTO local_schema.t VALUES(1,10),(3,12)')
            logical, before = definition(exp)
            if case == 'mini':
                exp.sql('ALTER TABLE local_schema.t ALTER COLUMN v SET DEFAULT 9')
                _, version = definition(exp)
                assert version > before
                exp.sql('INSERT INTO local_schema.t(id) VALUES(2)')
                exp.sql('ALTER SYSTEM MINOR FREEZE')
                rows = wait_for(lambda: [row for row in trace.read('LOCAL_SCHEMA_MINI ', physical_id(1, logical))
                                        if row['observed'] == version], 'mini did not observe new definition')
                assert any(row['before'] < row['observed'] and row['before_columns'] == row['observed_columns']
                           for row in rows), rows
                assert all(row['result'] == version and row['simplified'] == 1 for row in rows), rows
                assert exp.sql('SELECT * FROM local_schema.t ORDER BY id') == ((1, 10), (2, 9), (3, 12))
            else:
                exp.sql('FORK NAMESPACE local_schema_child FROM ns1')
                ns = namespace_id(exp, 'local_schema_child')
                tablet = physical_id(ns, logical)
                with connect(exp, 'root@local_schema_child') as child:
                    if case == 'fork':
                        exp.sql('ALTER TABLE local_schema.t ADD COLUMN child_col INT DEFAULT 7', child)
                    exp.sql('UPDATE local_schema.t SET v=20 WHERE id=1', child)
                    _, version = definition(exp, child)
                    exp.sql('ALTER TABLE local_schema.t ADD COLUMN parent_col INT DEFAULT 11')
                    if case == 'fork_wide':
                        exp.sql('ALTER TABLE local_schema.t ADD COLUMN parent_extra INT DEFAULT 13')
                    _, parent_version = definition(exp)
                    assert parent_version > version
                    exp.sql('UPDATE local_schema.t SET parent_col=99 WHERE id=1')
                    exp.sql('ALTER SYSTEM MINOR FREEZE')
                    pause.unlink()
                    rows = wait_for(lambda: trace.read('LOCAL_SCHEMA_FORK ', tablet), 'fork did not install baseline', 180)
                    assert all(row['result'] == version for row in rows), (parent_version, version, rows)
                    expected = ((1, 20, 7), (3, 12, 7)) if case == 'fork' else ((1, 20), (3, 12))
                    assert exp.sql('SELECT * FROM local_schema.t ORDER BY id', child) == expected
                    assert exp.sql('SELECT id,v,parent_col FROM local_schema.t ORDER BY id') == ((1, 10, 99), (3, 12, 11))
                exp.connection.close()
                exp.connection = None
                exp.proc.kill()
                exp.proc.wait(timeout=15)
                exp.record('crash_for_recovery', pid=exp.proc.pid)
                exp.start()
                with connect(exp, 'root@local_schema_child') as child:
                    assert exp.sql('SELECT * FROM local_schema.t ORDER BY id', child) == expected
            exp.record('PASS', case='local_schema_' + case, version=version, installations=rows)
        finally:
            exp.close()


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary', required=True)
    p.add_argument('--case', required=True, choices=('mini', 'fork', 'fork_wide'))
    args = p.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    run(args.binary, args.case)
