#!/usr/bin/env python3
"""Hold a local readable horizon behind DELETE, then allow native reclamation."""
import argparse
import os
from pathlib import Path
import resource
import tempfile
import time
from ddl_physical_drop_probe import BootstrapExperiment

p=argparse.ArgumentParser()
p.add_argument('--binary',required=True)
a=p.parse_args()
resource.setrlimit(resource.RLIMIT_CORE,(0,0))
with tempfile.TemporaryDirectory(prefix='seekdb-weak-gc-') as tmp:
    control=Path(tmp)/'horizon'
    os.environ['SEEKDB_EMPTY_SHELL_READ_HORIZON']=str(control)
    exp=BootstrapExperiment(a.binary,'empty_shell_horizon',prototype=6)
    try:
        exp.start()
        exp.sql('CREATE DATABASE horizon')
        exp.sql('CREATE TABLE horizon.t(id INT PRIMARY KEY,v INT)')
        exp.sql('INSERT INTO horizon.t VALUES(1,10)')
        logical=int(exp.sql("SELECT tablet_id FROM oceanbase.__all_table WHERE table_name='t'")[0][0])
        physical=(1<<62)|(1<<37)|logical
        def state():
            return exp.sql('SELECT tablet_status,is_committed,is_empty_shell FROM oceanbase.__all_virtual_tablet_info '
                           f'WHERE tablet_id={physical}',log=False)
        assert state()==((1,1,0),),state()
        # No View, user transaction or fork pin protects this table.
        time.sleep(.1)
        control.write_text(str(time.time_ns()))
        time.sleep(.1)
        exp.sql('DROP TABLE horizon.t')
        until=time.monotonic()+12
        while time.monotonic()<until:
            current=state()
            assert current==((3,1,0),),('reclaimed ahead of readable horizon',current)
            time.sleep(.25)
        exp.record('horizon_held',tablet=physical,seconds=12)
        control.unlink()
        until=time.monotonic()+30
        while time.monotonic()<until:
            current=state()
            if not current or current==((3,1,1),):break
            time.sleep(.25)
        else: raise AssertionError(('horizon passed but tablet not reclaimed',current))
        exp.record('PASS',case='empty_shell_horizon',tablet=physical,released=current)
    finally: exp.close()
