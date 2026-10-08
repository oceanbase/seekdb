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
#include "rootserver/pl_ddl/native_routine_revoke_writer.h"
#include "native_routine_grant_fixture.h"
#include "native_routine_revoke_fixture.h"

// Runs the real planner, metadata-root collector, host writer and decrease-only
// SQL service. Rows, SQL effects and version allocation are controlled. This
// is not a SQL REVOKE endpoint, isolation, commit, restart or recovery test.
namespace native_routine_revoke_writer_test {
using namespace native_routine_revoke_test;
using Row = ExtensionVersionRows::Row;

inline void run(const ObRoutineInfo &prototype)
{
  for (int scenario = 0; scenario < 66; ++scenario) {
    auto manager = std::make_unique<ObSchemaMgr>(); CHECK(manager->init() == OB_SUCCESS);
    auto service = std::make_unique<native_routine_grant_test::Service>();
    ObSchemaGetterGuard guard; CHECK(MockSchemaService::bind(guard,*service,*manager) == OB_SUCCESS);
    ObMySQLProxy proxy;
    auto versions = std::make_unique<native_routine_grant_test::Versions>(proxy,*service); service->bind_sql(*versions);
    ObDatabaseSchema database; database.set_database_id(100); database.set_schema_version(42);
    CHECK(database.set_database_name("native_db") == OB_SUCCESS && MockSchemaService::cache_database(guard,database) == OB_SUCCESS);
    std::map<uint64_t,std::unique_ptr<ObUserInfo>> users;
    for (uint64_t id : {123,124,125,126,127,128,997,999}) {
      auto &value = users[id]; value = std::make_unique<ObUserInfo>();
      value->set_user_id(id); value->set_schema_version(42);
      CHECK(value->set_user_name("revoke_fixture") == OB_SUCCESS && value->set_host("localhost") == OB_SUCCESS);
      if (!(scenario == 38 && id == 128)) CHECK(MockSchemaService::cache_user(guard,*value) == OB_SUCCESS);
    }
    users.at(999)->set_priv_set(OB_PRIV_EXECUTE | OB_PRIV_ALTER_ROUTINE | OB_PRIV_GRANT);
    if (scenario >= 62) users.at(123)->set_priv_set(OB_PRIV_SUPER);
    const bool role_source = scenario == 4 || scenario == 18;
    const uint64_t source = scenario >= 62 ? 997 : role_source ? 125 : 123;
    ObUserInfo without_roles; CHECK(without_roles.assign(*users.at(123)) == OB_SUCCESS);
    ObSEArray<uint64_t,4> enabled, recipients;
    if (role_source) {
      users.at(125)->set_type(OB_ROLE);
      CHECK(users.at(123)->add_role_id(125) == OB_SUCCESS && enabled.push_back(125) == OB_SUCCESS);
    }
    if (scenario == 29) users.at(125)->set_type(OB_ROLE);
    ObRoutineInfo expected; CHECK(expected.assign(prototype) == OB_SUCCESS); expected.set_overload(77);
    expected.set_owner_id(997); // Keep delegation-loss cases separate from immutable ownership.
    CHECK(expected.set_native_binding(ObString::make_string("org.seekdb.gis"),ObString::make_string("org.seekdb.gis.area"),1) == OB_SUCCESS);
    auto privileges = std::make_shared<RoutinePrivilegeOverlay>(100,123);
    auto view = std::make_shared<RoutineSchemaOverlay>(privileges);
    CHECK(view->stage(expected) == OB_SUCCESS);
    if (scenario == 27) {
      CHECK(MockSchemaService::add(*manager,100,"area_alias",1001,ROUTINE_FUNCTION_TYPE,42,77) == OB_SUCCESS);
      CHECK(MockSchemaService::cache_routine(guard,expected) == OB_SUCCESS);
    } else CHECK(guard.attach_routine_overlay(view) == OB_SUCCESS);
    oceanbase::obcall::NativeRoutinePrivilegeTarget target;
    CHECK(target.assign(expected,true) == OB_SUCCESS);
    CHECK(target.bind_actor(scenario == 29 ? 125 : scenario == 30 ? 998 : 123,enabled) == OB_SUCCESS);
    if (scenario == 28) target.clear_actor();
    for (uint64_t id : {127,124,127}) CHECK(recipients.push_back(id) == OB_SUCCESS);
    if (scenario == 31 || scenario == 32) CHECK(recipients.push_back(scenario == 31 ? 998 : 0) == OB_SUCCESS);
    if (scenario == 36) recipients.reset();
    const bool option_only = scenario == 1 || scenario == 60 || scenario == 61;
    const ObPrivSet requested = scenario == 2 || scenario == 61 ? OB_PRIV_EXECUTE :
        scenario == 33 ? 0 : rights(3) | (scenario == 34 ? OB_PRIV_GRANT : 0);
    const auto behavior = scenario == 5 || scenario == 3 ? Behavior::RESTRICT :
        scenario == 35 ? static_cast<Behavior>(99) : Behavior::CASCADE;
    std::map<Key,unsigned> graph;
    graph[{source,999}] = 15;
    graph[{124,source}] = graph[{127,source}] = scenario == 60 ? 3 : 15;
    graph[{126,124}] = graph[{128,127}] = scenario == 61 ? 7 : 15;
    if (scenario == 3) {
      graph[{125,999}] = graph[{124,125}] = graph[{127,125}] = 15;
    }
    if (scenario == 6 || scenario == 39) {
      graph.erase({124,source}); graph.erase({127,source});
      graph[{124,999}] = graph[{127,999}] = 15; // Valid graph, but no direct grant from selected source.
    }
    if (scenario == 60) {
      users.at(124)->set_priv_set(OB_PRIV_EXECUTE | OB_PRIV_ALTER_ROUTINE | OB_PRIV_GRANT);
      users.at(127)->set_priv_set(OB_PRIV_EXECUTE | OB_PRIV_ALTER_ROUTINE | OB_PRIV_GRANT);
    }
    if (scenario == 7) graph[{source,999}] = 3; // Ordinary rights cannot authorize REVOKE.
    if (scenario == 8) graph[{127,source}] = 0; // Preexisting unrooted downstream grant.
    if (scenario == 9) versions->fixed = true;
    if (scenario == 10) versions->fail_at = 3;
    if (scenario == 11) versions->throw_at = 3;
    auto publish_view = view;
    auto publish_privileges = privileges;
    if (scenario == 24 || scenario == 27) { publish_view.reset(); publish_privileges.reset(); }
    if (scenario == 25) view->retire();
    if (scenario == 26) publish_privileges = std::make_shared<RoutinePrivilegeOverlay>(100,123);
    const uint64_t seeded = scenario == 23 ? 127 : scenario == 39 ? 124 : 0;
    if (seeded) CHECK(privileges->record_object_change(expected,source,seeded,43,0,packed(1,0),123) == OB_SUCCESS);
    Row routine;
    routine.integers = {{0,100},{1,997},{2,77},{3,scenario == 12 ? 41 : 42},{4,ROUTINE_FUNCTION_TYPE},{5,1}};
    routine.strings = {{6,"area_alias"},{7,"org.seekdb.gis"},{8,"org.seekdb.gis.area"}};
    ExtensionVersionRows rows;
    rows.active = scenario != 37; rows.write_status = OB_SUCCESS;
    if (scenario >= 40 && scenario < 60) rows.fail_write_at = scenario - 39;
    int snapshots = 0;
    std::vector<Key> keys;
    const auto append = [&](ExtensionVersionRows &transport, Key key, unsigned bits, bool full) {
      std::map<int,int> columns;
      if (bits & 1) columns[OBJ_PRIV_ID_EXECUTE] = (bits & 4) ? 1 : 0;
      if (bits & 2) columns[OBJ_PRIV_ID_ALTER] = (bits & 8) ? 1 : 0;
      for (auto column : columns) {
        Row row;
        if (full) row.integers = {{0,int64_t(key.first)},{1,int64_t(key.second)},{2,OBJ_LEVEL_FOR_TAB_PRIV},{3,column.first},{4,column.second}};
        else row.integers = {{0,column.first},{1,column.second}};
        transport.rows.push_back(row);
      }
    };
    rows.on_read = [&](ExtensionVersionRows &transport) {
      transport.rows.clear(); transport.read_status = transport.close_status = OB_SUCCESS;
      if (transport.sql.find("SELECT database_id,owner_id") == 0) transport.rows = {routine};
      else if (transport.sql.find("SELECT grantee_id,grantor_id,col_id") == 0) {
        ++snapshots;
        auto supplied = graph;
        if (snapshots == 2) {
          if (scenario == 15) supplied[{source,999}] = 11;
          if (scenario == 16) supplied[{126,999}] = 15;
          if (scenario == 17) users.at(999)->set_priv_set(OB_PRIV_EXECUTE|OB_PRIV_GRANT);
          if (scenario == 18) CHECK(users.at(123)->assign(without_roles) == OB_SUCCESS);
          if (scenario == 63) users.at(123)->set_priv_set(0);
        }
        for (auto entry : supplied) append(transport,entry.first,entry.second,true);
        if (scenario == 13 || (scenario == 14 && snapshots == 2)) transport.close_status = OB_TIMEOUT;
      } else {
        CHECK(transport.sql.find("SELECT priv_id,priv_option") == 0);
        bool found = false;
        for (const Key &key : {Key{124,source},Key{126,124},Key{127,source},Key{128,127}}) {
          if (transport.sql.find("grantor_id="+std::to_string(key.second)+" AND grantee_id="+std::to_string(key.first)+" ") == std::string::npos) continue;
          found = true; keys.push_back(key);
          unsigned bits = graph.count(key) ? graph.at(key) : 0;
          if ((scenario == 19 && keys.size() == 1) || (scenario == 20 && keys.size() == 3)) bits &= ~4U;
          append(transport,key,bits,false);
          if (scenario == 21) transport.read_status = OB_TIMEOUT;
          if (scenario == 22 && keys.size() == 3) transport.close_status = OB_TIMEOUT;
          break;
        }
        CHECK(found);
      }
    };
    rows.affected_rows = [&](const std::string &) {
      CHECK(snapshots == 2);
      for (uint64_t id : {124,126,127,128}) CHECK(privileges->has_object_changes(1001,id) == (id == seeded));
      return int64_t{1};
    };
    int status = OB_SUCCESS, writes = scenario == 2 || scenario == 61 ? 12 : 20, allocations = 4;
    switch (scenario) {
      case 3: writes = 10; allocations = 2; break;
      case 5: status = OB_OP_NOT_ALLOW; writes = allocations = 0; break;
      case 6: case 60: writes = 0; allocations = 2; break;
      case 7: status = OB_ERR_NO_ROUTINE_PRIVILEGE; writes = allocations = 0; break;
      case 8: case 12: case 25: case 26: case 37: status = OB_STATE_NOT_MATCH; writes = allocations = 0; break;
      case 9: status = OB_STATE_NOT_MATCH; writes = 0; allocations = 2; break;
      case 10: status = OB_TIMEOUT; writes = 0; allocations = 3; break;
      case 11: status = OB_ALLOCATE_MEMORY_FAILED; writes = 0; allocations = 3; break;
      case 13: status = OB_TIMEOUT; writes = allocations = 0; break;
      case 14: case 21: status = OB_TIMEOUT; writes = 0; break;
      case 15: case 16: case 17: case 19: status = OB_STATE_NOT_MATCH; writes = 0; break;
      case 18: case 63: status = OB_ERR_NO_ROUTINE_PRIVILEGE; writes = 0; break;
      case 20: status = OB_STATE_NOT_MATCH; writes = 10; break;
      case 22: status = OB_TIMEOUT; writes = 10; break;
      case 23: status = OB_STATE_NOT_MATCH; break;
      case 24: case 28: case 29: case 32: case 33: case 34: case 35: case 36:
        status = OB_INVALID_ARGUMENT; writes = allocations = 0; break;
      case 30: case 31: case 38: status = OB_USER_NOT_EXIST; writes = allocations = 0; break;
      case 39: status = OB_STATE_NOT_MATCH; writes = 0; allocations = 2; break;
      case 65: status = OB_STATE_NOT_MATCH; writes = 0; break;
      default: if (scenario >= 40 && scenario < 60) { status = OB_TIMEOUT; writes = scenario - 39; } break;
    }
    RoutineCatalogSavepoint caller(view,privileges);
    NativeRoutineAclVersionReservation reserved;
    if (scenario >= 64) {
      ObSEArray<NativeRoutineAclChange,4> changes;
      for (const Key &key : {Key{124,source},Key{126,124},Key{127,source},Key{128,127}})
        CHECK(changes.push_back({key.second,key.first,packed(3,scenario == 65 ? 0 : 3),0}) == OB_SUCCESS);
      CHECK(NativeRoutineAclVersionReservation::reserve(*service,rows,target,
          NativeRoutineAclVersionReservation::Kind::REVOKE,changes,reserved) == OB_SUCCESS);
      CHECK(versions->calls == 4 && rows.writes == 0);
    }
    NativeRoutineRevokeWriter writer(*service,guard,rows);
    int64_t changed = -1;
    const int result = writer.revoke(target,recipients,requested,option_only,behavior,nullptr,changed,
        publish_view,publish_privileges,scenario >= 64 ? &reserved : nullptr);
    CHECK(reserved.count() == 0);
    if (result != status || rows.writes != writes || versions->calls != allocations)
      std::cerr << "revoke writer scenario=" << scenario << " result=" << result << "/" << status
          << " writes=" << rows.writes << "/" << writes << " versions=" << versions->calls << "/" << allocations << std::endl;
    CHECK(result == status && rows.writes == writes && versions->calls == allocations);
    CHECK(changed == (result == OB_SUCCESS && writes ? 5000+allocations : 0));
    CHECK(rows.starts == 0 && rows.ends == 0);
    if (result == OB_SUCCESS) {
      const bool only_direct = scenario == 3 || scenario == 6 || scenario == 60;
      const std::vector<Key> direct{{124,source},{127,source}}, all{{124,source},{126,124},{127,source},{128,127}};
      CHECK(keys == (only_direct ? direct : all));
      for (auto key : keys) CHECK(privileges->has_object_changes(1001,key.first) == (scenario != 27));
      if (scenario != 27) {
        auto remaining = graph;
        for (auto key : keys) {
          const bool direct_key = key.second == source;
          remaining[key] = scenario == 60 ? 3 : scenario == 6 ? 0 : scenario == 2 ? 10 :
              scenario == 61 ? (direct_key ? 11 : 2) : scenario == 1 && direct_key ? 3 : 0;
        }
        ObSEArray<ObObjPriv,8> storage;
        for (auto entry : graph) if (entry.second)
          CHECK(storage.push_back(row({entry.first.second,entry.first.first,entry.second&3,entry.second>>2})) == OB_SUCCESS);
        for (uint64_t recipient : {124,126,127,128}) {
          ObSEArray<const ObObjPriv *,8> base;
          for (int64_t i = 0; i < storage.count(); ++i) if (storage.at(i).get_grantee_id() == recipient)
            CHECK(base.push_back(&storage.at(i)) == OB_SUCCESS);
          ObPackedObjPriv visible = 0, wanted = 0;
          for (auto entry : remaining) if (entry.first.first == recipient) wanted |= packed(entry.second&3,entry.second>>2);
          CHECK(privileges->merge_object_privileges(expected,recipient,base,visible) == OB_SUCCESS && visible == wanted);
        }
      }
      int logs = 0;
      for (const auto &sql : rows.written) if (sql.find("oceanbase.__all_ddl_operation") != std::string::npos) ++logs;
      CHECK(logs == (writes ? allocations : 0));
      for (int version = 5001; writes && version <= 5000+allocations; ++version) {
        int history = 0, operations = 0;
        for (auto sql : rows.written) {
          sql.erase(std::remove_if(sql.begin(),sql.end(),[](char c) { return c == ' ' || c == '\n' || c == '\t' || c == '`'; }),sql.end());
          const auto value = std::to_string(version);
          history += sql.find("oceanbase.__all_objauth_history") != std::string::npos &&
              (sql.find(","+value+",0)") != std::string::npos || sql.find(","+value+",1)") != std::string::npos);
          operations += sql.find("oceanbase.__all_ddl_operation") != std::string::npos &&
              sql.find("VALUES("+value+",") != std::string::npos;
        }
        CHECK(history == (scenario == 2 || scenario == 61 ? 1 : 2) && operations == 1);
      }
    } else for (uint64_t id : {124,126,127,128}) CHECK(privileges->has_object_changes(1001,id) == (id == seeded));
    const size_t reads = rows.queries.size(); changed = -1;
    CHECK(writer.revoke(target,recipients,requested,option_only,behavior,nullptr,changed) == OB_INIT_TWICE && changed == 0);
    CHECK(rows.queries.size() == reads && rows.writes == writes && versions->calls == allocations);
    if (caller.valid()) CHECK(caller.rollback() == OB_SUCCESS);
    for (uint64_t id : {124,126,127,128}) CHECK(privileges->has_object_changes(1001,id) == (id == seeded));
  }
  std::cout << "PASS: 66 host REVOKE writer scenarios: whole-plan authority, pre-reserved execution/mismatch, independent-path RESTRICT, caller/role and SUPER-to-owner grantors, snapshot/root/membership/SUPER rechecks, exact-key stale checks, no-op private views, per-key versions and all 20 SQL failure positions; no endpoint/commit claims" << std::endl;
}
}
