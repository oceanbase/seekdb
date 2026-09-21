// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Controlled in-memory schema manager binding for guard dispatch tests only.
// Does not initialize the real SQL schema service, acquire a production guard,
// emulate transactions, or establish any authorization.
#ifndef SEEKDB_TEST_ROUTINE_OVERLAY_GUARD_FIXTURE_H_
#define SEEKDB_TEST_ROUTINE_OVERLAY_GUARD_FIXTURE_H_
#include "share/schema/ob_schema_getter_guard.h"
#include "share/schema/ob_multi_version_schema_service.h"
#include "share/schema/ob_schema_mgr.h"
#include "share/schema/routine_schema_overlay.h"

namespace oceanbase { namespace share { namespace schema {

// These two production classes already grant this test-fixture friendship.
class MockSchemaService : public ObMultiVersionSchemaService
{
public:
  MockSchemaService() = default;
  ~MockSchemaService() override = default;
  // Caller owns the full user longer than the guard. This fixture supplies a
  // schema object, not authenticated identity or a production cache entry.
  static int cache_user(ObSchemaGetterGuard &guard, const ObUserInfo &user)
  {
    const ObSchema *schema = &user;
    common::ObKVCacheHandle handle;
    return guard.put_to_local_cache(USER_SCHEMA, user.get_user_id(), schema, handle);
  }
  static int cache_database(ObSchemaGetterGuard &guard, const ObDatabaseSchema &database)
  {
    const ObSchema *schema = &database;
    common::ObKVCacheHandle handle;
    return guard.put_to_local_cache(DATABASE_SCHEMA, database.get_database_id(), schema, handle);
  }
  static int cache_table(ObSchemaGetterGuard &guard, const ObTableSchema &table)
  {
    const ObSchema *schema = &table;
    common::ObKVCacheHandle handle;
    return guard.put_to_local_cache(TABLE_SCHEMA, table.get_table_id(), schema, handle);
  }
  static int cache_variables(ObSchemaGetterGuard &guard, const ObSysVariableSchema &variables)
  {
    const ObSchema *schema = &variables;
    common::ObKVCacheHandle handle;
    return guard.put_to_local_cache(SYS_VARIABLE_SCHEMA, 1UL, schema, handle);
  }
  static int cache_routine(ObSchemaGetterGuard &guard, const ObRoutineInfo &routine)
  {
    const ObSchema *schema = &routine;
    common::ObKVCacheHandle handle;
    return guard.put_to_local_cache(ROUTINE_SCHEMA, routine.get_routine_id(), schema, handle);
  }
  static int grant(ObSchemaMgr &manager, uint64_t user, const char *name, ObPrivSet rights,
                   const char *database = common::OB_SYS_DATABASE_NAME,
                   ObRoutineType type = ROUTINE_FUNCTION_TYPE)
  {
    ObRoutinePriv privilege;
    privilege.set_user_id(user);
    privilege.set_schema_version(42);
    privilege.set_priv_set(rights);
    privilege.set_routine_type(type);
    int ret = privilege.set_database_name(database);
    if (ret == common::OB_SUCCESS) ret = privilege.set_routine_name(name);
    if (ret == common::OB_SUCCESS) ret = manager.priv_mgr_.add_routine_priv(privilege);
    return ret;
  }
  static int set_name_case_mode(ObSchemaMgr &manager, common::ObNameCaseMode mode)
  {
    ObSimpleSysVariableSchema variables;
    variables.set_schema_version(42);
    variables.set_name_case_mode(mode);
    return manager.sys_variable_mgr_.add_sys_variable(variables);
  }
  static int bind(ObSchemaGetterGuard &guard, ObMultiVersionSchemaService &service, ObSchemaMgr &manager,
                  ObSchemaGetterGuard::SchemaGuardType type = ObSchemaGetterGuard::RUNTIME_SCHEMA_GUARD)
  {
    int ret = guard.init();
    if (ret == common::OB_SUCCESS) {
      guard.schema_service_ = &service;
      guard.schema_guard_type_ = type;
      ObSchemaMgrInfo info;
      info.set_schema_mgr(&manager);
      ret = guard.schema_mgr_infos_.push_back(info);
      if (ret != common::OB_SUCCESS) guard.reset();
    }
    return ret;
  }
  static int add(ObSchemaMgr &manager, uint64_t database, const char *name,
                 uint64_t id, ObRoutineType type, int64_t version)
  {
    ObSimpleRoutineSchema routine;
    routine.set_database_id(database);
    routine.set_package_id(common::OB_INVALID_ID);
    routine.set_routine_id(id);
    routine.set_overload(0);
    routine.set_routine_type(type);
    routine.set_schema_version(version);
    int ret = routine.set_routine_name(common::ObString::make_string(name));
    if (ret == common::OB_SUCCESS) ret = manager.routine_mgr_.add_routine(routine);
    return ret;
  }
};

} } }
#endif
