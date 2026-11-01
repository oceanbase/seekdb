"""Manage the course-image binary without replacing another local SQL server."""
import argparse
import os
from pathlib import Path
import socket
import subprocess
import sys
import sdb


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('base_dir', type=Path)
    parser.add_argument('--stop', action='store_true')
    args = parser.parse_args()
    base = args.base_dir.expanduser().resolve()
    binary = (Path(os.environ.get('SEEKDB_HOME', '/seekdb_workspace'))/'bin/seekdb').resolve()
    manager = Path(__file__).with_name('sdb.py')
    if args.stop:
        return subprocess.call([sys.executable,str(manager),'stop','--base-dir',str(base)])
    if not binary.is_file():
        raise RuntimeError('Course seekdb binary missing: '+str(binary))
    with socket.socket() as connection:
        connection.settimeout(2)
        occupied = connection.connect_ex(('127.0.0.1',2881)) == 0
    if occupied:
        try:
            expected = sdb.read_instance_binary(base)
            pid = sdb.inspect_instance_process(base,expected)
        except (OSError,RuntimeError,ValueError):
            pid = None
        if not pid or expected != binary:
            raise RuntimeError('Port 2881 belongs to another server; stop that instance explicitly first.')
    else:
        if (base/sdb.INSTANCE_MARKER_NAME).exists():
            expected = sdb.read_instance_binary(base)
            if expected != binary:
                raise RuntimeError('Existing instance uses a different binary: '+str(expected))
            try:
                live_pid = sdb.inspect_instance_process(base,expected)
            except (OSError,RuntimeError):
                live_pid = None
            if live_pid:
                subprocess.run([sys.executable,str(manager),'wait-ready','--client','mysql',
                                '--base-dir',str(base),'--port','2881','--timeout','180'],check=True)
                return 0
            # The manager checks ownership before removing stale PID files.
            subprocess.run([sys.executable,str(manager),'stop','--base-dir',str(base)],check=True)
        home = binary.parent.parent
        env = dict(os.environ)
        env['LD_LIBRARY_PATH'] = ':'.join([str(home/'bridge'),'/opt/hipvs/lib','/opt/rocm/lib',env.get('LD_LIBRARY_PATH','')])
        subprocess.run([sys.executable,str(manager),'start','--binary',str(binary),
                        '--base-dir',str(base),'--port','2881','--nodaemon','--parameter','_memory_budget=16G',
                        '--parameter','datafile_size=20G','--parameter','datafile_maxsize=40G',
                        '--parameter','log_disk_size=10G'],env=env,check=True)
    subprocess.run([sys.executable,str(manager),'wait-ready','--client','mysql',
                    '--base-dir',str(base),'--port','2881','--timeout','180'],check=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
