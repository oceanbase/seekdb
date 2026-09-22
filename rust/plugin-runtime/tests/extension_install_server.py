#!/usr/bin/env python3
"""Opt-in CREATE/ALTER UPDATE/DROP EXTENSION regression on a disposable loopback server.

Start a current experimental build with --extension-dir pointing to the parent
of text_ops/ and text_composed/ (e.g. this worktree's plugins/sql_packages).
Requires PyMySQL and the current Extension dependency system table.
Creates unique fixture databases/user; never changes server/package settings.
Failure retains fixtures and prints their names. Successful cleanup removes only
objects created by this invocation. Not part of standalone CTest.
"""
import argparse
import os
import secrets

import pymysql
from pymysql.constants import ER
from sql_spi_server import must_fail, scalar


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def installed(connection, database_id, name="text_ops"):
    with connection.cursor() as cursor:
        cursor.execute("SELECT extension_id, extension_version FROM oceanbase.__all_extension_instance "
                       "WHERE tenant_id=1 AND database_id=%s AND extension_name=%s", (database_id, name))
        return cursor.fetchall()


def dependency_edges(connection, database_id):
    with connection.cursor() as cursor:
        cursor.execute("SELECT required_extension_id, extension_id FROM oceanbase.__all_extension_dependency "
                       "WHERE tenant_id=1 AND database_id=%s ORDER BY required_extension_id, extension_id",
                       (database_id,))
        return cursor.fetchall()


def check_composed_functions(connection, updated=False):
    for value, expected in [("ASCII", 1), ("海洋", 0), ("", 1), (None, None)]:
        require(scalar(connection, "SELECT seekdb_text_is_ascii(%s)", (value,)) == expected,
                "composed ASCII function result")
    if updated:
        for value, expected in [("ASCII", 0), ("海洋", 4), ("", 0), (None, None)]:
            require(scalar(connection, "SELECT seekdb_text_extra_bytes(%s)", (value,)) == expected,
                    "composed update function result")


def check_functions(connection):
    require(scalar(connection, "SELECT seekdb_char_count('海洋')") == 2, "character function result")
    require(scalar(connection, "SELECT seekdb_byte_count('海洋')") == 6, "byte function result")


