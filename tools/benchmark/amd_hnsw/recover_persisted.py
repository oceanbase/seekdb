#!/usr/bin/env python3
"""Recover the already-stopped owned observer; never sends any process signal."""
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import socket
import subprocess
import time

ROOT = Path('/workspace/amd107')
EXECUTABLE = ROOT / 'runtime108/observer'
BASE = ROOT / 'candidate-quality64'
EXPECTED_BRIDGE = '65c69d59001855d4a5c87ff037f38f66557020499e7d40b8dcd62f6e34b76f8d'


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def digest(path):
    result = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1048576), b''):
            result.update(block)
    return result.hexdigest()


def state(path):
    try:
        fields = path.read_text().rsplit(') ', 1)[1].split()
        return {'state': fields[0], 'start_ticks': fields[19]}
    except FileNotFoundError:
        return None


def old_process(pid):
    result = {'pid': pid, 'leader': state(Path('/proc/%d/stat' % pid)), 'threads': []}
    try:
        paths = list(Path('/proc/%d/task' % pid).iterdir())
    except FileNotFoundError:
        paths = []
    for path in paths:
        observed = state(path / 'stat')
        if observed:
            result['threads'].append({'tid': int(path.name), **observed})
    result['active_threads'] = [row for row in result['threads'] if row['state'] not in ('Z', 'X', 'x')]
    result['fully_exited_or_zombie_only'] = result['leader'] is None or not result['active_threads']
    return result


def listener_owners():
    listeners = []
    for name in ('tcp', 'tcp6'):
        path = Path('/proc/net') / name
        if not path.exists():
            continue
        for line in path.read_text().splitlines()[1:]:
            fields = line.split()
            if len(fields) > 9 and fields[1].rsplit(':', 1)[1].upper() == '0B42' and fields[3] == '0A':
                listeners.append({'table': name, 'address': fields[1], 'inode': fields[9], 'owners': []})
    if listeners:
        by_inode = {row['inode']: row for row in listeners}
        for entry in Path('/proc').iterdir():
            if not entry.name.isdigit():
                continue
            try:
                fds = list((entry / 'fd').iterdir())
            except (FileNotFoundError, PermissionError, ProcessLookupError):
                continue
            for fd in fds:
                try:
                    target = os.readlink(fd)
                except (FileNotFoundError, PermissionError, ProcessLookupError):
                    continue
                if target.startswith('socket:[') and target.endswith(']'):
                    inode = target[8:-1]
                    if inode in by_inode:
                        by_inode[inode]['owners'].append({'pid': int(entry.name), 'fd': int(fd.name)})
    return listeners


def port_connects():
    try:
        connection = socket.create_connection(('127.0.0.1', 2882), timeout=0.5)
    except OSError:
        return False
    connection.close()
    return True


def save(path, receipt):
    path.write_text(json.dumps(receipt, indent=2), encoding='utf-8')


