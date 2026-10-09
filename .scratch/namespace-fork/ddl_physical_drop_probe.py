#!/usr/bin/env python3
"""A child DDL drop must delete the encoded physical tablet."""
import argparse
from pathlib import Path
import resource
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'excluded-from-branch/tools/obtest'))
from namespace_worker_bootstrap_prototype import BootstrapExperiment
from namespace_inprocess_prototype import connect


def run(binary):
    experiment = BootstrapExperiment(binary, 'ddl_physical_drop', prototype=6)
    child = None
    try:
        experiment.start()
        experiment.sql('FORK NAMESPACE ddl_child FROM ns1')
        child = connect(experiment, 'root@ddl_child')
        experiment.sql('CREATE DATABASE ddl_db', child)
        experiment.sql('CREATE TABLE ddl_db.t(id INT PRIMARY KEY, v INT)', child)
        experiment.sql('INSERT INTO ddl_db.t VALUES(1,10)', child)
        local_tablet = int(experiment.sql(
            "SELECT tablet_id FROM oceanbase.__all_table WHERE table_name='t'", child)[0][0])
        physical_rows = experiment.sql(
            'SELECT tablet_id FROM oceanbase.__all_virtual_tablet_info', log=False)
        physical_ids = [int(row[0]) for row in physical_rows
                        if row[0] & (1 << 62) and row[0] & ((1 << 37) - 1) == local_tablet
                        and ((row[0] & ~(1 << 62)) >> 37) > 2]
        assert len(physical_ids) == 1, physical_ids
        physical = physical_ids[0]
        def storage():
            return experiment.sql(
                'SELECT tablet_status,is_committed,is_empty_shell '
                f'FROM oceanbase.__all_virtual_tablet_info WHERE tablet_id={physical}', log=False)
        assert storage() == ((1, 1, 0),), storage()
        experiment.sql('DROP TABLE ddl_db.t', child)
        deadline = time.monotonic() + 30
        last = storage()
        while last and last[0][2] != 1 and time.monotonic() < deadline:
            time.sleep(.5)
            last = storage()
        assert not last or last[0] == (3, 1, 1), ('DDL tablet remained live', physical, last)
        experiment.record('PASS', case='ddl_physical_drop', tablet=physical, storage=last)
    finally:
        if child is not None:
            child.close()
        experiment.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True)
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    run(args.binary)
