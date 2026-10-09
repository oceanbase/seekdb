#!/usr/bin/env python3
"""Throwaway real-engine experiment; owns its instance and retains PROTOTYPE artifacts.

python3 tools/obtest/namespace_fork_prototype.py --binary build_release/src/observer/seekdb
No namespace recovery, source DROP, or arbitrary SQL routing is implemented.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import socket
import subprocess
import tarfile
import tempfile
import time

import pymysql


class Experiment:
    def __init__(self, binary, label, prototype=True):
        self.binary = str(Path(binary).resolve(strict=True))
        self.base = Path(tempfile.mkdtemp(prefix="namespace_fork_PROTOTYPE_" + label + "_",
                                         dir=os.environ.get("SEEKDB_FORK_PROTOTYPE_TEST_ROOT", "/data/1/nijia.nj/test")))
        (self.base / "PROTOTYPE.txt").write_text("Disposable experiment. Do not reopen as a persistent namespace.\n")
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            self.port = sock.getsockname()[1]
        self.events = open(self.base / "experiment.jsonl", "a", buffering=1)
        self.output = open(self.base / "process.out", "ab", buffering=0)
        self.proc = None
        self.connection = None
        self.prototype = prototype

    def record(self, event, **fields):
        item = dict(event=event, **fields)
        line = json.dumps(item, ensure_ascii=False, default=str)
        self.events.write(line + "\n")
        print(line, flush=True)

    def connect(self):
        read_timeout = int(os.environ.get("SEEKDB_FORK_READ_TIMEOUT_S", "45"))
        con = pymysql.connect(host="127.0.0.1", port=self.port, user="root", password="",
                              autocommit=True, connect_timeout=2, read_timeout=read_timeout, write_timeout=10)
        with con.cursor() as cur:
            query_timeout = int(os.environ.get("SEEKDB_FORK_QUERY_TIMEOUT_US", "30000000"))
            cur.execute("SET ob_query_timeout=" + str(query_timeout))
            cur.execute("SET ob_trx_timeout=300000000")
        return con

    def start(self):
        env = os.environ.copy()
        command = [self.binary, "--nodaemon", "--base-dir=" + str(self.base), "-P" + str(self.port),
                   "--log-level=INFO", "--parameter", "memory_budget=2G",
                   "--parameter", "datafile_size=256M", "--parameter", "datafile_maxsize=512M",
                   "--parameter", "log_disk_size=2G", "--parameter", "cpu_count=4",
                   # Keep pre-crash evidence through the verbose recovery bootstrap.
                   "--parameter", "max_syslog_file_count=16"]
        self.proc = subprocess.Popen(command, env=env, stdout=self.output, stderr=subprocess.STDOUT)
        self.record("setup", binary=self.binary, prototype=self.prototype, base=self.base, pid=self.proc.pid)
        deadline = time.monotonic() + 180
        while time.monotonic() < deadline:
            if self.proc.poll() is not None:
                raise RuntimeError("seekdb exited; see " + str(self.base / "process.out"))
            try:
                self.connection = self.connect()
                break
            except pymysql.MySQLError:
                time.sleep(1)
        if self.connection is None:
            raise TimeoutError("seekdb startup: " + str(self.base))
        self.sql("ALTER SYSTEM SET debug_sync_timeout='600s'")
        self.sql("SET recyclebin=off")
        # Make the old-S test stricter, instead of extending the ordinary history window.
        self.sql("ALTER SYSTEM SET undo_retention=0")
        # Remove a separate conservative active-tx watermark (initially zero),
        # so the acquired snapshot must be the effective historical-version guard.
        self.sql("ALTER SYSTEM SET _mvcc_gc_using_min_txn_snapshot=false")
        if self.prototype not in (2, 3, 4, 5, 6):
            self.sql("SET ob_global_debug_sync='FORK_TABLE_WAIT_FREEZE_END wait_for prototype_hold execute 10000'")

    def close(self):
        if self.connection is not None:
            self.connection.close()
            self.connection = None
        if self.proc is not None and self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait(timeout=10)
            self.record("stopped", pid=self.proc.pid, returncode=self.proc.returncode)
        # Keep the complete disposable data as an archive; this host has little free disk.
        entries = [p for p in self.base.iterdir()
                   if p.name not in {"log", "process.out", "experiment.jsonl", "PROTOTYPE.txt",
                                     "vector_loading.log", "directory_snapshot.json"}]
        if entries:
            archive = self.base / "data.tar.gz"
            with tarfile.open(archive, "w:gz", compresslevel=1) as out:
                for entry in entries:
                    out.add(entry, arcname=entry.name)
            for entry in entries:
                if entry.is_dir() and not entry.is_symlink():
                    shutil.rmtree(entry)
                else:
                    entry.unlink()
            self.record("data_archived", path=archive, bytes=archive.stat().st_size)
        self.output.close()
        self.events.close()

    def sql(self, statement, con=None, log=True):
        with (con or self.connection).cursor() as cur:
            cur.execute(statement)
            result = cur.fetchall() if cur.description else ()
        if log:
            self.record("sql", sql=statement, rows=result)
        return result

    def expect(self, table, rows):
        actual = self.sql("SELECT id,v FROM " + table + " ORDER BY id")
        if list(actual) != rows:
            raise AssertionError((table, actual, rows))

    def user_tables(self, database):
        return self.sql("SELECT table_name,table_id,tablet_id FROM oceanbase.__all_table "
                        "WHERE database_id=(SELECT database_id FROM oceanbase.__all_database "
                        "WHERE database_name='" + database + "') AND table_type=3 ORDER BY table_name", log=False)

    def tasks(self):
        return self.sql("SELECT task_id,status,object_id,target_object_id,snapshot_version "
                        "FROM oceanbase.__all_ddl_task_status WHERE ddl_type=10009 ORDER BY task_id", log=False)

    def wait_paused(self, count):
        deadline = time.monotonic() + 60
        while time.monotonic() < deadline:
            tasks = self.tasks()
            if len(tasks) == count and all(row[1] == 19 for row in tasks):
                self.record("fork_tasks_paused_before_freeze", tasks=tasks)
                return tasks
            time.sleep(0.2)
        raise AssertionError(("fork tasks did not pause", self.tasks()))

    def ensure_table(self, source, target, table):
        tables = {row[0]: row[1:] for row in self.user_tables(target)}
        if table not in tables:
            self.sql("FORK TABLE " + source + "." + table + " TO " + target + "." + table)
        return {row[0]: row[1:] for row in self.user_tables(target)}[table]

    def engine_log(self):
        return "\n".join(p.read_text(errors="replace") for p in sorted((self.base / "log").glob("seekdb.log*")))

    def run(self, table_count):
        source, target = "__fork_proto_a" + str(table_count), "__fork_proto_b" + str(table_count)
        self.sql("CREATE DATABASE " + source)
        for n in range(1, table_count + 1):
            self.sql("CREATE TABLE " + source + ".t" + str(n) + "(id INT PRIMARY KEY, v INT)", log=False)
        self.sql("INSERT INTO " + source + ".t1 VALUES(1,100),(2,200),(3,300)")
        self.sql("INSERT INTO " + source + ".t2 VALUES(1,10),(2,20),(3,30)")
        source_tables = self.user_tables(source)
        assert len(source_tables) == table_count, source_tables
        source_ids = ",".join(str(row[2]) for row in source_tables if row[0] in ("t1", "t2"))
        memtables = self.sql("SELECT tablet_id,is_active,insert_row_count FROM "
                             "oceanbase.__all_virtual_tablet_memstore_info WHERE tablet_id IN (" + source_ids + ")")
        assert {r[0] for r in memtables if r[1] in ("YES", 1) and r[2] >= 3} == set(map(int, source_ids.split(","))), memtables

        tx = self.connect()
        try:
            self.sql("BEGIN", tx)
            self.sql("UPDATE " + source + ".t1 SET v=333 WHERE id=3", tx)
            self.sql("UPDATE " + source + ".t2 SET v=33 WHERE id=3", tx)
            self.sql("FORK DATABASE " + source + " TO " + target)
            assert self.user_tables(target) == (), self.user_tables(target)
            assert self.tasks() == (), self.tasks()
            pins = self.sql("SELECT snapshot_scn,tablet_id,schema_version FROM oceanbase.__all_acquired_snapshot "
                            "WHERE snapshot_type=2 ORDER BY snapshot_scn")
            assert len(pins) == 1 and pins[0][1] == 0, pins
            snapshot = int(pins[0][0])
            self.record("capture_has_no_target_user_tables_or_fork_tasks", snapshot=snapshot, source_tables=table_count)
            self.sql("COMMIT", tx)
        finally:
            tx.close()

        first_id = self.ensure_table(source, target, "t1")
        assert [r[0] for r in self.user_tables(target)] == ["t1"]
        tasks = self.wait_paused(1)
        assert int(tasks[0][4]) == snapshot, tasks
        self.expect(target + ".t1", [(1, 100), (2, 200), (3, 300)])
        self.sql("UPDATE " + source + ".t2 SET v=11 WHERE id=1")
        self.sql("DELETE FROM " + source + ".t2 WHERE id=2")
        self.sql("INSERT INTO " + source + ".t2 VALUES(4,40)")

        # Renewal is demand-driven. Flush one unrelated tablet to request it without
        # flushing either fork branch or relying on the timing of startup catchup.
        self.sql("CREATE DATABASE __fork_proto_gc")
        self.sql("CREATE TABLE __fork_proto_gc.probe(id INT PRIMARY KEY)")
        self.sql("INSERT INTO __fork_proto_gc.probe VALUES(1)")
        probe_tablet = self.user_tables("__fork_proto_gc")[0][2]
        self.sql("ALTER SYSTEM MINOR FREEZE TABLET_ID=" + str(probe_tablet))
        self.record("unrelated_table_flush_requested_gc_renewal", tablet_id=probe_tablet)

        deadline = time.monotonic() + 150
        last_report = 0
        while time.monotonic() < deadline:
            gc = int(self.sql("SELECT column_value FROM oceanbase.__all_core_table "
                              "WHERE table_name='__all_global_stat' AND column_name='snapshot_gc_scn'", log=False)[0][0])
            if gc > snapshot:
                self.record("new_snapshot_gc_watermark_passed_original_S", snapshot=snapshot, gc_scn=gc)
                break
            if time.monotonic() - last_report > 10:
                self.record("waiting_snapshot_gc", snapshot=snapshot, gc_scn=gc)
                last_report = time.monotonic()
            time.sleep(1)
        else:
            raise TimeoutError("snapshot GC watermark did not advance past S")

        self.ensure_table(source, target, "t2")
        tasks = self.wait_paused(2)
        assert all(int(row[4]) == snapshot for row in tasks), tasks
        self.expect(target + ".t2", [(1, 10), (2, 20), (3, 30)])
        self.expect(source + ".t2", [(1, 11), (3, 33), (4, 40)])

        self.sql("UPDATE " + target + ".t1 SET v=80 WHERE id=1")
        self.sql("DELETE FROM " + target + ".t1 WHERE id=2")
        self.sql("INSERT INTO " + target + ".t1 VALUES(4,400)")
        self.sql("BEGIN")
        self.sql("UPDATE " + target + ".t1 SET v=999 WHERE id=1")
        self.sql("ROLLBACK")
        self.expect(target + ".t1", [(1, 80), (3, 300), (4, 400)])
        self.expect(source + ".t1", [(1, 100), (2, 200), (3, 333)])
        assert self.ensure_table(source, target, "t1") == first_id
        assert len(self.tasks()) == 2

        # A table created after capture cannot enter the captured object set.
        self.sql("CREATE TABLE " + source + ".late(id INT PRIMARY KEY, v INT)")
        try:
            self.ensure_table(source, target, "late")
        except pymysql.MySQLError as error:
            self.record("late_schema_rejected", error=error.args)
            assert error.args[0] == 1235, error.args
        else:
            raise AssertionError("late source schema was accepted")

        trace = self.engine_log()
        work = [line for line in trace.splitlines() if "PROTOTYPE_FORK_WORK" in line]
        assert len(work) == 2, work
        for line in work:
            assert re.search(r"materializations=0\b", line), line
            assert re.search(r"tablet_collections=0\b", line), line
        pin_uses = [line for line in trace.splitlines() if "PROTOTYPE_FORK_PIN_USED" in line]
        assert len(pin_uses) == 2, pin_uses
        assert "SNAPSHOT_FOR_MULTI_VERSION" in pin_uses[-1], pin_uses[-1]
        assert "fork table freeze stage done" not in trace
        self.record("engine_evidence", capture_work=work, pin_uses=pin_uses)
        self.record("PASS", case="lazy_fixed_snapshot", table_count=table_count,
                    target_user_tables=len(self.user_tables(target)), snapshot=snapshot,
                    scope="single process; source retained; background fork paused; no recovery claim")

    def legacy_control(self):
        self.sql("CREATE DATABASE __fork_proto_a_control")
        for name in ("t1", "t2"):
            self.sql("CREATE TABLE __fork_proto_a_control." + name + "(id INT PRIMARY KEY, v INT)")
            self.sql("INSERT INTO __fork_proto_a_control." + name + " VALUES(1,10)")
        self.sql("FORK DATABASE __fork_proto_a_control TO __fork_proto_b_control")
        assert len(self.user_tables("__fork_proto_b_control")) == 2
        self.wait_paused(2)
        self.expect("__fork_proto_b_control.t1", [(1, 10)])
        self.expect("__fork_proto_b_control.t2", [(1, 10)])
        assert "PROTOTYPE_FORK_CAPTURED" not in self.engine_log()
        self.record("PASS", case="flag_off_keeps_eager_database_fork")

    def vector_probe(self):
        self.sql("CREATE DATABASE prototype_vectors")
        self.sql("USE prototype_vectors")
        self.sql("SET ob_enable_index_direct_select=1")
        self.sql("ALTER SYSTEM SET vector_index_optimize_duty_time='[00:00:00, 00:00:00]'")
        for number, state in enumerate(("incremental", "mixed"), 1):
            source, target = "src_" + state, "dst_" + state
            self.sql("CREATE TABLE " + source + "(id INT PRIMARY KEY, embedding VECTOR(3), "
                     "VECTOR INDEX idx1(embedding) WITH (distance=l2,type=hnsw,lib=vsag,"
                     "sync_mode=async,ef_search=200)) ORGANIZATION HEAP")
            self.sql("INSERT INTO " + source + " VALUES(1,'[1,0,0]'),(2,'[4,0,0]'),(3,'[9,0,0]')")
            definition = self.sql("SHOW CREATE TABLE " + source)[0][1]
            assert "ASYNC" in definition.upper() and "HEAP" in definition.upper(), definition
            source_id = int(self.sql("SELECT table_id FROM oceanbase.__all_table WHERE table_name='" + source +
                                    "' AND database_id=(SELECT database_id FROM oceanbase.__all_database "
                                    "WHERE database_name='prototype_vectors')", log=False)[0][0])
            snapshot_table = "`__idx_" + str(source_id) + "_idx1_index_snapshot_data_table`"
            query = "SELECT id FROM {} ORDER BY l2_distance(embedding,[0,0,0]) APPROXIMATE LIMIT 1"
            deadline = time.monotonic() + 60
            while time.monotonic() < deadline:
                if self.sql(query.format(source), log=False) == ((1,),):
                    break
                time.sleep(0.2)
            else:
                raise AssertionError("source async index did not become queryable")
            if state == "mixed":
                self.sql("CALL dbms_vector.rebuild_index('idx1','" + source + "','embedding')")
                deadline = time.monotonic() + 60
                while time.monotonic() < deadline:
                    if int(self.sql("SELECT COUNT(*) FROM " + snapshot_table, log=False)[0][0]) > 0:
                        break
                    time.sleep(0.2)
                else:
                    raise AssertionError("source persistent graph did not appear")
            snapshot_rows = int(self.sql("SELECT COUNT(*) FROM " + snapshot_table)[0][0])
            assert (snapshot_rows > 0) == (state == "mixed"), (state, snapshot_rows)
            self.sql("INSERT INTO " + source + " VALUES(4,'[0.5,0,0]')")
            self.sql("FORK TABLE " + source + " TO " + target)
            self.wait_paused(number)
            plan = self.sql("EXPLAIN " + query.format(target))
            assert "VECTOR INDEX SCAN" in str(plan).upper(), plan
            self.record("vector_first_query_begin", input_state=state, snapshot_rows=snapshot_rows)
            try:
                result = self.sql(query.format(target))
            except pymysql.MySQLError as error:
                self.record("VECTOR_LIMITATION", input_state=state,
                            error=error.args, fork_background="paused_before_freeze")
                raise
            trace_id = self.sql("SELECT last_trace_id()", log=False)[0][0]
            self.record("vector_first_query_trace", input_state=state, trace_id=trace_id)
            # For this tiny fixture the exact nearest point is unambiguous.
            assert result == ((4,),), result
            self.sql("DELETE FROM " + source + " WHERE id=4")
            assert self.sql(query.format(target)) == ((4,),)
            self.sql("DELETE FROM " + target + " WHERE id=4")
            exact = self.sql("SELECT id FROM " + target +
                             " ORDER BY l2_distance(embedding,[0,0,0]) LIMIT 1")
            assert exact == ((1,),), exact
            self.record("PASS", case="vector_query_before_fork_freeze", input_state=state,
                        snapshot_rows=snapshot_rows, scope="small functional fixture; no latency claim")
        self.wait_paused(2)
        trace = self.engine_log()
        assert "fork table freeze stage done" not in trace
        evidence = [line for line in trace.splitlines()
                    if ("ob_plugin_vector_index" in line or "ob_vsag_adaptor.cpp" in line
                        or "ob_das_hnsw_scan_iter.cpp" in line)
                    and ("deserialize" in line or "SYCN_DELTA" in line or "complete memdata" in line)]
        (self.base / "vector_loading.log").write_text("\n".join(evidence))
        self.record("vector_loading_log", path=self.base / "vector_loading.log")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--tables", nargs="+", type=int, default=[2, 20])
    parser.add_argument("--skip-legacy-control", action="store_true")
    parser.add_argument("--vector-only", action="store_true", help="Probe existing async HNSW fork separately")
    args = parser.parse_args()
    if any(n < 2 for n in args.tables):
        parser.error("--tables must be >= 2")
    cases = [(str(n), True, n) for n in args.tables]
    if args.vector_only:
        cases = [("vector", False, None)]
    elif not args.skip_legacy_control:
        cases.append(("legacy", False, None))
    for label, prototype, count in cases:
        experiment = Experiment(args.binary, label, prototype)
        try:
            experiment.start()
            if label == "vector":
                experiment.vector_probe()
            elif prototype:
                experiment.run(count)
            else:
                experiment.legacy_control()
        except BaseException as error:
            experiment.record("FAIL", error=repr(error), base=experiment.base)
            raise
        finally:
            experiment.close()


if __name__ == "__main__":
    main()