def main():
    launch_path = ROOT / 'candidate-quality-launch.json'
    receipt_path = ROOT / 'candidate-persisted-launch.json'
    output_path = ROOT / 'candidate-persisted.out'
    pid_path = ROOT / 'candidate-persisted.pid'
    require(not any(path.exists() for path in (receipt_path, output_path, pid_path)),
            'Persistence outputs already exist; refuse a duplicate start')
    launch = json.loads(launch_path.read_text())
    pid, argv, settings = launch['pid'], launch['argv'], launch['settings']
    require(type(pid) is int and pid > 1, 'Invalid original owned PID')
    require(isinstance(argv, list) and argv and argv[0] == str(EXECUTABLE), 'Unexpected saved executable')
    require(argv.count('--base-dir=' + str(BASE)) == 1 and argv.count('--port=2882') == 1,
            'Saved argv must identify the existing task database and port2882')
    require(isinstance(settings, dict) and all(isinstance(k, str) and isinstance(v, str)
                                             for k, v in settings.items()), 'Malformed original settings')
    for key in ('SEEKDB_CUVS_BRIDGE', 'LD_LIBRARY_PATH', 'TMPDIR'):
        require(bool(settings.get(key)), 'Missing saved setting ' + key)
    require(BASE.is_dir() and any(BASE.iterdir()), 'Existing populated database directory required')
    base_before = BASE.stat()
    bridge = Path(settings['SEEKDB_CUVS_BRIDGE'])
    require(bridge.is_absolute() and digest(bridge) == launch['bridge_sha256'] == EXPECTED_BRIDGE,
            'Bridge SHA mismatch')
    require(digest(EXECUTABLE) == launch['executable_sha256'], 'Observer SHA mismatch')
    initial = old_process(pid)
    deadline = time.monotonic() + 45
    while True:
        observed = old_process(pid)
        listeners = listener_owners()
        connected = port_connects()
        if observed['fully_exited_or_zombie_only'] and not listeners and not connected:
            break
        if time.monotonic() >= deadline:
            print(json.dumps({'blocked': True, 'old_process': observed, 'port2882_listeners': listeners,
                              'port2882_connects': connected, 'signals_sent': []}))
            raise RuntimeError('Original threads or port still active; no signal or replacement started')
        # A different owner is never stopped, and there is no broad process-name kill.
        if any(owner['pid'] != pid for row in listeners for owner in row['owners']):
            print(json.dumps({'blocked': True, 'port2882_listeners': listeners, 'signals_sent': []}))
            raise RuntimeError('Port2882 has another owner; refuse replacement')
        time.sleep(0.5)
    require((BASE.stat().st_dev, BASE.stat().st_ino) == (base_before.st_dev, base_before.st_ino),
            'Database directory identity changed during this recovery check')
    require(digest(bridge) == EXPECTED_BRIDGE and digest(EXECUTABLE) == launch['executable_sha256'],
            'Runtime hashes changed during recovery')
    env = os.environ.copy()
    env.update(settings)
    output = output_path.open('xb')
    try:
        proc = subprocess.Popen(argv, env=env, stdout=output, stderr=subprocess.STDOUT, start_new_session=True)
    finally:
        output.close()
    receipt = {'pid': proc.pid, 'sql_ready': False, 'argv': argv, 'settings': settings,
               'executable_sha256': digest(EXECUTABLE), 'bridge_sha256': digest(bridge),
               'original_launch': str(launch_path), 'original_pid': pid,
               'old_process_before_wait': initial, 'old_process_at_recovery_start': observed,
               'port2882_listeners_before_start': listeners, 'port2882_connects_before_start': connected,
               'signals_sent': [], 'cloud_instance_restarted': False, 'new_cloud_instance_created': False,
               'database_path': str(BASE), 'database_directory_device_at_recovery': base_before.st_dev,
               'database_directory_inode_at_recovery': base_before.st_ino,
               'database_directory_deleted_or_created': False, 'index_ddl_issued': False,
               'argv_reused_exactly': True, 'saved_settings_reused_exactly': True,
               'environment_source': 'current shell environment overlaid with exact saved launch settings',
               'complete_original_process_environment_reused': False,
               'historical_database_inode_continuity_proven': False,
               'started_utc': dt.datetime.now(dt.timezone.utc).isoformat(), 'stdout': str(output_path),
               'scope': 'Recover same existing DB after owned SIGTERM; persistence/recall check is separate'}
    pid_path.write_text(str(proc.pid))
    save(receipt_path, receipt)
    for _ in range(150):
        if proc.poll() is not None:
            receipt['exit_code'] = proc.returncode
            save(receipt_path, receipt)
            raise RuntimeError('Recovered observer exited: ' + str(proc.returncode))
        try:
            check = subprocess.run(['mysql', '-h127.0.0.1', '-P2882', '-uroot', '-N',
                                    '-e', 'SELECT VERSION();'], capture_output=True, text=True, timeout=3)
        except subprocess.TimeoutExpired:
            time.sleep(1)
            continue
        if check.returncode == 0:
            receipt.update(sql_ready=True, version=check.stdout.strip())
            save(receipt_path, receipt)
            print(json.dumps(receipt))
            return
        time.sleep(1)
    raise RuntimeError('Recovered observer did not become SQL ready; inspect owned PID and output')


if __name__ == '__main__':
    main()
