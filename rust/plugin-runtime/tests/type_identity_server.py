#!/usr/bin/env python3
"""Opt-in, two-phase persistent type identity regression on a disposable server.

prepare creates its own database; restart the server externally, then verify
using the printed database name. No automatic server/package/config changes.
Failed verification retains fixtures; cleanup checks the fixture marker first.
This script is not part of standalone CTest and has not been run against a DB.
"""

import argparse
import os
import re
import secrets

import pymysql


PLUGIN = "org.seekdb.sql_extension"
DATABASE_PATTERN = re.compile(r"plugin_type_[0-9a-f]{24}\Z")


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def query(connection, sql, args=None):
    with connection.cursor() as cursor:
        cursor.execute(sql, args)
        return cursor.fetchall()


def generation(connection):
    rows = query(connection,
                 "SELECT generation FROM oceanbase.__all_plugin_package "
                 "WHERE plugin_id=%s AND desired_state=0 AND actual_state=4", (PLUGIN,))
    check(len(rows) == 1 and int(rows[0][0]) > 0, "SQL extension must be catalog ACTIVE")
    return int(rows[0][0])


def marker(connection, database):
    check(DATABASE_PATTERN.fullmatch(database) is not None, "Not a fixture database name")
    connection.select_db(database)
    rows = query(connection, "SELECT fixture_name, plugin_generation FROM fixture_identity")
    check(len(rows) == 1 and rows[0][0] == database and int(rows[0][1]) > 0,
          "Fixture marker mismatch; refusing to modify this database")
    return int(rows[0][1])


def run(options):
    with pymysql.connect(host="127.0.0.1", port=options.port, user=options.user,
                         password=os.environ.get("SEEKDB_TEST_PASSWORD", ""),
                         autocommit=True, charset="utf8mb4", connect_timeout=5,
                         read_timeout=60, write_timeout=60) as connection:
        if options.phase == "prepare":
            before = generation(connection)
            database = "plugin_type_" + secrets.token_hex(12)
            query(connection, f"CREATE DATABASE `{database}`")
            # Print immediately, so a subsequent failure does not hide a fixture.
            print(f"Created fixture database: {database}", flush=True)
            connection.select_db(database)
            query(connection, "CREATE TABLE fixture_identity "
                              "(fixture_name VARCHAR(64) PRIMARY KEY, plugin_generation BIGINT)")
            query(connection, "INSERT INTO fixture_identity VALUES (%s, %s)", (database, before))
            query(connection, "CREATE TABLE payloads (id BIGINT PRIMARY KEY, payload seekdb_payload)")
            query(connection, "INSERT INTO payloads VALUES (1, X'010203')")
            rows = query(connection, "SHOW CREATE TABLE payloads")
            check(len(rows) == 1 and "seekdb_payload" in rows[0][1].lower(),
                  "Plugin type did not survive column metadata printing")
            print(f"Restart this disposable server, then run verify --database {database} "
                  f"with the same --port (previous generation: {before}).")
            return

        before = marker(connection, options.database)
        if options.phase == "verify":
            after = generation(connection)
            check(after > before, "Provider generation did not advance; restart is not verified")
            check(query(connection, "SELECT HEX(payload) FROM payloads WHERE id=1") == (("010203",),),
                  "Stored bytes changed after recovery")
            rows = query(connection, "SHOW CREATE TABLE payloads")
            check(len(rows) == 1 and "seekdb_payload" in rows[0][1].lower(),
                  "Recovered column lost its logical type")
            # LIKE reuses persisted metadata; ADD must resolve the new durable
            # generation instead of using the old column's runtime fence.
            query(connection, "CREATE TABLE copied LIKE payloads")
            # Recovery has rebound existing dependency rows. DROP must remove
            # their recorded generation, not the generation stored by old v1.
            query(connection, "ALTER TABLE copied DROP COLUMN payload")
            query(connection, "DROP TABLE copied")
            query(connection, "DROP TABLE payloads")
            print(f"Verified logical type operations across generations {before} -> {after}")
        # Both successful verify and explicitly selected cleanup remove only
        # the exact fixture whose marker was checked on this connection.
        query(connection, f"DROP DATABASE `{options.database}`")
        print(f"Removed fixture database: {options.database} (not recoverable by this script)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=("prepare", "verify", "cleanup"))
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--user", default="root")
    parser.add_argument("--database")
    parser.add_argument("--confirm-disposable-server", action="store_true", required=True)
    options = parser.parse_args()
    if not 1 <= options.port <= 65535:
        parser.error("port must be between 1 and 65535")
    if options.phase == "prepare" and options.database is not None:
        parser.error("prepare generates its own unique database")
    if options.phase != "prepare" and not options.database:
        parser.error("verify/cleanup requires --database from prepare")
    run(options)


if __name__ == "__main__":
    main()
