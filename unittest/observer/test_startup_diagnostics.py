#!/usr/bin/env python3
"""Linux startup-diagnostic regression; all repository Python runs unprivileged.

From a non-root shell, set up fresh PID/network namespaces using trusted host tools:
  outside_netns=$(stat -Lc %i /proc/self/ns/net)
  sudo unshare --net --pid --fork --mount-proc --kill-child=SIGKILL -- \
    setpriv --reuid="$(id -u)" --regid="$(id -g)" --clear-groups \
      --bounding-set=-all --inh-caps=-all --ambient-caps=-all --no-new-privs -- \
    env -i PATH=/usr/bin:/bin LANG=C.UTF-8 LC_ALL=C.UTF-8 \
      TELEMETRY_MODE=0 TELEMETRY_ENABLED=false \
    /usr/bin/python3 -I -B unittest/observer/test_startup_diagnostics.py \
      --binary /path/to/seekdb --work-dir /path/to/writable-scratch \
      --outside-netns "$outside_netns"

Use a disposable, credential-free workspace and an outer resource/time limit.
Root is used only by unshare/setpriv, never to execute this script or inspect/clean
its fixtures. As PID-namespace init, exiting the runner also kills descendants
that escape a process group. Only fresh test-created temporary files are removed.
Requires Linux, Python 3, procfs, and util-linux unshare/setpriv.
"""
import argparse
from contextlib import ExitStack
import os
from pathlib import Path
import resource
import signal
import socket
import stat
import subprocess
import tempfile
import time
import unittest

SAFE_ENV = {
    'PATH': '/usr/bin:/bin', 'LANG': 'C.UTF-8', 'LC_ALL': 'C.UTF-8',
    'TELEMETRY_MODE': '0', 'TELEMETRY_ENABLED': 'false',
}
MAX_OUTPUT = 1024 * 1024
MAX_FILE = 8 * MAX_OUTPUT
BINARY = WORK_DIR = OUTSIDE_NETNS = None


def require_unprivileged():
    if (0 in os.getresuid() or 0 in os.getresgid() or os.getgroups()):
        raise RuntimeError('Use a non-root UID/GID with no supplementary groups')
    status = dict(line.split(':', 1) for line in Path('/proc/self/status').read_text().splitlines()
                  if ':' in line)
    if status.get('NoNewPrivs', '').strip() != '1':
        raise RuntimeError('no-new-privileges is required')
    if any(int(status.get(key, '1').strip(), 16)
           for key in ('CapEff', 'CapPrm', 'CapInh', 'CapAmb')):
        raise RuntimeError('The test runner must have no effective/permitted/inherited/ambient capabilities')


def require_isolated_runner(outside_netns):
    require_unprivileged()
    if os.getpid() != 1:
        raise RuntimeError('Run as init of a fresh PID namespace using the documented launcher')
    if os.stat('/proc/self/ns/net').st_ino == outside_netns:
        raise RuntimeError('A separate network namespace is required')
    if {name for _, name in socket.if_nameindex()} != {'lo'}:
        raise RuntimeError('The test namespace must contain only loopback')
    if dict(os.environ) != SAFE_ENV:
        raise RuntimeError('Start the runner with the documented clean environment')


def open_directory(name, *, dir_fd=None):
    return os.open(name, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC,
                   dir_fd=dir_fd)


def read_bounded(stream, label):
    stream.seek(0)
    content = stream.read(MAX_OUTPUT + 1)
    if len(content) > MAX_OUTPUT:
        raise ValueError(f'{label} exceeds the diagnostic output limit')
    return content.decode('utf-8', errors='replace')


def read_runtime_log(base_fd):
    """Read only a bounded regular log beneath the already-open fixture directory."""
    require_unprivileged()
    with ExitStack() as handles:
        try:
            log_fd = open_directory('log', dir_fd=base_fd)
        except FileNotFoundError:
            return ''
        handles.callback(os.close, log_fd)
        try:
            fd = os.open('seekdb.log', os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC,
                         dir_fd=log_fd)
        except FileNotFoundError:
            return ''
        handles.callback(os.close, fd)
        info = os.fstat(fd)
        if not stat.S_ISREG(info.st_mode):
            raise ValueError('seekdb.log must be a regular file')
        if info.st_size > MAX_OUTPUT:
            raise ValueError('seekdb.log exceeds the diagnostic output limit')
        stream = handles.enter_context(os.fdopen(fd, 'rb', closefd=False))
        return read_bounded(stream, 'seekdb.log')


