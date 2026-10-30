"""Embedded lock recovery regression. Requires pyseekdb and pylibseekdb.

Run with ``python unittest/storage/test_embedded_lock_recovery.py -v``. Set
SEEKDB_ORPHAN_FIXTURE to a stopped database containing completed FORK unlocks
from an older kernel. The fixture is copied before opening it. Set
SEEKDB_TEST_ROOT to retain each test's database and worker logs there.
Set SEEKDB_BASELINE_PYTHONPATH to the old kernel's isolated SDK package so the
orphan case can verify its fixture before testing the repaired kernel.

The persistence case pauses a real FORK at BUILD_DATA. It temporarily stores an
unsupported task status in this disposable database to stop the recovered task
from unlocking before the assertion. It restores the original status and
restarts again to verify that the FORK finishes and releases its locks.
"""

import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest


LOCKS = (
    "select obj_id,lock_mode,owner_id,owner_type,op_type,op_status,"
    "create_trans_id,trans_version,lock_id from oceanbase.__all_virtual_obj_lock "
    "where owner_type=2 order by obj_id,lock_mode,op_type"
)
TASKS = "select task_id,status from oceanbase.__all_ddl_task_status where ddl_type=10009"
CHECKPOINT = "select checkpoint_scn from oceanbase.__all_virtual_ls_info"


def emit(event, **values):
    print(json.dumps({"event": event, **values}, default=str), flush=True)


def wait_for(query, predicate, description, timeout=30):
    deadline = time.monotonic() + timeout
    while True:
        value = query()
        if predicate(value):
            return value
        if time.monotonic() >= deadline:
            raise AssertionError(f"Timed out waiting for {description}; last value={value!r}")
        time.sleep(0.1)


def stop_engine(db):
    """Kill only the engine recorded in this test-owned database directory."""
    pid_file = db / "run" / "seekdb.pid"
    if not pid_file.exists():
        return
    pid = int(pid_file.read_text().strip())
    process = Path("/proc") / str(pid)
    if not process.exists():
        return
    # The driver runs seekdb with the database as its working directory.
    if (process / "cwd").resolve() != db.resolve():
        raise RuntimeError(f"Refusing to kill PID {pid}; it does not belong to {db}")
    os.kill(pid, signal.SIGKILL)
    try:
        os.waitpid(pid, 0)
    except ChildProcessError:
        pass


def worker(stage, db):
    import pyseekdb
    import pylibseekdb

    client = pyseekdb.Client(path=str(db), database="test")
    connection = client._server.get_raw_connection()

    def sql(statement):
        cursor = connection.cursor()
        try:
            cursor.execute(statement)
            return cursor.fetchall() if statement.lstrip().lower().startswith("select ") else []
        finally:
            cursor.close()

    sql("set ob_query_timeout=10000000")
    state_file = db.parent / (db.name + ".state.json")
    emit("opened", stage=stage, locks=sql(LOCKS), checkpoint=sql(CHECKPOINT))
    try:
        if stage == "prepare":
            sql("alter system set debug_sync_timeout='180s'")
            sql("alter system set _advance_checkpoint_interval='1s'")
            # MySQL LOCK TABLES is a no-op unless lock priority is enabled.
            sql("alter system set enable_lock_priority=true")
            sql("set ob_global_debug_sync = 'FORK_TABLE_BUILD_DATA wait_for lock_recovery_release execute 1000000'")
            sql("create table lock_recovery_src(id int primary key, value int)")
            sql("insert into lock_recovery_src values (1,101),(2,202)")
            sql("fork table lock_recovery_src to lock_recovery_dst")
            tasks = wait_for(lambda: sql(TASKS), lambda rows: len(rows) == 1 and rows[0][1] == 48,
                             "FORK to reach BUILD_DATA")
            expected = sql(LOCKS)
            assert expected and all(list(row[4:6]) == ["OUT_TRANS_LOCK", "COMPLETE"]
                                    for row in expected), expected
            task_id, original_status = tasks[0]
            # This barrier survives restart. Only the disposable test DB is changed.
            sql(f"update oceanbase.__all_ddl_task_status set status=9999 where task_id={task_id}")
            lock_version = max(int(row[7]) for row in expected)
            state_file.write_text(json.dumps({"locks": expected, "task_id": task_id,
                                               "original_status": original_status}))
            def flush_checkpoint():
                sql("alter system minor freeze")
                return sql(CHECKPOINT)

            checkpoint = wait_for(flush_checkpoint, lambda rows: rows[0][0] > lock_version,
                                  "persisted replay boundary to pass the held lock", timeout=45)
            assert [list(row) for row in sql(LOCKS)] == [list(row) for row in expected]
            emit("flushed_held_lock", locks=expected, checkpoint=checkpoint,
                 handlers=sql("select * from oceanbase.__all_virtual_checkpoint"))
            stop_engine(db)
            os._exit(0)
        elif stage == "recover":
            state = json.loads(state_file.read_text())
            tasks = [list(row) for row in sql(TASKS)]
            assert tasks == [[state["task_id"], 9999]], tasks
            actual = [list(row) for row in sql(LOCKS)]
            emit("recovered_held_lock", tasks=tasks, expected=state["locks"], actual=actual)
            assert actual == state["locks"], "Persistent FORK locks were lost or changed after flush and restart"
            sql("set ob_trx_lock_timeout=1000000")
            try:
                sql("lock tables lock_recovery_src write")
            except pylibseekdb.SeekdbError as error:
                assert "Lock wait timeout exceeded" in str(error), error
                emit("recovered_lock_blocks_exclusive_request", error=str(error))
            else:
                sql("unlock tables")
                raise AssertionError("Recovered FORK locks allowed an exclusive table lock")
            assert [list(row) for row in sql(LOCKS)] == state["locks"]
            task_id = state["task_id"]
            original_status = state["original_status"]
            sql(f"update oceanbase.__all_ddl_task_status set status={original_status} where task_id={task_id}")
        elif stage == "finish":
            wait_for(lambda: sql(TASKS), lambda rows: not rows, "recovered FORK to finish")
            rows = sql("select id,value from lock_recovery_dst order by id")
            assert [list(row) for row in rows] == [[1, 101], [2, 202]], rows
            wait_for(lambda: sql(LOCKS), lambda rows: not rows, "FORK locks to be released")
            sql("lock tables lock_recovery_src write")
            sql("unlock tables")
            emit("completed_fork", rows=rows)
        elif stage == "check_orphan_fixture":
            orphan_locks = [row for row in sql(LOCKS)
                            if list(row[4:6]) == ["OUT_TRANS_UNLOCK", "COMPLETE"]]
            assert orphan_locks, "Baseline recovered no completed orphan unlocks from the fixture"
            emit("verified_orphan_fixture", locks=orphan_locks)
        elif stage == "orphan":
            before = sql(LOCKS)
            emit("before_orphan_fork", locks=before)
            source = client.get_collection("smoke")
            expected = source.count()
            fork = source.fork("lock_recovery_regression_fork")
            assert fork.count() == expected
            after = sql(LOCKS)
            assert not any(list(row[4:6]) == ["OUT_TRANS_UNLOCK", "COMPLETE"]
                           for row in after), after
            emit("recovered_orphan_fork", rows=expected, locks=after)
        else:
            raise ValueError(stage)
    except BaseException:
        stop_engine(db)
        raise
    finally:
        pylibseekdb.close()


