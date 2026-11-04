#!/usr/bin/env python3
"""Restart the verified task observer with the SAME database and configuration.

Run only after the parent has completed formal measurements and full export.
This script performs no DDL, index rebuild, instance operation or data deletion.
"""
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import signal
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
    hasher = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1048576), b''):
            hasher.update(block)
    return hasher.hexdigest()


def process_state(pid):
    try:
        raw = Path('/proc/%d/stat' % pid).read_text()
    except FileNotFoundError:
        return None
    fields = raw.rsplit(') ', 1)[1].split()
    return {'state': fields[0], 'start_ticks': fields[19]}


def write_receipt(path, receipt):
    path.write_text(json.dumps(receipt, indent=2), encoding='utf-8')


def main():
    launch_path = ROOT / 'candidate-quality-launch.json'
    receipt_path = ROOT / 'candidate-persisted-launch.json'
    output_path = ROOT / 'candidate-persisted.out'
    pid_path = ROOT / 'candidate-persisted.pid'
    require(not any(path.exists() for path in (receipt_path, output_path, pid_path)),
            'Persistence restart outputs already exist; refuse duplicate restart')
    require(BASE.is_dir() and any(BASE.iterdir()), 'Existing populated database directory required')
    base_stat = BASE.stat()
    launch = json.loads(launch_path.read_text())
    pid, argv, settings = launch['pid'], launch['argv'], launch['settings']
    require(type(pid) is int and pid > 1, 'Invalid owned observer PID')
    require(isinstance(argv, list) and argv and argv[0] == str(EXECUTABLE), 'Unexpected executable')
    require(argv.count('--base-dir=' + str(BASE)) == 1 and argv.count('--port=2882') == 1,
            'Launch must identify the existing candidate-quality64 database and port2882')
    require(isinstance(settings, dict) and all(isinstance(k, str) and isinstance(v, str)
                                             for k, v in settings.items()), 'Invalid saved settings')
    for key in ('SEEKDB_CUVS_BRIDGE', 'LD_LIBRARY_PATH', 'TMPDIR'):
        require(bool(settings.get(key)), 'Missing original setting ' + key)
    bridge = Path(settings['SEEKDB_CUVS_BRIDGE'])
    require(bridge.is_absolute() and bridge.is_file(), 'Invalid bridge path')
    require(digest(bridge) == launch['bridge_sha256'] == EXPECTED_BRIDGE, 'Bridge hash mismatch')
    require(digest(EXECUTABLE) == launch['executable_sha256'], 'Observer hash mismatch')
    original = process_state(pid)
    require(original is not None and original['state'] != 'Z', 'Original live observer required')
    require(os.readlink('/proc/%d/exe' % pid) == str(EXECUTABLE), 'PID executable ownership mismatch')
    live_argv = [os.fsdecode(value) for value in
                 Path('/proc/%d/cmdline' % pid).read_bytes().rstrip(b'\0').split(b'\0')]
    require(live_argv == argv, 'Actual argv differs from original launch')
    raw_environment = Path('/proc/%d/environ' % pid).read_bytes()
    env = {}
    for item in raw_environment.split(b'\0'):
        if item:
            key, value = item.split(b'=', 1)
            env[os.fsdecode(key)] = os.fsdecode(value)
    require(all(env.get(key) == value for key, value in settings.items()),
            'Live observer environment differs from saved settings')
    # Reuse the complete verified process environment, not just selected settings.
    pidfd = os.pidfd_open(pid) if hasattr(os, 'pidfd_open') else None
    try:
        current = process_state(pid)
        require(current is not None and current['start_ticks'] == original['start_ticks']
                and current['state'] != 'Z', 'Owned observer changed during verification')
        if pidfd is not None and hasattr(signal, 'pidfd_send_signal'):
            signal.pidfd_send_signal(pidfd, signal.SIGTERM)
        else:
            os.kill(pid, signal.SIGTERM)
        deadline = time.monotonic() + 60
        while time.monotonic() < deadline:
            current = process_state(pid)
            if current is None or current['state'] == 'Z' or current['start_ticks'] != original['start_ticks']:
                break
            time.sleep(0.25)
        else:
            raise RuntimeError('Owned observer did not exit within60s; no SIGKILL or replacement started')
    finally:
        if pidfd is not None:
            os.close(pidfd)
    try:
        connection = socket.create_connection(('127.0.0.1', 2882), timeout=1)
    except OSError:
        pass
    else:
        connection.close()
        raise RuntimeError('Port2882 remains occupied; no replacement started')
    require(BASE.is_dir() and (BASE.stat().st_dev, BASE.stat().st_ino) ==
            (base_stat.st_dev, base_stat.st_ino), 'Original database directory identity changed')
    require(digest(bridge) == EXPECTED_BRIDGE and digest(EXECUTABLE) == launch['executable_sha256'],
            'Runtime artifacts changed after shutdown')
    output = output_path.open('xb')
    try:
        proc = subprocess.Popen(argv, env=env, stdout=output, stderr=subprocess.STDOUT, start_new_session=True)
    finally:
        output.close()
    receipt = {'pid': proc.pid, 'sql_ready': False, 'argv': argv, 'settings': settings,
               'executable_sha256': digest(EXECUTABLE), 'bridge_sha256': digest(bridge),
               'original_launch': str(launch_path), 'original_pid_stopped': pid,
               'original_process_start_ticks': original['start_ticks'], 'stopped_process_state': current,
               'database_path': str(BASE), 'database_device': base_stat.st_dev,
               'database_inode': base_stat.st_ino, 'database_directory_reused': True,
               'database_directory_deleted_or_created': False, 'index_ddl_issued': False,
               'cloud_instance_restarted': False, 'new_cloud_instance_created': False,
               'argv_reused_exactly': True, 'complete_original_environment_reused': True,
               'saved_settings_reused_exactly': True,
               'original_process_environment_sha256': hashlib.sha256(raw_environment).hexdigest(),
               'started_utc': dt.datetime.now(dt.timezone.utc).isoformat(), 'stdout': str(output_path),
               'scope': 'Owned server process restart only; post-restart index/recall validation remains separate'}
    pid_path.write_text(str(proc.pid))
    write_receipt(receipt_path, receipt)
    for _ in range(150):
        if proc.poll() is not None:
            receipt['exit_code'] = proc.returncode
            write_receipt(receipt_path, receipt)
            raise RuntimeError('Restarted observer exited: ' + str(proc.returncode))
        try:
            check = subprocess.run(['mysql', '-h127.0.0.1', '-P2882', '-uroot', '-N',
                                    '-e', 'SELECT VERSION();'], capture_output=True, text=True, timeout=3)
        except subprocess.TimeoutExpired:
            time.sleep(1)
            continue
        if check.returncode == 0:
            receipt.update(sql_ready=True, version=check.stdout.strip())
            write_receipt(receipt_path, receipt)
            print(json.dumps(receipt))
            return
        time.sleep(1)
    raise RuntimeError('Restarted observer did not become SQL ready; inspect owned PID/output')


if __name__ == '__main__':
    main()