class LogReaderTest(unittest.TestCase):
    def setUp(self):
        require_unprivileged()
        self.temporary = tempfile.TemporaryDirectory(prefix='seekdb-reader-', dir=WORK_DIR)
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name) / 'fixture'
        self.base.mkdir()
        self.base_fd = open_directory(self.base)
        self.addCleanup(os.close, self.base_fd)

    def make_log(self, content=b'normal diagnostic\n'):
        log = self.base / 'log'
        log.mkdir()
        path = log / 'seekdb.log'
        path.write_bytes(content)
        return path

    def test_missing_directory(self):
        self.assertEqual(read_runtime_log(self.base_fd), '')

    def test_missing_file(self):
        (self.base / 'log').mkdir()
        self.assertEqual(read_runtime_log(self.base_fd), '')

    def test_regular_log_and_size_boundaries(self):
        path = self.make_log()
        for content in (b'', b'x', '中文诊断\n'.encode(), b'x' * MAX_OUTPUT):
            with self.subTest(size=len(content)):
                path.write_bytes(content)
                self.assertEqual(read_runtime_log(self.base_fd), content.decode())
        os.fstat(self.base_fd)  # The reader must not close its caller's descriptor.

    def test_leaf_symlink(self):
        (self.base / 'log').mkdir()
        target = self.base / 'outside-marker'
        target.write_text('MUST_NOT_BE_READ')
        (self.base / 'log/seekdb.log').symlink_to(target)
        with self.assertRaises(OSError):
            read_runtime_log(self.base_fd)

    def test_parent_symlink(self):
        target = self.base / 'outside'
        target.mkdir()
        (target / 'seekdb.log').write_text('MUST_NOT_BE_READ')
        (self.base / 'log').symlink_to(target, target_is_directory=True)
        with self.assertRaises(OSError):
            read_runtime_log(self.base_fd)

    def test_multihop_symlink(self):
        (self.base / 'log').mkdir()
        target = self.base / 'outside-marker'
        target.write_text('MUST_NOT_BE_READ')
        alias = self.base / 'alias'
        alias.symlink_to(target)
        (self.base / 'log/seekdb.log').symlink_to(alias)
        with self.assertRaises(OSError):
            read_runtime_log(self.base_fd)

    def test_fifo_does_not_block(self):
        (self.base / 'log').mkdir()
        os.mkfifo(self.base / 'log/seekdb.log')
        started = time.monotonic()
        with self.assertRaises(ValueError):
            read_runtime_log(self.base_fd)
        self.assertLess(time.monotonic() - started, 1)

    def test_directory_is_rejected(self):
        (self.base / 'log/seekdb.log').mkdir(parents=True)
        with self.assertRaises(ValueError):
            read_runtime_log(self.base_fd)

    def test_oversized_log_is_rejected(self):
        self.make_log(b'x' * (MAX_OUTPUT + 1))
        with self.assertRaises(ValueError):
            read_runtime_log(self.base_fd)

    def test_rejections_do_not_leak_descriptors(self):
        (self.base / 'log/seekdb.log').mkdir(parents=True)
        before = len(os.listdir('/proc/self/fd'))
        for _ in range(8):
            with self.assertRaises(ValueError):
                read_runtime_log(self.base_fd)
        self.assertEqual(len(os.listdir('/proc/self/fd')), before)

    def test_pinned_directory_survives_path_replacement(self):
        self.make_log(b'original diagnostic')
        self.base.rename(self.base.parent / 'original-fixture')
        self.base.mkdir()
        outside = self.base.parent / 'outside'
        outside.mkdir()
        (outside / 'seekdb.log').write_text('MUST_NOT_BE_READ')
        (self.base / 'log').symlink_to(outside, target_is_directory=True)
        self.assertEqual(read_runtime_log(self.base_fd), 'original diagnostic')


