// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#ifndef SEEKDB_TEST_ROUTINE_CATALOG_SAVEPOINT_FIXTURE_H_
#define SEEKDB_TEST_ROUTINE_CATALOG_SAVEPOINT_FIXTURE_H_
#include "share/schema/routine_catalog_savepoint.h"

namespace routine_catalog_savepoint_test {
using namespace oceanbase::common;
using namespace oceanbase::share::schema;
inline void run()
{
  auto privileges = std::make_shared<RoutinePrivilegeOverlay>(100, 123);
  auto schema = std::make_shared<RoutineSchemaOverlay>(privileges);
  ObRoutineInfo routine;
  routine.set_database_id(100); routine.set_routine_id(1001); routine.set_owner_id(123);
  routine.set_schema_version(42); routine.set_package_id(OB_INVALID_ID); routine.set_overload(0);
  routine.set_routine_type(ROUTINE_FUNCTION_TYPE);
  CHECK(routine.set_routine_name(ObString::make_string("value")) == OB_SUCCESS);
  CHECK(routine.set_routine_body(ObString::make_string("RETURN 1")) == OB_SUCCESS);
  const auto current = [&](bool expected_handled, uint64_t id, ObPrivSet rights) {
    const ObRoutineInfo *found = nullptr; bool handled = false;
    CHECK(schema->lookup(100, OB_INVALID_ID, ObString::make_string("VALUE"), 0, ROUTINE_FUNCTION_TYPE, handled, found) == OB_SUCCESS);
    CHECK(handled == expected_handled && (found ? found->get_routine_id() : 0) == id);
    bool granted = false; ObPrivSet bits = 0;
    CHECK(privileges->lookup(100, ObString::make_string("value"), ROUTINE_FUNCTION_TYPE, 123, handled, found, granted, bits) == OB_SUCCESS);
    CHECK(granted == expected_handled && bits == rights);
    return found;
  };
  const auto create = [&](uint64_t id, bool automatic) {
    routine.set_routine_id(id);
    CHECK(schema->stage(routine) == OB_SUCCESS);
    CHECK(privileges->record_create(routine, automatic) == OB_SUCCESS);
  };
  const auto drop = [&]() {
    CHECK(schema->erase(100, routine.get_routine_name(), ROUTINE_FUNCTION_TYPE, routine.get_routine_id()) == OB_SUCCESS);
    CHECK(privileges->record_drop(routine) == OB_SUCCESS);
  };
  const auto rights = OB_PRIV_EXECUTE | OB_PRIV_ALTER_ROUTINE;
  {
    // Rolling back a base-only tombstone must resume fallback to the base
    // guard, not leave a handled=true NULL or an empty-grant shadow behind.
    RoutineCatalogSavepoint base_drop(schema, privileges);
    drop(); current(true, 0, 0);
  }
  current(false, 0, 0);
  RoutineCatalogSavepoint empty(schema, privileges);
  CHECK(empty.valid()); create(1001, true);
  const auto *original = current(true, 1001, rights);
  CHECK(schema->schema_bytes() == routine.get_convert_size() && schema->schema_bytes() > 0);
  RoutineCatalogSavepoint first(schema, privileges);
  drop(); current(true, 0, 0);
  RoutineCatalogSavepoint deleted(schema, privileges);
  create(1002, false); const auto *replacement = current(true, 1002, 0);
  RoutineCatalogSavepoint stale(schema, privileges);
  CHECK(deleted.rollback() == OB_SUCCESS); current(true, 0, 0);
  CHECK(stale.rollback() == OB_STATE_NOT_MATCH); current(true, 0, 0);
  CHECK(first.rollback() == OB_SUCCESS); current(true, 1001, rights);
  CHECK(first.rollback() == OB_STATE_NOT_MATCH);
  CHECK(original->get_routine_id() == 1001 && replacement->get_routine_id() == 1002);
  CHECK(original->get_routine_body() == ObString::make_string("RETURN 1"));
  const auto retained = schema->record_count(); const auto retained_bytes = schema->schema_bytes();
  CHECK(empty.rollback() == OB_SUCCESS); current(false, 0, 0);
  CHECK(schema->record_count() == retained && schema->schema_bytes() == retained_bytes);
  bool handled = true; const ObRoutineInfo *found = original;
  CHECK(schema->lookup(1001, handled, found) == OB_SUCCESS && !handled && !found);
  CHECK(schema->lookup(1002, handled, found) == OB_SUCCESS && !handled && !found);
  // Unwinding/early return restores both views even if only one changed.
  {
    RoutineCatalogSavepoint failed(schema, privileges);
    routine.set_routine_id(1003); CHECK(schema->stage(routine) == OB_SUCCESS);
    routine.set_owner_id(999); CHECK(privileges->record_create(routine, true) == OB_INVALID_ARGUMENT);
    routine.set_owner_id(123);
  }
  current(false, 0, 0);
  // A released inner mark remains undoable by its outer owner.
  {
    RoutineCatalogSavepoint outer(schema, privileges);
    create(1004, true);
    { RoutineCatalogSavepoint inner(schema, privileges); drop(); create(1005, false); inner.release(); }
    current(true, 1005, 0);
  }
  current(false, 0, 0);
  // Privilege-only branches have their own ancestry, not just schema depth.
  routine.set_routine_id(1006); CHECK(schema->stage(routine) == OB_SUCCESS);
  RoutineCatalogSavepoint without_grant(schema, privileges);
  CHECK(privileges->record_create(routine, true) == OB_SUCCESS);
  RoutineCatalogSavepoint abandoned_grant(schema, privileges);
  CHECK(without_grant.rollback() == OB_SUCCESS);
  CHECK(abandoned_grant.rollback() == OB_STATE_NOT_MATCH);
  bool granted = true; ObPrivSet bits = rights;
  CHECK(privileges->lookup(100, routine.get_routine_name(), ROUTINE_FUNCTION_TYPE, 123, true, &routine, granted, bits) == OB_SUCCESS);
  CHECK(!granted && bits == 0);
  auto unrelated = std::make_shared<RoutinePrivilegeOverlay>(100, 123);
  RoutineCatalogSavepoint mismatched(schema, unrelated);
  CHECK(!mismatched.valid() && mismatched.rollback() == OB_STATE_NOT_MATCH);
  // Same object may be altered repeatedly; rollback restores the borrowed old
  // version without freeing the post-savepoint version's backing strings.
  const auto *before = static_cast<const ObRoutineInfo *>(nullptr);
  CHECK(schema->lookup(1006, handled, before) == OB_SUCCESS && handled && before);
  {
    RoutineCatalogSavepoint alteration(schema, privileges);
    routine.set_schema_version(43); CHECK(routine.set_routine_body(ObString::make_string("RETURN 2")) == OB_SUCCESS);
    CHECK(schema->stage(routine) == OB_SUCCESS);
    CHECK(schema->lookup(1006, handled, found) == OB_SUCCESS && found != before);
  }
  const ObRoutineInfo *restored = nullptr;
  CHECK(schema->lookup(1006, handled, restored) == OB_SUCCESS && restored == before);
  CHECK(found->get_routine_body() == ObString::make_string("RETURN 2"));

  // Aggregate byte quota, including abandoned branches, cannot be bypassed by
  // repeatedly creating and rolling back modest (individually legal) schemas.
  auto quota_privileges = std::make_shared<RoutinePrivilegeOverlay>(100, 123);
  auto quota = std::make_shared<RoutineSchemaOverlay>(quota_privileges);
  const std::string body(1024 * 1024, 'x');
  CHECK(routine.set_routine_body(ObString(body.size(), body.data())) == OB_SUCCESS);
  const auto one = routine.get_convert_size();
  size_t successes = 0;
  for (size_t i = 0; i < 100; ++i) {
    RoutineCatalogSavepoint mark(quota, quota_privileges);
    routine.set_routine_id(2000 + i);
    const int code = quota->stage(routine);
    if (code == OB_SIZE_OVERFLOW) break;
    CHECK(code == OB_SUCCESS); ++successes;
    CHECK(quota_privileges->record_create(routine, true) == OB_SUCCESS);
  }
  CHECK(successes > 1 && successes < 100);
  CHECK(quota->schema_bytes() == successes * one && quota->schema_bytes() + one > RoutineSchemaOverlay::MAX_SCHEMA_BYTES);
  CHECK(quota->lookup(2000, handled, found) == OB_SUCCESS && !handled && !found);
  auto churn_privileges = std::make_shared<RoutinePrivilegeOverlay>(100, 123);
  auto churn = std::make_shared<RoutineSchemaOverlay>(churn_privileges);
  for (size_t i = 0; i < RoutinePrivilegeOverlay::MAX_RECORDS; ++i) {
    RoutineCatalogSavepoint mark(churn, churn_privileges);
    routine.set_routine_id(9000 + i);
    CHECK(churn_privileges->record_create(routine, true) == OB_SUCCESS);
  }
  routine.set_routine_id(999999);
  CHECK(churn_privileges->record_create(routine, true) == OB_SIZE_OVERFLOW);
  bool granted_after_churn = true; ObPrivSet churn_rights = rights;
  CHECK(churn_privileges->lookup(100, routine.get_routine_name(), ROUTINE_FUNCTION_TYPE, 123, false, nullptr,
      granted_after_churn, churn_rights) == OB_SUCCESS && !granted_after_churn && churn_rights == 0);
}
}
#endif
