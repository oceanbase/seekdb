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
"""Opt-in GIS SQL package regression on a disposable current-schema server.

Requires the rebuilt implementation-only GIS DSO already loaded, core GIS off,
and --extension-dir configured for the shipped gis.control/gis--1.0.sql files.
Creates unique databases/user; retains fixtures on failure. Never loads/unloads
modules, restarts servers or changes global settings. No recovery, concurrent
DDL, injected commit failure or GIS version-update coverage is claimed.
"""
from collections import Counter, defaultdict
import os
import pathlib
import re
import secrets

from native_extension_server import query, scalar, reject, require, parse_options

MODULE = "org.seekdb.gis"
MISSING = {1305, 1630}
DENIED = {1044, 1142, 1227, 1370}


def expected_bindings():
    source = pathlib.Path(__file__).resolve().parents[3] / "plugins/gis/sql/gis--1.0.sql"
    sql = source.read_text(encoding="utf-8")
    declarations = re.findall(
        r"CREATE FUNCTION `([^`]+)`\([^\n]*\)\nRETURNS [^\n]+\n"
        r"DETERMINISTIC NO SQL SQL SECURITY INVOKER\n"
        r"AS 'MODULE_PATHNAME', '([^']+)' LANGUAGE C;", sql)
    require(len(declarations) == 106 and len({name for name, _ in declarations}) == 82,
            "unrecognized local GIS package inventory")
    return Counter(declarations)


def routines(connection, database_id):
    return query(connection,
                 "SELECT routine_name,routine_id,native_module_id,native_implementation_id,"
                 "native_abi_version,overload FROM oceanbase.__all_routine "
                 "WHERE database_id=%s ORDER BY routine_name,overload", (database_id,))


def check_bindings(records, expected):
    require(len(records) == sum(expected.values()), "wrong GIS routine count")
    require(Counter((r[0], r[3]) for r in records) == expected, "wrong GIS names/implementation bindings")
    identities, slots = set(), defaultdict(list)
    for name, identity, module, implementation, abi, slot in records:
        require(type(identity) is int and 0 < identity < (1 << 63), "invalid GIS routine identity")
        require(identity not in identities, "duplicate GIS routine identity")
        identities.add(identity)
        require(module == MODULE and type(abi) is int and abi == 1, "wrong GIS module/ABI")
        require(type(slot) is int and slot >= 0, "invalid GIS overload slot")
        require(".alias." not in implementation, "SQL alias bound to noncanonical implementation")
        slots[name].append(slot)
    for family in slots.values():
        require(sorted(family) == list(range(len(family))), "wrong fresh GIS overload placement")


def installation(connection, database_id):
    return query(connection, "SELECT extension_id,extension_version FROM oceanbase.__all_extension_instance "
                 "WHERE tenant_id=1 AND database_id=%s AND extension_name='gis'", (database_id,))


def check_members(connection, database_id, extension_id, records):
    rows = query(connection, "SELECT object_id FROM oceanbase.__all_extension_member "
                 "WHERE tenant_id=1 AND database_id=%s AND extension_id=%s", (database_id, extension_id))
    require(len(rows) == len(records) and {r[0] for r in rows} == {r[1] for r in records},
            "GIS membership differs from routine identities")


def check_edges(connection, records, present):
    for _, identity, _, implementation, _, _ in records:
        count = scalar(connection, "SELECT COUNT(*) FROM oceanbase.__all_plugin_dependency "
                       "WHERE consumer_id=%s AND provider_plugin_id=%s AND dependency_id=%s",
                       (f"routine.{identity}", MODULE, implementation))
        require(count == int(present), f"wrong GIS dependency count for routine {identity}: {count}")


def check_values(connection):
    for sql in (
        "ST_X(POINT(1,2))=1 AND ST_Y(POINT(1,2))=2",
        "ST_Distance(POINT(0,0),POINT(3,4))=5",
        "ABS(ST_Y(ST_Transform(ST_GeomFromText('POINT(2 49)',4326),3857))-6274861.394006576)<0.000001",
        "ABS(ST_Y(ST_Transform(ST_GeomFromText('POINT(222638.98158654713 6274861.394006576)',3857),4326))-49)<0.000000001",
        "ST_Area(ST_GeomFromText('POLYGON((0 0,4 0,4 3,0 3,0 0))'))=12",
        "ST_X(ST_GeomFromWKB(ST_AsWKB(POINT(3,4))))=3",
        "ST_Y(ST_MakePoint(3,4))=4 AND ST_Y(ST_MakePoint(3,4,5))=4",
        "ST_Length(LINESTRING(POINT(0,0),POINT(3,4)))=5",
        "ST_AsText(GEOMETRYCOLLECTION(POINT(1,2),POINT(3,4)))="
        "'GEOMETRYCOLLECTION (POINT(1 2), POINT(3 4))'",
        "AREA(ST_GeomFromText('POLYGON((0 0,4 0,4 3,0 3,0 0))'))=12",
        "ST_Distance(NULL,POINT(0,0)) IS NULL",
        "ST_GeomFromText('POINT(3 4)',NULL) IS NULL",
        "LENGTH(ST_AsText(ST_GeomFromText(CONCAT('LINESTRING(',REPEAT('0 0,',20000),'1 1)'))))>65535",
    ):
        require(scalar(connection, "SELECT " + sql) == 1, "wrong GIS result: " + sql)