class EmbeddedLockRecoveryTest(unittest.TestCase):
    def setUp(self):
        root = os.environ.get("SEEKDB_TEST_ROOT")
        if root:
            Path(root).mkdir(parents=True, exist_ok=True)
        self.directory = Path(tempfile.mkdtemp(prefix=self._testMethodName.split("_")[1] + "_", dir=root))
        self.db = self.directory / "test.db"
        print(f"\nArtifacts: {self.directory}", flush=True)

    def run_stage(self, stage, timeout=65, db=None, env=None):
        db = self.db if db is None else db
        logfile = self.directory / (stage + ".log")
        with logfile.open("w") as output:
            process = subprocess.Popen([sys.executable, __file__, "--worker", stage, str(db)],
                                       stdout=output, stderr=subprocess.STDOUT,
                                       start_new_session=True, env=env)
            try:
                returncode = process.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                stop_engine(db)
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                process.wait()
                self.fail(f"{stage} exceeded {timeout}s.\n{logfile.read_text()}")
        self.assertEqual(returncode, 0, f"{stage} failed.\n{logfile.read_text()}")

    def test_held_fork_lock_survives_flush_and_restart(self):
        self.run_stage("prepare")
        self.run_stage("recover")
        self.run_stage("finish")

    def test_completed_orphan_unlock_does_not_block_fork(self):
        fixture = os.environ.get("SEEKDB_ORPHAN_FIXTURE")
        if not fixture:
            self.skipTest("Set SEEKDB_ORPHAN_FIXTURE to an old-kernel orphan-unlock database")
        source = Path(fixture).resolve()
        self.assertTrue((source / "store").is_dir(), "Fixture has no store directory")
        baseline_path = os.environ.get("SEEKDB_BASELINE_PYTHONPATH")
        self.assertTrue(baseline_path, "Set SEEKDB_BASELINE_PYTHONPATH to verify the fixture")
        baseline_db = self.directory / "baseline.db"
        subprocess.run(["cp", "-a", "--reflink=auto", "--sparse=always",
                        str(source), str(baseline_db)], check=True, timeout=60)
        baseline_env = dict(os.environ, PYTHONPATH=baseline_path)
        self.run_stage("check_orphan_fixture", db=baseline_db, env=baseline_env)
        subprocess.run(["cp", "-a", "--reflink=auto", "--sparse=always",
                        str(source), str(self.db)], check=True, timeout=60)
        self.run_stage("orphan", timeout=25)


if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[1] == "--worker":
        worker(sys.argv[2], Path(sys.argv[3]).resolve())
    else:
        unittest.main()
