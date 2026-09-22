#!/usr/bin/env python3
# Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
"""Opt-in query catalog regression against a disposable loopback seekdb.

Requires PyMySQL, the current rust_text plugin, automatic_sp_privileges=1,
and an administrator able to create fixture databases/users. Does not install
plugins or change global settings. Only generated fixture objects are removed
on success; on failure their names are printed and they are retained for diagnosis.
This suite is NOT run by standalone CTest; its offline helper tests are separate.
"""

import argparse
import os
import re
import secrets
import sys


# MySQL protocol errors; a timeout, missing plugin, syntax error or lost server
# must never count as evidence of the intended rejection.
MISSING_ROUTINE = {1305}
DUPLICATE_ROUTINE = {1304}
DENIED = {1044, 1142, 1227, 1370}
READ_ONLY = {1792}
OVERFLOW = {1690}
MUTATE = "SELECT seekdb_rust_routine_ddl(%s)"
LOOKUP = "SELECT seekdb_rust_routine_id(%s)"


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def identifier(name):
    # All identifiers originate in this fixture, never in user-supplied SQL.
    if not re.fullmatch(r"[a-z][a-z0-9_]{0,63}", name):
        raise ValueError("invalid fixture identifier")
    return "`" + name + "`"


def query(connection, statement, arguments=None):
    with connection.cursor() as cursor:
        cursor.execute(statement, arguments)
        rows = cursor.fetchall()
        # CALL sends a final status result. Drain every result so later queries
        # cannot accidentally inspect leftovers or hide a later server error.
        while cursor.nextset():
            cursor.fetchall()
        return rows


def scalar(connection, statement, arguments=None):
    rows = query(connection, statement, arguments)
    if not rows:
        return None
    require(len(rows) == 1 and len(rows[0]) == 1, "expected exactly one scalar result")
    return rows[0][0]


def expect_error(connection, statement, arguments, codes, error_type):
    require(bool(codes), "negative tests must name their expected protocol errors")
    try:
        query(connection, statement, arguments)
    except error_type as error:
        if not error.args or error.args[0] not in codes:
            raise
        return error.args[0]
    raise AssertionError("SQL unexpectedly succeeded; expected errors " + str(sorted(codes)))


def object_id(value):
    require(type(value) is int and 0 < value <= (1 << 63) - 1,
            "mutation did not return a valid provisional object identity")
    return value


def create_function(name, expression):
    return (f"CREATE FUNCTION {identifier(name)}() RETURNS BIGINT "
            f"DETERMINISTIC NO SQL RETURN {expression}")


def mutate(connection, statement):
    return object_id(scalar(connection, MUTATE, (statement,)))


def lookup(connection, name):
    value = scalar(connection, LOOKUP, (name,))
    return None if value is None else object_id(value)


def function_value(connection, name):
    return scalar(connection, f"SELECT {identifier(name)}()")


def function_definition(connection, name):
    rows = query(connection, f"SHOW CREATE FUNCTION {identifier(name)}")
    require(len(rows) == 1 and len(rows[0]) >= 3, "SHOW CREATE FUNCTION result shape")
    definition = rows[0][2]
    if isinstance(definition, bytes):
        definition = definition.decode("utf-8")
    require(isinstance(definition, str), "missing routine definition")
    return definition


def missing(connection, name, error_type):
    require(lookup(connection, name) is None, "lookup exposed a missing/uncommitted function: " + name)
    expect_error(connection, f"SELECT {identifier(name)}()", None, MISSING_ROUTINE, error_type)


def check_autocommit(writer, observer, error_type):
    created = mutate(writer, create_function("auto_value", "11"))
    observed = lookup(observer, "auto_value")
    require(observed == created,
            f"autocommit did not publish the object identity: created={created}, observed={observed}")
    require(function_value(observer, "auto_value") == 11, "autocommit object is not callable")
    require(mutate(writer, "DROP FUNCTION auto_value") == created, "DROP returned another identity")
    missing(observer, "auto_value", error_type)
    require(scalar(writer, MUTATE, ("DROP FUNCTION IF EXISTS auto_value",)) is None,
            "missing IF EXISTS must return NULL, not an invented object ID")


