#!/usr/bin/env python3
"""Opt-in real-server SQL SPI regression, never counted by standalone CTest.

Requires PyMySQL and a disposable loopback seekdb with sql_extension installed
from this worktree. Creates only uniquely named fixture databases/users; does
not install/uninstall packages or change server settings. No production target.
"""

import argparse
import os
import secrets
import sys

import pymysql
from pymysql.constants import ER


def scalar(connection, sql, args=None):
    with connection.cursor() as cursor:
        cursor.execute(sql, args)
        row = cursor.fetchone()
        return None if row is None else row[0]


def must_fail(connection, sql, args=None, expected_codes=None):
    try:
        scalar(connection, sql, args)
    except pymysql.MySQLError as error:
        # A disconnected server is never evidence of a correctly rejected SQL.
        if error.args[0] in (2002, 2003, 2006, 2013):
            raise
        if expected_codes is not None and error.args[0] not in expected_codes:
            raise AssertionError(f"Expected database errors {expected_codes}, got {error.args[0]}") from error
        return
    raise AssertionError("SQL unexpectedly succeeded")


def run(options):
    database = "plugin_spi_" + secrets.token_hex(6)
    reader = "spi_reader_" + secrets.token_hex(6)
    reader_password = secrets.token_urlsafe(24)
    config = dict(host="127.0.0.1", port=options.port, user=options.user,
                  password=os.environ.get("SEEKDB_TEST_PASSWORD", ""),
                  autocommit=True, charset="utf8mb4", connect_timeout=5,
                  read_timeout=60, write_timeout=60)
    connection = pymysql.connect(**config)
    observer = None
    reader_connection = None
    database_created = False
    reader_created = False
    try:
        # Fail before creating resources if the right plugin version is absent.
        assert scalar(connection, "SELECT seekdb_sql_add_one(41)") == 42
        scalar(connection, f"CREATE DATABASE `{database}`")
        database_created = True
        connection.select_db(database)
        scalar(connection, "CREATE TABLE writes (id BIGINT PRIMARY KEY)")
        scalar(connection, "CREATE TABLE inputs (id BIGINT PRIMARY KEY)")
        scalar(connection, "INSERT INTO inputs VALUES (1),(2),(3)")
        observer = pymysql.connect(**config, database=database)

        # Bound NULL and semicolons inside literals are not textual substitution
        # or naive splitting of SQL commands.
        assert scalar(connection, "SELECT seekdb_sql_exec(%s, %s)",
                      ("SELECT ?", None)) == 1
        assert scalar(connection, "SELECT seekdb_sql_exec(%s, %s)",
                      ("SELECT CONCAT('a;', 'b') WHERE ? = 1", 1)) == 1
        must_fail(connection, "SELECT seekdb_sql_exec(%s, 1)",
                  ("SELECT ?; DELETE FROM writes",))
        must_fail(connection, "SELECT seekdb_sql_exec(%s, 1)", ("COMMIT",))
        must_fail(connection, "SELECT seekdb_sql_exec(%s, 1)",
                  ("CREATE TABLE forbidden (id INT)",))
        assert scalar(connection, "SELECT COUNT(*) FROM information_schema.tables "
                      "WHERE table_schema=%s AND table_name='forbidden'", (database,)) == 0

        write_sql = "INSERT INTO writes VALUES (?)"
        # A table-free outer SELECT still needs a statement owner and commit.
        assert scalar(connection, "SELECT seekdb_sql_exec(%s, 10)", (write_sql,)) == 1
        assert scalar(observer, "SELECT COUNT(*) FROM writes WHERE id=10") == 1
        assert scalar(connection, "SELECT seekdb_sql_exec(%s, 10)",
                      ("UPDATE writes SET id=id+1 WHERE id=?",)) == 1
        assert scalar(connection, "SELECT seekdb_sql_exec(%s, 11)",
                      ("DELETE FROM writes WHERE id=?",)) == 1

        # The aggregate argument writes once per input row. The second output
        # expression executes *after* aggregation and overflows in host SQL.
        fail_after_rows = (
            "SELECT /*+ PARALLEL(1) */ SUM(seekdb_sql_exec(%s, id)), "
            "seekdb_sql_add_one(MAX(id) + 9223372036854775804) FROM inputs"
        )
        success_rows = "SELECT /*+ PARALLEL(1) */ SUM(seekdb_sql_exec(%s, id)) FROM inputs"
        assert scalar(connection, success_rows, (write_sql,)) == 3
        assert scalar(observer, "SELECT COUNT(*) FROM writes") == 3
        scalar(connection, "DELETE FROM writes")
        must_fail(connection, fail_after_rows, (write_sql,), {ER.DATA_OUT_OF_RANGE})
        assert scalar(observer, "SELECT COUNT(*) FROM writes") == 0

        connection.begin()
        scalar(connection, "INSERT INTO writes VALUES (100)")
        must_fail(connection, fail_after_rows, (write_sql,), {ER.DATA_OUT_OF_RANGE})
        assert scalar(connection, "SELECT COUNT(*) FROM writes") == 1
        assert scalar(connection, "SELECT id FROM writes") == 100
        assert scalar(observer, "SELECT COUNT(*) FROM writes") == 0
        # Internal savepoints must not hide/remove a user's named savepoint.
        scalar(connection, "SAVEPOINT caller_owned")
        assert scalar(connection, success_rows, (write_sql,)) == 3
        scalar(connection, "ROLLBACK TO SAVEPOINT caller_owned")
        assert scalar(connection, "SELECT COUNT(*) FROM writes") == 1
        connection.commit()
        assert scalar(observer, "SELECT COUNT(*) FROM writes") == 1

        # A later internal failure must not strand the session in nested state.
        must_fail(connection, "SELECT seekdb_sql_exec(%s, 100)", (write_sql,), {ER.DUP_ENTRY})
        assert scalar(connection, "SELECT @@autocommit") == 1
        assert scalar(connection, "SELECT DATABASE()") == database
        assert scalar(connection, "SELECT seekdb_sql_add_one(41)") == 42

        scalar(connection, f"CREATE USER '{reader}'@'%' IDENTIFIED BY %s", (reader_password,))
        reader_created = True
        scalar(connection, f"GRANT SELECT ON `{database}`.* TO '{reader}'@'%'")
        reader_config = dict(config, user=reader, password=reader_password, database=database)
        reader_connection = pymysql.connect(**reader_config)
        assert scalar(reader_connection, "SELECT seekdb_sql_exec(%s, 100)",
                      ("SELECT id FROM writes WHERE id=?",)) == 1
        must_fail(reader_connection, "SELECT seekdb_sql_exec(%s, 200)", (write_sql,),
                  {ER.TABLEACCESS_DENIED_ERROR, ER.SPECIFIC_ACCESS_DENIED_ERROR})
        assert scalar(observer, "SELECT COUNT(*) FROM writes") == 1
        assert scalar(reader_connection, "SELECT seekdb_sql_add_one(41)") == 42
        print("PASS: real SQL binding, DML, late-error rollback, caller savepoint, invoker privileges")
    finally:
        if reader_connection is not None:
            reader_connection.close()
        if observer is not None:
            observer.close()
        try:
            connection.rollback()
            cleanup_errors = []
            for created, name, command in (
                (reader_created, reader, f"DROP USER '{reader}'@'%'") ,
                (database_created, database, f"DROP DATABASE `{database}`"),
            ):
                if created:
                    try:
                        scalar(connection, command)
                        print("Removed generated fixture:", name)
                    except pymysql.MySQLError as error:
                        cleanup_errors.append(error)
                        print("Fixture cleanup failed; resource remains:", name, file=sys.stderr)
            if cleanup_errors and sys.exc_info()[0] is None:
                raise cleanup_errors[0]
        finally:
            connection.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--user", default="root")
    parser.add_argument("--confirm-disposable-server", action="store_true", required=True)
    run(parser.parse_args())