def run(options):
    suffix = secrets.token_hex(12)
    databases = ["ext_a_" + suffix, "ext_b_" + suffix]
    user = "ext_" + secrets.token_hex(10)
    password = secrets.token_urlsafe(24)
    config = dict(host="127.0.0.1", port=options.port, user=options.user,
                  password=os.environ.get("SEEKDB_TEST_PASSWORD", ""),
                  autocommit=True, charset="utf8mb4", connect_timeout=5,
                  read_timeout=120, write_timeout=60)
    created = []
    user_created = False
    try:
        with pymysql.connect(**config) as connection:
            # Ensure the installation catalog exists before creating fixtures.
            scalar(connection, "SELECT COUNT(*) FROM oceanbase.__all_extension_instance")
            scalar(connection, "SELECT COUNT(*) FROM oceanbase.__all_extension_dependency")
            ids = []
            for database in databases:
                scalar(connection, f"CREATE DATABASE `{database}`")
                created.append(database)
                print(f"Created fixture database: {database}", flush=True)
                ids.append(scalar(connection, "SELECT database_id FROM oceanbase.__all_database "
                                  "WHERE database_name=%s", (database,)))
                require(ids[-1] is not None, "missing database identity")
            connection.select_db(databases[0])
            must_fail(connection, "CREATE EXTENSION text_composed")
            require(installed(connection, ids[0], "text_composed") == () and dependency_edges(connection, ids[0]) == (),
                    "missing provider left consumer identity or dependencies")
            require(scalar(connection, "SELECT COUNT(*) FROM information_schema.routines WHERE routine_schema=%s",
                           (databases[0],)) == 0, "missing provider left schema objects")
            scalar(connection, "CREATE TABLE transaction_probe (id BIGINT PRIMARY KEY)")
            with pymysql.connect(**config, database=databases[0]) as observer:
                connection.begin()
                scalar(connection, "INSERT INTO transaction_probe VALUES (1)")
                must_fail(connection, "CREATE EXTENSION text_ops")
                require(scalar(connection, "SELECT COUNT(*) FROM transaction_probe") == 1,
                        "installation rolled back the caller transaction")
                require(scalar(observer, "SELECT COUNT(*) FROM transaction_probe") == 0,
                        "installation implicitly committed the caller transaction")
                connection.rollback()
                require(installed(connection, ids[0]) == (), "rejected install left an Extension")

                scalar(connection, "CREATE EXTENSION text_ops VERSION '1.0'")
                check_functions(observer)
                first = installed(connection, ids[0])
                require(len(first) == 1 and first[0][1] == "1.0", "installation identity/version")
                require(scalar(connection, "SELECT COUNT(*) FROM oceanbase.__all_extension_member "
                               "WHERE tenant_id=1 AND database_id=%s AND extension_id=%s",
                               (ids[0], first[0][0])) == 2, "both routines must be Extension members")
                must_fail(connection, "CREATE EXTENSION text_ops")
                require(installed(connection, ids[0]) == first, "duplicate install changed identity")
                must_fail(connection, "DROP FUNCTION seekdb_char_count")
                check_functions(observer)
                connection.begin()
                scalar(connection, "INSERT INTO transaction_probe VALUES (2)")
                must_fail(connection, "DROP EXTENSION text_ops")
                require(scalar(connection, "SELECT COUNT(*) FROM transaction_probe") == 1,
                        "removal rolled back the caller transaction")
                require(scalar(observer, "SELECT COUNT(*) FROM transaction_probe") == 0,
                        "removal implicitly committed the caller transaction")
                connection.rollback()
                must_fail(connection, "DROP EXTENSION text_ops CASCADE")
                require(installed(connection, ids[0]) == first, "rejected removal changed installation")
                check_functions(observer)

                # Default 1.0 is a confirmed no-op. Updating must not replay the
                # 1.0 base (its functions already exist), nor change instance ID.
                with connection.cursor() as cursor:
                    require(cursor.execute("ALTER EXTENSION text_ops UPDATE") == 0, "same-version affected rows")
                require(installed(connection, ids[0]) == first, "default no-op changed installation")
                must_fail(connection, "ALTER EXTENSION text_ops UPDATE TO 'unavailable'")
                require(installed(connection, ids[0]) == first, "missing path changed version")
                connection.begin()
                scalar(connection, "INSERT INTO transaction_probe VALUES (3)")
                must_fail(connection, "ALTER EXTENSION text_ops UPDATE TO '1.1'")
                require(scalar(connection, "SELECT COUNT(*) FROM transaction_probe") == 1,
                        "update rolled back the caller transaction")
                require(scalar(observer, "SELECT COUNT(*) FROM transaction_probe") == 0,
                        "update implicitly committed the caller transaction")
                connection.rollback()
                require(installed(connection, ids[0]) == first, "rejected update changed version")
                with connection.cursor() as cursor:
                    require(cursor.execute("ALTER EXTENSION text_ops UPDATE TO '1.1'") == 1, "update affected rows")
                require(installed(connection, ids[0]) == ((first[0][0], "1.1"),), "update changed identity or lost version")
                first = installed(connection, ids[0])
                check_functions(observer)
                require(scalar(observer, "SELECT seekdb_is_empty('')") == 1, "new update function is not callable")
                require(scalar(observer, "SELECT seekdb_is_empty('x')") == 0, "new update function result")
                require(scalar(connection, "SELECT COUNT(*) FROM oceanbase.__all_extension_member "
                               "WHERE tenant_id=1 AND database_id=%s AND extension_id=%s",
                               (ids[0], first[0][0])) == 3, "update did not retain old and new membership")
                with connection.cursor() as cursor:
                    require(cursor.execute("ALTER EXTENSION text_ops UPDATE TO '1.1'") == 0, "repeat update is not a no-op")
                must_fail(connection, "DROP FUNCTION seekdb_is_empty")

                scalar(connection, "CREATE EXTENSION text_composed VERSION '1.0'")
                consumer = installed(connection, ids[0], "text_composed")
                require(len(consumer) == 1 and consumer[0][1] == "1.0", "consumer installation identity")
                edges = ((first[0][0], consumer[0][0]),)
                require(dependency_edges(connection, ids[0]) == edges, "dependency must use stable provider/consumer IDs")
                check_composed_functions(observer)
                scalar(connection, "ALTER EXTENSION text_composed UPDATE TO '1.1'")
                require(installed(connection, ids[0], "text_composed") == ((consumer[0][0], "1.1"),),
                        "consumer update changed its identity")
                require(dependency_edges(connection, ids[0]) == edges, "consumer update changed dependencies")
                check_composed_functions(observer, updated=True)
                with connection.cursor() as cursor:
                    require(cursor.execute("ALTER EXTENSION text_composed UPDATE TO '1.1'") == 0,
                            "consumer same-version update is not a no-op")
                require(dependency_edges(connection, ids[0]) == edges, "consumer no-op changed dependencies")
                must_fail(connection, "DROP EXTENSION text_ops RESTRICT")
                require(installed(connection, ids[0]) == first and dependency_edges(connection, ids[0]) == edges,
                        "RESTRICT changed provider or dependencies")
                check_composed_functions(observer, updated=True)
                scalar(connection, "DROP EXTENSION text_composed")
                require(installed(connection, ids[0], "text_composed") == () and dependency_edges(connection, ids[0]) == (),
                        "consumer removal retained identity or outgoing dependencies")
                require(installed(connection, ids[0]) == first, "consumer removal changed provider")
                must_fail(observer, "SELECT seekdb_text_is_ascii('ASCII')")
                must_fail(observer, "SELECT seekdb_text_extra_bytes('海洋')")
                check_functions(observer)

            # A provider installed in another database must not satisfy requires.
            connection.select_db(databases[1])
            must_fail(connection, "CREATE EXTENSION text_composed")
            require(installed(connection, ids[1], "text_composed") == () and dependency_edges(connection, ids[1]) == (),
                    "requires resolved across databases")
            require(scalar(connection, "SELECT COUNT(*) FROM information_schema.routines WHERE routine_schema=%s",
                           (databases[1],)) == 0, "cross-database rejection left schema objects")
            connection.select_db(databases[0])

            scalar(connection, f"CREATE USER '{user}'@'%' IDENTIFIED BY %s", (password,))
            user_created = True
            print(f"Created fixture user: {user}@%", flush=True)
            scalar(connection, f"GRANT SELECT ON `{databases[1]}`.* TO '{user}'@'%'")
            restricted = dict(config, user=user, password=password, database=databases[1])
            with pymysql.connect(**restricted) as reader:
                must_fail(reader, "CREATE EXTENSION text_ops", expected_codes={
                    ER.DBACCESS_DENIED_ERROR, ER.TABLEACCESS_DENIED_ERROR, ER.SPECIFIC_ACCESS_DENIED_ERROR})
            require(installed(connection, ids[1]) == (), "permission rejection left an Extension")
            scalar(connection, f"GRANT CREATE ROUTINE ON `{databases[1]}`.* TO '{user}'@'%'")
            with pymysql.connect(**restricted) as installer:
                scalar(installer, "CREATE EXTENSION text_ops")
            second = installed(connection, ids[1])
            require(len(second) == 1 and second[0][0] != first[0][0], "database instances share identity")

            # Object privileges alone do not transfer Extension ownership.
            scalar(connection, f"GRANT SELECT, ALTER ROUTINE ON `{databases[0]}`.* TO '{user}'@'%'")
            with pymysql.connect(**dict(restricted, database=databases[0])) as nonowner:
                must_fail(nonowner, "ALTER EXTENSION text_ops UPDATE TO '1.1'")
                must_fail(nonowner, "DROP EXTENSION text_ops")
            require(installed(connection, ids[0]) == first, "nonowner removal changed installation")
            connection.select_db(databases[0])
            scalar(connection, "DROP EXTENSION text_ops RESTRICT")
            require(installed(connection, ids[0]) == (), "successful removal retained installation")
            require(scalar(connection, "SELECT COUNT(*) FROM oceanbase.__all_extension_member "
                           "WHERE tenant_id=1 AND database_id=%s", (ids[0],)) == 0,
                    "successful removal retained members")
            must_fail(connection, "SELECT seekdb_char_count('x')")
            must_fail(connection, "SELECT seekdb_byte_count('x')")
            must_fail(connection, "DROP EXTENSION text_ops")
            scalar(connection, "CREATE EXTENSION text_ops")
            recreated = installed(connection, ids[0])
            require(len(recreated) == 1 and recreated[0][0] != first[0][0], "recreated Extension reused old identity")

            # The database-scoped owner can remove the group without SUPER.
            scalar(connection, f"GRANT ALTER ROUTINE ON `{databases[1]}`.* TO '{user}'@'%'")
            with pymysql.connect(**restricted) as owner:
                scalar(owner, "DROP EXTENSION text_ops")
                require(installed(connection, ids[1]) == (), "owner removal retained installation")
                scalar(owner, "CREATE EXTENSION text_ops")
            require(installed(connection, ids[0]) == recreated, "removing other database changed installation")

            # Whole-database cleanup removes internal dependency edges, without
            # touching the independent provider/consumer pair in another DB.
            pairs = []
            for database, database_id in zip(databases, ids):
                connection.select_db(database)
                scalar(connection, "CREATE EXTENSION text_composed")
                consumer = installed(connection, database_id, "text_composed")
                provider = installed(connection, database_id)
                require(len(consumer) == 1 and len(provider) == 1, "missing database-local pair")
                pairs.append(((provider[0][0], consumer[0][0]),))
                require(dependency_edges(connection, database_id) == pairs[-1], "wrong database-local dependency")
                check_composed_functions(connection)
            require(pairs[0][0][1] != pairs[1][0][1], "consumer identity shared across databases")
            connection.select_db(databases[1])
            check_functions(connection)
            scalar(connection, f"DROP DATABASE `{databases[0]}`")
            created.remove(databases[0])
            require(installed(connection, ids[0]) == (), "database drop retained Extension identity")
            require(installed(connection, ids[0], "text_composed") == (), "database drop retained consumer identity")
            require(scalar(connection, "SELECT COUNT(*) FROM oceanbase.__all_extension_member "
                           "WHERE tenant_id=1 AND database_id=%s", (ids[0],)) == 0,
                    "database drop retained Extension members")
            require(dependency_edges(connection, ids[0]) == (), "database drop retained dependency edges")
            require(dependency_edges(connection, ids[1]) == pairs[1], "database drop changed another database's edges")
            check_composed_functions(connection)
            check_functions(connection)
            scalar(connection, f"DROP DATABASE `{databases[1]}`")
            created.remove(databases[1])
            require(dependency_edges(connection, ids[1]) == (), "second database drop retained dependency edges")
            require(installed(connection, ids[1], "text_composed") == (), "second database drop retained consumer identity")
            scalar(connection, f"DROP USER '{user}'@'%'")
            user_created = False
            print("PASS: SQL install/update/removal, composition/requires/RESTRICT, member ownership, independent databases, caller transaction and permissions")
            print("Removed this invocation's fixture databases and user; the script cannot recover them.")
    except BaseException:
        print(f"Failure: retained fixture databases {created}; user {user if user_created else '(not created)'}", flush=True)
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--user", default="root")
    parser.add_argument("--confirm-disposable-server", action="store_true", required=True)
    options = parser.parse_args()
    if not 1 <= options.port <= 65535:
        parser.error("port must be between 1 and 65535")
    run(options)


if __name__ == "__main__":
    main()