def check_caller_transaction(writer, observer, error_type):
    writer.begin()
    scalar(writer, "INSERT INTO writes VALUES (100)")
    created = mutate(writer, create_function("caller_value", "41"))
    wrapper = mutate(writer, create_function("caller_wrapper", "caller_value() + 1"))
    require(created != wrapper, "distinct routines reused an identity")
    require(lookup(writer, "caller_value") == created, "next statement lost its provisional schema")
    require(function_value(writer, "caller_wrapper") == 42, "provisional routine dependency/PL lookup failed")
    mutate(writer, "CREATE PROCEDURE caller_procedure() SELECT caller_wrapper()")
    require(scalar(writer, "CALL caller_procedure()") == 42, "provisional procedure cannot call its dependency")
    missing(observer, "caller_value", error_type)
    missing(observer, "caller_wrapper", error_type)
    expect_error(observer, "CALL caller_procedure()", None, MISSING_ROUTINE, error_type)
    require(scalar(observer, "SELECT COUNT(*) FROM writes") == 0, "catalog mutation implicitly committed caller DML")
    writer.commit()
    require(lookup(observer, "caller_value") == created, "commit changed/lost routine identity")
    require(function_value(observer, "caller_wrapper") == 42, "commit did not publish routine dependency")
    require(scalar(observer, "CALL caller_procedure()") == 42, "committed procedure is not callable")
    require(scalar(observer, "SELECT COUNT(*) FROM writes") == 1, "catalog commit lost caller DML")


def check_savepoints(writer, observer, reader, admin, database, user, error_type):
    original = mutate(writer, create_function("cycle_value", "7"))
    scalar(admin, f"GRANT EXECUTE ON FUNCTION {identifier(database)}.cycle_value TO '{user}'@'%'")
    require(function_value(reader, "cycle_value") == 7, "explicit routine grant is not usable")
    # Warm exactly the queries that will run again after DROP/recreation/undo.
    require(function_value(observer, "cycle_value") == 7, "initial cached function result")
    writer.begin()
    scalar(writer, "SAVEPOINT caller_owned")
    require(mutate(writer, "ALTER FUNCTION cycle_value COMMENT 'private change'") == original,
            "property ALTER changed the routine identity")
    altered_definition = function_definition(writer, "cycle_value")
    require("private change" in altered_definition,
            f"private ALTER property is not visible: definition={altered_definition!r}")
    require("private change" not in function_definition(observer, "cycle_value"), "private ALTER property escaped")
    require(mutate(writer, "DROP FUNCTION cycle_value") == original, "private DROP lost original identity")
    replacement = mutate(writer, create_function("cycle_value", "8"))
    require(replacement != original, "recreation reused the removed routine identity")
    require(function_value(writer, "cycle_value") == 8, "private recreation called stale PL code")
    require(function_value(observer, "cycle_value") == 7, "uncommitted recreation escaped to observer")
    scalar(writer, "ROLLBACK TO SAVEPOINT caller_owned")
    require(lookup(writer, "cycle_value") == original, "savepoint did not restore original identity")
    require(function_value(writer, "cycle_value") == 7, "savepoint did not restore function body")
    require("private change" not in function_definition(writer, "cycle_value"), "savepoint did not restore ALTER property")
    require(function_value(reader, "cycle_value") == 7, "savepoint damaged a committed EXECUTE grant")
    mutate(writer, create_function("after_savepoint", "9"))
    scalar(writer, "ROLLBACK TO SAVEPOINT caller_owned")
    missing(writer, "after_savepoint", error_type)
    writer.commit()
    require(function_value(observer, "cycle_value") == 7, "rollback queue later invalidated/replaced the wrong schema")

    writer.begin()
    require(mutate(writer, "DROP FUNCTION cycle_value") == original, "committing DROP identity")
    replacement = mutate(writer, create_function("cycle_value", "10"))
    require(replacement != original, "committed recreation reused old identity")
    writer.commit()
    require(lookup(observer, "cycle_value") == replacement, "observer retained old routine identity")
    require(function_value(observer, "cycle_value") == 10, "observer reused old compiled code after commit")
    expect_error(reader, "SELECT cycle_value()", None, DENIED, error_type)


