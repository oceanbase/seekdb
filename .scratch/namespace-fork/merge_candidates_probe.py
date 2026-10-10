#!/usr/bin/env python3
"""Real major rounds must renew old candidate lists and wait for readable F.

Requires merge_candidates_probe_injection.py and physical_merge_layout_injection.py.
The controls pause after a real enumeration, retain a partial old batch, and
temporarily lower only the scheduler's observed horizon. SQL DDL, freeze,
enumeration, merge files, physical C, and global completion remain real.
"""
import argparse
import os
import re
import resource
import time

from fork_parent_truncate_probe import BootstrapExperiment, physical_id
from physical_merge_layout_probe import Trace


def run(binary):
    exp = BootstrapExperiment(binary, 'merge_candidates', prototype=6)
    prefix = exp.base / 'candidates'
    os.environ['SEEKDB_MERGE_CANDIDATES_PROBE'] = str(prefix)
    os.environ['SEEKDB_PHYSICAL_MERGE_LAYOUT_PROBE'] = '1'
    layout_trace = Trace(exp)

    def control(suffix):
        return prefix.with_suffix('.' + suffix)

    def events():
        if not control('events').exists():
            return []
        rows = []
        for line in control('events').read_text().splitlines():
            match = re.fullmatch(r'(\w+) F=(-?\d+) a=(-?\d+) b=(-?\d+)', line)
            if match:
                rows.append((match[1], *map(int, match.groups()[1:])))
        return rows

    def wait(check, message, timeout=150):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            result = check()
            if result:
                return result
            assert exp.proc.poll() is None, 'instance exited'
            time.sleep(.1)
        raise AssertionError((message, events()[-12:]))

    def progress():
        return tuple(map(int, exp.sql('SELECT frozen_scn,global_broadcast_scn,last_scn '
            'FROM oceanbase.DBA_OB_MAJOR_COMPACTION', log=False)[0]))

    def freeze(previous):
        exp.sql('ALTER SYSTEM MAJOR FREEZE')
        f = wait(lambda: progress()[0] if progress()[0] > previous else None, 'freeze absent')
        wait(lambda: any(e[0] == 'REQUEST' and e[1] == f for e in events()), 'scheduler did not adopt F')
        return f

    def create(name):
        exp.sql(f'CREATE TABLE candidate_probe.{name}(id INT PRIMARY KEY,v INT)')
        exp.sql(f'INSERT INTO candidate_probe.{name} VALUES(1,10)')
        logical = int(exp.sql("SELECT tablet_id FROM oceanbase.__all_table WHERE table_name='"
                              + name + "'", log=False)[0][0])
        return physical_id(1, logical)

    def files(tablet, f):
        return exp.sql('SELECT tablet_id FROM oceanbase.V$OB_SSTABLES '
            f"WHERE tablet_id={tablet} AND table_type='MAJOR' AND end_log_scn={f}", log=False)

    def complete(tablet, f):
        wait(lambda: progress() == (f, f, f) and files(tablet, f), 'physical/global major incomplete')
        def observed_layout():
            layout_trace.read()
            return [r for r in layout_trace.layouts if r.get('tablet') == tablet and r.get('F') == f
                    and r.get('ret') == 0]
        actual = wait(observed_layout, 'layout trace has not reached the server log')
        assert actual and all(0 < r['C'] <= f for r in actual), (tablet, f, actual)
        assert exp.sql('SELECT SUM(v) FROM candidate_probe.' + names[tablet]) == ((10,),)
        return actual[-1]

    names = {}
    try:
        exp.start()
        exp.connection._read_timeout = 330
        exp.sql('SET ob_query_timeout=300000000')
        exp.sql("ALTER SYSTEM SET ob_compaction_schedule_interval='3s'")
        exp.sql('CREATE DATABASE candidate_probe')
        seed = create('seed'); names[seed] = 'seed'
        f1 = freeze(progress()[0])
        complete(seed, f1)

        # Pause an old list and keep it partial, rather than letting ordinary
        # end-of-scan refresh hide a missing reset on the next round.
        control('pause').touch()
        paused = wait(lambda: next((e for e in events() if e[0] == 'PAUSED'), None), 'no paused old list')
        assert paused[1] == f1 and paused[2] > 1, paused
        late = create('created_after_old_list'); names[late] = 'created_after_old_list'
        control('target').write_text(str(late))
        f2 = freeze(f1)
        control('release').touch()
        wait(lambda: any(e[0] == 'BATCH' and e[1] == f2 for e in events()), 'no new round batch')
        trace = events()
        first_batch = next(i for i, e in enumerate(trace) if e[0] == 'BATCH' and e[1] == f2)
        renewed = [e for e in trace[:first_batch] if e[0] == 'ENUM' and e[1] == f2]
        assert renewed and renewed[0][3] == 1, ('old candidate list reused', trace[-12:])
        result2 = complete(late, f2)
        exp.record('candidate_list_renewed', F1=f1, F2=f2, tablet=late, old_list=paused,
                   fresh_list=renewed[0], layout=result2)

        # A later committed tablet must remain invisible to enumeration until
        # the loop has a readable horizon covering this new target.
        waited = create('created_before_horizon'); names[waited] = 'created_before_horizon'
        control('target').write_text(str(waited))
        control('horizon').write_text(str(f2))
        f3 = freeze(f2)
        wait(lambda: sum(e[0] == 'BLOCK' and e[1] == f3 for e in events()) >= 3,
             'horizon-not-ready branch did not repeat')
        blocked = events()
        assert not any(e[0] == 'ENUM' and e[1] == f3 for e in blocked), blocked[-12:]
        assert progress()[2] < f3 and not files(waited, f3)
        control('horizon').unlink()
        wait(lambda: any(e[0] == 'ENUM' and e[1] == f3 and e[3] == 1 for e in events()),
             'no fresh candidates after horizon')
        result3 = complete(waited, f3)
        exp.record('PASS', case='merge_candidates', partial_old_list_renewed=True,
                   no_enumeration_below_horizon=True, fresh_object_included=True,
                   actual_major_files=True, global_completion=True, F2=f2, F3=f3, layout=result3)
    finally:
        control('release').touch()
        control('horizon').unlink(missing_ok=True)
        exp.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True)
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    run(args.binary)
