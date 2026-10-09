#!/usr/bin/env python3
"""A stale complete GC plan cannot mistake inherited birth for physical birth."""
import argparse, os, resource, tempfile, time
from pathlib import Path
import pymysql
from fork_parent_truncate_probe import BootstrapExperiment, connect, metadata, namespace_id, physical_id, physical_state
from namespace_inprocess_prototype import drop_after_client_close

p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',required=True);a=p.parse_args()
resource.setrlimit(resource.RLIMIT_CORE,(0,0))
with tempfile.TemporaryDirectory(prefix='seekdb-gc-plan-') as tmp:
    pause=Path(tmp)/'pause';output=Path(tmp)/'cut'
    os.environ['SEEKDB_FREEZE_PLAN_PAUSE']=str(pause)
    os.environ['SEEKDB_RETENTION_PLAN_OUTPUT']=str(output)
    e=BootstrapExperiment(a.binary,'physical_gc_plan',prototype=6)
    e.extra_parameters=[('ob_compaction_schedule_interval','5m')]
    b=c=None
    try:
        e.start()
        e.sql('CREATE DATABASE gc_plan')
        e.sql('CREATE TABLE gc_plan.t(id INT PRIMARY KEY,v INT)')
        e.sql('INSERT INTO gc_plan.t VALUES(1,10)')
        logical=int(e.sql("SELECT tablet_id FROM oceanbase.__all_table WHERE table_name='t' AND database_id=(SELECT database_id FROM oceanbase.__all_database WHERE database_name='gc_plan')")[0][0])
        e.sql('FORK NAMESPACE gc_parent FROM ns1')
        parent_id=namespace_id(e,'gc_parent')
        fork=int(next(v for _,v in metadata(e,1) if v['name']=='gc_parent')['fork_cap'])
        physical=physical_id(parent_id,logical)
        original=physical_id(1,logical)
        e.sql('DROP TABLE gc_plan.t')
        after_delete=time.time_ns()
        until=time.monotonic()+20
        while True:
            try:
                e.sql('FORK NAMESPACE retention_publish FROM ns1',log=False)
                cut=int(output.read_text())
                if cut>after_delete: break
            except pymysql.MySQLError as exc:
                if 'try again' not in str(exc).lower() and exc.args[0]!=4012: raise
            assert time.monotonic()<until,'weak cut did not pass fork'
            time.sleep(.1)
        pause.touch()
        # B's physical CREATE commits after R, but its logical birth is S<R.
        b=connect(e,'root@gc_parent')
        e.sql('UPDATE gc_plan.t SET v=20',b)
        e.sql('FORK NAMESPACE gc_child FROM gc_parent')
        c=connect(e,'root@gc_child')
        b.close();b=None
        drop_after_client_close(e,'gc_parent')
        until=time.monotonic()+12
        samples=0
        while time.monotonic()<until:
            assert e.sql('SELECT * FROM gc_plan.t',c,log=False)==((1,20),)
            rows=physical_state(e,[physical])
            assert rows and rows[0][1:]==(1,1,0),('new physical object must wait for a newer plan',cut,fork,rows)
            assert physical_state(e,[original])==((original,3,1,0),), 'old deleted source lost from retained graph'
            samples+=1;time.sleep(.5)
        e.record('new_physical_birth_protected',cut=cut,inherited_birth=fork,samples=samples,physical=physical)
        pause.unlink()
        c.close();c=None
        drop_after_client_close(e,'gc_child')
        until=time.monotonic()+60
        while physical_state(e,[physical,original]):
            assert time.monotonic()<until,'physical source did not release after new plan'
            time.sleep(.5)
        e.record('PASS',case='physical_gc_plan_cut',republished_plan_reclaimed=True)
    finally:
        if pause.exists(): pause.unlink()
        for conn in (b,c):
            if conn is not None: conn.close()
        e.close()
