#!/usr/bin/env python3
# Copyright (c) 2026 OceanBase.
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Opt-in native_math install/update/drop test on a disposable current-schema server.

Requires the rebuilt sql_extension module already loaded and --extension-dir
configured for the shipped SQL packages. Does not load/unload modules, restart
the server, alter package files or change global settings. Creates unique
databases/users; failures retain those fixtures. No recovery/concurrency claim.
"""
import argparse
import os
import secrets

MODULE = "org.seekdb.sql_extension"
IMPLEMENTATION = "org.seekdb.sql-extension.function.native-add-one"
UNNAMED = "org.seekdb.sql-extension.function.unnamed-add-one"


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def query(connection, sql, arguments=None):
    with connection.cursor() as cursor:
        cursor.execute(sql, arguments)
        rows = cursor.fetchall()
        while cursor.nextset():
            cursor.fetchall()
        return rows


def scalar(connection, sql, arguments=None):
    rows = query(connection, sql, arguments)
    if not rows:
        return None
    require(len(rows) == 1 and len(rows[0]) == 1, "expected exactly one scalar result")
    return rows[0][0]


def reject(connection, sql, driver, codes):
    require(bool(codes), "negative tests must name expected protocol errors")
    try:
        query(connection, sql)
    except driver.MySQLError as error:
        # An internal error or lost connection is never a successful assertion.
        require(error.args[0] not in (2002, 2003, 2006, 2013, 2055, 4016),
                f"server/transport failed during rejected SQL: {error.args[0]}")
        require(error.args[0] in codes, f"expected errors {codes}, got {error.args[0]}")
        return
    raise AssertionError("SQL unexpectedly succeeded: " + sql)


def routines(connection, database_id):
    return query(connection,
        "SELECT routine_name,routine_id,native_module_id,native_implementation_id,native_abi_version "
        "FROM oceanbase.__all_routine WHERE database_id=%s ORDER BY routine_name", (database_id,))


def check_bindings(records, updated=False):
    expected = {"native_add_one": IMPLEMENTATION, "native_increment": UNNAMED}
    if updated:
        expected["native_successor"] = IMPLEMENTATION
    require(len(records) == len(expected), "wrong native routine count")
    require(len({row[1] for row in records}) == len(records), "duplicate routine identities")
    require({row[0]: row[3] for row in records} == expected, "wrong native implementation bindings")
    for _, identity, module, _, abi in records:
        require(type(identity) is int and 0 < identity < (1 << 63), "invalid native routine identity")
        require(module == MODULE and abi == 1, "native module/ABI mismatch")


def check_members(connection, database_id, extension_id, records):
    members = query(connection, "SELECT object_id FROM oceanbase.__all_extension_member "
                    "WHERE tenant_id=1 AND database_id=%s AND extension_id=%s",
                    (database_id, extension_id))
    require(len(members) == len(records) and {row[0] for row in members} == {row[1] for row in records},
            "extension membership does not match native routines")


def installed(connection, database_id):
    return query(connection,
        "SELECT extension_id,extension_version FROM oceanbase.__all_extension_instance "
        "WHERE tenant_id=1 AND database_id=%s AND extension_name='native_math'", (database_id,))


def check_edges(connection, records, present):
    for _, identity, _, implementation, _ in records:
        count = scalar(connection,
            "SELECT COUNT(*) FROM oceanbase.__all_plugin_dependency "
            "WHERE consumer_id=%s AND provider_plugin_id=%s AND dependency_id=%s",
            (f"routine.{identity}", MODULE, implementation))
        require(count == (1 if present else 0), f"wrong dependency count for routine {identity}: {count}")


def check_values(connection, updated=False, replaced=False):
    require(scalar(connection, "SELECT native_increment()") == (100 if replaced else 42),
            "native default argument was not evaluated")
    for name in ("native_add_one", "native_increment") + (("native_successor",) if updated else ()):
        for value, expected in ((41, 42), (-1, 0), (None, None)):
            require(scalar(connection, f"SELECT {name}(%s)", (value,)) == expected,
                    f"wrong {name} result for {value}")


def check_replacement(before, after):
    """Same SQL name/signature does not imply the same persistent object."""
    check_bindings(before, True)
    check_bindings(after, True)
    old = {row[0]: row for row in before}
    new = {row[0]: row for row in after}
    for name in ("native_add_one", "native_successor"):
        require(old[name] == new[name], "replacement changed an untouched identity/binding")
    require(new["native_increment"][1] not in {row[1] for row in before},
            "replacement reused an old routine identity")
    return old["native_increment"]


def check_no_acl(connection, identity):
    require(scalar(connection, "SELECT COUNT(*) FROM oceanbase.__all_objauth "
                   "WHERE tenant_id=1 AND objtype=9 AND obj_id=%s", (identity,)) == 0,
            "replacement retained the old routine's live object ACL")


def check_mixed_update(admin, observer, reader, driver, database, database_id, extension_id, account, before):
    query(admin, f"GRANT EXECUTE ON FUNCTION `{database}`.native_increment TO {account}")
    for connection in (observer, reader):
        query(connection, "PREPARE native_replace_plan FROM 'SELECT native_increment()'")
        require(scalar(connection, "EXECUTE native_replace_plan") == 42, "old default call failed")
    # No retry: an unknown UPDATE result must preserve fixtures for diagnosis.
    query(admin, "ALTER EXTENSION native_math UPDATE TO '1.2'")
    require(installed(admin, database_id) == ((extension_id, "1.2"),), "wrong mixed update identity/version")
    after = routines(admin, database_id)
    removed = check_replacement(before, after)
    check_members(admin, database_id, extension_id, after)
    check_edges(admin, (removed,), False)
    check_edges(admin, after, True)
    check_no_acl(admin, removed[1])
    statement = query(observer, "SHOW CREATE FUNCTION native_add_one")[0][2]
    require("native_math 1.2" in statement, "mixed ALTER comment was not published")
    check_values(observer, True, True)
    require(scalar(observer, "EXECUTE native_replace_plan") == 100,
            "prepared call retained the dropped routine/default argument")
    denied = {1044, 1142, 1227, 1370}
    reject(reader, "SELECT native_increment()", driver, denied)
    reject(reader, "EXECUTE native_replace_plan", driver, denied)
    query(admin, f"GRANT EXECUTE ON FUNCTION `{database}`.native_increment TO {account}")
    require(scalar(reader, "SELECT native_increment()") == 100, "new identity grant not honored")
    require(scalar(reader, "EXECUTE native_replace_plan") == 100,
            "prepared call did not rebind after granting the new identity")
    for connection in (observer, reader):
        query(connection, "DEALLOCATE PREPARE native_replace_plan")
    return after


def check_invoker_package(admin, reader, database_id, user):
    """The fresh fixture user has CREATE ROUTINE only, never SUPER."""
    identity = scalar(admin, "SELECT user_id FROM oceanbase.__all_user WHERE user_name=%s AND host=%s",
                      (user, "%"))
    require(type(identity) is int and identity > 0, "missing fixture user identity")
    query(reader, "CREATE EXTENSION text_ops")
    owners = query(admin, "SELECT owner_id FROM oceanbase.__all_extension_instance "
                   "WHERE tenant_id=1 AND database_id=%s AND extension_name='text_ops'", (database_id,))
    require(owners == ((identity,),), "invoker installation elevated or changed its owner")
    require(scalar(reader, "SELECT seekdb_char_count('雪')") == 1, "invoker SQL package call failed")
    query(reader, "ALTER EXTENSION text_ops UPDATE TO '1.1'")
    require(scalar(reader, "SELECT seekdb_is_empty('')") == 1, "invoker package update failed")
    query(reader, "DROP EXTENSION text_ops")
    require(not routines(admin, database_id), "invoker package removal retained routines")
    require(scalar(admin, "SELECT COUNT(*) FROM oceanbase.__all_extension_instance "
                   "WHERE tenant_id=1 AND database_id=%s AND extension_name='text_ops'", (database_id,)) == 0,
            "invoker package removal retained its installation")


def run(options, driver):
    suffix = secrets.token_hex(10)
    databases = ["native_ext_a_" + suffix, "native_ext_b_" + suffix]
    user = "ne_read_" + suffix
    password = secrets.token_urlsafe(24)
    config = dict(user=options.user, password=os.environ.get("SEEKDB_TEST_PASSWORD", ""),
                  autocommit=True, charset="utf8mb4", connect_timeout=5,
                  read_timeout=120, write_timeout=60)
    if options.unix_socket:
        config["unix_socket"] = options.unix_socket
    else:
        config.update(host="127.0.0.1", port=options.port)
    created, accounts = [], []
    try:
        with driver.connect(**config) as admin:
            # Fail before fixtures if the module/schema is not the expected one.
            require(scalar(admin, "SELECT seekdb_add_one(41)") == 42, "reference module is not ready")
            query(admin, "SELECT native_module_id,native_implementation_id,native_abi_version "
                         "FROM oceanbase.__all_routine WHERE 1=0")
            for implementation in (IMPLEMENTATION, UNNAMED):
                flags = scalar(admin, "SELECT flags FROM oceanbase.__all_sql_extension_function "
                               "WHERE function_id=%s", (implementation,))
                require(flags is not None and flags & 64, "implementation-only module build required")
            ids = []
            for database in databases:
                query(admin, f"CREATE DATABASE `{database}`")
                created.append(database)
                print("Created fixture database:", database, flush=True)
                identity = scalar(admin, "SELECT database_id FROM oceanbase.__all_database WHERE database_name=%s", (database,))
                require(identity is not None, "missing database ID")
                ids.append(identity)
            query(admin, "CREATE USER %s@%s IDENTIFIED BY %s", (user, "%", password))
            accounts.append(user)
            print("Created fixture user:", user + "@%", flush=True)
            account = f"'{user}'@'%'"
            query(admin, f"GRANT CREATE ROUTINE ON `{databases[0]}`.* TO {account}")
            reader_config = dict(config, user=user, password=password, database=databases[0])
            with driver.connect(**reader_config) as reader, driver.connect(**config) as observer:
                admin.select_db(databases[0])
                observer.select_db(databases[0])
                reject(admin, "SELECT native_add_one(41)", driver, {1305, 1630})
                reject(reader, "CREATE EXTENSION native_math", driver, {1044, 1142, 1227, 1370})
                require(not installed(admin, ids[0]) and not routines(admin, ids[0]),
                        "rejected native install left catalog objects")
                check_invoker_package(admin, reader, ids[0], user)
                query(admin, "CREATE EXTENSION native_math")
                check_values(observer)
                first = installed(admin, ids[0])
                require(len(first) == 1 and first[0][1] == "1.0", "wrong installation identity/version")
                before = routines(admin, ids[0])
                check_bindings(before)
                check_members(admin, ids[0], first[0][0], before)
                check_edges(admin, before, True)
                for name, _, _, implementation, _ in before:
                    statement = query(admin, f"SHOW CREATE FUNCTION {name}")[0][2]
                    require("LANGUAGE C" in statement and MODULE in statement and implementation in statement,
                            "SHOW CREATE lost the native binding")
                observer.select_db(databases[1])
                reject(observer, "SELECT native_add_one(41)", driver, {1305, 1630})
                observer.select_db(databases[0])
                reject(reader, "SELECT native_add_one(NULL)", driver, {1044, 1142, 1227, 1370})
                query(admin, f"GRANT EXECUTE ON FUNCTION `{databases[0]}`.native_add_one TO {account}")
                require(scalar(reader, "SELECT native_add_one(41)") == 42, "EXECUTE grant not honored")
                query(admin, f"REVOKE EXECUTE ON FUNCTION `{databases[0]}`.native_add_one FROM {account}")
                reject(reader, "SELECT native_add_one(41)", driver, {1044, 1142, 1227, 1370})
                query(observer, "PREPARE native_plan FROM 'SELECT native_add_one(41)'")
                require(scalar(observer, "EXECUTE native_plan") == 42, "prepared native call failed")
                reject(admin, "DROP FUNCTION native_add_one", driver, {4179})
                query(admin, "ALTER EXTENSION native_math UPDATE TO '1.1'")
                require(installed(admin, ids[0]) == ((first[0][0], "1.1"),), "update changed extension identity")
                after = routines(admin, ids[0])
                check_bindings(after, True)
                check_members(admin, ids[0], first[0][0], after)
                for old in before:
                    require(old in after, "update changed an existing native identity/binding")
                check_values(observer, True)
                require(scalar(observer, "EXECUTE native_plan") == 42, "prepared call did not survive update")
                check_edges(admin, after, True)
                admin.select_db(databases[1])
                query(admin, "CREATE EXTENSION native_math VERSION '1.0'")
                second = routines(admin, ids[1])
                check_bindings(second)
                second_install = installed(admin, ids[1])
                require(len(second_install) == 1 and second_install[0][1] == "1.0", "wrong second installation")
                check_members(admin, ids[1], second_install[0][0], second)
                require(not ({r[1] for r in second} & {r[1] for r in after}),
                        "databases share routine identities")
                admin.select_db(databases[0])
                after = check_mixed_update(admin, observer, reader, driver, databases[0], ids[0],
                                           first[0][0], account, after)
                require(scalar(observer, "EXECUTE native_plan") == 42,
                        "mixed update broke an untouched prepared call")
                query(admin, "DROP EXTENSION native_math")
                require(not installed(admin, ids[0]) and not routines(admin, ids[0]), "drop retained native objects")
                check_members(admin, ids[0], first[0][0], ())
                check_edges(admin, after, False)
                reject(observer, "EXECUTE native_plan", driver, {1305, 1615, 1630})
                query(observer, "DEALLOCATE PREPARE native_plan")
                admin.select_db(databases[1])
                check_values(admin)
                check_edges(admin, second, True)
                # Also cover the database-drop path, which bypasses DROP EXTENSION.
                query(admin, f"DROP DATABASE `{databases[1]}`")
                created.remove(databases[1])
                require(not installed(admin, ids[1]), "database drop retained extension")
                require(not routines(admin, ids[1]), "database drop retained routines")
                check_members(admin, ids[1], second_install[0][0], ())
                check_edges(admin, second, False)
            for database in list(created):
                query(admin, f"DROP DATABASE `{database}`")
                created.remove(database)
            query(admin, f"DROP USER {account}")
            accounts.clear()
        print("PASS: native SQL package install/update/drop, mixed ALTER/DROP/recreate, identities, "
              "dependencies, ACL, SHOW CREATE, prepared-call rebinding and invoker-only SQL package lifecycle")
        print("Removed only this run's fixture databases/user; not recoverable. Recovery/concurrency are not covered.")
    except BaseException:
        print(f"Run incomplete; retained databases={created}, users={accounts}", flush=True)
        raise


def parse_options(arguments=None, description=__doc__):
    parser = argparse.ArgumentParser(description=description)
    endpoint = parser.add_mutually_exclusive_group(required=True)
    endpoint.add_argument("--port", type=int)
    endpoint.add_argument("--unix-socket")
    parser.add_argument("--user", default="root")
    parser.add_argument("--confirm-disposable-server", action="store_true", required=True)
    options = parser.parse_args(arguments)
    if options.port is not None and not 1 <= options.port <= 65535:
        parser.error("port must be between 1 and 65535")
    if options.unix_socket and not os.path.isabs(options.unix_socket):
        parser.error("Unix socket path must be absolute")
    return options


def main():
    options = parse_options()
    import pymysql
    run(options, pymysql)


if __name__ == "__main__":
    main()
