#!/usr/bin/env python3
"""A committed child tablet with a missing owned row must still be reclaimed."""
import argparse
from pathlib import Path
import resource
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'excluded-from-branch/tools/obtest'))
from namespace_worker_bootstrap_prototype import BootstrapExperiment
from namespace_inprocess_prototype import connect


def run(binary):
    experiment = BootstrapExperiment(binary, 'orphan_physical_gc', prototype=6)
    child = None
    try:
        experiment.start()
        experiment.sql('FORK NAMESPACE orphan_child FROM ns1')
        child = connect(experiment, 'root@orphan_child')
        experiment.sql('CREATE DATABASE orphan_db', child)
        experiment.sql('CREATE TABLE orphan_db.t(id INT PRIMARY KEY, v INT)', child)
        experiment.sql('INSERT INTO orphan_db.t VALUES(1,10)', child)
        namespace_id = int(experiment.sql(
            "SELECT namespace_id FROM __fork_proto_meta.namespaces WHERE name='orphan_child'")[0][0])
        local_tablet = int(experiment.sql(
            "SELECT tablet_id FROM oceanbase.__all_table WHERE table_name='t'", child)[0][0])
        physical = (1 << 62) | (namespace_id << 37) | local_tablet
        owned = experiment.sql(
            'SELECT tablet_id FROM __fork_proto_meta.exceptions '
            f'WHERE namespace_id={namespace_id} AND tablet_id={local_tablet} AND kind=0')
        assert owned == ((local_tablet,),), owned
        def storage():
            return experiment.sql(
                'SELECT tablet_id,tablet_status,is_committed,is_empty_shell '
                f'FROM oceanbase.__all_virtual_tablet_info WHERE tablet_id={physical}', log=False)
        before = storage()
        assert len(before) == 1 and before[0][0] == physical \
            and before[0][2:] == (1, 0), before
        experiment.sql(
            'DELETE FROM __fork_proto_meta.exceptions '
            f'WHERE namespace_id={namespace_id} AND tablet_id={local_tablet}')
        assert experiment.sql(
            'SELECT tablet_id FROM __fork_proto_meta.exceptions '
            f'WHERE namespace_id={namespace_id} AND tablet_id={local_tablet}') == ()
        child.close()
        child = None
        deadline = time.monotonic() + 5
        while True:
            try:
                experiment.sql('DROP NAMESPACE orphan_child')
                break
            except Exception as exc:
                if 'active connections' not in str(exc).lower() or time.monotonic() >= deadline:
                    raise
                time.sleep(.1)
        deadline = time.monotonic() + 30
        last = storage()
        while last and last[0][3] != 1 and time.monotonic() < deadline:
            time.sleep(.5)
            last = storage()
        assert not last or last[0][1] == 3 and last[0][3] == 1, \
            ('orphan tablet remained live', physical, last)
        experiment.record('PASS', case='orphan_physical_gc', tablet=physical, storage=last)
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
