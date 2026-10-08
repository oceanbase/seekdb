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
#pragma once
#include "rootserver/pl_ddl/native_routine_grant_writer.h"
#include "rootserver/pl_ddl/native_routine_acl_version_reservation.h"
#include "share/ob_rpc_struct.h"
#include "share/schema/routine_catalog_savepoint.h"
#include "routine_overlay_guard_fixture.h"

// The actual host batch writer, version dispatch and ACL SQL services run here.
// Metadata, version allocation and SQL rows/effects are controlled; this is not
// a SQL GRANT endpoint, transaction isolation, commit or recovery test.
namespace native_routine_grant_test {
using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::share::schema;

class Service final : public MockSchemaService {
public:
  ~Service() override { schema_service_ = nullptr; }
  void bind_sql(ObSchemaService &sql) { schema_service_ = &sql; }
  int get_runtime_refreshed_schema_version(int64_t &version, bool = false) const override
  { version = 42; return OB_SUCCESS; }
};
class Versions final : public ObSchemaServiceSQLImpl {
public:
  Versions(ObMySQLProxy &proxy, ObMultiVersionSchemaService &service)
      : ObSchemaServiceSQLImpl(nullptr, proxy, service) {}
  int calls = 0, fail_at = -1, throw_at = -1;
  bool fixed = false;
  int gen_new_schema_version(int64_t refreshed, int64_t &version) override {
    CHECK(refreshed == 42);
    ++calls; version = fixed ? 5001 : 5000 + calls;
    if (calls == throw_at) throw std::bad_alloc();
    return calls == fail_at ? OB_TIMEOUT : OB_SUCCESS;
  }
};

inline void run(const ObRoutineInfo &prototype)
{
  using Row = ExtensionVersionRows::Row;
  for (int scenario = 0; scenario < 47; ++scenario) {
    auto manager = std::make_unique<ObSchemaMgr>(); CHECK(manager->init() == OB_SUCCESS);
    auto service = std::make_unique<Service>();
    ObSchemaGetterGuard guard; CHECK(MockSchemaService::bind(guard, *service, *manager) == OB_SUCCESS);
    ObMySQLProxy proxy;
    auto versions = std::make_unique<Versions>(proxy, *service); service->bind_sql(*versions);
    ObDatabaseSchema database;
    database.set_database_id(100); database.set_schema_version(42);
    CHECK(database.set_database_name("native_db") == OB_SUCCESS);
    CHECK(MockSchemaService::cache_database(guard, database) == OB_SUCCESS);
    ObUserInfo user, role, first, second;
    uint64_t id = 123;
    for (auto *principal : {&user, &role, &first, &second}) {
      if (id == 125) ++id;
      principal->set_user_id(id++); principal->set_schema_version(42);
      CHECK(principal->set_user_name("grant_fixture") == OB_SUCCESS);
      CHECK(principal->set_host("localhost") == OB_SUCCESS);
      CHECK(MockSchemaService::cache_user(guard, *principal) == OB_SUCCESS);
    }
    role.set_type(OB_ROLE); CHECK(user.add_role_id(124) == OB_SUCCESS);
    if (scenario == 1) user.set_priv_set(OB_PRIV_SUPER);
    ObRoutineInfo expected; CHECK(expected.assign(prototype) == OB_SUCCESS); expected.set_overload(77);
    // Authorization denial scenarios exercise a non-owner. Ownership is now
    // intrinsically delegable, not dependent on these fixture ACL rows.
    expected.set_owner_id(998);
    ObUserInfo owner;
    owner.set_user_id(998); owner.set_schema_version(42);
    CHECK(owner.set_user_name("owner") == OB_SUCCESS && owner.set_host("localhost") == OB_SUCCESS);
    CHECK(MockSchemaService::cache_user(guard, owner) == OB_SUCCESS);
    CHECK(expected.set_native_binding(ObString::make_string("org.seekdb.gis"),
        ObString::make_string("org.seekdb.gis.area"), 1) == OB_SUCCESS);
    auto privileges = std::make_shared<RoutinePrivilegeOverlay>(100, 123);
    auto view = std::make_shared<RoutineSchemaOverlay>(privileges);
    CHECK(view->stage(expected) == OB_SUCCESS);
    if (scenario == 27) {
      CHECK(MockSchemaService::add(*manager, 100, "area_alias", 1001,
          ROUTINE_FUNCTION_TYPE, 42, 77) == OB_SUCCESS);
      CHECK(MockSchemaService::cache_routine(guard, expected) == OB_SUCCESS);
    }
    else CHECK(guard.attach_routine_overlay(view) == OB_SUCCESS);
    oceanbase::obcall::NativeRoutinePrivilegeTarget target;
    CHECK(target.assign(expected, true) == OB_SUCCESS);
    ObSEArray<uint64_t, 4> enabled, recipients;
    if (scenario != 6) CHECK(enabled.push_back(124) == OB_SUCCESS);
    CHECK(target.bind_actor(scenario == 21 ? 124 : scenario == 22 ? 999 : 123, enabled) == OB_SUCCESS);
    for (uint64_t recipient : {127, 126, 127}) CHECK(recipients.push_back(recipient) == OB_SUCCESS);
    if (scenario == 4 || scenario == 5) CHECK(recipients.push_back(scenario == 4 ? 999 : 0) == OB_SUCCESS);
    if (scenario == 7) target.clear_actor();
    if (scenario == 9) versions->fixed = true;
    if (scenario == 10) versions->fail_at = 3;
    if (scenario == 11) versions->throw_at = 3;
    const ObPrivSet rights = scenario == 2 ? OB_PRIV_EXECUTE : scenario == 23 ? 0 :
        (OB_PRIV_EXECUTE | OB_PRIV_ALTER_ROUTINE | (scenario == 24 ? OB_PRIV_GRANT : 0));
    ObPackedObjPriv execute = 0;
    CHECK(ObPrivPacker::raw_obj_priv_to_packed_info(NO_OPTION, OBJ_PRIV_ID_EXECUTE, execute) == OB_SUCCESS);
    if (scenario == 17) CHECK(privileges->record_object_change(expected, 123, 127, 43, 0, execute, 123) == OB_SUCCESS);
    auto publish_view = view;
    auto publish_privileges = privileges;
    if (scenario == 18) publish_view = std::make_shared<RoutineSchemaOverlay>(privileges);
    if (scenario == 19) publish_privileges = std::make_shared<RoutinePrivilegeOverlay>(100, 123);
    if (scenario == 20) view->retire();
    if (scenario == 27 || scenario == 42) { publish_view.reset(); publish_privileges.reset(); }
    if (scenario == 28) publish_privileges.reset();
    // Each group is visited once. Current rows are explicit fixtures rather
    // than an attempted SQL interpreter; unrelated groups remain independent.
    const auto existing = [&](uint64_t recipient, uint64_t grantor) {
      return scenario == 15 || (scenario == 16 && !(recipient == 127 && grantor == 124)) ||
          (scenario == 29 && recipient == 127);
    };
    Row routine;
    routine.integers = {{0, 100}, {1, int64_t(expected.get_owner_id())}, {2, 77}, {3, scenario == 12 ? 41 : 42},
        {4, ROUTINE_FUNCTION_TYPE}, {5, 1}};
    routine.strings = {{6, "area_alias"}, {7, "org.seekdb.gis"}, {8, "org.seekdb.gis.area"}};
    const auto acl_row = [](uint64_t grantee, uint64_t grantor, int privilege, int option) {
      Row row; row.integers = {{0, int64_t(grantee)}, {1, int64_t(grantor)},
          {2, OBJ_LEVEL_FOR_TAB_PRIV}, {3, privilege}, {4, option}}; return row;
    };
    ExtensionVersionRows rows;
    rows.active = scenario != 8; rows.write_status = OB_SUCCESS;
    if (scenario >= 30 && scenario < 42) rows.fail_write_at = scenario - 29;
    rows.affected_rows = [&](const std::string &) {
      CHECK(versions->calls == (scenario == 1 || scenario == 2 ? 2 : 4));
      CHECK(!privileges->has_object_changes(1001, 126));
      CHECK(privileges->has_object_changes(1001, 127) == (scenario == 17));
      return int64_t{1}; // No successful SQL prefix is published into the view.
    };
    int snapshots = 0;
    std::vector<std::pair<uint64_t, uint64_t>> keys;
    rows.on_read = [&](ExtensionVersionRows &transport) {
      transport.rows.clear(); transport.read_status = transport.close_status = OB_SUCCESS;
      if (transport.sql.find("SELECT database_id,owner_id") == 0) transport.rows = {routine};
      else if (transport.sql.find("SELECT grantee_id,grantor_id,col_id") == 0) {
        ++snapshots;
        if (scenario != 13) transport.rows.push_back(acl_row(123, 999, OBJ_PRIV_ID_EXECUTE, 1));
        if (!(scenario == 14 && snapshots >= 3)) transport.rows.push_back(acl_row(124, 999, OBJ_PRIV_ID_ALTER, 1));
        for (uint64_t grantee : {126, 127}) for (uint64_t grantor : {123, 124})
          if (existing(grantee, grantor)) transport.rows.push_back(acl_row(grantee, grantor,
              grantor == 123 ? OBJ_PRIV_ID_EXECUTE : OBJ_PRIV_ID_ALTER, 0));
        if (scenario == 25) transport.read_status = OB_TIMEOUT;
        if (scenario == 26 && snapshots == 3) transport.close_status = OB_TIMEOUT;
      } else {
        CHECK(transport.sql.find("SELECT priv_id,priv_option") == 0);
        const uint64_t grantee = transport.sql.find("grantee_id=126 ") != std::string::npos ? 126 : 127;
        const uint64_t grantor = scenario == 1 ? 998 :
            transport.sql.find("grantor_id=123 ") != std::string::npos ? 123 : 124;
        CHECK(transport.sql.find("grantor_id=" + std::to_string(grantor) + " AND grantee_id=" +
            std::to_string(grantee) + " ") != std::string::npos);
        keys.emplace_back(grantee, grantor);
        if (existing(grantee, grantor)) {
          Row row; row.integers = {{0, grantor == 123 ? OBJ_PRIV_ID_EXECUTE : OBJ_PRIV_ID_ALTER}, {1, 0}};
          transport.rows.push_back(row);
        }
        if ((scenario == 43 || scenario == 44) && grantee == 126 && grantor == 123) {
          // Exact-key state diverges from the initial full locked snapshot.
          // Both a changed write and an unexpected no-op must invalidate the
          // plan before publishing ANY private ACL prefix.
          Row row; row.integers = {{0, scenario == 43 ? OBJ_PRIV_ID_ALTER : OBJ_PRIV_ID_EXECUTE}, {1, 0}};
          transport.rows.push_back(row);
        }
      }
    };
    int status = OB_SUCCESS, writes = scenario == 1 ? 10 : scenario == 2 ? 6 : 12;
    int allocations = scenario == 1 || scenario == 2 ? 2 : 4;
    switch (scenario) {
      case 4: case 22: status = OB_USER_NOT_EXIST; writes = allocations = 0; break;
      case 5: case 7: case 18: case 21: case 23: case 24: case 28: case 42:
        status = OB_INVALID_ARGUMENT; writes = allocations = 0; break;
      case 6: case 13: status = OB_ERR_NO_ROUTINE_PRIVILEGE; writes = allocations = 0; break;
      case 8: case 12: case 19: case 20: status = OB_STATE_NOT_MATCH; writes = allocations = 0; break;
      case 9: status = OB_STATE_NOT_MATCH; allocations = 2; writes = 0; break;
      case 10: status = OB_TIMEOUT; allocations = 3; writes = 0; break;
      case 11: status = OB_ALLOCATE_MEMORY_FAILED; allocations = 3; writes = 0; break;
      case 14: status = OB_ERR_NO_ROUTINE_PRIVILEGE; writes = 3; break;
      case 15: writes = 0; break;
      case 16: writes = 3; break;
      case 17: status = OB_STATE_NOT_MATCH; break;
      case 25: status = OB_TIMEOUT; writes = allocations = 0; break;
      case 26: status = OB_TIMEOUT; writes = 3; break;
      case 29: writes = 6; break;
      case 43: status = OB_STATE_NOT_MATCH; writes = 3; break;
      case 44: status = OB_STATE_NOT_MATCH; writes = 0; break;
      case 45: break;
      case 46: status = OB_STATE_NOT_MATCH; writes = 0; break;
      default: if (scenario >= 30) { status = OB_TIMEOUT; writes = scenario - 29; } break;
    }
    RoutineCatalogSavepoint caller(view, privileges);
    oceanbase::rootserver::NativeRoutineAclVersionReservation reserved;
    if (scenario >= 45) {
      ObSEArray<oceanbase::rootserver::NativeRoutineAclChange,4> changes;
      for (uint64_t grantee : {126,127}) for (uint64_t grantor : {123,124}) {
        ObPackedObjPriv after = 0;
        CHECK(ObPrivPacker::raw_obj_priv_to_packed_info(scenario == 46 ? GRANT_OPTION : NO_OPTION,
            grantor == 123 ? OBJ_PRIV_ID_EXECUTE : OBJ_PRIV_ID_ALTER,after) == OB_SUCCESS);
        CHECK(changes.push_back({grantor,grantee,0,after}) == OB_SUCCESS);
      }
      CHECK(oceanbase::rootserver::NativeRoutineAclVersionReservation::reserve(*service,rows,target,
          oceanbase::rootserver::NativeRoutineAclVersionReservation::Kind::GRANT,changes,reserved) == OB_SUCCESS);
      CHECK(versions->calls == 4 && rows.writes == 0);
    }
    oceanbase::rootserver::NativeRoutineGrantWriter writer(*service, guard, rows);
    int64_t changed = -1;
    const int result = writer.grant(target, recipients, rights, scenario == 3, nullptr, changed,
        publish_view, publish_privileges, scenario >= 45 ? &reserved : nullptr);
    CHECK(reserved.count() == 0);
    if (result != status || rows.writes != writes || versions->calls != allocations)
      std::cerr << "native grant batch scenario=" << scenario << " result=" << result << "/" << status
          << " writes=" << rows.writes << "/" << writes << " versions=" << versions->calls << "/" << allocations << std::endl;
    CHECK(result == status && rows.writes == writes && versions->calls == allocations);
    if (scenario == 4 || scenario == 5 || scenario == 7 || scenario == 8 ||
        (scenario >= 18 && scenario <= 24) || scenario == 28 || scenario == 42)
      CHECK(rows.queries.empty());
    CHECK(changed == (result != OB_SUCCESS || !writes ? 0 : scenario == 29 ? 5002 : 5000 + allocations));
    CHECK(rows.starts == 0 && rows.ends == 0);
    if (result == OB_SUCCESS) {
      const std::vector<std::pair<uint64_t, uint64_t>> all{{126, 123}, {126, 124}, {127, 123}, {127, 124}};
      const uint64_t single_source = scenario == 1 ? 998 : 123;
      const std::vector<std::pair<uint64_t, uint64_t>> merged{{126, single_source}, {127, single_source}};
      CHECK(keys == (scenario == 1 || scenario == 2 ? merged : all));
      for (uint64_t recipient : {126, 127}) CHECK(privileges->has_object_changes(1001, recipient) == (scenario != 27));
      int logs = 0;
      for (const auto &statement : rows.written) if (statement.find("oceanbase.__all_ddl_operation") != std::string::npos) ++logs;
      CHECK(logs == (scenario == 15 ? 0 : scenario == 16 ? 1 : scenario == 29 ? 2 : allocations));
      const auto normalize = [](std::string statement) {
        statement.erase(std::remove_if(statement.begin(), statement.end(),
            [](char c) { return c == ' ' || c == '\n' || c == '\t' || c == '`'; }), statement.end());
        return statement;
      };
      size_t offset = 0;
      for (size_t group = 0; group < keys.size(); ++group) {
        const auto key = keys[group];
        if (existing(key.first, key.second)) continue;
        const std::string identity = "VALUES(1001," + std::to_string(uint64_t(ObObjectType::FUNCTION)) +
            "," + std::to_string(OBJ_LEVEL_FOR_TAB_PRIV) + "," + std::to_string(key.second) +
            "," + std::to_string(key.first) + ",";
        const std::string version = std::to_string(5001 + group);
        for (const int raw : {OBJ_PRIV_ID_EXECUTE, OBJ_PRIV_ID_ALTER}) {
          if (scenario != 1 && raw != (key.second == 123 ? OBJ_PRIV_ID_EXECUTE : OBJ_PRIV_ID_ALTER)) continue;
          const auto current = normalize(rows.written.at(offset++));
          const auto history = normalize(rows.written.at(offset++));
          CHECK(current.find("REPLACEINTOoceanbase.__all_objauth(") == 0);
          CHECK(current.find(identity + std::to_string(raw) + "," + (scenario == 3 ? "1," : "0,")) != std::string::npos);
          CHECK(history.find("INSERTINTOoceanbase.__all_objauth_history(") == 0);
          CHECK(history.find("," + version + ",0)") != std::string::npos);
        }
        const auto operation = normalize(rows.written.at(offset++));
        CHECK(operation.find("oceanbase.__all_ddl_operation(") != std::string::npos);
        CHECK(operation.find(version) != std::string::npos);
      }
      CHECK(offset == rows.written.size());
    } else {
      CHECK(!privileges->has_object_changes(1001, 126));
      CHECK(privileges->has_object_changes(1001, 127) == (scenario == 17));
    }
    const size_t queries = rows.queries.size();
    changed = -1;
    CHECK(writer.grant(target, recipients, rights, false, nullptr, changed) == OB_INIT_TWICE && changed == 0);
    CHECK(rows.queries.size() == queries && rows.writes == writes && versions->calls == allocations);
    if (caller.valid()) CHECK(caller.rollback() == OB_SUCCESS);
    CHECK(!privileges->has_object_changes(1001, 126));
    CHECK(privileges->has_object_changes(1001, 127) == (scenario == 17));
  }
  std::cout << "PASS: 47 host native GRANT batch scenarios: per-grantor versions, pre-reserved execution/mismatch, full recipient preflight, exact-key writes, grant rechecks, no-op/max-change version, planned image divergence, private publication rollback and all 12 SQL failure positions; no endpoint/commit claims" << std::endl;
}
}