def check_statement_rollback(writer, observer, error_type):
    # Successful control proves the aggregate really invokes the mutator for
    # both source rows. No SQL text is executed merely to fabricate a receipt.
    ddl = "CONCAT('CREATE FUNCTION late_', id, '() RETURNS BIGINT DETERMINISTIC NO SQL RETURN ', id)"
    expression = f"SUM(CASE WHEN seekdb_rust_routine_ddl({ddl}) IS NOT NULL THEN 1 ELSE 0 END)"
    source = " FROM inputs"
    writer.begin()
    require(scalar(writer, "SELECT /*+ PARALLEL(1) */ " + expression + source) == 2,
            "multirow catalog control did not execute both mutations")
    require(function_value(writer, "late_1") == 1 and function_value(writer, "late_2") == 2,
            "successful multirow control did not create callable routines")
    writer.rollback()
    for name in ("late_1", "late_2"):
        missing(writer, name, error_type)
        missing(observer, name, error_type)

    # EXP consumes the aggregate result, so its overflow follows all argument
    # mutations, not an unspecified order between two SELECT target columns.
    # Scientific notation pins the floating-point EXP path/error contract.
    failing = "SELECT /*+ PARALLEL(1) */ EXP(1e3 + " + expression + ")" + source
    for explicit in (False, True):
        if explicit:
            writer.begin()
            scalar(writer, "INSERT INTO writes VALUES (200)")
        expect_error(writer, failing, None, OVERFLOW, error_type)
        for name in ("late_1", "late_2"):
            missing(writer, name, error_type)
            missing(observer, name, error_type)
        require(scalar(observer, "SELECT COUNT(*) FROM writes") == 1, "failed statement committed caller data")
        if explicit:
            require(scalar(writer, "SELECT COUNT(*) FROM writes") == 2, "failed statement lost earlier caller DML")
            # Failure must not strand nested SQL/session state; retry in this
            # same transaction, then prove full rollback removes both effects.
            mutate(writer, create_function("after_failure", "17"))
            require(function_value(writer, "after_failure") == 17, "transaction cannot continue after statement rollback")
            writer.rollback()
            missing(observer, "after_failure", error_type)
    require(scalar(writer, "SELECT @@autocommit") == 1, "nested transport leaked autocommit state")


def check_invoker(creator, reader, observer, error_type):
    denied = create_function("denied_value", "50")
    expect_error(reader, MUTATE, (denied,), DENIED, error_type)
    missing(observer, "denied_value", error_type)
    creator.begin()
    scalar(creator, "SAVEPOINT before_create")
    created = mutate(creator, create_function("owned_value", "55"))
    require(function_value(creator, "owned_value") == 55, "creator's provisional automatic EXECUTE grant is missing")
    missing(observer, "owned_value", error_type)
    scalar(creator, "ROLLBACK TO SAVEPOINT before_create")
    missing(creator, "owned_value", error_type)
    recreated = mutate(creator, create_function("owned_value", "56"))
    require(recreated != created, "savepoint recreation reused identity")
    creator.commit()
    require(function_value(creator, "owned_value") == 56, "creator's committed EXECUTE grant is missing")
    expect_error(reader, "SELECT owned_value()", None, DENIED, error_type)
    creator.begin()
    scalar(creator, "SAVEPOINT before_drop")
    require(mutate(creator, "DROP FUNCTION owned_value") == recreated, "creator's automatic ALTER grant is missing")
    scalar(creator, "ROLLBACK TO SAVEPOINT before_drop")
    require(function_value(creator, "owned_value") == 56, "DROP rollback lost creator ACL/body")
    creator.commit()


def check_rejections(writer, observer, error_type):
    writer.begin()
    scalar(writer, "INSERT INTO writes VALUES (300)")
    expect_error(writer, MUTATE, (create_function("caller_value", "999"),), DUPLICATE_ROUTINE, error_type)
    require(function_value(writer, "caller_value") == 41, "duplicate CREATE replaced existing routine")
    require(scalar(writer, "SELECT COUNT(*) FROM writes") == 2, "rejected CREATE rolled back previous caller write")
    require(scalar(observer, "SELECT COUNT(*) FROM writes") == 1, "rejected CREATE committed previous caller write")
    writer.rollback()
    scalar(writer, "START TRANSACTION READ ONLY")
    expect_error(writer, MUTATE, (create_function("readonly_value", "1"),), READ_ONLY, error_type)
    writer.rollback()
    missing(observer, "readonly_value", error_type)


