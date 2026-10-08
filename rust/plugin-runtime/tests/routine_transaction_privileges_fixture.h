// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Real paired views, schema guards/ACL checks and Rust journal rollback. Schema
// snapshots, users, grants and host authorization are controlled, not live SQL.
#ifndef SEEKDB_TEST_ROUTINE_TRANSACTION_PRIVILEGES_FIXTURE_H_
#define SEEKDB_TEST_ROUTINE_TRANSACTION_PRIVILEGES_FIXTURE_H_
#include "share/schema/routine_catalog_transaction.h"
#include "share/schema/routine_catalog_savepoint.h"
#include "data_plane/transaction/ob_tx_seq.h"

namespace routine_transaction_privileges_test {
using namespace oceanbase::common;
using namespace oceanbase::share::schema;
using oceanbase::transaction::ObTxSEQ;

inline void run()
{
  auto service = std::make_unique<MockSchemaService>();
  auto manager = std::make_unique<ObSchemaMgr>();
  CHECK(manager->init() == OB_SUCCESS);
  CHECK(MockSchemaService::set_name_case_mode(*manager, OB_ORIGIN_AND_INSENSITIVE) == OB_SUCCESS);
  ObSimpleServerRuntimeSchema runtime;
  runtime.set_schema_version(42); runtime.set_name_case_mode(OB_ORIGIN_AND_INSENSITIVE);
  runtime.set_status(SERVER_RUNTIME_STATUS_NORMAL);
  CHECK(runtime.set_runtime_name(ObString::make_string("transaction_privileges")) == OB_SUCCESS);
  CHECK(manager->add_runtime_schema(runtime) == OB_SUCCESS);
  const char *databases[] = {"flex_a", "flex_b"};
  for (int i = 0; i < 2; ++i) {
    ObSimpleDatabaseSchema database;
    database.set_database_id(100 + i); database.set_schema_version(42);
    CHECK(database.set_database_name(databases[i]) == OB_SUCCESS);
    CHECK(manager->add_database(database) == OB_SUCCESS);
    CHECK(MockSchemaService::add(*manager, 100 + i, "value", 8001 + i,
                                ROUTINE_FUNCTION_TYPE, 42) == OB_SUCCESS);
    CHECK(MockSchemaService::grant(*manager, 123, "value", OB_PRIV_EXECUTE, databases[i]) == OB_SUCCESS);
    CHECK(MockSchemaService::grant(*manager, 124, "value", OB_PRIV_ALTER_ROUTINE, databases[i]) == OB_SUCCESS);
  }
  ObUserInfo users[3];
  for (int i = 0; i < 3; ++i) {
    users[i].set_user_id(123 + i); users[i].set_schema_version(42);
    CHECK(users[i].set_user_name("fixture") == OB_SUCCESS && users[i].set_host("localhost") == OB_SUCCESS);
  }
  CHECK(users[0].add_role_id(124) == OB_SUCCESS);
  CHECK(users[2].add_role_id(124) == OB_SUCCESS);
  ObSchemaGetterGuard first, next, independent;
  for (auto *guard : {&first, &next, &independent}) {
    CHECK(MockSchemaService::bind(*guard, *service, *manager) == OB_SUCCESS);
    for (const auto &user : users) CHECK(MockSchemaService::cache_user(*guard, user) == OB_SUCCESS);
  }
  auto privileges = std::make_shared<RoutinePrivilegeOverlay>();
  auto schema = std::make_shared<RoutineSchemaOverlay>(privileges);
  CHECK(first.attach_routine_overlay(schema) == OB_SUCCESS);
  CHECK(next.inherit_routine_overlay(first) == OB_SUCCESS);
  const ObPrivSet rights = OB_PRIV_EXECUTE | OB_PRIV_ALTER_ROUTINE;
  const auto acl = [&](ObSchemaGetterGuard &guard, int db, uint64_t user, ObPrivSet expected,
                       ObRoutineType type = ROUTINE_FUNCTION_TYPE) {
    ObPrivSet bits = ~ObPrivSet{0};
    ObRoutinePrivSortKey key(user, ObString::make_string(databases[db]), ObString::make_string("VALUE"), type);
    CHECK(guard.get_routine_priv_set(key, bits) == OB_SUCCESS && bits == expected);
  };
  const auto check = [&](int db, uint64_t user, int expected) {
    ObSessionPrivInfo identity;
    identity.user_id_ = user; identity.user_name_ = ObString::make_string("fixture");
    identity.host_name_ = ObString::make_string("localhost");
    identity.db_ = ObString::make_string(databases[db]);
    identity.user_priv_set_ = 0; identity.db_priv_set_ = 0;
    ObSEArray<uint64_t, 1> roles; CHECK(roles.push_back(124) == OB_SUCCESS);
    ObNeedPriv need;
    need.db_ = identity.db_; need.table_ = ObString::make_string("value");
    need.obj_type_ = ObObjectType::FUNCTION; need.priv_level_ = OB_PRIV_ROUTINE_LEVEL;
    need.priv_set_ = rights;
    const int actual = next.check_routine_priv(identity, roles, need);
    if (actual != expected) std::cerr << "transaction privilege db=" << db << " user=" << user
                                    << " actual=" << actual << " expected=" << expected << std::endl;
    CHECK(actual == expected);
  };
  ObRoutineInfo routines[2], base_routines[2];
  for (int i = 0; i < 2; ++i) {
    auto &routine = routines[i];
    routine.set_database_id(100 + i); routine.set_routine_id(8001 + i); routine.set_owner_id(123);
    routine.set_schema_version(42); routine.set_package_id(OB_INVALID_ID); routine.set_overload(0);
    routine.set_routine_type(ROUTINE_FUNCTION_TYPE);
    CHECK(routine.set_routine_name("value") == OB_SUCCESS);
    CHECK(routine.set_routine_body("RETURN 1") == OB_SUCCESS);
    // Name-only privilege checks inspect the complete candidate family before
    // choosing native object ACL versus ordinary PL name grants. Keep the base
    // snapshot alive and immutable while the working routines are replaced.
    CHECK(base_routines[i].assign(routine) == OB_SUCCESS);
    for (auto *guard : {&first, &next, &independent}) {
      CHECK(MockSchemaService::cache_routine(*guard, base_routines[i]) == OB_SUCCESS);
    }
  }
  const auto drop = [&](const ObRoutineInfo &routine) {
    CHECK(privileges->record_drop(routine) == OB_SUCCESS);
    CHECK(schema->erase(routine.get_database_id(), routine.get_routine_name(),
                        routine.get_routine_type(), routine.get_routine_id()) == OB_SUCCESS);
  };
  const auto create = [&](ObRoutineInfo &routine, uint64_t id, uint64_t owner, bool automatic) {
    routine.set_routine_id(id); routine.set_owner_id(owner); routine.set_schema_version(43);
    CHECK(privileges->record_create(routine, automatic) == OB_SUCCESS);
    CHECK(schema->stage(routine) == OB_SUCCESS);
  };
  RoutineCatalogTransaction journal(12345);
  CHECK(journal.valid());
  CHECK(journal.record(12345, ObTxSEQ(10, 0), schema, privileges) == OB_SUCCESS);
  drop(routines[0]); create(routines[0], 9001, 123, true);
  CHECK(journal.record(12345, ObTxSEQ(20, 0), schema, privileges) == OB_SUCCESS);
  drop(routines[1]); create(routines[1], 9002, 125, true);
  // Both guards share the current transaction view; the independent guard sees
  // only controlled base grants. Database and creator transitions do not leak.
  for (auto *guard : {&first, &next}) {
    acl(*guard, 0, 123, rights); acl(*guard, 0, 125, 0);
    acl(*guard, 1, 125, rights); acl(*guard, 1, 123, 0);
    for (int i = 0; i < 2; ++i) {
      uint64_t id = OB_INVALID_ID;
      CHECK(guard->get_standalone_function_id(100 + i, ObString::make_string("VALUE"), id) == OB_SUCCESS);
      CHECK(id == static_cast<uint64_t>(9001 + i));
    }
  }
  acl(independent, 1, 123, OB_PRIV_EXECUTE);
  check(0, 123, OB_SUCCESS); check(1, 125, OB_SUCCESS);
  check(0, 125, OB_ERR_NO_ROUTINE_PRIVILEGE); check(1, 123, OB_ERR_NO_ROUTINE_PRIVILEGE);
  ObRoutineInfo procedure;
  CHECK(procedure.assign(routines[0]) == OB_SUCCESS);
  procedure.set_routine_type(ROUTINE_PROCEDURE_TYPE);
  CHECK(journal.record(12345, ObTxSEQ(20, 0), schema, privileges) == OB_SUCCESS);
  create(procedure, 9003, 125, true);
  acl(next, 0, 123, 0, ROUTINE_PROCEDURE_TYPE);
  acl(next, 0, 125, rights, ROUTINE_PROCEDURE_TYPE);
  acl(next, 1, 125, 0, ROUTINE_PROCEDURE_TYPE);
  {
    bool handled = true; ObPrivSet bits = rights;
    ObRoutineInfo foreign;
    CHECK(foreign.assign(routines[0]) == OB_SUCCESS);
    foreign.set_database_id(101); // Deliberately the SAME ID/name/owner/version.
    CHECK(privileges->lookup(100, foreign.get_routine_name(), ROUTINE_FUNCTION_TYPE, 123,
        true, &foreign, handled, bits) == OB_STATE_NOT_MATCH && !handled && bits == 0);
    CHECK(privileges->record_create(foreign, true) == OB_STATE_NOT_MATCH);
    CHECK(privileges->record_drop(foreign) == OB_STATE_NOT_MATCH);
    for (uint64_t bad : {uint64_t{0}, OB_INVALID_ID}) {
      foreign.set_database_id(bad);
      CHECK(privileges->record_create(foreign, true) == OB_INVALID_ARGUMENT);
      CHECK(privileges->lookup(bad, foreign.get_routine_name(), ROUTINE_FUNCTION_TYPE, 123,
          true, &foreign, handled, bits) == OB_INVALID_ARGUMENT && !handled && bits == 0);
    }
    RoutinePrivilegeOverlay scoped(100, 123), invalid_scope(0, 0);
    CHECK(scoped.record_create(routines[1], true) == OB_INVALID_ARGUMENT);
    CHECK(scoped.record_create(procedure, true) == OB_INVALID_ARGUMENT);
    CHECK(invalid_scope.record_create(routines[0], true) == OB_INVALID_ARGUMENT);
    CHECK(invalid_scope.lookup(100, routines[0].get_routine_name(), ROUTINE_FUNCTION_TYPE,
        123, true, &routines[0], handled, bits) == OB_INVALID_ARGUMENT && !handled && bits == 0);
  }
  CHECK(journal.record(12345, ObTxSEQ(30, 0), schema, privileges) == OB_SUCCESS);
  drop(routines[0]); create(routines[0], 9004, 125, false);
  CHECK(journal.record(12345, ObTxSEQ(30, 0), schema, privileges) == OB_SUCCESS);
  drop(routines[1]);
  const ObRoutineInfo *borrowed = nullptr;
  CHECK(next.get_routine_info(9004, borrowed) == OB_SUCCESS && borrowed);
  check(0, 123, OB_ERR_NO_ROUTINE_PRIVILEGE); check(0, 125, OB_ERR_NO_ROUTINE_PRIVILEGE);
  check(1, 125, OB_ERR_NO_ROUTINE_PRIVILEGE);
  CHECK(journal.rollback(12345, ObTxSEQ(30, 0)) == OB_SUCCESS);
  check(0, 123, OB_SUCCESS); check(1, 125, OB_SUCCESS); check(1, 123, OB_ERR_NO_ROUTINE_PRIVILEGE);
  CHECK(borrowed->get_routine_id() == 9004 && borrowed->get_owner_id() == 125);
  CHECK(journal.rollback(12345, ObTxSEQ(20, 0)) == OB_SUCCESS);
  acl(next, 0, 123, rights); acl(next, 1, 123, OB_PRIV_EXECUTE);
  check(1, 123, OB_SUCCESS); // Restores both original user and role grants.
  acl(next, 0, 125, 0, ROUTINE_PROCEDURE_TYPE);
  CHECK(journal.finish(12345, false) == OB_SUCCESS);
  acl(next, 0, 123, OB_PRIV_EXECUTE); check(0, 123, OB_SUCCESS);
  CHECK(borrowed->get_routine_id() == 9004); // Rollback keeps borrowed storage.
  // The budget is shared by all databases and creators, not reset on USE or a
  // nested identity change. The failing extra record publishes nothing.
  RoutinePrivilegeOverlay full;
  for (size_t i = 0; i < RoutinePrivilegeOverlay::MAX_RECORDS; ++i) {
    ObRoutineInfo item;
    CHECK(item.assign(routines[0]) == OB_SUCCESS);
    item.set_database_id(100 + i % 2); item.set_owner_id(123 + i % 3);
    item.set_routine_id(10000 + i);
    const std::string name = std::to_string(i);
    CHECK(item.set_routine_name(ObString(name.size(), name.data())) == OB_SUCCESS);
    CHECK(full.record_drop(item) == OB_SUCCESS);
  }
  CHECK(full.record_create(routines[0], true) == OB_SIZE_OVERFLOW);
  bool handled = true; ObPrivSet bits = rights;
  CHECK(full.lookup(100, routines[0].get_routine_name(), ROUTINE_FUNCTION_TYPE,
      125, true, &routines[0], handled, bits) == OB_SUCCESS && !handled && bits == 0);
}
}
#endif
