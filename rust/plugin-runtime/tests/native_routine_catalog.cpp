/*
 * Copyright (c) 2026 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
// Real schema value/reader/writer code, controlled row and SQL transport.
// This does not execute catalog DML or exercise a live server transaction.
#include "share/schema/ob_routine_info.h"
#include "share/schema/ob_priv_sql_service.h"
#include "share/schema/routine_schema_overlay.h"
#include "share/schema/ob_routine_sql_service.h"
#include "share/schema/ob_multi_version_schema_service.h"
#include "observer/schema/ob_schema_retrieve_utils.h"
#include "observer/schema/ob_schema_service_sql_impl.h"
#include "observer/ob_server_plugin_runtime.h"
#include "share/inner_table/ob_inner_table_schema.h"
#include "catalog_version_fixture.h"
#include <cstdlib>
#include <iostream>
#include <map>
#include <vector>

using namespace oceanbase::common;
using namespace oceanbase::share::schema;
using oceanbase::share::ObInnerTableSchema;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __LINE__ << ": " << #expr << std::endl; std::abort(); } } while (false)
#include "native_routine_dependency_fixture.h"
#include "share/schema/native_routine_signature.h"
#include "share/schema/routine_catalog_savepoint.h"
#include "routine_overlay_guard_fixture.h"
#include "native_routine_grant_fixture.h"
#include "native_routine_revoke_fixture.h"
#include "native_routine_grant_plan_fixture.h"
#include "native_routine_acl_snapshot_fixture.h"
#include "native_routine_acl_versions_fixture.h"
#include "native_routine_revoke_writer_fixture.h"
#include "sql/resolver/native_routine_overload.h"

static void family_index()
{
  ObRoutineMgr manager, assigned, copied, uninitialized;
  CHECK(manager.init() == OB_SUCCESS && assigned.init() == OB_SUCCESS && copied.init() == OB_SUCCESS);
  ObSEArray<const ObSimpleRoutineSchema *, 4> found;
  const auto family = [&](const ObRoutineMgr &mgr, const char *name, std::initializer_list<uint64_t> ids) {
    CHECK(mgr.get_standalone_function_schemas(100, ObString::make_string(name), found) == OB_SUCCESS);
    CHECK(found.count() == ids.size());
    int64_t i = 0; for (auto id : ids) CHECK(found.at(i++)->get_routine_id() == id);
  };
  const auto add = [&](uint64_t id, uint64_t slot, const char *name = "Family", uint64_t db = 100,
                       ObRoutineType type = ROUTINE_FUNCTION_TYPE, uint64_t package = OB_INVALID_ID,
                       int64_t version = 42) {
    ObSimpleRoutineSchema routine;
    routine.set_database_id(db); routine.set_package_id(package); routine.set_routine_id(id);
    routine.set_overload(slot); routine.set_routine_type(type); routine.set_schema_version(version);
    CHECK(routine.set_routine_name(ObString::make_string(name)) == OB_SUCCESS);
    return manager.add_routine(routine);
  };
  family(manager, "family", {});
  CHECK(add(2000, 900) == OB_SUCCESS && add(5000, 0) == OB_SUCCESS && add(1000, 10) == OB_SUCCESS);
  CHECK(add(6000, 0, "Family", 101) == OB_SUCCESS);
  CHECK(add(6001, 0, "Familx") == OB_SUCCESS && add(6002, 0, "Familz") == OB_SUCCESS);
  CHECK(add(6003, 0, "Family", 100, ROUTINE_PROCEDURE_TYPE) == OB_SUCCESS);
  CHECK(add(6004, 0, "Family", 100, ROUTINE_FUNCTION_TYPE, 77) == OB_SUCCESS);
  family(manager, "fAmIlY", {5000, 1000, 2000});
  const auto *saved = found.at(1);
  CHECK(add(7000, 10, "FAMILY") == OB_STATE_NOT_MATCH);
  family(manager, "family", {5000, 1000, 2000});
  CHECK(add(1000, 10, "FAMILY", 100, ROUTINE_FUNCTION_TYPE, OB_INVALID_ID, 43) == OB_SUCCESS);
  family(manager, "family", {5000, 1000, 2000});
  CHECK(found.at(1) != saved && saved->get_schema_version() == 42 && found.at(1)->get_schema_version() == 43);
  CHECK(add(1000, 99, "Moved") == OB_SUCCESS);
  family(manager, "family", {5000, 2000}); family(manager, "moved", {1000});
  const ObSimpleRoutineSchema *single = nullptr;
  CHECK(manager.get_routine_schema(100, OB_INVALID_ID, ObString::make_string("family"), 10,
      ROUTINE_FUNCTION_TYPE, single) == OB_SUCCESS && single == nullptr);
  CHECK(manager.del_routine(ObRoutineId(5000)) == OB_SUCCESS);
  family(manager, "family", {2000}); // A missing slot zero/one must not terminate enumeration.
  CHECK(manager.del_routine(ObRoutineId(2000)) == OB_SUCCESS);
  family(manager, "family", {});
  CHECK(add(8000, 0) == OB_SUCCESS && add(8001, 1234) == OB_SUCCESS);
  CHECK(assigned.assign(manager) == OB_SUCCESS && copied.deep_copy(manager) == OB_SUCCESS);
  CHECK(manager.assign(manager) == OB_SUCCESS && manager.deep_copy(manager) == OB_SUCCESS);
  family(manager, "family", {8000, 8001}); saved = found.at(0);
  family(assigned, "family", {8000, 8001}); CHECK(found.at(0) == saved);
  family(copied, "family", {8000, 8001}); CHECK(found.at(0) != saved);
  manager.reset(); family(manager, "family", {});
  family(assigned, "family", {8000, 8001}); family(copied, "family", {8000, 8001});
  CHECK(copied.get_standalone_function_schemas(OB_INVALID_ID, ObString::make_string("family"), found)
      == OB_INVALID_ARGUMENT && found.empty());
  CHECK(copied.get_standalone_function_schemas(100, ObString(), found) == OB_INVALID_ARGUMENT && found.empty());
  CHECK(uninitialized.get_standalone_function_schemas(100, ObString::make_string("family"), found)
      == OB_NOT_INIT && found.empty());
  CHECK(copied.assign(uninitialized) == OB_STATE_NOT_MATCH);
  CHECK(copied.deep_copy(uninitialized) == OB_STATE_NOT_MATCH);
  family(copied, "family", {8000, 8001});
  {
    struct FailingAllocator final : ObIAllocator {
      ObArenaAllocator arena;
      bool fail = false;
      void *alloc(int64_t size) override { return fail ? nullptr : arena.alloc(size); }
      void *alloc(int64_t size, const ObMemAttr &) override { return alloc(size); }
      void free(void *pointer) override { arena.free(pointer); }
    } allocator;
    ObRoutineMgr constrained(allocator);
    CHECK(constrained.init() == OB_SUCCESS && constrained.deep_copy(copied) == OB_SUCCESS);
    family(constrained, "family", {8000, 8001}); saved = found.at(0);
    ObSimpleRoutineSchema change; CHECK(change.assign(*saved) == OB_SUCCESS);
    change.set_overload(19); change.set_schema_version(99);
    allocator.fail = true;
    CHECK(constrained.add_routine(change) == OB_ALLOCATE_MEMORY_FAILED);
    family(constrained, "family", {8000, 8001}); CHECK(found.at(0) == saved);
    allocator.fail = false;
    CHECK(constrained.add_routine(change) == OB_SUCCESS);
    family(constrained, "family", {8000, 8001}); CHECK(found.at(0)->get_overload() == 19);
    CHECK(saved->get_overload() == 0 && saved->get_schema_version() == 42);
  }
  std::cout << "PASS: schema-manager name-family index: sparse slots, namespaces, update/rename/delete, collision, copy/reset and old-pointer lifetime" << std::endl;
}

static void initialize(ObRoutineInfo &value)
{
  value.set_database_id(100); value.set_routine_id(1001); value.set_owner_id(123);
  value.set_package_id(OB_INVALID_ID); value.set_overload(0); value.set_subprogram_id(0);
  value.set_routine_type(ROUTINE_FUNCTION_TYPE); value.set_schema_version(42);
  CHECK(value.set_routine_name(ObString::make_string("area_alias")) == OB_SUCCESS);
  CHECK(value.set_routine_body(ObString::make_string("native source")) == OB_SUCCESS);
  ObRoutineParam result;
  result.set_routine_id(1001); result.set_sequence(0); result.set_subprogram_id(0);
  result.set_param_position(0); result.set_param_level(0); result.set_param_type(ObDoubleType);
  result.set_schema_version(42);
  CHECK(value.add_routine_param(result) == OB_SUCCESS);
  CHECK(value.is_valid());
}

static void native_object_privileges()
{
  using namespace oceanbase::share;
  auto manager = std::make_unique<ObSchemaMgr>();
  auto service = std::make_unique<MockSchemaService>();
  CHECK(manager->init() == OB_SUCCESS);
  ObSchemaGetterGuard guard;
  CHECK(MockSchemaService::bind(guard, *service, *manager) == OB_SUCCESS);
  ObDatabaseSchema database;
  database.set_database_id(100); database.set_schema_version(42);
  CHECK(database.set_database_name("native_db") == OB_SUCCESS);
  CHECK(MockSchemaService::cache_database(guard, database) == OB_SUCCESS);
  ObUserInfo user, role, nested, unrelated;
  uint64_t id = 123;
  for (auto *principal : {&user, &role, &nested, &unrelated}) {
    principal->set_user_id(id++); principal->set_schema_version(42);
    if (principal != &user) principal->set_type(OB_ROLE);
    CHECK(principal->set_user_name("acl_fixture") == OB_SUCCESS);
    CHECK(principal->set_host("localhost") == OB_SUCCESS);
    CHECK(MockSchemaService::cache_user(guard, *principal) == OB_SUCCESS);
  }
  CHECK(user.add_role_id(124) == OB_SUCCESS);
  CHECK(role.add_role_id(125) == OB_SUCCESS);
  CHECK(nested.add_role_id(124) == OB_SUCCESS); // A corrupt cycle must not spin.
  ObSessionPrivInfo session;
  session.user_id_ = 123; session.user_name_ = user.get_user_name_str(); session.host_name_ = user.get_host_name_str();
  ObSEArray<uint64_t, 4> enabled;
  ObRoutineInfo first, second;
  initialize(first); first.set_owner_id(999); first.set_overload(7);
  ObUserInfo owner;
  owner.set_user_id(999); owner.set_schema_version(42);
  CHECK(owner.set_user_name("owner") == OB_SUCCESS && owner.set_host("localhost") == OB_SUCCESS);
  CHECK(MockSchemaService::cache_user(guard, owner) == OB_SUCCESS);
  CHECK(first.set_native_binding(ObString::make_string("org.seekdb.gis"),
      ObString::make_string("org.seekdb.gis.function.st_area"), 1) == OB_SUCCESS);
  CHECK(second.assign(first) == OB_SUCCESS); second.set_routine_id(1002); second.set_overload(19);
  ObRoutineParam parameter;
  parameter.set_param_position(1); parameter.set_sequence(1); parameter.set_schema_version(42);
  parameter.set_routine_id(1002); parameter.set_param_type(ObGeometryType); parameter.set_in_sp_param_flag();
  parameter.set_subprogram_id(0); parameter.set_param_level(0);
  CHECK(parameter.set_param_name(ObString::make_string("geometry")) == OB_SUCCESS);
  CHECK(second.add_routine_param(parameter) == OB_SUCCESS);
  auto private_privileges = std::make_shared<RoutinePrivilegeOverlay>();
  auto overlay = std::make_shared<RoutineSchemaOverlay>(private_privileges);
  CHECK(overlay->stage(first) == OB_SUCCESS && overlay->stage(second) == OB_SUCCESS);
  CHECK(guard.attach_routine_overlay(overlay) == OB_SUCCESS);
  const auto packed = [](ObRawObjPriv permission, uint64_t option = NO_OPTION) {
    ObPackedObjPriv bits = 0;
    CHECK(ObPrivPacker::raw_obj_priv_to_packed_info(option, permission, bits) == OB_SUCCESS);
    return bits;
  };
  const auto check = [&](const ObRoutineInfo &routine, ObPrivSet rights, int expected) {
    const int status = guard.check_native_routine_priv(session, enabled, routine, rights);
    if (status != expected) std::cerr << "native object ACL: id=" << routine.get_routine_id()
        << " rights=" << rights << " status=" << status << " expected=" << expected << std::endl;
    CHECK(status == expected);
  };
  const auto exec = packed(OBJ_PRIV_ID_EXECUTE);
  const auto alter = packed(OBJ_PRIV_ID_ALTER);
  check(first, OB_PRIV_EXECUTE, OB_ERR_NO_ROUTINE_PRIVILEGE);
  CHECK(MockSchemaService::grant(*manager, 123, "area_alias", OB_PRIV_EXECUTE | OB_PRIV_ALTER_ROUTINE,
      "native_db") == OB_SUCCESS);
  check(first, OB_PRIV_EXECUTE, OB_ERR_NO_ROUTINE_PRIVILEGE); // No name fallback.
  CHECK(MockSchemaService::grant_object(*manager, 1001, 999, 123, exec, ObObjectType::TABLE) == OB_SUCCESS);
  CHECK(MockSchemaService::grant_object(*manager, 1001, 999, 123, exec, ObObjectType::FUNCTION, 1) == OB_SUCCESS);
  check(first, OB_PRIV_EXECUTE, OB_ERR_NO_ROUTINE_PRIVILEGE);
  CHECK(MockSchemaService::grant_object(*manager, 1001, 999, 123, exec) == OB_SUCCESS);
  CHECK(MockSchemaService::grant_object(*manager, 1001, 998, 123, alter) == OB_SUCCESS);
  check(first, OB_PRIV_EXECUTE | OB_PRIV_ALTER_ROUTINE, OB_SUCCESS); // Merge grantors.
  check(second, OB_PRIV_EXECUTE, OB_ERR_NO_ROUTINE_PRIVILEGE); // Same name, different identity.
  check(first, OB_PRIV_EXECUTE | OB_PRIV_GRANT, OB_ERR_NO_ROUTINE_PRIVILEGE);
  CHECK(MockSchemaService::grant_object(*manager, 1001, 999, 123, packed(OBJ_PRIV_ID_EXECUTE, GRANT_OPTION)) == OB_SUCCESS);
  check(first, OB_PRIV_EXECUTE | OB_PRIV_GRANT, OB_SUCCESS);
  check(first, OB_PRIV_EXECUTE | OB_PRIV_ALTER_ROUTINE | OB_PRIV_GRANT, OB_ERR_NO_ROUTINE_PRIVILEGE);
  CHECK(MockSchemaService::revoke_object(*manager, 1001, 999, 123) == OB_SUCCESS);
  check(first, OB_PRIV_EXECUTE, OB_ERR_NO_ROUTINE_PRIVILEGE);
  check(first, OB_PRIV_ALTER_ROUTINE, OB_SUCCESS);
  CHECK(MockSchemaService::grant_object(*manager, 1002, 999, 125, exec) == OB_SUCCESS);
  CHECK(MockSchemaService::grant_object(*manager, 1001, 999, 126, exec) == OB_SUCCESS);
  CHECK(enabled.push_back(126) == OB_SUCCESS); // Enabled but not granted to this user.
  check(first, OB_PRIV_EXECUTE, OB_ERR_NO_ROUTINE_PRIVILEGE);
  check(second, OB_PRIV_EXECUTE, OB_ERR_NO_ROUTINE_PRIVILEGE);
  CHECK(enabled.push_back(124) == OB_SUCCESS);
  CHECK(enabled.push_back(124) == OB_SUCCESS); // Duplicates and cycles are bounded.
  check(second, OB_PRIV_EXECUTE, OB_SUCCESS);
  check(first, OB_PRIV_EXECUTE, OB_ERR_NO_ROUTINE_PRIVILEGE);
  {
    RoutineCatalogSavepoint role_save(overlay, private_privileges); CHECK(role_save.valid());
    CHECK(private_privileges->record_object_change(second, 999, 125, 43, exec, 0) == OB_SUCCESS);
    check(second, OB_PRIV_EXECUTE, OB_ERR_NO_ROUTINE_PRIVILEGE);
  }
  check(second, OB_PRIV_EXECUTE, OB_SUCCESS);
  ObUserInfo without_roles;
  without_roles.set_user_id(123); without_roles.set_schema_version(43);
  CHECK(without_roles.set_user_name("acl_fixture") == OB_SUCCESS);
  CHECK(without_roles.set_host("localhost") == OB_SUCCESS);
  user = without_roles;
  session.user_name_ = user.get_user_name_str(); session.host_name_ = user.get_host_name_str();
  check(second, OB_PRIV_EXECUTE, OB_ERR_NO_ROUTINE_PRIVILEGE); // Revoked membership, stale enable list.
  session.user_priv_set_ = OB_PRIV_SUPER;
  check(second, OB_PRIV_EXECUTE, OB_ERR_NO_ROUTINE_PRIVILEGE); // Ignore stale session bits.
  user.set_priv_set(OB_PRIV_EXECUTE);
  check(second, OB_PRIV_EXECUTE, OB_SUCCESS);
  user.set_priv_set(0);
  ObRoutineInfo stale; CHECK(stale.assign(first) == OB_SUCCESS); stale.set_schema_version(41);
  check(stale, OB_PRIV_EXECUTE, OB_SCHEMA_EAGAIN);
  CHECK(stale.assign(first) == OB_SUCCESS); stale.set_overload(8);
  check(stale, OB_PRIV_EXECUTE, OB_SCHEMA_EAGAIN);
  check(first, OB_PRIV_GRANT, OB_INVALID_ARGUMENT);
  check(first, OB_PRIV_SELECT, OB_INVALID_ARGUMENT);
  // A separate guard shares only the committed ACL cache, never this session's
  // private changes. Both use controlled schemas; no commit/isolation claim.
  ObSchemaGetterGuard observer;
  CHECK(MockSchemaService::bind(observer, *service, *manager) == OB_SUCCESS);
  CHECK(MockSchemaService::cache_database(observer, database) == OB_SUCCESS);
  CHECK(MockSchemaService::cache_user(observer, user) == OB_SUCCESS);
  CHECK(MockSchemaService::cache_user(observer, owner) == OB_SUCCESS);
  auto committed_schema = std::make_shared<RoutineSchemaOverlay>();
  CHECK(committed_schema->stage(first) == OB_SUCCESS && committed_schema->stage(second) == OB_SUCCESS);
  CHECK(observer.attach_routine_overlay(committed_schema) == OB_SUCCESS);
  CHECK(MockSchemaService::grant_object(*manager, 1001, 999, 123, exec) == OB_SUCCESS);
  const auto observe = [&](const ObRoutineInfo &routine, int result) {
    CHECK(observer.check_native_routine_priv(session, enabled, routine, OB_PRIV_EXECUTE) == result);
  };
  {
    RoutineCatalogSavepoint outer(overlay, private_privileges); CHECK(outer.valid());
    CHECK(private_privileges->record_object_change(first, 999, 123, 43, exec, 0) == OB_SUCCESS);
    check(first, OB_PRIV_EXECUTE, OB_ERR_NO_ROUTINE_PRIVILEGE);
    check(first, OB_PRIV_ALTER_ROUTINE, OB_SUCCESS); // Another grantor is unaffected.
    observe(first, OB_SUCCESS);
    RoutineCatalogSavepoint inner(overlay, private_privileges); CHECK(inner.valid());
    const auto grantable = packed(OBJ_PRIV_ID_EXECUTE, GRANT_OPTION);
    CHECK(private_privileges->record_object_change(first, 999, 123, 44, 0, grantable) == OB_SUCCESS);
    check(first, OB_PRIV_EXECUTE | OB_PRIV_GRANT, OB_SUCCESS);
    check(first, OB_PRIV_ALTER_ROUTINE | OB_PRIV_GRANT, OB_ERR_NO_ROUTINE_PRIVILEGE);
    CHECK(private_privileges->record_object_change(second, 997, 123, 43, 0, exec) == OB_SUCCESS);
    check(second, OB_PRIV_EXECUTE, OB_SUCCESS);
    observe(second, OB_ERR_NO_ROUTINE_PRIVILEGE);
    CHECK(private_privileges->record_object_change(second, 997, 123, 44, 0, exec) == OB_STATE_NOT_MATCH);
    CHECK(private_privileges->record_object_change(second, 997, 123, 43, exec, 0) == OB_STATE_NOT_MATCH);
    CHECK(private_privileges->record_object_change(second, 997, 123, 44, exec, grantable ^ exec) == OB_INVALID_ARGUMENT);
    CHECK(private_privileges->record_object_change(second, 997, 123, 44, exec, packed(OBJ_PRIV_ID_SELECT)) == OB_INVALID_ARGUMENT);
    ObRoutineInfo wrong; CHECK(wrong.assign(second) == OB_SUCCESS); wrong.set_owner_id(998);
    CHECK(private_privileges->record_object_change(wrong, 997, 123, 44, exec, 0) == OB_STATE_NOT_MATCH);
    RoutineCatalogSavepoint abandoned(overlay, private_privileges); CHECK(abandoned.valid());
    CHECK(inner.rollback() == OB_SUCCESS);
    CHECK(abandoned.rollback() == OB_STATE_NOT_MATCH); // Descendant on an abandoned ACL branch.
    check(first, OB_PRIV_EXECUTE, OB_ERR_NO_ROUTINE_PRIVILEGE);
    check(second, OB_PRIV_EXECUTE, OB_ERR_NO_ROUTINE_PRIVILEGE);
    CHECK(!private_privileges->has_object_changes(1002, 123));
    CHECK(outer.rollback() == OB_SUCCESS);
    CHECK(!private_privileges->has_object_changes(1001, 123));
  }
  check(first, OB_PRIV_EXECUTE, OB_SUCCESS);
  {
    RoutineCatalogSavepoint automatic(overlay, private_privileges);
    CHECK(automatic.valid());
    CHECK(private_privileges->record_object_change(first, 999, 123, 45, exec, 0) == OB_SUCCESS);
    check(first, OB_PRIV_EXECUTE, OB_ERR_NO_ROUTINE_PRIVILEGE);
  }
  check(first, OB_PRIV_EXECUTE, OB_SUCCESS); // RAII rollback includes object ACL.
  std::cout << "PASS: native private ACL overrides per grantor, observer separation, grant options, new grants, stale events and nested/abandoned/RAII savepoints; controlled schema/ACL views" << std::endl;
  {
    RoutineCatalogSavepoint point(overlay, private_privileges);
    CHECK(point.valid());
    CHECK(private_privileges->record_object_change(first, 999, 123, 46, exec, 0) == OB_SUCCESS);
    CHECK(overlay->erase(100, first.get_routine_name(), ROUTINE_FUNCTION_TYPE, 1001, 7) == OB_SUCCESS);
    check(first, OB_PRIV_ALTER_ROUTINE, OB_ERR_SP_DOES_NOT_EXIST);
  }
  check(first, OB_PRIV_ALTER_ROUTINE, OB_SUCCESS);
  check(first, OB_PRIV_EXECUTE, OB_SUCCESS);
  overlay->retire();
  check(first, OB_PRIV_ALTER_ROUTINE, OB_STATE_NOT_MATCH);
  std::cout << "PASS: native object ACL cache isolation, grantors, per-right grant option, enabled nested roles/cycles, stale identities and dropped/retired views; no SQL GRANT or durable lifecycle claims" << std::endl;
}

static void check_binding(const ObRoutineInfo &value)
{
  CHECK(value.is_native() && value.is_native_binding_valid());
  CHECK(value.get_native_abi_version() == 1);
  CHECK(value.get_native_module_id() == ObString::make_string("org.seekdb.gis"));
  CHECK(value.get_native_implementation_id() == ObString::make_string("org.seekdb.gis.area"));
}

static void overload_selection()
{
  using oceanbase::sql::NativeRoutineOverload;
  using Argument = NativeRoutineOverload::Argument;
  using Type = NativeRoutineOverload::Type;
  const auto make = [](ObRoutineInfo &routine, uint64_t id, std::initializer_list<ObObjType> types,
                       bool variadic = false, bool defaults = false, ObCollationType collation = CS_TYPE_UTF8MB4_GENERAL_CI) {
    routine.reset(); initialize(routine); routine.set_routine_id(id); routine.set_overload(id);
    CHECK(routine.set_native_binding(ObString::make_string("org.seekdb.example"),
        ObString::make_string("org.seekdb.example.fn"), 1) == OB_SUCCESS);
    int i = 0;
    for (auto type : types) {
      ObRoutineParam parameter;
      parameter.set_routine_id(id); parameter.set_schema_version(42); parameter.set_subprogram_id(0);
      parameter.set_sequence(++i); parameter.set_param_position(i); parameter.set_param_level(0);
      parameter.set_param_type(type); parameter.set_param_coll_type(collation); parameter.set_in_sp_param_flag();
      CHECK(parameter.set_param_name(ObString::make_string(i == 1 ? "x" : "y")) == OB_SUCCESS);
      if (variadic && i == types.size()) parameter.set_native_variadic();
      if (defaults) CHECK(parameter.set_default_value(ObString::make_string("NULL")) == OB_SUCCESS);
      CHECK(routine.add_routine_param(parameter) == OB_SUCCESS);
    }
  };
  ObRoutineInfo integer, floating, array, optional, narrow, character, text, binary;
  make(integer, 1101, {ObIntType}); make(floating, 1102, {ObDoubleType});
  make(array, 1103, {ObDoubleType}, true); make(optional, 1104, {ObDoubleType, ObDoubleType}, false, true);
  make(narrow, 1105, {ObInt32Type}); make(character, 1106, {ObVarcharType});
  make(text, 1107, {ObLongTextType}); make(binary, 1108, {ObVarcharType}, false, false, CS_TYPE_BINARY);
  const auto select = [&](std::initializer_list<const ObRoutineInfo *> routines,
                          std::initializer_list<Type> types, int expected, uint64_t id = OB_INVALID_ID) {
    ObSEArray<const ObIRoutineInfo *, 4> candidates;
    ObSEArray<Argument, 4> arguments;
    for (auto *routine : routines) CHECK(candidates.push_back(routine) == OB_SUCCESS);
    for (auto type : types) CHECK(arguments.push_back({type, ObString()}) == OB_SUCCESS);
    const ObIRoutineInfo *selected = &integer;
    const int status = NativeRoutineOverload::select(arguments, candidates, selected);
    CHECK(status == expected);
    if (expected == OB_SUCCESS) {
      const auto *native = dynamic_cast<const ObRoutineInfo *>(selected);
      CHECK(native && native->get_routine_id() == id);
    } else CHECK(selected == nullptr);
  };
  const Type i{ObIntType}, d{ObDoubleType}, unknown{ObUnknownType}, null{ObNullType};
  for (bool reverse : {false, true}) {
    const auto *a = reverse ? &floating : &integer, *b = reverse ? &integer : &floating;
    select({a, b}, {i}, OB_SUCCESS, 1101); select({a, b}, {d}, OB_SUCCESS, 1102);
    select({a, b}, {null}, OB_SUCCESS, 1102); select({a, b}, {unknown}, OB_SUCCESS, 1102);
  }
  select({&integer, &narrow}, {unknown}, OB_ERR_FUNC_DUP);
  select({&narrow, &integer}, {null}, OB_ERR_FUNC_DUP);
  select({&array, &floating}, {d}, OB_SUCCESS, 1102);
  select({&floating, &array}, {d, d}, OB_SUCCESS, 1103);
  select({&integer, &array}, {d}, OB_SUCCESS, 1103);
  select({&array, &integer}, {i}, OB_SUCCESS, 1101);
  select({&floating, &optional}, {d}, OB_ERR_FUNC_DUP);
  select({&optional, &floating}, {d}, OB_ERR_FUNC_DUP);
  select({&optional, &floating}, {}, OB_SUCCESS, 1104);
  select({&integer, &floating}, {}, OB_ERR_SP_WRONG_ARG_NUM);
  select({&binary, &character}, {{ObVarcharType, CS_TYPE_BINARY}}, OB_SUCCESS, 1108);
  select({&binary, &character}, {{ObVarcharType, CS_TYPE_UTF8MB4_BIN}}, OB_SUCCESS, 1106);
  select({&integer, &character, &text}, {null}, OB_SUCCESS, 1107);
  select({&text, &character}, {{ObVarcharType, CS_TYPE_UTF8MB4_GENERAL_CI}}, OB_SUCCESS, 1106);
  ObRoutineInfo decimal; make(decimal, 1109, {ObNumberType});
  select({&decimal, &floating}, {{ObDecimalIntType}}, OB_SUCCESS, 1109);
  ObSEArray<Argument, 2> named;
  CHECK(named.push_back({d, ObString::make_string("x")}) == OB_SUCCESS);
  ObSEArray<const ObIRoutineInfo *, 2> candidates;
  CHECK(candidates.push_back(&floating) == OB_SUCCESS && candidates.push_back(&array) == OB_SUCCESS);
  const ObIRoutineInfo *selected = nullptr;
  CHECK(NativeRoutineOverload::select(named, candidates, selected) == OB_SUCCESS && selected == &floating);
  CHECK(named.push_back({d, ObString()}) == OB_SUCCESS);
  CHECK(NativeRoutineOverload::select(named, candidates, selected) == OB_ERR_POSITIONAL_FOLLOW_NAME && !selected);
  std::cout << "PASS: deterministic native overload selection, exact/binary/decimal inputs, defaults, expanded variadic and unknown-type inference; no SQL installation claims" << std::endl;
}

struct Row
{
  std::map<std::string, int64_t> numbers{
    {"routine_id", 1001}, {"is_deleted", 0}, {"database_id", 100}, {"package_id", -1},
    {"overload", 0}, {"subprogram_id", 0}, {"schema_version", 42},
    {"routine_type", ROUTINE_FUNCTION_TYPE}, {"flag", 0}, {"owner_id", 123}, {"type_id", -1}};
  std::map<std::string, std::string> strings{
    {"routine_name", "area_alias"}, {"priv_user", "owner@%"}, {"exec_env", ""},
    {"routine_body", "native source"}, {"comment", ""}, {"route_sql", ""}};
  std::string fail_column;
  int get_int(const char *name, int64_t &out)
  {
    if (fail_column == name) return OB_TIMEOUT;
    const auto it = numbers.find(name);
    if (it == numbers.end()) return OB_ERR_COLUMN_NOT_FOUND;
    out = it->second; return OB_SUCCESS;
  }
  int get_varchar(const char *name, ObString &out)
  {
    if (fail_column == name) return OB_TIMEOUT;
    const auto it = strings.find(name);
    if (it == strings.end()) return OB_ERR_COLUMN_NOT_FOUND;
    out.assign_ptr(it->second.data(), it->second.size()); return OB_SUCCESS;
  }
};

static std::vector<char> wire(const ObRoutineInfo &value)
{
  std::vector<char> bytes(value.get_serialize_size());
  int64_t position = 0;
  CHECK(value.serialize(bytes.data(), bytes.size(), position) == OB_SUCCESS);
  CHECK(position == bytes.size());
  return bytes;
}

static void value_and_reader()
{
  ObRoutineInfo source;
  initialize(source);
  CHECK(!source.is_native() && source.is_native_binding_valid());
  const auto plain_size = source.get_convert_size();
  {
    std::string module = "org.seekdb.gis", implementation = "org.seekdb.gis.area";
    CHECK(source.set_native_binding(ObString(module.size(), module.data()),
        ObString(implementation.size(), implementation.data()), 1) == OB_SUCCESS);
    module.assign(module.size(), 'x'); implementation.assign(implementation.size(), 'x');
  }
  check_binding(source);
  CHECK(source.get_convert_size() == plain_size + source.get_native_module_id().length() +
      source.get_native_implementation_id().length());
  CHECK(source.set_native_binding(source.get_native_module_id(), source.get_native_implementation_id(), 1) == OB_SUCCESS);
  check_binding(source); // Self-aliasing setter.
  ObRoutineInfo copy;
  CHECK(copy.assign(source) == OB_SUCCESS);
  CHECK(copy.get_native_module_id().ptr() != source.get_native_module_id().ptr());
  CHECK(copy.get_routine_params().at(0) != source.get_routine_params().at(0));
  source.reset();
  check_binding(copy);
  RoutineSchemaOverlay overlay;
  {
    ObArenaAllocator request;
    ObRoutineInfo temporary(&request);
    initialize(temporary);
    CHECK(temporary.set_native_binding(copy.get_native_module_id(), copy.get_native_implementation_id(), 1) == OB_SUCCESS);
    CHECK(overlay.stage(temporary) == OB_SUCCESS);
  }
  bool handled = false;
  const ObRoutineInfo *retained = nullptr;
  CHECK(overlay.lookup(1001, handled, retained) == OB_SUCCESS && handled && retained);
  check_binding(*retained);
  CHECK(retained->get_ret_type() && retained->get_ret_type()->get_obj_type() == ObDoubleType);
  for (const char *invalid : {"", "../gis", "org.GIS", "contains space"}) {
    CHECK(copy.set_native_binding(ObString::make_string(invalid), copy.get_native_implementation_id(), 1) == OB_INVALID_ARGUMENT);
    check_binding(copy); // Rejected updates retain the complete prior identity.
  }
  CHECK(copy.set_native_binding(copy.get_native_module_id(), copy.get_native_implementation_id(), 2) == OB_NOT_SUPPORTED);
  CHECK(copy.set_native_binding(copy.get_native_module_id(), copy.get_native_implementation_id(), 0) == OB_INVALID_ARGUMENT);
  check_binding(copy);
  copy.set_package_id(5); CHECK(!copy.is_user_field_valid()); copy.set_package_id(OB_INVALID_ID);
  copy.set_routine_type(ROUTINE_PROCEDURE_TYPE); CHECK(!copy.is_user_field_valid());
  copy.set_routine_type(ROUTINE_FUNCTION_TYPE);
  auto bytes = wire(copy);
  ObRoutineInfo decoded;
  int64_t position = 0;
  CHECK(decoded.deserialize(bytes.data(), bytes.size(), position) == OB_SUCCESS && position == bytes.size());
  check_binding(decoded);
  {
    auto unsupported = bytes;
    const int64_t abi_size = serialization::encoded_length(int64_t{1});
    position = unsupported.size() - abi_size;
    CHECK(serialization::encode(unsupported.data(), unsupported.size(), position, int64_t{2}) == OB_SUCCESS);
    ObRoutineInfo rejected;
    position = 0;
    CHECK(rejected.deserialize(unsupported.data(), unsupported.size(), position) == OB_NOT_SUPPORTED);
  }
  // Existing PL fields borrow wire memory. Native binding specifically owns its
  // IDs; do not read the other fields after overwriting this input buffer.
  std::fill(bytes.begin(), bytes.end(), '\0');
  check_binding(decoded);

  // Remove the appended trailer and update only the UNIS frame length: this
  // yields the actual previous routine wire format, including return params.
  CHECK(copy.set_native_binding({}, {}, 0) == OB_SUCCESS);
  bytes = wire(copy);
  const int64_t trailer = serialization::encoded_length(copy.get_native_module_id()) +
      serialization::encoded_length(copy.get_native_implementation_id()) +
      serialization::encoded_length(copy.get_native_abi_version());
  bytes.resize(bytes.size() - trailer);
  int64_t header_pos = 0, version = 0;
  CHECK(serialization::decode(bytes.data(), bytes.size(), header_pos, version) == OB_SUCCESS);
  const auto payload_size = bytes.size() - header_pos - serialization::OB_SERIALIZE_SIZE_NEED_BYTES;
  CHECK(serialization::encode_fixed_bytes_i64(bytes.data(), bytes.size(), header_pos, payload_size) == OB_SUCCESS);
  position = 0;
  ObRoutineInfo old;
  CHECK(old.deserialize(bytes.data(), bytes.size(), position) == OB_SUCCESS && position == bytes.size());
  CHECK(!old.is_native() && old.get_routine_params().count() == 1 && old.is_user_field_valid());

  Row row;
  bool deleted = true;
  ObRoutineInfo retrieved;
  CHECK(ObSchemaRetrieveUtils::fill_routine_schema(row, retrieved, deleted) == OB_SUCCESS);
  CHECK(!deleted && !retrieved.is_native());
  row.strings["native_module_id"] = "org.seekdb.gis";
  row.strings["native_implementation_id"] = "org.seekdb.gis.area";
  CHECK(ObSchemaRetrieveUtils::fill_routine_schema(row, retrieved, deleted) == OB_INVALID_ARGUMENT);
  row.numbers["native_abi_version"] = 1;
  CHECK(ObSchemaRetrieveUtils::fill_routine_schema(row, retrieved, deleted) == OB_SUCCESS);
  row.strings["native_module_id"] = "overwritten source";
  check_binding(retrieved);
  row.strings["native_module_id"] = "org.seekdb.gis";
  row.numbers["native_abi_version"] = 2;
  CHECK(ObSchemaRetrieveUtils::fill_routine_schema(row, retrieved, deleted) == OB_NOT_SUPPORTED);
  row.numbers["native_abi_version"] = 1;
  row.fail_column = "native_implementation_id";
  CHECK(ObSchemaRetrieveUtils::fill_routine_schema(row, retrieved, deleted) == OB_TIMEOUT);
  row.numbers["is_deleted"] = 1;
  CHECK(ObSchemaRetrieveUtils::fill_routine_schema(row, retrieved, deleted) == OB_SUCCESS);
  CHECK(deleted && !retrieved.is_native());
}

static void writer_and_tables()
{
  class Service final : public ObMultiVersionSchemaService {
  public: Service() = default; ~Service() override = default;
  } service;
  ObMySQLProxy proxy;
  ObSchemaServiceSQLImpl sql_service(nullptr, proxy, service);
  ObRoutineSqlService writer(sql_service);
  ExtensionVersionRows rows;
  rows.write_status = OB_SUCCESS;
  ObRoutineInfo value;
  initialize(value);
  CHECK(writer.add_routine(rows, value) == OB_SUCCESS);
  CHECK(rows.written.size() == 2);
  for (const auto &sql : rows.written) CHECK(sql.find("native_module_id") == std::string::npos);
  rows.written.clear();
  CHECK(value.set_native_binding(ObString::make_string("org.seekdb.gis"),
      ObString::make_string("org.seekdb.gis.area"), 1) == OB_SUCCESS);
  CHECK(writer.add_routine(rows, value) == OB_SUCCESS);
  CHECK(rows.written.size() == 2);
  for (const auto &sql : rows.written) {
    CHECK(sql.find("oceanbase.__all_routine") != std::string::npos);
    for (const char *column : {"native_module_id", "native_implementation_id", "native_abi_version"})
      CHECK(sql.find(column) != std::string::npos);
  }
  rows.written.clear();
  CHECK(value.set_native_binding({}, {}, 0) == OB_SUCCESS);
  CHECK(writer.add_routine(rows, value, true, false, true) == OB_SUCCESS);
  CHECK(rows.written.size() == 2);
  CHECK(rows.written.front().find("UPDATE oceanbase.__all_routine") == 0);
  CHECK(rows.written.front().find("native_abi_version = 0") != std::string::npos);
  rows.written.clear();
  CHECK(value.set_native_binding(ObString::make_string("org.seekdb.gis"),
      ObString::make_string("org.seekdb.gis.area"), 1) == OB_SUCCESS);
  value.set_routine_type(ROUTINE_PROCEDURE_TYPE);
  CHECK(writer.add_routine(rows, value) == OB_INVALID_ARGUMENT && rows.written.empty());
  value.set_routine_type(ROUTINE_FUNCTION_TYPE);
  rows.fail_write_at = rows.writes + 2; // History write fails after current row.
  CHECK(writer.add_routine(rows, value) == OB_TIMEOUT && rows.written.size() == 2);
  CHECK(rows.starts == 0 && rows.ends == 0); // No hidden commit/transaction ownership.
  oceanbase::observer::ObServerPluginRuntime runtime;
  seekdb_plugin_sql_binding_v1_t binding;
  MEMSET(&binding, 0xa5, sizeof(binding));
  CHECK(runtime.resolve_native_function("org.seekdb.gis", "org.seekdb.gis.area", nullptr, 0, &binding) == OB_NOT_INIT);
  const seekdb_plugin_sql_binding_v1_t empty{};
  CHECK(MEMCMP(&binding, &empty, sizeof(binding)) == 0);
  CHECK(runtime.resolve_native_function(nullptr, nullptr, nullptr, 0, nullptr) == OB_INVALID_ARGUMENT);
  for (auto generator : {&ObInnerTableSchema::all_routine_schema, &ObInnerTableSchema::all_routine_history_schema}) {
    ObTableSchema table;
    CHECK(generator(table) == OB_SUCCESS);
    for (const char *column : {"native_module_id", "native_implementation_id", "native_abi_version"})
      CHECK(table.get_column_schema(column) != nullptr);
  }
  // Reuse the existing ID-keyed object ACL writer, including its history and
  // schema-operation log. This is real SQL generation over a controlled sink,
  // not proof that the server's GRANT command routes native functions here.
  ObPrivSqlService privileges(sql_service);
  oceanbase::share::ObRawObjPrivArray rights;
  CHECK(rights.push_back(OBJ_PRIV_ID_EXECUTE) == OB_SUCCESS);
  ObObjPrivSortKey key(1001, static_cast<uint64_t>(ObObjectType::FUNCTION), OBJ_LEVEL_FOR_TAB_PRIV, 999, 123);
  rows.fail_write_at = -1;
  for (bool revoke : {false, true}) {
    rows.written.clear();
    CHECK(privileges.grant_table_ora_only(nullptr, rows, rights, GRANT_OPTION, key, 43, revoke, revoke) == OB_SUCCESS);
    CHECK(rows.written.size() == 3);
    CHECK(rows.written.at(0).find("oceanbase.__all_objauth") != std::string::npos);
    CHECK(rows.written.at(1).find("oceanbase.__all_objauth_history") != std::string::npos);
    CHECK(rows.written.at(2).find("oceanbase.__all_ddl_operation") != std::string::npos);
    if (revoke) {
      auto deletion = rows.written.at(0);
      deletion.erase(std::remove_if(deletion.begin(), deletion.end(),
          [](char c) { return c == ' ' || c == '\n' || c == '\t' || c == '`'; }), deletion.end());
      for (const char *condition : {"obj_id=1001", "grantor_id=999", "grantee_id=123"})
        CHECK(deletion.find(condition) != std::string::npos);
    }
    for (int i = 0; i < 2; ++i) {
      for (const char *column : {"obj_id", "objtype", "col_id", "grantor_id", "grantee_id", "priv_id"})
        CHECK(rows.written.at(i).find(column) != std::string::npos);
      CHECK(rows.written.at(i).find("routine_name") == std::string::npos);
    }
  }
  rows.written.clear(); rows.fail_write_at = rows.writes + 2;
  CHECK(privileges.grant_table_ora_only(nullptr, rows, rights, NO_OPTION, key, 44, false, false) == OB_TIMEOUT);
  CHECK(rows.written.size() == 2 && rows.starts == 0 && rows.ends == 0);
  for (auto generator : {&ObInnerTableSchema::all_objauth_schema, &ObInnerTableSchema::all_objauth_history_schema}) {
    ObTableSchema table; CHECK(generator(table) == OB_SUCCESS);
    for (const char *column : {"obj_id", "objtype", "col_id", "grantor_id", "grantee_id", "priv_id"}) {
      const auto *field = table.get_column_schema(column);
      CHECK(field && field->get_rowkey_position() > 0);
    }
  }
  std::cout << "PASS: existing object-ID ACL current/history/operation SQL writer, revoke key and failure propagation; controlled sink, no SQL GRANT or commit claims" << std::endl;
}

static void native_privilege_drop_snapshot()
{
  using namespace oceanbase::share;
  ObRoutineInfo expected;
  initialize(expected); expected.set_overload(77);
  CHECK(expected.set_native_binding(ObString::make_string("org.seekdb.gis"),
      ObString::make_string("org.seekdb.gis.area"), 1) == OB_SUCCESS);
  ExtensionVersionRows rows;
  using Row = ExtensionVersionRows::Row;
  Row routine;
  routine.integers = {{0, 100}, {1, 123}, {2, 77}, {3, 42}, {4, ROUTINE_FUNCTION_TYPE}, {5, 1}};
  routine.strings = {{6, "area_alias"}, {7, "org.seekdb.gis"}, {8, "org.seekdb.gis.area"}};
  const auto grant = [](int64_t grantee, int64_t grantor, int64_t column, int64_t right, int64_t option) {
    Row row; row.integers = {{0, grantee}, {1, grantor}, {2, column}, {3, right}, {4, option}};
    return row;
  };
  const std::vector<Row> original{
      grant(123, 999, 7, OBJ_PRIV_ID_SELECT, 0),
      grant(123, 999, OBJ_LEVEL_FOR_TAB_PRIV, OBJ_PRIV_ID_ALTER, 0),
      grant(123, 999, OBJ_LEVEL_FOR_TAB_PRIV, OBJ_PRIV_ID_EXECUTE, 1),
      grant(123, 1000, OBJ_LEVEL_FOR_TAB_PRIV, OBJ_PRIV_ID_EXECUTE, 0),
      grant(124, 999, OBJ_LEVEL_FOR_TAB_PRIV, OBJ_PRIV_ID_EXECUTE, 0)};
  std::vector<Row> catalog{routine}, grants = original;
  rows.active = true;
  rows.on_read = [&](ExtensionVersionRows &transport) {
    if (transport.sql.find("FROM oceanbase.__all_routine ") != std::string::npos) {
      CHECK(transport.sql.find("routine_id=1001 AND package_id=18446744073709551615 FOR UPDATE") != std::string::npos);
      transport.rows = catalog;
    } else {
      CHECK(transport.sql == "SELECT grantee_id,grantor_id,col_id,priv_id,priv_option FROM oceanbase.__all_objauth "
          "WHERE obj_id=1001 AND objtype=" + std::to_string(uint64_t(ObObjectType::FUNCTION)) +
          " ORDER BY grantee_id,grantor_id,col_id,priv_id FOR UPDATE");
      transport.rows = grants;
    }
  };
  ObSEArray<ObObjPriv, 4> snapshot;
  const auto attempt = [&](int status) {
    rows.queries.clear();
    CHECK(ObPrivSqlService::get_native_routine_privileges_for_drop(expected, rows, snapshot) == status);
    CHECK(rows.writes == 0 && rows.starts == 0 && rows.ends == 0);
    if (status != OB_SUCCESS) CHECK(snapshot.empty());
  };
  attempt(OB_SUCCESS);
  CHECK(snapshot.count() == 4 && rows.queries.size() == 2 && rows.closes == 2);
  ObPackedObjPriv execute = 0, alter = 0;
  CHECK(ObPrivPacker::raw_obj_priv_to_packed_info(GRANT_OPTION, OBJ_PRIV_ID_EXECUTE, execute) == OB_SUCCESS);
  CHECK(ObPrivPacker::raw_obj_priv_to_packed_info(NO_OPTION, OBJ_PRIV_ID_ALTER, alter) == OB_SUCCESS);
  CHECK(snapshot.at(1).get_obj_privs() == (execute | alter));
  CHECK(snapshot.at(0).get_col_id() == 7 && snapshot.at(2).get_grantor_id() == 1000 &&
      snapshot.at(3).get_grantee_id() == 124);
  for (const auto &entry : snapshot) {
    CHECK(entry.get_obj_id() == 1001 && entry.get_objtype() == uint64_t(ObObjectType::FUNCTION));
    CHECK(entry.get_schema_version() == 42 && entry.is_valid());
  }
  // Own the result independently of the SQL result/transport lifetime.
  rows.rows.clear(); grants.clear();
  CHECK(snapshot.at(1).get_obj_privs() == (execute | alter));
  attempt(OB_SUCCESS); CHECK(snapshot.empty());
  grants = original;
  rows.active = false; attempt(OB_STATE_NOT_MATCH); CHECK(rows.queries.empty()); rows.active = true;
  catalog.clear(); attempt(OB_ERR_SP_DOES_NOT_EXIST); CHECK(rows.queries.size() == 1);
  catalog = {routine, routine}; attempt(OB_ERR_UNEXPECTED);
  catalog = {routine}; catalog[0].integers[3] = 41; attempt(OB_STATE_NOT_MATCH);
  catalog = {routine};
  grants.push_back(grants.back()); attempt(OB_INVALID_DATA);
  grants = original; std::swap(grants[1], grants[2]); attempt(OB_INVALID_DATA);
  for (const auto &field : std::vector<std::pair<int, int64_t>>{
      {0, 0}, {1, -1}, {2, -1}, {3, OBJ_PRIV_ID_NONE}, {3, OBJ_PRIV_ID_MAX}, {4, 2}}) {
    grants = original; grants.back().integers[field.first] = field.second; attempt(OB_INVALID_DATA);
  }
  grants = original;
  const auto read_callback = rows.on_read;
  for (int stage = 0; stage < 2; ++stage) for (int failure = 0; failure < 4; ++failure) {
    rows.on_read = [&](ExtensionVersionRows &transport) {
      read_callback(transport);
      const bool acl = transport.sql.find("FROM oceanbase.__all_objauth ") != std::string::npos;
      if (acl == bool(stage)) {
        if (failure == 0) transport.read_status = OB_TIMEOUT;
        if (failure == 1) transport.close_status = OB_TIMEOUT;
        if (failure == 2) transport.fail_field = 0;
        // Fail after at least one row was materialized, not just before iteration.
        if (failure == 3) transport.fail_next_at = 1;
      }
    };
    attempt(failure == 2 ? OB_ERR_NULL_VALUE : OB_TIMEOUT);
    CHECK(rows.queries.size() == stage + 1);
    rows.read_status = rows.close_status = OB_SUCCESS; rows.fail_field = rows.fail_next_at = -1;
  }
  rows.on_read = read_callback;
  attempt(OB_SUCCESS); CHECK(snapshot.count() == 4);
  grants.clear();
  for (int grantee = 1; grantee <= 16385; ++grantee)
    grants.push_back(grant(grantee, 999, OBJ_LEVEL_FOR_TAB_PRIV, OBJ_PRIV_ID_EXECUTE, 0));
  attempt(OB_SIZE_OVERFLOW); // Never return the first 16384 groups as a complete snapshot.
  grants = original; attempt(OB_SUCCESS); CHECK(snapshot.count() == 4);
  std::cout << "PASS: native DROP reads owned transaction-local ACL groups across grantors/grantees/columns; malformed/failed reads leave no partial snapshot" << std::endl;
}

static void native_privilege_transaction_authority()
{
  using namespace oceanbase::share;
  using Change = ObPrivSqlService::NativePrivilegeChange;
  auto manager = std::make_unique<ObSchemaMgr>();
  auto service = std::make_unique<MockSchemaService>();
  CHECK(manager->init() == OB_SUCCESS);
  ObSchemaGetterGuard guard;
  CHECK(MockSchemaService::bind(guard, *service, *manager) == OB_SUCCESS);
  ObDatabaseSchema database;
  database.set_database_id(100); database.set_schema_version(42);
  CHECK(database.set_database_name("native_db") == OB_SUCCESS);
  CHECK(MockSchemaService::cache_database(guard, database) == OB_SUCCESS);
  ObUserInfo user, role, nested, recipient;
  uint64_t id = 123;
  for (auto *principal : {&user, &role, &nested, &recipient}) {
    principal->set_user_id(id++); principal->set_schema_version(42);
    if (principal == &role || principal == &nested) principal->set_type(OB_ROLE);
    CHECK(principal->set_user_name("authority_fixture") == OB_SUCCESS);
    CHECK(principal->set_host("localhost") == OB_SUCCESS);
    CHECK(MockSchemaService::cache_user(guard, *principal) == OB_SUCCESS);
  }
  CHECK(user.add_role_id(124) == OB_SUCCESS && role.add_role_id(125) == OB_SUCCESS);
  CHECK(nested.add_role_id(124) == OB_SUCCESS);
  ObSessionPrivInfo actor;
  actor.user_id_ = 123; actor.user_name_ = user.get_user_name_str(); actor.host_name_ = user.get_host_name_str();
  actor.user_priv_set_ = OB_PRIV_SUPER; // Not an authority source.
  ObSEArray<uint64_t, 4> enabled;
  ObRoutineInfo expected;
  initialize(expected); expected.set_overload(77); expected.set_owner_id(998);
  ObUserInfo owner;
  owner.set_user_id(998); owner.set_schema_version(42);
  CHECK(owner.set_user_name("owner") == OB_SUCCESS && owner.set_host("localhost") == OB_SUCCESS);
  CHECK(MockSchemaService::cache_user(guard, owner) == OB_SUCCESS);
  CHECK(expected.set_native_binding(ObString::make_string("org.seekdb.gis"),
      ObString::make_string("org.seekdb.gis.area"), 1) == OB_SUCCESS);
  auto private_acl = std::make_shared<RoutinePrivilegeOverlay>(100, 123);
  auto overlay = std::make_shared<RoutineSchemaOverlay>(private_acl);
  CHECK(overlay->stage(expected) == OB_SUCCESS && guard.attach_routine_overlay(overlay) == OB_SUCCESS);
  ObMySQLProxy proxy;
  ObSchemaServiceSQLImpl sql_service(nullptr, proxy, *service);
  ObPrivSqlService writer(sql_service);
  ExtensionVersionRows rows;
  using Row = ExtensionVersionRows::Row;
  Row routine;
  routine.integers = {{0, 100}, {1, 998}, {2, 77}, {3, 42}, {4, ROUTINE_FUNCTION_TYPE}, {5, 1}};
  routine.strings = {{6, "area_alias"}, {7, "org.seekdb.gis"}, {8, "org.seekdb.gis.area"}};
  const auto grant = [](uint64_t principal, int right, int option) {
    Row row; row.integers = {{0, int64_t(principal)}, {1, 999}, {2, OBJ_LEVEL_FOR_TAB_PRIV},
        {3, right}, {4, option}}; return row;
  };
  std::vector<Row> acl, current;
  int snapshot_error = OB_SUCCESS, close_error = OB_SUCCESS;
  uint64_t selected = 123, grantee = 126;
  rows.active = true; rows.write_status = OB_SUCCESS;
  rows.on_read = [&](ExtensionVersionRows &transport) {
    if (transport.sql.find("SELECT database_id,owner_id") == 0) transport.rows = {routine};
    else if (transport.sql.find("SELECT grantee_id,grantor_id,col_id") == 0) {
      transport.rows = acl; transport.read_status = snapshot_error; transport.close_status = close_error;
      for (const auto &existing : current) {
        Row target;
        target.integers = {{0, 126}, {1, int64_t(selected)}, {2, OBJ_LEVEL_FOR_TAB_PRIV},
            {3, existing.integers.at(0)}, {4, existing.integers.at(1)}};
        transport.rows.push_back(target);
      }
    } else {
      CHECK(transport.sql.find("SELECT priv_id,priv_option") == 0);
      CHECK(transport.sql.find("grantor_id=" + std::to_string(selected) + " AND grantee_id=126 ") != std::string::npos);
      transport.rows = current;
    }
  };
  ObPackedObjPriv before = 0, after = 0, execute_option = 0;
  CHECK(ObPrivPacker::raw_obj_priv_to_packed_info(GRANT_OPTION, OBJ_PRIV_ID_EXECUTE, execute_option) == OB_SUCCESS);
  CHECK(MockSchemaService::grant_object(*manager, 1001, 999, 123, execute_option) == OB_SUCCESS);
  const auto attempt = [&](int expected_status, int writes = 0, ObPrivSet rights = OB_PRIV_EXECUTE,
                           RoutinePrivilegeOverlay *publish = nullptr,
                           Change change = Change::GRANT, bool option = false) {
    rows.queries.clear(); rows.written.clear(); before = after = ~ObPackedObjPriv{0};
    const int status = writer.change_native_routine_privileges_authorized(guard, actor, enabled,
        expected, selected, grantee, rights, change, option, 44, rows, nullptr, before, after, publish);
    if (status != expected_status) std::cerr << "native transactional authority status=" << status
        << " expected=" << expected_status << " writes=" << rows.written.size() << std::endl;
    CHECK(status == expected_status && rows.written.size() == writes);
    CHECK(rows.starts == 0 && rows.ends == 0);
    if (status != OB_SUCCESS) CHECK(before == 0 && after == 0);
  };
  attempt(OB_ERR_NO_ROUTINE_PRIVILEGE); // Empty transaction ACL overrides a cached grant option.
  {
    RoutineCatalogSavepoint speculative(overlay, private_acl); CHECK(speculative.valid());
    CHECK(private_acl->record_object_change(expected, 123, 123, 43, 0, execute_option) == OB_SUCCESS);
    attempt(OB_ERR_NO_ROUTINE_PRIVILEGE); // Nor can a provisional view fill a missing SQL row.
  }
  acl = {grant(123, OBJ_PRIV_ID_EXECUTE, 0)}; attempt(OB_ERR_NO_ROUTINE_PRIVILEGE);
  acl = {grant(123, OBJ_PRIV_ID_EXECUTE, 1)}; attempt(OB_SUCCESS, 3);
  CHECK(rows.queries.size() == 4 && before == 0 && after != 0);
  selected = 124; acl = {grant(124, OBJ_PRIV_ID_EXECUTE, 1)};
  attempt(OB_ERR_NO_ROUTINE_PRIVILEGE);
  CHECK(enabled.push_back(124) == OB_SUCCESS); attempt(OB_SUCCESS, 3);
  selected = 123; attempt(OB_ERR_NO_ROUTINE_PRIVILEGE); // Role authority cannot be relabeled as actor.
  selected = 125; acl = {grant(125, OBJ_PRIV_ID_EXECUTE, 1)}; attempt(OB_SUCCESS, 3);
  enabled.reset(); CHECK(enabled.push_back(126) == OB_SUCCESS);
  selected = 126; acl = {grant(126, OBJ_PRIV_ID_EXECUTE, 1)}; attempt(OB_ERR_NO_ROUTINE_PRIVILEGE);
  CHECK(enabled.push_back(124) == OB_SUCCESS);
  acl = {grant(123, OBJ_PRIV_ID_EXECUTE, 1), grant(124, OBJ_PRIV_ID_ALTER, 1)};
  selected = 123; attempt(OB_ERR_NO_ROUTINE_PRIVILEGE, 0, OB_PRIV_EXECUTE | OB_PRIV_ALTER_ROUTINE);
  selected = 124; attempt(OB_ERR_NO_ROUTINE_PRIVILEGE, 0, OB_PRIV_EXECUTE | OB_PRIV_ALTER_ROUTINE);
  selected = 123; acl = {grant(123, OBJ_PRIV_ID_EXECUTE, 1)};
  grantee = 10000; attempt(OB_USER_NOT_EXIST); CHECK(rows.queries.empty()); grantee = 126;
  routine.integers[3] = 41; attempt(OB_STATE_NOT_MATCH); routine.integers[3] = 42;
  snapshot_error = OB_TIMEOUT; attempt(OB_TIMEOUT);
  snapshot_error = rows.read_status = OB_SUCCESS;
  close_error = OB_TIMEOUT; attempt(OB_TIMEOUT); close_error = rows.close_status = OB_SUCCESS;
  for (int failure = 1; failure <= 3; ++failure) {
    rows.fail_write_at = rows.writes + failure; attempt(OB_TIMEOUT, failure, OB_PRIV_EXECUTE, private_acl.get());
    CHECK(!private_acl->has_object_changes(1001, 126));
  }
  rows.fail_write_at = -1;
  {
    RoutineCatalogSavepoint success(overlay, private_acl); CHECK(success.valid());
    attempt(OB_SUCCESS, 3, OB_PRIV_EXECUTE, private_acl.get());
    CHECK(private_acl->has_object_changes(1001, 126));
  }
  CHECK(!private_acl->has_object_changes(1001, 126));
  {
    RoutineCatalogSavepoint role_change(overlay, private_acl); CHECK(role_change.valid());
    selected = 124; acl = {grant(124, OBJ_PRIV_ID_EXECUTE, 1)};
    attempt(OB_SUCCESS, 3, OB_PRIV_EXECUTE, private_acl.get());
    CHECK(private_acl->has_object_changes(1001, 126));
  }
  CHECK(!private_acl->has_object_changes(1001, 126));
  {
    RoutinePrivilegeOverlay wrong_actor(100, 125);
    attempt(OB_INVALID_ARGUMENT, 3, OB_PRIV_EXECUTE, &wrong_actor);
    CHECK(!wrong_actor.has_object_changes(1001, 126));
  }
  selected = 123; acl = {grant(123, OBJ_PRIV_ID_EXECUTE, 1)};
  Row already; already.integers = {{0, OBJ_PRIV_ID_EXECUTE}, {1, 0}}; current = {already};
  attempt(OB_SUCCESS); CHECK(before == after && before != 0);
  attempt(OB_SUCCESS, 3, OB_PRIV_EXECUTE, nullptr, Change::GRANT, true);
  CHECK(after == execute_option);
  attempt(OB_SUCCESS, 3, OB_PRIV_EXECUTE, nullptr, Change::REVOKE);
  CHECK(before != 0 && after == 0);
  current[0].integers[1] = 1;
  attempt(OB_SUCCESS, 3, OB_PRIV_EXECUTE, nullptr, Change::REVOKE_GRANT_OPTION);
  CHECK(before == execute_option && after != 0 && after != execute_option);
  attempt(OB_INVALID_ARGUMENT, 0, OB_PRIV_EXECUTE, nullptr, Change::REVOKE, true);
  CHECK(rows.queries.empty());
  acl.clear(); attempt(OB_ERR_NO_ROUTINE_PRIVILEGE); // A no-op still needs current authority.
  current.clear();
  NativeRoutineGrantors sources;
  const auto select = [&](int status, ObPrivSet rights, uint64_t execute = OB_INVALID_ID,
                          uint64_t alter = OB_INVALID_ID) {
    ObSEArray<ObObjPriv, 4> snapshot;
    CHECK(writer.read_native_routine_privileges(expected, rows, snapshot) == OB_SUCCESS);
    sources.execute_ = sources.alter_ = 999; // Failed/reused output must be empty.
    CHECK(guard.select_native_routine_grantors(actor, enabled, expected, rights, snapshot, sources) == status);
    CHECK(sources.execute_ == execute && sources.alter_ == alter);
  };
  select(OB_ERR_NO_ROUTINE_PRIVILEGE, OB_PRIV_EXECUTE);
  acl = {grant(123, OBJ_PRIV_ID_EXECUTE, 1), grant(124, OBJ_PRIV_ID_ALTER, 1)};
  select(OB_SUCCESS, OB_PRIV_EXECUTE | OB_PRIV_ALTER_ROUTINE, 123, 124);
  acl = {grant(124, OBJ_PRIV_ID_EXECUTE, 1), grant(125, OBJ_PRIV_ID_EXECUTE, 1)};
  select(OB_SUCCESS, OB_PRIV_EXECUTE, 124);
  CHECK(user.add_role_id(125) == OB_SUCCESS);
  enabled.reset(); CHECK(enabled.push_back(125) == OB_SUCCESS && enabled.push_back(124) == OB_SUCCESS);
  select(OB_SUCCESS, OB_PRIV_EXECUTE, 124); // Independent of BFS/input order, including cycles.
  acl = {grant(123, OBJ_PRIV_ID_EXECUTE, 1), grant(124, OBJ_PRIV_ID_EXECUTE, 1)};
  select(OB_SUCCESS, OB_PRIV_EXECUTE, 123); // Prefer actor, not the first role.
  nested.set_type(OB_USER);
  select(OB_INVALID_DATA, OB_PRIV_EXECUTE); // A later graph failure must discard the already selected actor.
  nested.set_type(OB_ROLE);
  select(OB_ERR_NO_ROUTINE_PRIVILEGE, OB_PRIV_EXECUTE | OB_PRIV_ALTER_ROUTINE);
  select(OB_INVALID_ARGUMENT, OB_PRIV_EXECUTE | OB_PRIV_GRANT);
  select(OB_INVALID_ARGUMENT, 0);
  acl = {grant(124, OBJ_PRIV_ID_EXECUTE, 1)};
  enabled.reset(); select(OB_ERR_NO_ROUTINE_PRIVILEGE, OB_PRIV_EXECUTE);
  CHECK(enabled.push_back(126) == OB_SUCCESS);
  acl = {grant(126, OBJ_PRIV_ID_EXECUTE, 1)};
  select(OB_ERR_NO_ROUTINE_PRIVILEGE, OB_PRIV_EXECUTE); // Enabled is not evidence of membership.
  CHECK(enabled.push_back(124) == OB_SUCCESS);
  acl.clear();
  user.set_priv_set(OB_PRIV_EXECUTE); role.set_priv_set(OB_PRIV_GRANT);
  select(OB_ERR_NO_ROUTINE_PRIVILEGE, OB_PRIV_EXECUTE); // Do not synthesize a grantor from broad bits.
  role.set_priv_set(OB_PRIV_EXECUTE | OB_PRIV_GRANT);
  select(OB_SUCCESS, OB_PRIV_EXECUTE, 124);
  user.set_priv_set(OB_PRIV_SUPER);
  select(OB_SUCCESS, OB_PRIV_EXECUTE | OB_PRIV_ALTER_ROUTINE, 998, 998);
  user.set_priv_set(0); role.set_priv_set(0);
  CHECK(recipient.add_role_id(124) == OB_SUCCESS);
  actor.user_id_ = 126;
  acl = {grant(124, OBJ_PRIV_ID_EXECUTE, 1), grant(126, OBJ_PRIV_ID_EXECUTE, 1)};
  select(OB_SUCCESS, OB_PRIV_EXECUTE, 126); // Actor wins even over a numerically smaller role.
  actor.user_id_ = 123;
  // A previously selected role is not a bearer token: the checked writer must
  // still reject it if its authoritative grant option disappears.
  acl = {grant(124, OBJ_PRIV_ID_EXECUTE, 1)};
  select(OB_SUCCESS, OB_PRIV_EXECUTE, 124);
  selected = sources.execute_; acl.clear(); attempt(OB_ERR_NO_ROUTINE_PRIVILEGE);
  std::cout << "PASS: deterministic per-right actor/role grantor selection from complete transaction ACL; no cross-principal authority synthesis or partial plans" << std::endl;
  std::cout << "PASS: selected actor/role grantor rechecked against complete transaction ACL before actual delta SQL; stale cache/private views, per-grantor provenance, nested roles, read/write failures and no-op authority; controlled transport, no Root DCL/commit claims" << std::endl;
}

static void native_privilege_mutation()
{
  using namespace oceanbase::share;
  using Change = ObPrivSqlService::NativePrivilegeChange;
  class Service final : public ObMultiVersionSchemaService {
  public: Service() = default; ~Service() override = default;
  } service;
  ObMySQLProxy proxy;
  ObSchemaServiceSQLImpl sql_service(nullptr, proxy, service);
  ObPrivSqlService writer(sql_service);
  ObRoutineInfo expected;
  initialize(expected); expected.set_overload(77);
  CHECK(expected.set_native_binding(ObString::make_string("org.seekdb.gis"),
      ObString::make_string("org.seekdb.gis.area"), 1) == OB_SUCCESS);
  ExtensionVersionRows rows;
  using Row = ExtensionVersionRows::Row;
  Row routine;
  routine.integers = {{0, 100}, {1, 123}, {2, 77}, {3, 42}, {4, ROUTINE_FUNCTION_TYPE}, {5, 1}};
  routine.strings = {{6, "area_alias"}, {7, "org.seekdb.gis"}, {8, "org.seekdb.gis.area"}};
  std::vector<Row> catalog{routine}, grants;
  rows.active = true; rows.write_status = OB_SUCCESS;
  rows.on_read = [&](ExtensionVersionRows &transport) {
    if (transport.sql.find("FROM oceanbase.__all_routine ") != std::string::npos) {
      CHECK(transport.sql.find("routine_id=1001 AND package_id=18446744073709551615 FOR UPDATE") != std::string::npos);
      transport.rows = catalog;
    } else {
      CHECK(transport.sql.find("FROM oceanbase.__all_objauth ") != std::string::npos);
      const std::string key = "obj_id=1001 AND objtype=" + std::to_string(uint64_t(ObObjectType::FUNCTION)) +
          " AND col_id=" + std::to_string(OBJ_LEVEL_FOR_TAB_PRIV) + " AND grantor_id=999 AND grantee_id=123";
      CHECK(transport.sql.find(key) != std::string::npos);
      CHECK(transport.sql.find("ORDER BY priv_id FOR UPDATE") != std::string::npos);
      transport.rows = grants;
    }
  };
  const auto packed = [](int execute, int alter) {
    ObPackedObjPriv result = 0, bit = 0;
    if (execute >= 0) {
      CHECK(ObPrivPacker::raw_obj_priv_to_packed_info(execute, OBJ_PRIV_ID_EXECUTE, bit) == OB_SUCCESS);
      result |= bit;
    }
    if (alter >= 0) {
      CHECK(ObPrivPacker::raw_obj_priv_to_packed_info(alter, OBJ_PRIV_ID_ALTER, bit) == OB_SUCCESS);
      result |= bit;
    }
    return result;
  };
  const auto grant_row = [](ObRawObjPriv id, int option) {
    Row row; row.integers = {{0, int64_t(id)}, {1, option}}; return row;
  };
  ObPackedObjPriv before = 0, after = 0;
  int cases = 0;
  for (int execute : {-1, 0, 1}) for (int alter : {-1, 0, 1}) {
    grants.clear();
    if (execute >= 0) grants.push_back(grant_row(OBJ_PRIV_ID_EXECUTE, execute));
    if (alter >= 0) grants.push_back(grant_row(OBJ_PRIV_ID_ALTER, alter));
    for (ObPrivSet rights : {OB_PRIV_EXECUTE, OB_PRIV_ALTER_ROUTINE, OB_PRIV_EXECUTE | OB_PRIV_ALTER_ROUTINE}) {
      for (int operation = 0; operation < 4; ++operation) {
        const Change change = operation < 2 ? Change::GRANT : operation == 2 ? Change::REVOKE : Change::REVOKE_GRANT_OPTION;
        int next_execute = execute, next_alter = alter;
        const auto apply = [&](int old) {
          switch (operation) {
            case 0: return old == 1 ? 1 : 0;
            case 1: return 1;
            case 2: return -1;
            default: return old == 1 ? 0 : old;
          }
        };
        if (rights & OB_PRIV_EXECUTE) next_execute = apply(execute);
        if (rights & OB_PRIV_ALTER_ROUTINE) next_alter = apply(alter);
        rows.written.clear(); rows.queries.clear();
        const int old_closes = rows.closes;
        CHECK(writer.change_native_routine_privileges(expected, 999, 123, rights, change,
            operation == 1, 43, rows, nullptr, before, after) == OB_SUCCESS);
        CHECK(before == packed(execute, alter) && after == packed(next_execute, next_alter));
        CHECK(rows.queries.size() == 2 && rows.closes == old_closes + 2);
        const int changes = (execute != next_execute) + (alter != next_alter);
        CHECK(rows.written.size() == (changes ? 2 * changes + 1 : 0));
        int logs = 0;
        for (const auto &sql : rows.written) {
          if (sql.find("oceanbase.__all_ddl_operation") != std::string::npos) ++logs;
          else {
            CHECK(sql.find("oceanbase.__all_objauth") != std::string::npos);
            CHECK(sql.find("grantor_id") != std::string::npos && sql.find("grantee_id") != std::string::npos);
            CHECK(sql.find("routine_name") == std::string::npos);
          }
        }
        CHECK(logs == (changes ? 1 : 0));
        // Check emitted values too: a correct returned mask alone would not
        // catch swapping grant-option groups or deleting another grantor.
        const int previous[] = {execute, alter}, next[] = {next_execute, next_alter};
        const ObRawObjPriv ids[] = {OBJ_PRIV_ID_EXECUTE, OBJ_PRIV_ID_ALTER};
        const std::string values = "VALUES(1001," + std::to_string(uint64_t(ObObjectType::FUNCTION)) +
            "," + std::to_string(OBJ_LEVEL_FOR_TAB_PRIV) + ",999,123,";
        for (int right = 0; right < 2; ++right) if (previous[right] != next[right]) {
          int matches = 0;
          for (int index = 0; index + 1 < int(rows.written.size()); index += 2) {
            auto current = rows.written[index], history = rows.written[index + 1];
            const auto normalize = [](std::string &sql) {
              sql.erase(std::remove_if(sql.begin(), sql.end(),
                  [](char c) { return c == ' ' || c == '\n' || c == '\t' || c == '`'; }), sql.end());
            };
            normalize(current); normalize(history);
            const bool match = next[right] < 0
                ? current == "DELETEFROMoceanbase.__all_objauthWHEREobj_id=1001ANDobjtype=" +
                    std::to_string(uint64_t(ObObjectType::FUNCTION)) + "ANDcol_id=" +
                    std::to_string(OBJ_LEVEL_FOR_TAB_PRIV) + "ANDgrantor_id=999ANDgrantee_id=123ANDpriv_id=" +
                    std::to_string(ids[right])
                : current.find("REPLACEINTOoceanbase.__all_objauth(") == 0 &&
                    current.find(values + std::to_string(ids[right]) + "," + std::to_string(next[right]) + ",") != std::string::npos;
            if (match) {
              ++matches;
              CHECK(history.find("INSERTINTOoceanbase.__all_objauth_history(") == 0);
              if (history.find(values + std::to_string(ids[right]) + "," +
                  (next[right] < 0 ? "" : std::to_string(next[right]) + ",")) == std::string::npos)
                std::cerr << "unexpected ACL history: " << history << std::endl;
              CHECK(history.find(values + std::to_string(ids[right]) + "," +
                  (next[right] < 0 ? "" : std::to_string(next[right]) + ",")) != std::string::npos);
            }
          }
          CHECK(matches == 1);
        }
        CHECK(rows.starts == 0 && rows.ends == 0);
        ++cases;
      }
    }
  }
  CHECK(cases == 108);
  int reductions = 0;
  for (int old_execute : {-1,0,1}) for (int old_alter : {-1,0,1}) {
    grants.clear();
    if (old_execute >= 0) grants.push_back(grant_row(OBJ_PRIV_ID_EXECUTE,old_execute));
    if (old_alter >= 0) grants.push_back(grant_row(OBJ_PRIV_ID_ALTER,old_alter));
    const auto old = packed(old_execute,old_alter);
    for (int new_execute : {-1,0,1}) for (int new_alter : {-1,0,1}) {
      const bool decrease = new_execute <= old_execute && new_alter <= old_alter;
      rows.written.clear(); rows.queries.clear();
      CHECK(writer.apply_native_routine_privilege_reduction(expected,999,123,old,
          packed(new_execute,new_alter),43,rows,nullptr) == (decrease ? OB_SUCCESS : OB_INVALID_ARGUMENT));
      const int changes = (old_execute != new_execute) + (old_alter != new_alter);
      CHECK(rows.written.size() == (decrease && changes ? changes*2+1 : 0));
      CHECK(rows.queries.size() == (decrease ? 2 : 0));
      if (decrease) {
        const int old_options[] = {old_execute,old_alter}, next_options[] = {new_execute,new_alter};
        const ObRawObjPriv raw[] = {OBJ_PRIV_ID_EXECUTE,OBJ_PRIV_ID_ALTER};
        for (int i = 0; i < 2; ++i) if (old_options[i] != next_options[i]) {
          int matches = 0;
          for (auto sql : rows.written) {
            sql.erase(std::remove_if(sql.begin(),sql.end(),[](char c) { return c == ' ' || c == '\n' || c == '\t' || c == '`'; }),sql.end());
            if (next_options[i] < 0) {
              matches += sql == "DELETEFROMoceanbase.__all_objauthWHEREobj_id=1001ANDobjtype=" +
                  std::to_string(uint64_t(ObObjectType::FUNCTION)) + "ANDcol_id=" + std::to_string(OBJ_LEVEL_FOR_TAB_PRIV) +
                  "ANDgrantor_id=999ANDgrantee_id=123ANDpriv_id=" + std::to_string(raw[i]);
            } else {
              matches += sql.find("REPLACEINTOoceanbase.__all_objauth(") == 0 &&
                  sql.find("VALUES(1001," + std::to_string(uint64_t(ObObjectType::FUNCTION)) + "," +
                  std::to_string(OBJ_LEVEL_FOR_TAB_PRIV) + ",999,123," + std::to_string(raw[i]) + "," +
                  std::to_string(next_options[i]) + ",") != std::string::npos;
            }
          }
          CHECK(matches == 1);
        }
      }
      if (decrease) ++reductions;
    }
    rows.written.clear();
    CHECK(writer.apply_native_routine_privilege_reduction(expected,999,123,
        old ? 0 : packed(0,-1),0,43,rows,nullptr) == OB_STATE_NOT_MATCH);
    CHECK(rows.written.empty()); // An obsolete before-image cannot erase a newer grant.
  }
  CHECK(reductions == 36);
  for (auto invalid : {packed(1,-1)^packed(0,-1), ~ObPackedObjPriv{0}}) {
    rows.queries.clear(); rows.written.clear();
    CHECK(writer.apply_native_routine_privilege_reduction(expected,999,123,invalid,0,43,rows,nullptr) == OB_INVALID_ARGUMENT);
    CHECK(rows.queries.empty() && rows.written.empty());
  }
  grants = {grant_row(OBJ_PRIV_ID_EXECUTE,1),grant_row(OBJ_PRIV_ID_ALTER,1)};
  for (int failure = 1; failure <= 5; ++failure) {
    rows.written.clear(); rows.fail_write_at = rows.writes + failure;
    CHECK(writer.apply_native_routine_privilege_reduction(expected,999,123,
        packed(1,1),packed(-1,0),43,rows,nullptr) == OB_TIMEOUT);
    CHECK(rows.written.size() == failure && rows.starts == 0 && rows.ends == 0);
  }
  rows.fail_write_at = -1;
  std::cout << "PASS: exact-before native ACL reductions: 81 transitions, 36 valid decreases/no-ops, stale masks, malformed bits and all five mixed-reduction SQL failure positions" << std::endl;
  const auto attempt = [&](int status, int writes = 0) {
    rows.written.clear(); rows.queries.clear(); before = after = ~ObPackedObjPriv{0};
    CHECK(writer.change_native_routine_privileges(expected, 999, 123,
        OB_PRIV_EXECUTE | OB_PRIV_ALTER_ROUTINE, Change::GRANT, true, 43, rows,
        nullptr, before, after) == status);
    CHECK(rows.written.size() == writes && rows.starts == 0 && rows.ends == 0);
    if (status != OB_SUCCESS) CHECK(before == 0 && after == 0);
  };
  grants.clear();
  rows.active = false; attempt(OB_STATE_NOT_MATCH); CHECK(rows.queries.empty()); rows.active = true;
  expected.set_schema_version(43); attempt(OB_INVALID_ARGUMENT); CHECK(rows.queries.empty()); expected.set_schema_version(42);
  catalog.clear(); attempt(OB_ERR_SP_DOES_NOT_EXIST); CHECK(rows.queries.size() == 1);
  catalog = {routine, routine}; attempt(OB_ERR_UNEXPECTED);
  for (int column = 0; column < 9; ++column) {
    catalog = {routine};
    if (column < 6) ++catalog[0].integers[column];
    else catalog[0].strings[column] += "_changed";
    attempt(OB_STATE_NOT_MATCH); CHECK(rows.queries.size() == 1);
  }
  catalog = {routine};
  grants = {grant_row(OBJ_PRIV_ID_EXECUTE, 0), grant_row(OBJ_PRIV_ID_EXECUTE, 1)};
  attempt(OB_INVALID_DATA);
  grants = {grant_row(OBJ_PRIV_ID_EXECUTE, 2)}; attempt(OB_INVALID_DATA);
  grants = {grant_row(OBJ_PRIV_ID_SELECT, 0)}; attempt(OB_INVALID_DATA);
  grants.clear();
  rows.read_status = OB_TIMEOUT; attempt(OB_TIMEOUT); rows.read_status = OB_SUCCESS;
  rows.fail_field = 0; attempt(OB_ERR_NULL_VALUE); rows.fail_field = -1;
  rows.fail_next_at = 1; attempt(OB_TIMEOUT); rows.fail_next_at = -1;
  rows.close_status = OB_TIMEOUT; attempt(OB_TIMEOUT); rows.close_status = OB_SUCCESS;
  const auto read_callback = rows.on_read;
  for (int failure = 0; failure < 4; ++failure) {
    rows.on_read = [&](ExtensionVersionRows &transport) {
      read_callback(transport);
      if (transport.sql.find("FROM oceanbase.__all_objauth ") != std::string::npos) {
        if (failure == 0) transport.read_status = OB_TIMEOUT;
        if (failure == 1) transport.close_status = OB_TIMEOUT;
        if (failure == 2) { transport.fail_field = 0; transport.rows = {grant_row(OBJ_PRIV_ID_EXECUTE, 0)}; }
        if (failure == 3) transport.fail_next_at = 0;
      }
    };
    attempt(failure == 2 ? OB_ERR_NULL_VALUE : OB_TIMEOUT);
    CHECK(rows.queries.size() == 2);
    rows.read_status = rows.close_status = OB_SUCCESS; rows.fail_field = rows.fail_next_at = -1;
  }
  rows.on_read = read_callback;
  for (int failure = 1; failure <= 5; ++failure) {
    rows.fail_write_at = rows.writes + failure;
    attempt(OB_TIMEOUT, failure);
  }
  rows.fail_write_at = -1;
  attempt(OB_SUCCESS, 5);
  CHECK(before == 0 && after == packed(1, 1));
  // Bridge the real SQL writer to the same private ACL view used by the guard.
  // This is deliberately not a simulated commit: the owner still rolls back
  // SQL, while RoutineCatalogSavepoint restores its provisional views.
  auto private_acl = std::make_shared<RoutinePrivilegeOverlay>();
  auto private_schema = std::make_shared<RoutineSchemaOverlay>(private_acl);
  CHECK(private_schema->stage(expected) == OB_SUCCESS);
  ObSEArray<const ObObjPriv *, 1> empty_base;
  ObPackedObjPriv visible = 0;
  {
    RoutineCatalogSavepoint mark(private_schema, private_acl); CHECK(mark.valid());
    grants.clear();
    CHECK(writer.change_native_routine_privileges(expected, 999, 123, OB_PRIV_EXECUTE,
        Change::GRANT, false, 43, rows, nullptr, before, after, private_acl.get()) == OB_SUCCESS);
    CHECK(private_acl->merge_object_privileges(expected, 123, empty_base, visible) == OB_SUCCESS && visible == packed(0, -1));
    grants = {grant_row(OBJ_PRIV_ID_EXECUTE, 0)};
    CHECK(writer.change_native_routine_privileges(expected, 999, 123, OB_PRIV_EXECUTE,
        Change::REVOKE, false, 44, rows, nullptr, before, after, private_acl.get()) == OB_SUCCESS);
    CHECK(private_acl->merge_object_privileges(expected, 123, empty_base, visible) == OB_SUCCESS && visible == 0);
    CHECK(private_acl->has_object_changes(1001, 123)); // Zero remains an explicit override.
    CHECK(mark.rollback() == OB_SUCCESS);
    CHECK(!private_acl->has_object_changes(1001, 123));
  }
  grants.clear(); rows.fail_write_at = rows.writes + 2;
  CHECK(writer.change_native_routine_privileges(expected, 999, 123, OB_PRIV_EXECUTE,
      Change::GRANT, false, 45, rows, nullptr, before, after, private_acl.get()) == OB_TIMEOUT);
  CHECK(before == 0 && after == 0 && !private_acl->has_object_changes(1001, 123));
  rows.fail_write_at = -1;
  {
    RoutineCatalogSavepoint mark(private_schema, private_acl); CHECK(mark.valid());
    CHECK(private_acl->record_object_change(expected, 999, 123, 45, 0, packed(-1, 0)) == OB_SUCCESS);
    // If the view and writer disagree on the previous grant, fail without
    // publishing a new view. SQL may already be written and MUST be rolled back.
    CHECK(writer.change_native_routine_privileges(expected, 999, 123, OB_PRIV_EXECUTE,
        Change::GRANT, false, 46, rows, nullptr, before, after, private_acl.get()) == OB_STATE_NOT_MATCH);
    CHECK(before == 0 && after == 0);
    CHECK(private_acl->merge_object_privileges(expected, 123, empty_base, visible) == OB_SUCCESS && visible == packed(-1, 0));
  }
  CHECK(!private_acl->has_object_changes(1001, 123));
  {
    RoutineCatalogSavepoint mark(private_schema, private_acl); CHECK(mark.valid());
    grants = {grant_row(OBJ_PRIV_ID_EXECUTE, 1)};
    const int writes = rows.writes;
    CHECK(writer.change_native_routine_privileges(expected, 999, 123, OB_PRIV_EXECUTE,
        Change::GRANT, false, 47, rows, nullptr, before, after, private_acl.get()) == OB_SUCCESS);
    CHECK(rows.writes == writes); // No-op SQL can still supply an authoritative private snapshot.
    CHECK(private_acl->merge_object_privileges(expected, 123, empty_base, visible) == OB_SUCCESS && visible == packed(1, -1));
  }
  private_acl->retire();
  const int reads = rows.reads, writes = rows.writes;
  CHECK(writer.change_native_routine_privileges(expected, 999, 123, OB_PRIV_EXECUTE,
      Change::GRANT, false, 48, rows, nullptr, before, after, private_acl.get()) == OB_STATE_NOT_MATCH);
  CHECK(rows.reads == reads && rows.writes == writes && before == 0 && after == 0);
  CHECK(private_acl->merge_object_privileges(expected, 123, empty_base, visible) == OB_STATE_NOT_MATCH && visible == 0);
  std::cout << "PASS: successful transaction writer results publish private ACL, no-op snapshots, savepoint restoration, writer/view failures and retirement; no server commit claims" << std::endl;
  std::cout << "PASS: 108 native object ACL mutation combinations, exact transaction catalog locks, no-op/delta writes, one operation log and read/close/write failure propagation; controlled SQL transport, no commit/concurrency claims" << std::endl;
}

static void variadic_metadata()
{
  ObRoutineInfo value;
  initialize(value);
  CHECK(value.set_native_binding(ObString::make_string("org.seekdb.gis"),
      ObString::make_string("org.seekdb.gis.function.st_linestring"), 1) == OB_SUCCESS);
  ObRoutineParam parameter;
  parameter.set_routine_id(1001); parameter.set_sequence(2); parameter.set_param_position(1);
  parameter.set_param_level(0); parameter.set_subprogram_id(0); parameter.set_schema_version(42);
  parameter.set_param_type(ObGeometryType); parameter.set_in_sp_param_flag(); parameter.set_native_variadic();
  CHECK(parameter.set_param_name(ObString::make_string("points")) == OB_SUCCESS);
  CHECK(value.add_routine_param(parameter) == OB_SUCCESS && value.is_native_binding_valid());
  CHECK(NativeRoutineSignature::variadic(value) && value.get_param_count() == 1);
  int64_t count = -1;
  for (int supplied : {1, 2, 64, 65, 1024}) {
    CHECK(NativeRoutineSignature::call_count(value, supplied, count) == OB_SUCCESS && count == supplied);
    CHECK(NativeRoutineSignature::parameter_index(value, supplied - 1) == 0);
  }
  for (int supplied : {-1, 0, 1025})
    CHECK(NativeRoutineSignature::call_count(value, supplied, count) == OB_ERR_SP_WRONG_ARG_NUM);
  auto bytes = wire(value);
  ObRoutineInfo decoded; int64_t position = 0;
  CHECK(decoded.deserialize(bytes.data(), bytes.size(), position) == OB_SUCCESS);
  ObRoutineParam *last = nullptr;
  CHECK(decoded.get_routine_param(0, last) == OB_SUCCESS && last->is_native_variadic());
  CHECK(last->get_param_type().get_obj_type() == ObGeometryType);
  CHECK(last->get_default_value().empty());
  CHECK(last->set_default_value(ObString::make_string("NULL")) == OB_SUCCESS);
  CHECK(!decoded.is_native_binding_valid());
  char invalid_wire[8192]; position = 0;
  CHECK(decoded.serialize(invalid_wire, sizeof(invalid_wire), position) == OB_INVALID_ARGUMENT);
  CHECK(last->set_default_value(ObString()) == OB_SUCCESS);
  last->set_out_sp_param_flag(); CHECK(!decoded.is_native_binding_valid());
  last->set_in_sp_param_flag(); CHECK(decoded.is_native_binding_valid());
  CHECK(decoded.set_native_binding(ObString(), ObString(), 0) == OB_SUCCESS);
  CHECK(!decoded.is_native_binding_valid()); // Never treat a native array as an ordinary PL scalar.
  CHECK(decoded.assign(value) == OB_SUCCESS);
  decoded.get_routine_params().at(0)->set_native_variadic();
  CHECK(!decoded.is_native_binding_valid()); // Return/multiple variadic parameters rejected.
  CHECK(decoded.assign(value) == OB_SUCCESS);
  parameter.set_flag(SP_PARAM_IN); parameter.set_param_position(2); parameter.set_sequence(3);
  CHECK(parameter.set_param_name(ObString::make_string("after_array")) == OB_SUCCESS);
  CHECK(decoded.add_routine_param(parameter) == OB_SUCCESS);
  CHECK(!decoded.is_native_binding_valid()); // The array must be last.
}

static void overload_views()
{
  const auto make = [](ObRoutineInfo &value, uint64_t id, int64_t slot, ObObjType type) {
    value.reset(); initialize(value);
    value.set_routine_id(id); value.set_overload(slot);
    CHECK(value.set_routine_name(ObString::make_string("overloaded")) == OB_SUCCESS);
    CHECK(value.set_native_binding(ObString::make_string("org.seekdb.example"),
        ObString::make_string("org.seekdb.example.fn"), 1) == OB_SUCCESS);
    ObRoutineParam argument;
    argument.set_routine_id(id); argument.set_sequence(1); argument.set_param_position(1);
    argument.set_subprogram_id(0); argument.set_param_level(0); argument.set_schema_version(42);
    argument.set_param_type(type); argument.set_in_sp_param_flag();
    CHECK(argument.set_param_name(ObString::make_string("arg")) == OB_SUCCESS);
    CHECK(value.add_routine_param(argument) == OB_SUCCESS);
  };
  ObRoutineInfo a, b, base_value;
  make(a, 1010, 10, ObDoubleType); make(b, 1020, 20, ObIntType);
  make(base_value, 1030, 0, ObGeometryType);
  {
    auto service = std::make_unique<MockSchemaService>();
    auto manager = std::make_unique<ObSchemaMgr>();
    CHECK(manager->init() == OB_SUCCESS);
    ObSchemaGetterGuard guard;
    CHECK(MockSchemaService::bind(guard, *service, *manager) == OB_SUCCESS);
    for (auto *routine : {&a, &base_value}) {
      CHECK(MockSchemaService::add(*manager, 100, "overloaded", routine->get_routine_id(),
          ROUTINE_FUNCTION_TYPE, 42, routine->get_overload()) == OB_SUCCESS);
      CHECK(MockSchemaService::cache_routine(guard, *routine) == OB_SUCCESS);
    }
    ObSEArray<const ObRoutineInfo *, 3> candidates;
    CHECK(guard.get_standalone_function_infos(100, ObString::make_string("OVERLOADED"), candidates) == OB_SUCCESS);
    CHECK(candidates.count() == 2 && candidates.at(0) == &base_value && candidates.at(1) == &a);
    a.set_schema_version(43); // A stale/mismatched full-cache entry is not a candidate.
    CHECK(guard.get_standalone_function_infos(100, ObString::make_string("overloaded"), candidates) == OB_STATE_NOT_MATCH && candidates.empty());
    a.set_schema_version(42);
    auto grants = std::make_shared<RoutinePrivilegeOverlay>();
    auto changes = std::make_shared<RoutineSchemaOverlay>(grants);
    CHECK(guard.attach_routine_overlay(changes) == OB_SUCCESS);
    CHECK(changes->stage(b) == OB_SUCCESS);
    CHECK(guard.get_standalone_function_infos(100, ObString::make_string("overloaded"), candidates) == OB_SUCCESS);
    CHECK(candidates.count() == 3 && candidates.at(2)->get_routine_id() == 1020);
    {
      RoutineCatalogSavepoint save(changes, grants); CHECK(save.valid());
      CHECK(changes->erase(100, ObString::make_string("overloaded"), ROUTINE_FUNCTION_TYPE, 1010, 10) == OB_SUCCESS);
      CHECK(guard.get_standalone_function_infos(100, ObString::make_string("overloaded"), candidates) == OB_SUCCESS);
      CHECK(candidates.count() == 2 && candidates.at(0) == &base_value && candidates.at(1)->get_routine_id() == 1020);
      CHECK(save.rollback() == OB_SUCCESS);
    }
    CHECK(guard.get_standalone_function_infos(100, ObString::make_string("overloaded"), candidates) == OB_SUCCESS);
    CHECK(candidates.count() == 3 && candidates.at(1) == &a);
    CHECK(guard.get_standalone_function_infos(101, ObString::make_string("overloaded"), candidates) == OB_SUCCESS && candidates.empty());
    CHECK(guard.get_standalone_function_infos(100, ObString::make_string("overloaded"), candidates) == OB_SUCCESS && candidates.count() == 3);
    changes->retire();
    CHECK(guard.get_standalone_function_infos(100, ObString::make_string("overloaded"), candidates) == OB_STATE_NOT_MATCH && candidates.empty());
  }
  std::string identity, changed;
  CHECK(NativeRoutineSignature::input_identity(a, identity) == OB_SUCCESS);
  ObRoutineInfo modified; CHECK(modified.assign(a) == OB_SUCCESS);
  ObRoutineParam *argument = nullptr;
  CHECK(modified.get_routine_param(0, argument) == OB_SUCCESS);
  CHECK(argument->set_param_name(ObString::make_string("renamed")) == OB_SUCCESS);
  CHECK(argument->set_default_value(ObString::make_string("1")) == OB_SUCCESS);
  argument->set_param_precision(10); argument->set_param_scale(3);
  modified.get_routine_params().at(0)->set_param_type(ObIntType);
  CHECK(modified.set_native_binding(ObString::make_string("org.seekdb.other"),
      ObString::make_string("org.seekdb.other.fn"), 1) == OB_SUCCESS);
  CHECK(NativeRoutineSignature::input_identity(modified, changed) == OB_SUCCESS && changed == identity);
  CHECK(argument->set_default_value(ObString()) == OB_SUCCESS);
  argument->set_native_variadic();
  CHECK(NativeRoutineSignature::input_identity(modified, changed) == OB_SUCCESS && changed != identity);
  make(modified, 1040, 40, ObLongTextType);
  CHECK(modified.get_routine_param(0, argument) == OB_SUCCESS);
  argument->set_param_coll_type(CS_TYPE_UTF8MB4_GENERAL_CI);
  CHECK(NativeRoutineSignature::input_identity(modified, identity) == OB_SUCCESS);
  argument->set_param_coll_type(CS_TYPE_UTF8MB4_BIN);
  CHECK(NativeRoutineSignature::input_identity(modified, changed) == OB_SUCCESS && changed == identity);
  argument->set_param_coll_type(CS_TYPE_BINARY);
  CHECK(NativeRoutineSignature::input_identity(modified, changed) == OB_SUCCESS && changed != identity);
  argument->set_param_type(ObExtendType); changed = "stale";
  CHECK(NativeRoutineSignature::input_identity(modified, changed) == OB_NOT_SUPPORTED && changed.empty());
  argument->set_param_type(ObNumberType); argument->set_param_precision(10); argument->set_param_scale(2);
  CHECK(NativeRoutineSignature::input_identity(modified, identity) == OB_SUCCESS);
  argument->set_param_type(ObDecimalIntType); argument->set_param_precision(30); argument->set_param_scale(5);
  CHECK(NativeRoutineSignature::input_identity(modified, changed) == OB_SUCCESS && changed == identity);

  auto privileges = std::make_shared<RoutinePrivilegeOverlay>();
  auto overlay = std::make_shared<RoutineSchemaOverlay>(privileges);
  CHECK(overlay->stage(a) == OB_SUCCESS && overlay->stage(b) == OB_SUCCESS);
  bool handled = false; const ObRoutineInfo *found = nullptr;
  CHECK(overlay->lookup(100, OB_INVALID_ID, ObString::make_string("OVERLOADED"), 10,
      ROUTINE_FUNCTION_TYPE, handled, found) == OB_SUCCESS && handled && found->get_routine_id() == 1010);
  const auto *borrowed = found; const auto borrowed_wire = wire(*borrowed);
  CHECK(overlay->lookup(100, OB_INVALID_ID, ObString::make_string("overloaded"), 0,
      ROUTINE_FUNCTION_TYPE, handled, found) == OB_SUCCESS && !handled && !found);
  const auto records = overlay->record_count(); const auto bytes = overlay->schema_bytes();
  CHECK(modified.assign(a) == OB_SUCCESS); modified.set_routine_id(1050); modified.set_overload(50);
  CHECK(overlay->stage(modified) == OB_STATE_NOT_MATCH); // Duplicate input signature, different slot/ID.
  CHECK(modified.assign(a) == OB_SUCCESS);
  CHECK(modified.get_routine_param(0, argument) == OB_SUCCESS); argument->set_param_type(ObIntType);
  CHECK(overlay->stage(modified) == OB_STATE_NOT_MATCH); // Cannot change an existing identity's inputs.
  CHECK(overlay->erase(100, ObString::make_string("overloaded"), ROUTINE_FUNCTION_TYPE, 1010) == OB_STATE_NOT_MATCH);
  CHECK(overlay->record_count() == records && overlay->schema_bytes() == bytes);
  ObSEArray<const ObRoutineInfo *, 3> base, family;
  CHECK(base.push_back(&base_value) == OB_SUCCESS);
  CHECK(overlay->merge_function_candidates(100, ObString::make_string("overloaded"), base, family) == OB_SUCCESS);
  CHECK(family.count() == 3 && family.at(0) == &base_value && family.at(1)->get_routine_id() == 1010 &&
        family.at(2)->get_routine_id() == 1020);
  {
    RoutineCatalogSavepoint save(overlay, privileges); CHECK(save.valid());
    CHECK(overlay->erase(100, ObString::make_string("overloaded"), ROUTINE_FUNCTION_TYPE, 1030) == OB_SUCCESS);
    CHECK(overlay->erase(100, ObString::make_string("overloaded"), ROUTINE_FUNCTION_TYPE, 1010, 10) == OB_SUCCESS);
    CHECK(overlay->merge_function_candidates(100, ObString::make_string("overloaded"), base, family) == OB_SUCCESS);
    CHECK(family.count() == 1 && family.at(0)->get_routine_id() == 1020);
    CHECK(overlay->stage(a) == OB_STATE_NOT_MATCH);
    CHECK(modified.assign(a) == OB_SUCCESS); modified.set_routine_id(1060);
    CHECK(overlay->stage(modified) == OB_SUCCESS);
    CHECK(overlay->merge_function_candidates(100, ObString::make_string("overloaded"), base, family) == OB_SUCCESS);
    CHECK(family.count() == 2 && family.at(0)->get_routine_id() == 1060);
    const auto *replacement = family.at(0);
    CHECK(save.rollback() == OB_SUCCESS);
    CHECK(replacement->get_routine_id() == 1060 && wire(*borrowed) == borrowed_wire);
  }
  CHECK(overlay->merge_function_candidates(100, ObString::make_string("OVERLOADED"), base, family) == OB_SUCCESS && family.count() == 3);
  CHECK(modified.assign(a) == OB_SUCCESS); modified.set_routine_id(1070); modified.set_overload(0);
  base.at(0) = &modified;
  CHECK(overlay->merge_function_candidates(100, ObString::make_string("overloaded"), base, family) == OB_STATE_NOT_MATCH && family.empty());
  CHECK(modified.assign(base_value) == OB_SUCCESS);
  CHECK(modified.set_native_binding(ObString(), ObString(), 0) == OB_SUCCESS);
  CHECK(overlay->merge_function_candidates(100, ObString::make_string("overloaded"), base, family) == OB_NOT_SUPPORTED && family.empty());
  base.at(0) = &base_value;
  {
    RoutineSchemaOverlay changed_base;
    CHECK(modified.assign(base_value) == OB_SUCCESS);
    CHECK(modified.get_routine_param(0, argument) == OB_SUCCESS); argument->set_param_type(ObDoubleType);
    CHECK(changed_base.stage(modified) == OB_SUCCESS); // Base guard is not yet supplied here.
    CHECK(changed_base.merge_function_candidates(100, ObString::make_string("overloaded"), base, family) == OB_STATE_NOT_MATCH && family.empty());
    RoutineSchemaOverlay wrong_slot;
    CHECK(modified.assign(base_value) == OB_SUCCESS); modified.set_overload(9);
    CHECK(wrong_slot.stage(modified) == OB_SUCCESS);
    CHECK(wrong_slot.merge_function_candidates(100, ObString::make_string("overloaded"), base, family) == OB_STATE_NOT_MATCH && family.empty());
    RoutineSchemaOverlay overwrite;
    CHECK(modified.assign(base_value) == OB_SUCCESS); modified.set_routine_id(1080);
    CHECK(overwrite.stage(modified) == OB_SUCCESS);
    CHECK(overwrite.merge_function_candidates(100, ObString::make_string("overloaded"), base, family) == OB_STATE_NOT_MATCH && family.empty());
  }
  CHECK(overlay->merge_function_candidates(101, ObString::make_string("overloaded"), base, family) == OB_INVALID_ARGUMENT && family.empty());
  CHECK(overlay->merge_function_candidates(100, ObString::make_string("overloaded"), base, base) == OB_INVALID_ARGUMENT && base.count() == 1);
  overlay->retire();
  CHECK(overlay->merge_function_candidates(100, ObString::make_string("overloaded"), base, family) == OB_STATE_NOT_MATCH && family.empty());
  CHECK(wire(*borrowed) == borrowed_wire);
  std::cout << "PASS: native input identities and owned overload-family merge/drop/savepoint/lifetime; no persistent admission or ACL claims" << std::endl;
}

static void native_initial_acl_view()
{
  using namespace oceanbase::share;
  auto manager = std::make_unique<ObSchemaMgr>(); CHECK(manager->init() == OB_SUCCESS);
  auto service = std::make_unique<MockSchemaService>();
  ObSchemaGetterGuard guard; CHECK(MockSchemaService::bind(guard, *service, *manager) == OB_SUCCESS);
  ObDatabaseSchema database; database.set_database_id(100); database.set_schema_version(42);
  CHECK(database.set_database_name("native_db") == OB_SUCCESS);
  CHECK(MockSchemaService::cache_database(guard, database) == OB_SUCCESS);
  ObUserInfo owner; owner.set_user_id(123); owner.set_schema_version(42);
  CHECK(owner.set_user_name("owner") == OB_SUCCESS && owner.set_host("localhost") == OB_SUCCESS);
  CHECK(MockSchemaService::cache_user(guard, owner) == OB_SUCCESS);
  auto privileges = std::make_shared<RoutinePrivilegeOverlay>(100, 123);
  auto view = std::make_shared<RoutineSchemaOverlay>(privileges);
  CHECK(guard.attach_routine_overlay(view) == OB_SUCCESS);
  ObRoutineInfo first, second;
  initialize(first);
  CHECK(first.set_native_binding(ObString::make_string("org.seekdb.gis"),
      ObString::make_string("org.seekdb.gis.area"), 1) == OB_SUCCESS);
  CHECK(second.assign(first) == OB_SUCCESS); second.set_routine_id(1002); second.set_overload(7);
  ObRoutineParam parameter;
  parameter.set_routine_id(1002); parameter.set_schema_version(42); parameter.set_param_position(1);
  parameter.set_sequence(1); parameter.set_subprogram_id(0); parameter.set_param_level(0);
  parameter.set_param_type(ObGeometryType); parameter.set_in_sp_param_flag();
  CHECK(parameter.set_param_name("geometry") == OB_SUCCESS && second.add_routine_param(parameter) == OB_SUCCESS);
  ObSessionPrivInfo session; session.user_id_ = 123;
  ObSEArray<uint64_t, 1> enabled;
  const auto check = [&](const ObRoutineInfo &routine, int status) {
    CHECK(guard.check_native_routine_priv(session, enabled, routine, OB_PRIV_EXECUTE) == status);
  };
  const auto execute = native_routine_revoke_test::packed(1, 0);
  const auto both = native_routine_revoke_test::packed(3, 0);
  ObSEArray<const ObObjPriv *, 1> base;
  ObPackedObjPriv result = 0;
  RoutineCatalogSavepoint outer(view, privileges); CHECK(outer.valid());
  CHECK(view->stage(first) == OB_SUCCESS && privileges->record_create(first, true) == OB_SUCCESS);
  CHECK(privileges->merge_object_privileges(first, 123, base, result) == OB_SUCCESS && result == both);
  CHECK(privileges->record_create(first, true) == OB_STATE_NOT_MATCH);
  check(first, OB_SUCCESS);
  bool handled = true; ObPrivSet named = ~ObPrivSet{0};
  CHECK(privileges->lookup(100, first.get_routine_name(), ROUTINE_FUNCTION_TYPE, 123, true, &first,
      handled, named) == OB_SUCCESS && !handled && named == 0);
  CHECK(view->stage(second) == OB_SUCCESS && privileges->record_create(second, false) == OB_SUCCESS);
  check(second, OB_ERR_NO_ROUTINE_PRIVILEGE); check(first, OB_SUCCESS);
  CHECK(guard.check_native_routine_priv(session, enabled, second, OB_PRIV_EXECUTE | OB_PRIV_GRANT) == OB_SUCCESS);
  {
    RoutineCatalogSavepoint mutation(view, privileges); CHECK(mutation.valid());
    CHECK(privileges->record_object_change(first, 123, 123, 44, both, execute) == OB_SUCCESS);
    CHECK(privileges->record_object_change(first, 123, 123, 45, execute, 0) == OB_SUCCESS);
    check(first, OB_ERR_NO_ROUTINE_PRIVILEGE);
  }
  check(first, OB_SUCCESS);
  {
    RoutineCatalogSavepoint dropped(view, privileges); CHECK(dropped.valid());
    CHECK(view->erase(100, first.get_routine_name(), ROUTINE_FUNCTION_TYPE, 1001) == OB_SUCCESS);
    CHECK(privileges->record_drop(first) == OB_SUCCESS);
    check(first, OB_ERR_SP_DOES_NOT_EXIST); check(second, OB_ERR_NO_ROUTINE_PRIVILEGE);
    CHECK(privileges->record_create(first, true) == OB_STATE_NOT_MATCH); // No identity reuse after DROP.
  }
  check(first, OB_SUCCESS);
  CHECK(outer.rollback() == OB_SUCCESS);
  CHECK(!privileges->has_object_changes(1001, 123) && !privileges->has_object_changes(1002, 123));
  // Reusing inactive map slots after rollback is permitted; prior active ACL
  // mutations, however, cannot be overwritten by a speculative CREATE.
  CHECK(privileges->record_object_change(first, 123, 123, 44, 0, execute) == OB_SUCCESS);
  CHECK(privileges->record_create(first, true) == OB_STATE_NOT_MATCH);
  CHECK(privileges->merge_object_privileges(first, 123, base, result) == OB_SUCCESS && result == execute);
  std::cout << "PASS: native CREATE private ACL is object-keyed, automatic policy and overloads isolated, no name grant, duplicate/reused IDs rejected, and create/mutation/drop/savepoint rollback preserved" << std::endl;
}

static void native_owner_authority()
{
  using namespace oceanbase::share;
  auto manager = std::make_unique<ObSchemaMgr>(); CHECK(manager->init() == OB_SUCCESS);
  auto service = std::make_unique<MockSchemaService>();
  ObSchemaGetterGuard guard; CHECK(MockSchemaService::bind(guard, *service, *manager) == OB_SUCCESS);
  ObDatabaseSchema database; database.set_database_id(100); database.set_schema_version(42);
  CHECK(database.set_database_name("native_db") == OB_SUCCESS);
  CHECK(MockSchemaService::cache_database(guard, database) == OB_SUCCESS);
  ObUserInfo actor, owner, role;
  uint64_t id = 123;
  for (auto *principal : {&actor, &owner, &role}) {
    principal->set_user_id(id++); principal->set_schema_version(42);
    CHECK(principal->set_user_name("ownership_fixture") == OB_SUCCESS);
    CHECK(principal->set_host("localhost") == OB_SUCCESS);
    CHECK(MockSchemaService::cache_user(guard, *principal) == OB_SUCCESS);
  }
  role.set_type(OB_ROLE);
  ObUserInfo without_roles; CHECK(without_roles.assign(actor) == OB_SUCCESS);
  ObRoutineInfo routine; initialize(routine); routine.set_owner_id(124); routine.set_overload(77);
  CHECK(routine.set_native_binding(ObString::make_string("org.seekdb.gis"),
      ObString::make_string("org.seekdb.gis.area"), 1) == OB_SUCCESS);
  auto privileges = std::make_shared<RoutinePrivilegeOverlay>();
  auto view = std::make_shared<RoutineSchemaOverlay>(privileges);
  CHECK(view->stage(routine) == OB_SUCCESS && guard.attach_routine_overlay(view) == OB_SUCCESS);
  ObSessionPrivInfo session; session.user_id_ = 124;
  ObSEArray<uint64_t, 4> enabled;
  ObSEArray<ObObjPriv, 4> acl;
  constexpr ObPrivSet both = OB_PRIV_EXECUTE | OB_PRIV_ALTER_ROUTINE;
  const auto check = [&](ObPrivSet rights, int status, uint64_t grantor = OB_INVALID_ID) {
    CHECK(guard.check_native_routine_priv(session, enabled, routine, rights,
        grantor == OB_INVALID_ID ? nullptr : &acl, grantor) == status);
  };
  const auto select = [&](int status, uint64_t expected = OB_INVALID_ID) {
    NativeRoutineGrantors selected; selected.execute_ = selected.alter_ = 999;
    CHECK(guard.select_native_routine_grantors(session, enabled, routine, both, acl, selected) == status);
    CHECK(selected.execute_ == expected && selected.alter_ == expected);
  };
  check(OB_PRIV_EXECUTE, OB_ERR_NO_ROUTINE_PRIVILEGE); // Ownership is not an unrevocable EXECUTE grant.
  check(OB_PRIV_ALTER_ROUTINE, OB_SUCCESS);
  check(both | OB_PRIV_GRANT, OB_SUCCESS);
  select(OB_SUCCESS, 124); // Empty ACL still permits the owner to grant to itself or others.
  const auto execute = native_routine_revoke_test::packed(1, 0);
  CHECK(MockSchemaService::grant_object(*manager, 1001, 124, 124, execute) == OB_SUCCESS);
  check(OB_PRIV_EXECUTE, OB_SUCCESS);
  {
    RoutineCatalogSavepoint savepoint(view, privileges); CHECK(savepoint.valid());
    CHECK(privileges->record_object_change(routine, 124, 124, 43, execute, 0) == OB_SUCCESS);
    check(OB_PRIV_EXECUTE, OB_ERR_NO_ROUTINE_PRIVILEGE);
    check(both | OB_PRIV_GRANT, OB_SUCCESS); select(OB_SUCCESS, 124);
  }
  check(OB_PRIV_EXECUTE, OB_SUCCESS);
  CHECK(MockSchemaService::revoke_object(*manager, 1001, 124, 124) == OB_SUCCESS);
  check(OB_PRIV_EXECUTE, OB_ERR_NO_ROUTINE_PRIVILEGE);
  session.user_id_ = 123; session.user_priv_set_ = OB_PRIV_SUPER;
  check(OB_PRIV_EXECUTE, OB_ERR_NO_ROUTINE_PRIVILEGE); select(OB_ERR_NO_ROUTINE_PRIVILEGE);
  actor.set_priv_set(OB_PRIV_SUPER);
  check(OB_PRIV_EXECUTE, OB_SUCCESS); select(OB_SUCCESS, 124);
  check(both | OB_PRIV_GRANT, OB_SUCCESS, 124);
  check(both | OB_PRIV_GRANT, OB_ERR_NO_ROUTINE_PRIVILEGE, 123); // Do not relabel owner grants as SUPER's own grants.
  auto malformed = native_routine_revoke_test::row({124, 123, 1, 1}); malformed.set_obj_id(999);
  CHECK(acl.push_back(malformed) == OB_SUCCESS); select(OB_INVALID_DATA); acl.reset();
  actor.set_priv_set(0); select(OB_ERR_NO_ROUTINE_PRIVILEGE); // Current SUPER removal is effective immediately.
  role.set_priv_set(OB_PRIV_SUPER);
  CHECK(actor.add_role_id(125) == OB_SUCCESS && enabled.push_back(125) == OB_SUCCESS);
  select(OB_ERR_NO_ROUTINE_PRIVILEGE); check(OB_PRIV_EXECUTE, OB_ERR_NO_ROUTINE_PRIVILEGE);
  CHECK(actor.assign(without_roles) == OB_SUCCESS); enabled.reset(); role.set_priv_set(0);
  owner.set_type(OB_ROLE);
  CHECK(actor.add_role_id(124) == OB_SUCCESS);
  select(OB_ERR_NO_ROUTINE_PRIVILEGE); // Membership alone is not an enabled role.
  CHECK(enabled.push_back(124) == OB_SUCCESS);
  select(OB_SUCCESS, 124); check(OB_PRIV_ALTER_ROUTINE, OB_SUCCESS);
  check(OB_PRIV_EXECUTE, OB_ERR_NO_ROUTINE_PRIVILEGE);
  CHECK(acl.push_back(native_routine_revoke_test::row({124, 123, 3, 3})) == OB_SUCCESS);
  select(OB_SUCCESS, 124); // Owning-role identity takes precedence over an actor ACL grant.
  acl.reset();
  check(both | OB_PRIV_GRANT, OB_SUCCESS, 124);
  CHECK(actor.assign(without_roles) == OB_SUCCESS);
  select(OB_ERR_NO_ROUTINE_PRIVILEGE);
  check(both | OB_PRIV_GRANT, OB_ERR_NO_ROUTINE_PRIVILEGE, 124); // Stale enabled-role list is not authority.
  ObSEArray<oceanbase::rootserver::NativeRoutineGrantRoot, 4> roots;
  CHECK(oceanbase::rootserver::NativeRoutineRevokePlan::collect_roots(guard, routine, acl, roots) == OB_SUCCESS);
  CHECK(roots.count() == 1 && roots.at(0).principal_ == 124 && roots.at(0).rights_ == both);
  for (bool option_only : {false, true}) {
    using namespace oceanbase::rootserver;
    CHECK(acl.push_back(native_routine_revoke_test::row({124, 124, 3, 3})) == OB_SUCCESS);
    CHECK(acl.push_back(native_routine_revoke_test::row({124, 125, 1, 1})) == OB_SUCCESS);
    ObSEArray<NativeRoutineRevokeRequest, 1> requests;
    ObSEArray<NativeRoutineRevokeDelta, 1> changes;
    CHECK(requests.push_back({124, 124, both, option_only}) == OB_SUCCESS);
    CHECK(NativeRoutineRevokePlan::build(routine, acl, roots, requests,
        NativeRoutineRevokePlan::Behavior::RESTRICT, changes) == OB_SUCCESS);
    CHECK(changes.count() == 1 && changes.at(0).grantee_ == 124 && changes.at(0).grantor_ == 124 &&
        changes.at(0).after_ == (option_only ? native_routine_revoke_test::packed(3, 0) : 0));
    acl.reset(); // Removing owner self-ACL never disconnects grants issued by that owner.
  }
  actor.set_priv_set(OB_PRIV_SUPER);
  CHECK(acl.push_back(native_routine_revoke_test::row({123, 125, 1, 1})) == OB_SUCCESS);
  CHECK(oceanbase::rootserver::NativeRoutineRevokePlan::collect_roots(guard, routine, acl, roots) == OB_SUCCESS);
  CHECK(roots.count() == 1 && roots.at(0).principal_ == 124); // SUPER is mapped to owner, not another intrinsic root.
  acl.reset();
  ObRoutineInfo changed; CHECK(changed.assign(routine) == OB_SUCCESS);
  changed.set_schema_version(43); changed.set_owner_id(999);
  CHECK(view->stage(changed) == OB_SUCCESS);
  select(OB_SCHEMA_EAGAIN);
  CHECK(routine.assign(changed) == OB_SUCCESS);
  select(OB_USER_NOT_EXIST); // Even SUPER cannot create grants for a missing owner.
  CHECK(oceanbase::rootserver::NativeRoutineRevokePlan::collect_roots(guard, routine, acl, roots) == OB_USER_NOT_EXIST && roots.empty());
  std::cout << "PASS: native owner grant options independent of revocable EXECUTE, ownership ALTER, current owning-role membership, SUPER-to-owner provenance, stale session/owner rejection and owner-root collection; no SQL endpoint claims" << std::endl;
}

int main()
{
  CHECK(ObCharset::init_charset() == OB_SUCCESS);
  value_and_reader();
  family_index();
  overload_selection();
  variadic_metadata();
  overload_views();
  native_object_privileges();
  writer_and_tables();
  native_privilege_mutation();
  native_privilege_transaction_authority();
  native_owner_authority();
  native_initial_acl_view();
  { ObRoutineInfo prototype; initialize(prototype); native_routine_grant_test::run(prototype); }
  { ObRoutineInfo prototype; initialize(prototype); native_routine_grant_plan_test::run(prototype); }
  { ObRoutineInfo prototype; initialize(prototype); native_routine_acl_snapshot_test::run(prototype); }
  { ObRoutineInfo prototype; initialize(prototype); native_routine_acl_versions_test::run(prototype); }
  { ObRoutineInfo prototype; initialize(prototype); native_routine_revoke_test::run(prototype); }
  { ObRoutineInfo prototype; initialize(prototype); native_routine_revoke_writer_test::run(prototype); }
  native_privilege_drop_snapshot();
  native_routine_dependency_test::run();
  std::cout << "PASS: native routine schema ownership, wire, legacy rows, reader, DML, generated tables, transactional dependency admission/removal and RESTRICT blockers; no live catalog execution claims" << std::endl;
}