def run(options, driver):
    suffix = secrets.token_hex(10)
    database = "query_catalog_" + suffix
    reader_name = "qc_read_" + suffix
    creator_name = "qc_make_" + suffix
    config = dict(host="127.0.0.1", port=options.port, user=options.user,
                  password=os.environ.get("SEEKDB_TEST_PASSWORD", ""),
                  autocommit=True, charset="utf8mb4", connect_timeout=5,
                  read_timeout=120, write_timeout=60)
    connections, users = [], []
    database_created = passed = False

    def connect(**changes):
        connection = driver.connect(**dict(config, **changes))
        connections.append(connection)
        return connection

    admin = connect()
    try:
        # Preconditions have no DDL side effects; a missing capability must stop
        # the run, not be reclassified as an expected later rejection.
        require(scalar(admin, "SELECT seekdb_rust_routine_ddl(NULL)") is None, "unexpected NULL mutator result")
        require(scalar(admin, "SELECT seekdb_rust_routine_id(NULL)") is None, "unexpected NULL lookup result")
        require(int(scalar(admin, "SELECT @@automatic_sp_privileges")) == 1,
                "suite requires automatic_sp_privileges=1; no global setting was changed")
        scalar(admin, "SELECT COUNT(*) FROM oceanbase.__all_extension_member")
        scalar(admin, f"CREATE DATABASE {identifier(database)}")
        database_created = True
        print("Created fixture database:", database, flush=True)
        admin.select_db(database)
        scalar(admin, "CREATE TABLE writes(id BIGINT PRIMARY KEY)")
        scalar(admin, "CREATE TABLE inputs(id BIGINT PRIMARY KEY)")
        scalar(admin, "INSERT INTO inputs VALUES (1), (2)")
        restricted = []
        for name, privileges in ((reader_name, "SELECT"), (creator_name, "SELECT, CREATE ROUTINE")):
            password = secrets.token_urlsafe(24)
            # PyMySQL interpolates %s client-side: escape the literal host %
            # only in this parameterized statement. GRANT/DROP have no args.
            scalar(admin, f"CREATE USER '{name}'@'%%' IDENTIFIED BY %s", (password,))
            users.append(name)
            print("Created fixture user:", name + "@%", flush=True)
            scalar(admin, f"GRANT {privileges} ON {identifier(database)}.* TO '{name}'@'%'")
            restricted.append(connect(user=name, password=password, database=database))
        reader, creator = restricted
        writer, observer = connect(database=database), connect(database=database)
        check_autocommit(writer, observer, driver.MySQLError)
        check_caller_transaction(writer, observer, driver.MySQLError)
        check_savepoints(writer, observer, reader, admin, database, reader_name, driver.MySQLError)
        check_statement_rollback(writer, observer, driver.MySQLError)
        check_invoker(creator, reader, observer, driver.MySQLError)
        check_rejections(writer, observer, driver.MySQLError)
        database_id = scalar(admin, "SELECT database_id FROM oceanbase.__all_database WHERE database_name=%s", (database,))
        object_id(database_id)
        require(scalar(admin, "SELECT COUNT(*) FROM oceanbase.__all_extension_member "
                             "WHERE tenant_id=1 AND database_id=%s", (database_id,)) == 0,
                "runtime-created business routines automatically became Extension members")
        require(scalar(writer, "SELECT DATABASE()") == database, "nested transport leaked current database")
        passed = True
    finally:
        # Roll back/close participants before cleanup. Failed/unknown commits
        # are never retried, and a failed run retains its uniquely named state.
        primary_failure = sys.exc_info()[0] is not None
        cleanup_errors = []
        for connection in reversed(connections[1:]):
            try:
                connection.rollback()
            except Exception as error:
                cleanup_errors.append(error)
            finally:
                try:
                    connection.close()
                except Exception as error:
                    cleanup_errors.append(error)
        try:
            if passed and not cleanup_errors:
                for name in tuple(users):
                    scalar(admin, f"DROP USER '{name}'@'%'")
                    users.remove(name)
                    print("Removed fixture user:", name + "@%", flush=True)
                if database_created:
                    scalar(admin, f"DROP DATABASE {identifier(database)}")
                    database_created = False
                    print("Removed fixture database:", database, flush=True)
        except Exception as error:
            cleanup_errors.append(error)
        finally:
            try:
                admin.close()
            except Exception as error:
                cleanup_errors.append(error)
        if database_created or users:
            remaining = ([database] if database_created else []) + [name + "@%" for name in users]
            print("Run incomplete; retain fixtures for diagnosis:", *remaining, file=sys.stderr)
        if cleanup_errors:
            print("Fixture rollback/close/cleanup failed; server state requires inspection.", file=sys.stderr)
            if not primary_failure:
                raise cleanup_errors[0]
    print("PASS: real query catalog CREATE/ALTER/DROP, caller visibility, PL dependencies, "
          "savepoints, statement/full rollback, invoker ACL and commit cache refresh")
    print("Not covered: concurrent DDL, injected commit failure/unknown outcome, recovery or performance.")


def parse_options(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--user", default="root")
    parser.add_argument("--confirm-disposable-server", action="store_true", required=True)
    options = parser.parse_args(argv)
    if not 1 <= options.port <= 65535:
        parser.error("port must be between 1 and 65535")
    return options


if __name__ == "__main__":
    options = parse_options()
    # Offline helper tests and --help do not need a database driver installed.
    import pymysql
    run(options, pymysql)
