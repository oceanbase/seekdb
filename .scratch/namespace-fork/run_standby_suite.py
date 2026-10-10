#!/usr/bin/env python3
"""Local standby regressions, using the current project and isolated test data."""
import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import time

local = Path(__file__).resolve().parent
project = local.parent.parent
obtest = project / 'tools/obtest'
data_root = Path('/data/1/nijia.nj/test/namespace_standby_20261003_v1/work')
cases = ['catalog_read_view', 'basic', 'cascade_standby', 'one_primary_multi_standby', 'standby_restart',
         'standby_sstable_replay', 'switchover_roundtrip',
         'failover_switchover_reentry', 'tls_standby', 'namespace_fork_local',
         'publication_initial', 'publication_child',
         'publication_restart_initial', 'publication_restart_child', 'schema_history']
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--binary', required=True)
parser.add_argument('--case', action='append', choices=cases)
parser.add_argument('--log-dir', type=Path, default=local / 'standby-results/suite')
args = parser.parse_args()
args.log_dir.mkdir(parents=True, exist_ok=True)


def stop_owned():
    owned = []
    for entry in Path('/proc').iterdir():
        if not entry.name.isdigit(): continue
        try: exe = (entry / 'exe').resolve()
        except OSError: continue
        if str(exe).startswith(str(data_root) + '/') and '/bin/observer' in str(exe):
            owned.append(int(entry.name))
    for pid in owned:
        try: os.kill(pid, signal.SIGTERM)
        except ProcessLookupError: pass
    deadline = time.monotonic() + 3
    while time.monotonic() < deadline and any(Path(f'/proc/{pid}/exe').exists() for pid in owned):
        time.sleep(.1)
    for pid in owned:
        try:
            if Path(f'/proc/{pid}/exe').exists(): os.kill(pid, signal.SIGKILL)
        except ProcessLookupError: pass


subprocess.run(['cp', '--reflink=auto', str(Path(args.binary).resolve()), str(obtest / 'bin/observer')], check=True)
results = []
for case in args.case or [name for name in cases if name != 'catalog_read_view']:
    stop_owned()
    if case == 'tls_standby':
        with (args.log_dir / 'generate_wallet.log').open('w') as log:
            subprocess.run(['./generate_wallet.sh'], cwd=obtest, stdout=log, stderr=subprocess.STDOUT, check=True)
            subprocess.run(['openssl', 'verify', '-CAfile', 'wallet/ca.pem', 'wallet/cert.pem'], cwd=obtest, stdout=log, stderr=subprocess.STDOUT, check=True)
    started = time.monotonic()
    extended = case in ('namespace_fork_local', 'catalog_read_view', 'schema_history') or case.startswith('publication_')
    file = 'namespace_setup_template_local' if extended else case
    if case == 'catalog_read_view':
        os.environ['SEEKDB_CATALOG_GC_TRIGGER'] = str((args.log_dir / 'gc-trigger').resolve())
        Path(os.environ['SEEKDB_CATALOG_GC_TRIGGER']).unlink(missing_ok=True)
    with (args.log_dir / (case + '.log')).open('w') as log:
        result = subprocess.run(['./mytest', 't/stanby/' + file + '.test'], cwd=obtest, stdout=log, stderr=subprocess.STDOUT)
        if result.returncode == 0 and case == 'catalog_read_view':
            result = subprocess.run(['python3', str(local / 'standby_catalog_view_probe.py')], cwd=project, stdout=log, stderr=subprocess.STDOUT)
        if result.returncode == 0 and case == 'standby_sstable_replay':
            result = subprocess.run(['python3', str(local / 'standby_major_progress_probe.py')], cwd=project, stdout=log, stderr=subprocess.STDOUT)
        if result.returncode == 0 and case == 'schema_history':
            result = subprocess.run(['python3', str(local / 'standby_schema_history_probe.py'),
                                    '--binary', str(Path(args.binary).resolve())],
                                   cwd=project, stdout=log, stderr=subprocess.STDOUT)
        if result.returncode == 0 and case == 'namespace_fork_local':
            result = subprocess.run(['python3', str(local / 'standby_refresh_race_probe.py')], cwd=project, stdout=log, stderr=subprocess.STDOUT)
            if result.returncode == 0:
                result = subprocess.run(['python3', str(local / 'standby_namespace_follow.py')], cwd=project, stdout=log, stderr=subprocess.STDOUT)
        if result.returncode == 0 and case.startswith('publication_'):
            command = ['python3', str(local / 'standby_publication_failover_probe.py'),
                       '--binary', str(Path(args.binary).resolve()), '--owner', case.rsplit('_', 1)[-1]]
            if 'restart' in case: command.append('--restart-standby')
            result = subprocess.run(command, cwd=project, stdout=log, stderr=subprocess.STDOUT)
    if case == 'catalog_read_view': os.environ.pop('SEEKDB_CATALOG_GC_TRIGGER', None)
    if result.returncode:
        # The next case redeploys these directories. Preserve diagnostics first.
        evidence = args.log_dir / (case + '-failure')
        evidence.mkdir(exist_ok=True)
        for node in ('db_p.z1.obs0', 'db_s.z1.obs0'):
            for source in (data_root / node / 'log').glob('*'):
                if not source.is_file() or source.suffix not in ('.log', '.wf'): continue
                with source.open('rb') as stream:
                    stream.seek(max(0, source.stat().st_size - 4 * 1024 * 1024))
                    (evidence / (node + '-' + source.name)).write_bytes(stream.read())
    row = dict(case=case, exit_code=result.returncode, elapsed_s=round(time.monotonic()-started, 2))
    results.append(row)
    print(json.dumps(row), flush=True)
    (args.log_dir / 'results.json').write_text(json.dumps(results, indent=2))
stop_owned()
raise SystemExit(0 if all(r['exit_code'] == 0 for r in results) else 1)
