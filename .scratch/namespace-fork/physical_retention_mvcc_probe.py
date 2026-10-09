#!/usr/bin/env python3
"""Actual compaction must retain a fork source without pinning unrelated data."""
import argparse
import resource
import time
from fork_parent_truncate_probe import BootstrapExperiment, connect, metadata, physical_id

p=argparse.ArgumentParser()
p.add_argument('--binary',required=True)
a=p.parse_args()
resource.setrlimit(resource.RLIMIT_CORE,(0,0))
exp=BootstrapExperiment(a.binary,'physical_retention_mvcc',prototype=6)
exp.extra_parameters=[('undo_retention','0'),('minor_compact_trigger','2'),
                      ('ob_compaction_schedule_interval','3s')]
child=None
try:
    exp.start()
    exp.sql('CREATE DATABASE retention_mvcc')
    exp.sql('CREATE TABLE retention_mvcc.source(id INT PRIMARY KEY,v INT)')
    exp.sql('INSERT INTO retention_mvcc.source VALUES(1,10)')
    source=int(exp.sql("SELECT tablet_id FROM oceanbase.__all_table WHERE table_name='source'")[0][0])
    exp.sql('FORK NAMESPACE retained_reader FROM ns1')
    fork=next(int(value['fork_cap']) for key,value in metadata(exp,1) if value['name']=='retained_reader')
    child=connect(exp,'root@retained_reader')
    exp.sql('CREATE TABLE retention_mvcc.unrelated(id INT PRIMARY KEY,v INT)')
    exp.sql('INSERT INTO retention_mvcc.unrelated VALUES(1,10)')
    unrelated=int(exp.sql("SELECT tablet_id FROM oceanbase.__all_table WHERE table_name='unrelated'")[0][0])
    ids=(physical_id(1,source),physical_id(1,unrelated))
    def versions():
        rows=exp.sql('SELECT tablet_id,multi_version_start,compaction_scn FROM oceanbase.__all_virtual_tablet_info '
                     f'WHERE tablet_id IN ({ids[0]},{ids[1]})',log=False)
        return {int(row[0]):(int(row[1]),int(row[2])) for row in rows}
    def verify():assert exp.sql('SELECT v FROM retention_mvcc.source',child,log=False)==((10,),)
    for i in range(6):
        exp.sql(f'UPDATE retention_mvcc.source SET v={100+i}')
        exp.sql(f'UPDATE retention_mvcc.unrelated SET v={100+i}')
        exp.sql('ALTER SYSTEM MINOR FREEZE')
        verify()
        time.sleep(1)
    exp.sql('ALTER SYSTEM MAJOR FREEZE')
    until=time.monotonic()+180
    last=None
    iteration=0
    while time.monotonic()<until:
        current=versions()
        verify()
        if current!=last:
            exp.record('retention_progress',fork=fork,physical=current)
            last=current
        assert ids[0] in current and current[ids[0]][0]<=fork,(fork,current)
        if ids[1] in current and current[ids[1]][0]>fork and current[ids[0]][0]>1:break
        if iteration%5==0:
            exp.sql('UPDATE retention_mvcc.source SET v=v+1')
            exp.sql('UPDATE retention_mvcc.unrelated SET v=v+1')
            exp.sql('ALTER SYSTEM MINOR FREEZE')
        iteration+=1
        time.sleep(1)
    else:raise AssertionError(('unrelated history remained pinned',fork,last))
    exp.record('PASS',case='physical_retention_mvcc',fork=fork,source=last[ids[0]],unrelated=last[ids[1]],undo_retention=0)
finally:
    if child is not None and child.open:child.close()
    exp.close()
