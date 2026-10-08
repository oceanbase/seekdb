// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Real guard privilege checks and single-statement SQL resolution. Users and
// Root-authorized CREATE/DROP records are controlled fixtures, not live auth/DDL.
#ifndef SEEKDB_TEST_ROUTINE_PRIVILEGE_FIXTURE_H_
#define SEEKDB_TEST_ROUTINE_PRIVILEGE_FIXTURE_H_
#include "share/schema/routine_catalog_savepoint.h"

namespace routine_privilege_test {
using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::share::schema;
using namespace oceanbase::share::plugin;

inline void run(const char *root, const ObResolverParams &services, const ObSqlCtx &outer,
                const ObRoutineInfo &original)
{
  ObUserInfo user, role, other;
  user.set_user_id(123); role.set_user_id(124); other.set_user_id(125);
  CHECK(user.set_user_name("fixture") == OB_SUCCESS && user.set_host("localhost") == OB_SUCCESS);
  CHECK(user.add_role_id(124) == OB_SUCCESS);
  auto service = std::make_unique<MockSchemaService>();
  auto manager = std::make_unique<ObSchemaMgr>();
  CHECK(manager->init() == OB_SUCCESS);
  CHECK(MockSchemaService::set_name_case_mode(*manager, OB_ORIGIN_AND_INSENSITIVE) == OB_SUCCESS);
  ObSchemaGetterGuard guard;
  CHECK(MockSchemaService::bind(guard, *service, *manager) == OB_SUCCESS);
  CHECK(MockSchemaService::cache_user(guard, user) == OB_SUCCESS);
  CHECK(MockSchemaService::cache_user(guard, role) == OB_SUCCESS);
  CHECK(MockSchemaService::cache_user(guard, other) == OB_SUCCESS);
  auto privileges = std::make_shared<RoutinePrivilegeOverlay>(original.get_database_id(), 123);
  auto overlay = std::make_shared<RoutineSchemaOverlay>(privileges);
  CHECK(overlay->stage(original) == OB_SUCCESS);
  CHECK(guard.attach_routine_overlay(overlay) == OB_SUCCESS);
  auto &session = *outer.session_info_;
  const auto saved_user = session.get_user_priv_set(), saved_db = session.get_db_priv_set();
  struct Restore {
    ObSQLSessionInfo &session_; ObPrivSet user_, db_;
    ~Restore() { session_.set_user_priv_set(user_); session_.set_db_priv_set(db_); }
  } restore{session, saved_user, saved_db};
  session.set_user_priv_set(OB_PRIV_CREATE_ROUTINE);
  session.set_db_priv_set(0);
  ObSessionPrivInfo identity;
  CHECK(session.get_session_priv_info(identity) == OB_SUCCESS);
  ObSEArray<uint64_t, 1> roles;
  CHECK(roles.push_back(124) == OB_SUCCESS);
  ObNeedPriv need;
  need.db_ = ObString::make_string(OB_SYS_DATABASE_NAME);
  need.table_ = original.get_routine_name();
  need.obj_type_ = ObObjectType::FUNCTION;
  need.priv_level_ = OB_PRIV_ROUTINE_LEVEL;
  need.priv_set_ = OB_PRIV_ALTER_ROUTINE;
  const auto check = [&](ObPrivSet rights, int expected) {
    need.priv_set_ = rights;
    CHECK(guard.check_routine_priv(identity, roles, need) == expected);
  };
  // Owner + schema stage alone is NOT permission, even with CREATE privilege.
  check(OB_PRIV_ALTER_ROUTINE, OB_ERR_NO_ROUTINE_PRIVILEGE);
  ExtensionUpdatePlan plan;
  std::string error;
  CHECK(plan.load(root, 1, original.get_database_id(), "sequence_success", {91, 123, "1", ""},
                  "2", session.get_sql_mode(), error) == OB_SUCCESS);
  ObSqlCtx context;
  context.session_info_ = &session;
  context.schema_guard_ = &guard;
  context.disable_privilege_check_ = PRIV_CHECK_FLAG_NORMAL;
  context.stmt_type_ = stmt::T_CREATE_EXTENSION;
  const auto resolve_alter = [&](int expected) {
    ExtensionRoutineScriptResolver resolver(plan, services, context);
    const ExtensionRoutineUpdateOperation *op = nullptr;
    const int status = resolver.resolve(0, guard, op, error);
    if (status != expected) std::cerr << "routine privilege resolver: " << status << " expected " << expected << std::endl;
    CHECK(status == expected);
    CHECK((op != nullptr) == (expected == OB_SUCCESS));
  };
  resolve_alter(OB_ERR_NO_ROUTINE_PRIVILEGE);
  CHECK(privileges->record_create(original, true) == OB_SUCCESS);
  {
    bool handled = true;
    ObPrivSet bits = ~ObPrivSet{0};
    CHECK(privileges->lookup(original.get_database_id() + 1, original.get_routine_name(),
        original.get_routine_type(), 123, true, &original, handled, bits) == OB_SUCCESS && !handled && bits == 0);
    CHECK(privileges->lookup(original.get_database_id(), original.get_routine_name(),
        ROUTINE_PROCEDURE_TYPE, 123, true, &original, handled, bits) == OB_SUCCESS && !handled && bits == 0);
    CHECK(privileges->lookup(original.get_database_id(), ObString::make_string("other_name"),
        original.get_routine_type(), 123, true, &original, handled, bits) == OB_SUCCESS && !handled && bits == 0);
    CHECK(privileges->lookup(original.get_database_id(), original.get_routine_name(),
        original.get_routine_type(), 123, false, &original, handled, bits) == OB_STATE_NOT_MATCH && !handled && bits == 0);
    ObRoutineInfo invalid;
    CHECK(invalid.assign(original) == OB_SUCCESS);
    invalid.set_routine_id(original.get_routine_id() + 1);
    CHECK(privileges->record_drop(invalid) == OB_STATE_NOT_MATCH);
    invalid.set_owner_id(125);
    CHECK(privileges->record_create(invalid, true) == OB_INVALID_ARGUMENT);
    invalid.set_owner_id(123);
    invalid.set_schema_version(0);
    CHECK(privileges->record_create(invalid, true) == OB_INVALID_ARGUMENT);
    invalid.set_schema_version(43);
    invalid.set_package_id(100);
    CHECK(privileges->record_create(invalid, true) == OB_INVALID_ARGUMENT);
  }
  check(OB_PRIV_ALTER_ROUTINE | OB_PRIV_EXECUTE, OB_SUCCESS);
  {
    // The actual guard must restore both schema identity and grants. A new
    // same-name object without automatic grants cannot inherit the old grant.
    const ObRoutineInfo *borrowed = nullptr;
    CHECK(guard.get_routine_info(original.get_routine_id(), borrowed) == OB_SUCCESS && borrowed);
    RoutineCatalogSavepoint savepoint(overlay, privileges);
    CHECK(overlay->erase(original.get_database_id(), original.get_routine_name(), original.get_routine_type(), original.get_routine_id()) == OB_SUCCESS);
    CHECK(privileges->record_drop(original) == OB_SUCCESS);
    ObRoutineInfo replacement;
    CHECK(replacement.assign(original) == OB_SUCCESS);
    replacement.set_routine_id(original.get_routine_id() + 999);
    CHECK(overlay->stage(replacement) == OB_SUCCESS);
    CHECK(privileges->record_create(replacement, false) == OB_SUCCESS);
    check(OB_PRIV_ALTER_ROUTINE | OB_PRIV_EXECUTE, OB_ERR_NO_ROUTINE_PRIVILEGE);
    CHECK(savepoint.rollback() == OB_SUCCESS);
    check(OB_PRIV_ALTER_ROUTINE | OB_PRIV_EXECUTE, OB_SUCCESS);
    const ObRoutineInfo *restored = nullptr;
    CHECK(guard.get_routine_info(original.get_routine_id(), restored) == OB_SUCCESS && restored == borrowed);
  }
  check(OB_PRIV_DROP, OB_ERR_NO_ROUTINE_PRIVILEGE);
  resolve_alter(OB_SUCCESS);
  ObRoutinePrivSortKey key(123, need.db_, ObString::make_string("EXT_VALUE"), ROUTINE_FUNCTION_TYPE);
  ObPrivSet rights = 0;
  CHECK(guard.get_routine_priv_set(key, rights) == OB_SUCCESS && rights == (OB_PRIV_ALTER_ROUTINE | OB_PRIV_EXECUTE));
  identity.user_id_ = 125;
  check(OB_PRIV_EXECUTE, OB_ERR_NO_ROUTINE_PRIVILEGE);
  identity.user_id_ = 126; // grants cannot replace the normal user-existence check
  check(OB_PRIV_EXECUTE, OB_USER_NOT_EXIST);
  identity.user_id_ = 123;
  // Old user/role grants must not survive a DROP + same-name new object ID.
  CHECK(MockSchemaService::grant(*manager, 123, "ext_value", OB_PRIV_ALTER_ROUTINE) == OB_SUCCESS);
  CHECK(MockSchemaService::grant(*manager, 124, "ext_value", OB_PRIV_EXECUTE) == OB_SUCCESS);
  CHECK(overlay->erase(original.get_database_id(), original.get_routine_name(), original.get_routine_type(),
                       original.get_routine_id()) == OB_SUCCESS);
  CHECK(privileges->record_drop(original) == OB_SUCCESS);
  check(OB_PRIV_ALTER_ROUTINE, OB_ERR_NO_ROUTINE_PRIVILEGE);
  CHECK(privileges->record_create(original, true) == OB_STATE_NOT_MATCH);
  CHECK(privileges->record_drop(original) == OB_STATE_NOT_MATCH);
  ObRoutineInfo replacement;
  CHECK(replacement.assign(original) == OB_SUCCESS);
  replacement.set_routine_id(original.get_routine_id() + 1);
  replacement.set_schema_version(original.get_schema_version() + 1);
  CHECK(overlay->stage(replacement) == OB_SUCCESS);
  // No matching authorization record yet: fail closed instead of using old ACL.
  CHECK(guard.get_routine_priv_set(key, rights) == OB_STATE_NOT_MATCH && rights == 0);
  CHECK(privileges->record_create(replacement, false) == OB_SUCCESS);
  check(OB_PRIV_ALTER_ROUTINE, OB_ERR_NO_ROUTINE_PRIVILEGE);
  check(OB_PRIV_EXECUTE, OB_ERR_NO_ROUTINE_PRIVILEGE);
  resolve_alter(OB_ERR_NO_ROUTINE_PRIVILEGE);
  CHECK(guard.get_routine_priv_set(key, rights) == OB_SUCCESS && rights == 0);
  identity.db_priv_set_ = OB_PRIV_ALTER_ROUTINE;
  check(OB_PRIV_ALTER_ROUTINE, OB_SUCCESS); // database rights are not object ACLs
  identity.db_priv_set_ = 0;
  identity.user_priv_set_ |= OB_PRIV_ALTER_ROUTINE;
  check(OB_PRIV_ALTER_ROUTINE, OB_SUCCESS);
  identity.user_priv_set_ = OB_PRIV_CREATE_ROUTINE;
  need.table_ = ObString::make_string("untouched");
  CHECK(MockSchemaService::grant(*manager, 123, "untouched", OB_PRIV_ALTER_ROUTINE) == OB_SUCCESS);
  CHECK(MockSchemaService::grant(*manager, 124, "untouched", OB_PRIV_EXECUTE) == OB_SUCCESS);
  check(OB_PRIV_ALTER_ROUTINE | OB_PRIV_EXECUTE, OB_SUCCESS);
  need.table_ = original.get_routine_name();
  // ALTER can advance a granted identity, but cannot create a grant or change its owner.
  CHECK(overlay->erase(replacement.get_database_id(), replacement.get_routine_name(), replacement.get_routine_type(),
                       replacement.get_routine_id()) == OB_SUCCESS);
  CHECK(privileges->record_drop(replacement) == OB_SUCCESS);
  replacement.set_routine_id(original.get_routine_id() + 2);
  CHECK(overlay->stage(replacement) == OB_SUCCESS);
  CHECK(privileges->record_create(replacement, true) == OB_SUCCESS);
  replacement.set_schema_version(replacement.get_schema_version() + 1);
  CHECK(overlay->stage(replacement) == OB_SUCCESS);
  check(OB_PRIV_EXECUTE, OB_SUCCESS);
  replacement.set_owner_id(125);
  CHECK(overlay->stage(replacement) == OB_SUCCESS);
  CHECK(guard.get_routine_priv_set(key, rights) == OB_STATE_NOT_MATCH && rights == 0);
  replacement.set_owner_id(123);
  CHECK(overlay->stage(replacement) == OB_SUCCESS);
  std::weak_ptr<const RoutinePrivilegeOverlay> lifetime = privileges;
  ObSchemaGetterGuard inherited;
  CHECK(MockSchemaService::bind(inherited, *service, *manager) == OB_SUCCESS);
  CHECK(inherited.inherit_routine_overlay(guard) == OB_SUCCESS);
  guard.reset(); overlay.reset(); privileges.reset();
  CHECK(!lifetime.expired());
  CHECK(inherited.get_routine_priv_set(key, rights) == OB_SUCCESS && rights == (OB_PRIV_ALTER_ROUTINE | OB_PRIV_EXECUTE));
  inherited.reset();
  CHECK(lifetime.expired());
  // Capacity and used-ID failures cannot publish a partial name or ACL.
  RoutinePrivilegeOverlay full(original.get_database_id(), 123);
  replacement.set_owner_id(123);
  for (size_t i = 0; i < RoutinePrivilegeOverlay::MAX_IDENTITIES; ++i) {
    const auto name = std::to_string(i);
    replacement.set_routine_id(i + 1);
    CHECK(replacement.set_routine_name(ObString(name.size(), name.data())) == OB_SUCCESS);
    CHECK(full.record_drop(replacement) == OB_SUCCESS);
  }
  replacement.set_routine_id(99999);
  CHECK(replacement.set_routine_name("overflow") == OB_SUCCESS);
  CHECK(full.record_create(replacement, true) == OB_SIZE_OVERFLOW);
  bool handled = true;
  CHECK(full.lookup(original.get_database_id(), replacement.get_routine_name(), ROUTINE_FUNCTION_TYPE,
      123, true, &replacement, handled, rights) == OB_SUCCESS && !handled && rights == 0);
}
}
#endif
