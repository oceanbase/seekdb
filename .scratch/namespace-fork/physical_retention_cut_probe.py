#!/usr/bin/env python3
"""A native physical plan retains capped sources and lets publication proceed."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import resource
import tempfile
import time
from fork_parent_truncate_probe import BootstrapExperiment, connect, metadata, namespace_id, physical_id
from namespace_inprocess_prototype import drop_after_client_close

p=argparse.ArgumentParser()
p.add_argument('--binary',required=True)
a=p.parse_args()
resource.setrlimit(resource.RLIMIT_CORE,(0,0))
with tempfile.TemporaryDirectory(prefix='seekdb-retention-cut-') as tmp:
    control=Path(tmp)/'control'
    output=Path(tmp)/'plan.json'
    os.environ['SEEKDB_RETENTION_CUT_CONTROL']=str(control)
    os.environ['SEEKDB_RETENTION_PLAN_OUTPUT']=str(output)
    exp=BootstrapExperiment(a.binary,'physical_retention_cut',prototype=6)
    exp.extra_parameters=[('ob_compaction_schedule_interval','5m')]
    parent=child=worker=None
    try:
        exp.start()
        exp.sql('CREATE DATABASE retention_cut')
        exp.sql('CREATE TABLE retention_cut.t(id INT PRIMARY KEY,v INT)')
        exp.sql('INSERT INTO retention_cut.t VALUES(1,10)')
        logical=int(exp.sql("SELECT tablet_id FROM oceanbase.__all_table WHERE table_name='t'")[0][0])
        exp.sql('FORK NAMESPACE cut_parent FROM ns1')
        parent_ns=namespace_id(exp,'cut_parent')
        parent=connect(exp,'root@cut_parent')
        exp.sql('UPDATE retention_cut.t SET v=20',parent)
        exp.sql('FORK NAMESPACE cut_child FROM cut_parent')
        child_ns=namespace_id(exp,'cut_child')
        caps={value['name']:int(value['fork_cap']) for key,value in metadata(exp,1)}
        parent.close();parent=None
        drop_after_client_close(exp,'cut_parent')
        exp.sql('DROP TABLE retention_cut.t')
        child=connect(exp,'root@cut_child')
        assert exp.sql('SELECT v FROM retention_cut.t',child)==((20,),)
        def collect(conn=None):
            exp.sql('FORK NAMESPACE retention_probe FROM ns1',conn,log=False)
            return json.loads(output.read_text())
        until=time.monotonic()+10
        while True:
            plan=collect()
            if plan['read_snapshot']>=caps['cut_child']:break
            assert time.monotonic()<until,plan
            time.sleep(.1)
        rows={int(row[0]):(int(row[1]),int(row[2])) for row in plan['tablets']}
        origin=physical_id(1,logical)
        intermediate=physical_id(parent_ns,logical)
        local=physical_id(child_ns,logical)
        assert rows[origin][0]>0 and rows[origin][1]==caps['cut_parent'],(rows.get(origin),caps)
        assert rows[intermediate][0]>0 and rows[intermediate][1]==caps['cut_child'],(rows.get(intermediate),caps)
        assert local not in rows,rows.get(local)
        assert 0<plan['new_source_floor']<=plan['read_snapshot'],plan
        exp.record('physical_plan',origin=rows[origin],intermediate=rows[intermediate],caps=caps,entries=len(rows))
        # Stop after the short cut fence was released, with the KV snapshot and
        # physical GC serialization still held. Cold child DML must materialize.
        worker=connect(exp)
        control.write_text('0 after_cut pause_once\n')
        with ThreadPoolExecutor(max_workers=1) as pool:
            pending=pool.submit(collect,worker)
            until=time.monotonic()+10
            ready=Path(str(control)+'.ready')
            while not ready.exists():
                if pending.done():pending.result()
                assert time.monotonic()<until,'collector never reached the cut'
                time.sleep(.02)
            started=time.monotonic()
            try:
                assert exp.sql('SELECT v FROM retention_cut.t',child)==((20,),)
                exp.sql('UPDATE retention_cut.t SET v=30',child)
                duration=time.monotonic()-started
                assert duration<5,('publication blocked by traversal',duration)
            finally:Path(str(control)+'.release').touch()
            old= pending.result(timeout=30)
        old_rows={int(row[0]):row for row in old['tablets']}
        assert local not in old_rows and origin in old_rows and intermediate in old_rows,old_rows
        assert exp.sql('SELECT v FROM retention_cut.t',child)==((30,),)
        exp.record('PASS',case='physical_retention_cut',native_edges=True,cut_immutable=True,publication_seconds=duration)
    finally:
        for conn in (parent,child,worker):
            if conn is not None and conn.open:conn.close()
        exp.close()
