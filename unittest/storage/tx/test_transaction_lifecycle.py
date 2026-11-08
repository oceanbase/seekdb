"""Native transaction regressions, including clean restart and crash recovery.

Run with a release binary and PyMySQL installed:
  python3 unittest/storage/tx/test_transaction_lifecycle.py --binary /path/to/seekdb
The test owns a new temporary instance; it never connects to an existing server.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest

try:
    import pymysql
except ModuleNotFoundError:
    pymysql = None


class TransactionLifecycle(unittest.TestCase):
    binary = None
    client = None

    @classmethod
    def manage(cls, *arguments):
        command = [sys.executable, str(cls.repository / '.github/script/seekdb/sdb.py'),
                   *map(str, arguments)]
        subprocess.run(command, cwd=cls.repository, check=True, timeout=180)

    @classmethod
    def start(cls):
        cls.manage('start', '--binary', cls.binary, '--base-dir', cls.instance, '--port', 2885,
                   '--parameter', 'cpu_count=2', '--parameter', 'memory_budget=1G')
        cls.manage('wait-ready', '--client', cls.client, '--base-dir', cls.instance,
                   '--host', '127.0.0.1', '--port', 2885, '--user', 'root', '--timeout', 120)

    @classmethod
    def connect(cls, database='tx_lifecycle'):
        return pymysql.connect(unix_socket=str(cls.instance / 'run/sql.sock'),
                               user='root', database=database, autocommit=True,
                               connect_timeout=10, read_timeout=30, write_timeout=30)

    @classmethod
    def setUpClass(cls):
        if cls.binary is None or pymysql is None:
            raise unittest.SkipTest('run directly with --binary and PyMySQL installed')
        cls.repository = Path(__file__).resolve().parents[3]
        cls.instance = Path(tempfile.mkdtemp(prefix='seekdb-tx-lifecycle-'))
        cls.addClassCleanup(cls.cleanup)
        cls.start()
        with cls.connect(database=None) as connection:
            with connection.cursor() as cursor:
                cursor.execute('CREATE DATABASE tx_lifecycle')

    @classmethod
    def cleanup(cls):
        cls.manage('destroy', '--base-dir', cls.instance)

    def execute(self, connection, sql, arguments=None):
        with connection.cursor() as cursor:
            cursor.execute(sql, arguments)
            return cursor.fetchall()

    def test_commit_rollback_and_savepoint(self):
        with self.connect() as connection:
            self.execute(connection, 'CREATE TABLE lifecycle(id INT PRIMARY KEY, value INT)')
            connection.begin()
            self.execute(connection, 'INSERT INTO lifecycle VALUES(1,10)')
            self.execute(connection, 'SAVEPOINT first_write')
            self.execute(connection, 'UPDATE lifecycle SET value=20 WHERE id=1')
            self.execute(connection, 'INSERT INTO lifecycle VALUES(2,20)')
            self.execute(connection, 'ROLLBACK TO SAVEPOINT first_write')
            self.execute(connection, 'RELEASE SAVEPOINT first_write')
            connection.commit()
            self.assertEqual(((1,10),), self.execute(connection, 'SELECT * FROM lifecycle ORDER BY id'))
            connection.begin()
            self.execute(connection, 'UPDATE lifecycle SET value=99')
            connection.rollback()
            self.assertEqual(((1,10),), self.execute(connection, 'SELECT * FROM lifecycle ORDER BY id'))

    def test_concurrent_context_reuse(self):
        with self.connect() as connection:
            self.execute(connection, 'CREATE TABLE accounts(id INT PRIMARY KEY, value INT)')
            self.execute(connection, 'CREATE TABLE events(id INT PRIMARY KEY, worker INT)')
            self.execute(connection, 'INSERT INTO accounts VALUES(0,0),(1,0),(2,0),(3,0)')

        def worker(worker_id):
            with self.connect() as connection:
                for number in range(100):
                    connection.begin()
                    self.execute(connection, 'UPDATE accounts SET value=value+1 WHERE id=%s', (worker_id,))
                    self.execute(connection, 'INSERT INTO events VALUES(%s,%s)', (worker_id*1000+number, worker_id))
                    if number % 2:
                        connection.rollback()
                    else:
                        connection.commit()
        with ThreadPoolExecutor(max_workers=4) as pool:
            list(pool.map(worker, range(4)))
        with self.connect() as connection:
            self.assertEqual(((0,50),(1,50),(2,50),(3,50)),
                             self.execute(connection, 'SELECT * FROM accounts ORDER BY id'))
            self.assertEqual(((200,),), self.execute(connection, 'SELECT COUNT(*) FROM events'))

    def test_disconnect_rolls_back(self):
        with self.connect() as connection:
            self.execute(connection, 'CREATE TABLE disconnected(id INT PRIMARY KEY)')
        writer = self.connect()
        writer.begin()
        self.execute(writer, 'INSERT INTO disconnected VALUES(1)')
        writer.close()
        with self.connect() as reader:
            self.assertEqual(((0,),), self.execute(reader, 'SELECT COUNT(*) FROM disconnected'))
            self.execute(reader, 'INSERT INTO disconnected VALUES(1)')
            self.assertEqual(((1,),), self.execute(reader, 'SELECT COUNT(*) FROM disconnected'))

    def test_read_committed_isolation(self):
        with self.connect() as connection:
            self.execute(connection, 'CREATE TABLE isolated(id INT PRIMARY KEY, value INT)')
            self.execute(connection, 'INSERT INTO isolated VALUES(1,10)')
        with self.connect() as reader, self.connect() as writer:
            self.execute(reader, 'SET SESSION TRANSACTION ISOLATION LEVEL READ COMMITTED')
            reader.begin()
            writer.begin()
            self.execute(writer, 'UPDATE isolated SET value=20 WHERE id=1')
            self.assertEqual(((10,),), self.execute(reader, 'SELECT value FROM isolated WHERE id=1'))
            writer.commit()
            self.assertEqual(((20,),), self.execute(reader, 'SELECT value FROM isolated WHERE id=1'))
            reader.commit()

    def test_restart_and_uncommitted_crash_recovery(self):
        expected = ((1,10),(2,20),(3,30))
        with self.connect() as connection:
            self.execute(connection, 'CREATE TABLE recovered(id INT PRIMARY KEY, value INT)')
            self.execute(connection, 'INSERT INTO recovered VALUES(1,10),(2,20),(3,30)')
        self.manage('stop', '--base-dir', self.instance)
        self.start()
        with self.connect() as connection:
            self.assertEqual(expected, self.execute(connection, 'SELECT * FROM recovered ORDER BY id'))
        pending = self.connect()
        pending.begin()
        self.execute(pending, 'UPDATE recovered SET value=777 WHERE id=1')
        self.execute(pending, 'INSERT INTO recovered VALUES(99,999)')
        process_id = int((self.instance / 'run/seekdb.pid').read_text().strip())
        self.assertEqual(self.binary.resolve(), Path(os.readlink(f'/proc/{process_id}/exe')).resolve())
        command = Path(f'/proc/{process_id}/cmdline').read_bytes().split(b'\0')
        self.assertIn(f'--base-dir={self.instance}'.encode(), command)
        os.kill(process_id, signal.SIGKILL)
        deadline = time.monotonic() + 10
        while Path(f'/proc/{process_id}').exists() and time.monotonic() < deadline:
            status = Path(f'/proc/{process_id}/stat').read_text().split()[2]
            if status == 'Z':
                break
            time.sleep(0.05)
        pending.close()
        self.start()
        with self.connect() as connection:
            self.assertEqual(expected, self.execute(connection, 'SELECT * FROM recovered ORDER BY id'))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True, type=Path)
    parser.add_argument('--client', type=Path)
    options, unittest_arguments = parser.parse_known_args()
    if pymysql is None:
        parser.error('PyMySQL is required for the native SQL checks')
    TransactionLifecycle.binary = options.binary.resolve()
    TransactionLifecycle.client = (options.client or
        Path(__file__).resolve().parents[3] / 'deps/3rd/u01/obclient/bin/obclient').resolve()
    unittest.main(argv=[sys.argv[0], *unittest_arguments], verbosity=2)