class StartupDiagnosticsTest(unittest.TestCase):
    def assert_directory_unchanged(self, parent_fd, name, original_fd):
        current = os.stat(name, dir_fd=parent_fd, follow_symlinks=False)
        original = os.fstat(original_fd)
        self.assertTrue(stat.S_ISDIR(current.st_mode))
        self.assertEqual((current.st_dev, current.st_ino), (original.st_dev, original.st_ino))

    def check_redirected_diagnostic(self, level):
        require_isolated_runner(OUTSIDE_NETNS)
        with tempfile.TemporaryDirectory(prefix='seekdb-startup-diagnostic-', dir=WORK_DIR) as temporary:
            base = Path(temporary)
            sstable = base / 'store/sstable'
            redo = base / 'store/redo'
            sstable.mkdir(parents=True)
            redo.mkdir()
            (sstable / 'block_file').touch()
            with ExitStack() as handles:
                work_fd = open_directory(WORK_DIR)
                handles.callback(os.close, work_fd)
                base_fd = open_directory(base)
                handles.callback(os.close, base_fd)
                store_fd = open_directory('store', dir_fd=base_fd)
                handles.callback(os.close, store_fd)
                sstable_fd = open_directory('sstable', dir_fd=store_fd)
                handles.callback(os.close, sstable_fd)
                redo_fd = open_directory('redo', dir_fd=store_fd)
                handles.callback(os.close, redo_fd)
                original_block = os.stat('block_file', dir_fd=sstable_fd, follow_symlinks=False)
                command = [str(BINARY), '--nodaemon', f'--base-dir={base}',
                           '--port=2881', f'--log-level={level}']
                for parameter in (f'syslog_level={level}', 'cpu_count=2', 'memory_budget=2G',
                                  'datafile_size=32M', 'datafile_maxsize=512M',
                                  'log_disk_size=2G', 'syslog_disk_size=64M'):
                    command.extend(['--parameter', parameter])
                environment = dict(SAFE_ENV, HOME=str(base), TMPDIR=str(base))
                stdout = handles.enter_context(tempfile.TemporaryFile(dir=WORK_DIR))
                stderr = handles.enter_context(tempfile.TemporaryFile(dir=WORK_DIR))
                process = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=stdout, stderr=stderr,
                                           env=environment, start_new_session=True)
                try:
                    process.wait(timeout=20)
                except subprocess.TimeoutExpired:
                    self.fail('inconsistent deployment did not fail promptly')
                finally:
                    if process.poll() is None:
                        try:
                            os.killpg(process.pid, signal.SIGKILL)
                        except ProcessLookupError:
                            pass
                        process.wait(timeout=2)
                # Regular capture files avoid pipe EOF hangs from escaped descendants.
                # The enclosing PID namespace kills such descendants when this runner exits.
                output = read_bounded(stdout, 'stdout') + read_bounded(stderr, 'stderr')
                output += read_runtime_log(base_fd)
                self.assertEqual(process.returncode, 1, output)
                reason = 'The status of deployment environment is not consistent'
                self.assertEqual(output.count(reason), 1, output)
                expected = (
                    f'{reason}. Please clear the directories and restart. ret=-4016\n'
                    f'    base-dir: {base}{os.sep}\n'
                    '    data-dir: store\n'
                    f'    redo-dir: {Path("store") / "redo"}\n')
                self.assertEqual(output.count(expected), 1, output)
                self.assert_directory_unchanged(work_fd, base.name, base_fd)
                # Also bind the original absolute CLI path if an ancestor was replaced.
                named_base = os.stat(base, follow_symlinks=False)
                opened_base = os.fstat(base_fd)
                self.assertTrue(stat.S_ISDIR(named_base.st_mode))
                self.assertEqual((named_base.st_dev, named_base.st_ino),
                                 (opened_base.st_dev, opened_base.st_ino))
                self.assert_directory_unchanged(base_fd, 'store', store_fd)
                self.assert_directory_unchanged(store_fd, 'sstable', sstable_fd)
                self.assert_directory_unchanged(store_fd, 'redo', redo_fd)
                current = os.stat('block_file', dir_fd=sstable_fd, follow_symlinks=False)
                self.assertTrue(stat.S_ISREG(current.st_mode))
                self.assertEqual((current.st_dev, current.st_ino, current.st_size),
                                 (original_block.st_dev, original_block.st_ino, 0))
                with os.scandir(redo_fd) as entries:
                    self.assertIsNone(next(entries, None), 'redo must remain empty')

    def test_warn_with_redirected_stderr(self):
        self.check_redirected_diagnostic('WARN')

    def test_debug_with_redirected_stderr(self):
        self.check_redirected_diagnostic('DEBUG')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--work-dir', type=Path, required=True)
    parser.add_argument('--outside-netns', type=int, required=True)
    args = parser.parse_args()
    try:
        require_isolated_runner(args.outside_netns)
        BINARY = args.binary.resolve(strict=True)
        WORK_DIR = args.work_dir.resolve(strict=True)
        if not BINARY.is_file() or not os.access(BINARY, os.X_OK):
            raise ValueError('an existing executable binary is required')
        if not WORK_DIR.is_dir() or not os.access(WORK_DIR, os.W_OK | os.X_OK):
            raise ValueError('scratch directory must already be writable by the non-root runner')
        OUTSIDE_NETNS = args.outside_netns
    except (OSError, RuntimeError, ValueError) as error:
        parser.error(str(error))
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    resource.setrlimit(resource.RLIMIT_FSIZE, (MAX_FILE, MAX_FILE))
    unittest.main(argv=[__file__], verbosity=2)