def check_installation(connection, database_id, expected):
    installed = installation(connection, database_id)
    require(len(installed) == 1 and installed[0][1] == "1.0", "wrong GIS installation identity/version")
    require(type(installed[0][0]) is int and 0 < installed[0][0] < (1 << 63), "invalid GIS extension identity")
    records = routines(connection, database_id)
    check_bindings(records, expected)
    check_members(connection, database_id, installed[0][0], records)
    check_edges(connection, records, True)
    return installed[0][0], records


def check_removed(connection, database_id, extension_id, records):
    require(not installation(connection, database_id) and not routines(connection, database_id),
            "GIS removal retained catalog objects")
    check_members(connection, database_id, extension_id, ())
    check_edges(connection, records, False)
    if records:
        identities = tuple(r[1] for r in records)
        # ObObjectType::FUNCTION is 9. History is intentionally not inspected:
        # removing a live ACL must not require erasing its schema history.
        count = scalar(connection, "SELECT COUNT(*) FROM oceanbase.__all_objauth WHERE objtype=9 "
                       "AND obj_id IN (" + ",".join(["%s"] * len(identities)) + ")", identities)
        require(count == 0, "GIS removal retained live object ACL entries")


def run(options, driver):
    require(options.confirm_disposable_server, "explicit disposable-server confirmation required")
    expected = expected_bindings()
    suffix = secrets.token_hex(10)
    databases = ["gis_ext_a_" + suffix, "gis_ext_b_" + suffix]
    user, password = "ge_read_" + suffix, secrets.token_urlsafe(24)
    config = dict(user=options.user, password=os.environ.get("SEEKDB_TEST_PASSWORD", ""),
                  autocommit=True, charset="utf8mb4", connect_timeout=5, read_timeout=120, write_timeout=60)
    if options.unix_socket:
        config["unix_socket"] = options.unix_socket
    else:
        config.update(host="127.0.0.1", port=options.port)
    created, accounts, attempted = [], [], []
    try:
        with driver.connect(**config) as admin:
            # Read-only preflight: no stale global-name module or old schema.
            query(admin, "SELECT native_module_id,native_implementation_id,native_abi_version,overload "
                         "FROM oceanbase.__all_routine WHERE 1=0")
            for implementation in sorted({implementation for _, implementation in expected}):
                flags = scalar(admin, "SELECT flags FROM oceanbase.__all_sql_extension_function "
                               "WHERE function_id=%s", (implementation,))
                require(type(flags) is int and flags & 64, "implementation-only GIS module required")
            ids = []
            for database in databases:
                attempted.append(database)
                query(admin, f"CREATE DATABASE `{database}`")
                created.append(database)
                print("Created fixture database:", database, flush=True)
                identity = scalar(admin, "SELECT database_id FROM oceanbase.__all_database WHERE database_name=%s",
                                  (database,))
                require(type(identity) is int and identity > 0, "missing database identity")
                ids.append(identity)
            attempted.append(user + "@%")
            query(admin, "CREATE USER %s@%s IDENTIFIED BY %s", (user, "%", password))
            accounts.append(user)
            print("Created fixture user:", user + "@%", flush=True)
            account = f"'{user}'@'%'"
            query(admin, f"GRANT CREATE ROUTINE ON `{databases[0]}`.* TO {account}")
            with driver.connect(**dict(config, user=user, password=password, database=databases[0])) as reader, \
                    driver.connect(**config) as observer:
                admin.select_db(databases[0]); observer.select_db(databases[0])
                for sql in ("SELECT POINT(1,2)", "SELECT ST_Area(NULL)", "SELECT AREA(NULL)"):
                    reject(observer, sql, driver, MISSING)
                reject(reader, "CREATE EXTENSION gis", driver, DENIED)
                require(not installation(admin, ids[0]) and not routines(admin, ids[0]),
                        "rejected GIS installation left objects")
                query(admin, "CREATE EXTENSION gis")
                first, before = check_installation(admin, ids[0], expected)
                check_values(observer)
                observer.select_db(databases[1])
                reject(observer, "SELECT POINT(1,2)", driver, MISSING)
                reject(observer, "SELECT ST_Area(NULL)", driver, MISSING)
                require(scalar(observer, f"SELECT `{databases[0]}`.ST_X(`{databases[0]}`.POINT(3,4))") == 3,
                        "qualified GIS call failed")
                observer.select_db(databases[0])
                # EXECUTE belongs to each overload ID; a grant on 2D must not
                # authorize 3D, nor a NULL fast path, nor a cached prepared call.
                reject(reader, "SELECT ST_MakePoint(NULL,NULL)", driver, DENIED)
                target = f"`{databases[0]}`.ST_MakePoint(DOUBLE,DOUBLE)"
                query(admin, f"GRANT EXECUTE ON FUNCTION {target} TO {account}")
                require(scalar(reader, "SELECT ST_MakePoint(3,4)") is not None, "2D EXECUTE grant failed")
                reject(reader, "SELECT ST_MakePoint(3,4,5)", driver, DENIED)
                query(reader, "PREPARE gis_acl_plan FROM 'SELECT ST_MakePoint(3,4)'")
                require(scalar(reader, "EXECUTE gis_acl_plan") is not None, "prepared 2D call failed")
                query(admin, f"REVOKE EXECUTE ON FUNCTION {target} FROM {account}")
                reject(reader, "EXECUTE gis_acl_plan", driver, DENIED)
                query(reader, "DEALLOCATE PREPARE gis_acl_plan")
                target_3d = f"`{databases[0]}`.ST_MakePoint(DOUBLE,DOUBLE,DOUBLE)"
                query(admin, f"GRANT EXECUTE ON FUNCTION {target_3d} TO {account}")
                require(scalar(reader, "SELECT ST_MakePoint(3,4,5)") is not None, "3D EXECUTE grant failed")
                query(observer, "PREPARE gis_plan FROM 'SELECT ST_Distance(POINT(0,0),POINT(3,4))'")
                require(scalar(observer, "EXECUTE gis_plan") == 5, "prepared distance failed")
                reject(admin, "DROP FUNCTION ST_MakePoint(DOUBLE,DOUBLE)", driver, {4179})
                admin.select_db(databases[1])
                query(admin, "CREATE EXTENSION gis")
                second, other = check_installation(admin, ids[1], expected)
                require(not ({r[1] for r in before} & {r[1] for r in other}), "GIS databases share routine identities")
                require(first != second, "GIS databases share extension identity")
                admin.select_db(databases[0])
                query(admin, "DROP EXTENSION gis")
                check_removed(admin, ids[0], first, before)
                reject(observer, "EXECUTE gis_plan", driver, MISSING | {1615})
                query(observer, "DEALLOCATE PREPARE gis_plan")
                reject(observer, "SELECT POINT(1,2)", driver, MISSING)
                # Reinstall cannot resurrect old object IDs or old ACL grants.
                query(admin, "CREATE EXTENSION gis")
                replacement, fresh = check_installation(admin, ids[0], expected)
                require(replacement != first and not ({r[1] for r in fresh} & {r[1] for r in before}),
                        "GIS reinstall reused removed identities")
                reject(reader, "SELECT ST_MakePoint(3,4)", driver, DENIED)
                reject(reader, "SELECT ST_MakePoint(3,4,5)", driver, DENIED)
                check_values(observer)
                query(admin, "DROP EXTENSION gis")
                check_removed(admin, ids[0], replacement, fresh)
                admin.select_db(databases[1])
                check_values(admin); check_edges(admin, other, True)
                query(admin, f"DROP DATABASE `{databases[1]}`")
                created.remove(databases[1])
                check_removed(admin, ids[1], second, other)
            query(admin, f"DROP DATABASE `{databases[0]}`")
            created.remove(databases[0])
            query(admin, f"DROP USER {account}")
            accounts.clear()
        print("PASS: GIS SQL package install/drop/reinstall, two-database isolation, overload identities, "
              "object ACL, prepared calls, dependencies and database-drop cleanup")
        print("Removed only this run's fixture databases/user; not recoverable. "
              "Recovery, version updates, concurrency and injected commit failures are not covered.")
    except BaseException:
        print(f"Run incomplete; retained databases={created}, users={accounts}; "
              f"attempted creations (inspect unknown outcomes)={attempted}", flush=True)
        raise


def main():
    options = parse_options(description=__doc__)
    import pymysql
    run(options, pymysql)


if __name__ == "__main__":
    main()
