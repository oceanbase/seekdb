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
#include "rootserver/pl_ddl/native_routine_revoke_plan.h"
#include <map>
#include <tuple>

// Actual pure dependency planner; metadata roots and ACL are explicit inputs.
// This does not authorize users, read/write SQL, or simulate catalog commits.
namespace native_routine_revoke_test {
using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::share::schema;
using namespace oceanbase::rootserver;
using Behavior = NativeRoutineRevokePlan::Behavior;
using Key = std::pair<uint64_t, uint64_t>; // grantee, grantor
struct Edge { uint64_t from, to; unsigned rights, options; };
struct Request { uint64_t from, to; unsigned rights; bool option_only = false; };

inline ObPrivSet rights(unsigned bits)
{ return ((bits & 1) ? OB_PRIV_EXECUTE : 0) | ((bits & 2) ? OB_PRIV_ALTER_ROUTINE : 0); }
inline ObPackedObjPriv packed(unsigned bits, unsigned options)
{
  ObPackedObjPriv result = 0;
  for (unsigned i = 0; i < 2; ++i) if (bits & (1U << i)) {
    ObPackedObjPriv value = 0;
    CHECK(ObPrivPacker::raw_obj_priv_to_packed_info((options & (1U << i)) ? GRANT_OPTION : NO_OPTION,
        i == 0 ? OBJ_PRIV_ID_EXECUTE : OBJ_PRIV_ID_ALTER, value) == OB_SUCCESS);
    result |= value;
  }
  return result;
}
inline ObObjPriv row(Edge edge)
{
  ObObjPriv value;
  value.set_user_id(edge.to); value.set_grantee_id(edge.to); value.set_grantor_id(edge.from);
  value.set_obj_id(1001); value.set_objtype(uint64_t(ObObjectType::FUNCTION));
  value.set_col_id(OBJ_LEVEL_FOR_TAB_PRIV); value.set_schema_version(42);
  value.set_obj_privs(packed(edge.rights, edge.options));
  return value;
}

inline void run(const ObRoutineInfo &prototype)
{
  ObRoutineInfo expected; CHECK(expected.assign(prototype) == OB_SUCCESS); expected.set_overload(77);
  CHECK(expected.set_native_binding(ObString::make_string("org.seekdb.gis"),
      ObString::make_string("org.seekdb.gis.area"), 1) == OB_SUCCESS);
  {
    ObRoutineInfo rooted; CHECK(rooted.assign(expected) == OB_SUCCESS); rooted.set_owner_id(4);
    auto manager = std::make_unique<ObSchemaMgr>(); CHECK(manager->init() == OB_SUCCESS);
    auto service = std::make_unique<MockSchemaService>();
    ObSchemaGetterGuard guard; CHECK(MockSchemaService::bind(guard,*service,*manager) == OB_SUCCESS);
    ObDatabaseSchema database;
    database.set_database_id(100); database.set_schema_version(42);
    CHECK(database.set_database_name("native_db") == OB_SUCCESS);
    CHECK(MockSchemaService::cache_database(guard,database) == OB_SUCCESS);
    auto view = std::make_shared<RoutineSchemaOverlay>();
    CHECK(view->stage(rooted) == OB_SUCCESS && guard.attach_routine_overlay(view) == OB_SUCCESS);
    ObUserInfo principals[4];
    for (uint64_t i = 0; i < 4; ++i) {
      principals[i].set_user_id(i+1); principals[i].set_schema_version(42);
      CHECK(principals[i].set_user_name("root_fixture") == OB_SUCCESS);
      CHECK(principals[i].set_host("localhost") == OB_SUCCESS);
      CHECK(MockSchemaService::cache_user(guard,principals[i]) == OB_SUCCESS);
    }
    principals[0].set_priv_set(OB_PRIV_EXECUTE | OB_PRIV_GRANT);
    principals[1].set_type(OB_ROLE); principals[1].set_priv_set(OB_PRIV_ALTER_ROUTINE | OB_PRIV_GRANT);
    principals[2].set_priv_set(OB_PRIV_EXECUTE);
    CHECK(principals[2].add_role_id(2) == OB_SUCCESS);
    ObSEArray<ObObjPriv, 4> snapshot;
    for (auto edge : {Edge{1,2,1,1}, Edge{2,3,2,2}, Edge{3,4,1,0}}) CHECK(snapshot.push_back(row(edge)) == OB_SUCCESS);
    ObSEArray<NativeRoutineGrantRoot, 4> roots;
    const auto collect = [&](std::initializer_list<std::pair<uint64_t, unsigned>> wanted) {
      CHECK(roots.push_back({999,OB_PRIV_EXECUTE}) == OB_SUCCESS);
      CHECK(NativeRoutineRevokePlan::collect_roots(guard,rooted,snapshot,roots) == OB_SUCCESS);
      CHECK(roots.count() == wanted.size()); int i = 0;
      for (auto value : wanted) { CHECK(roots.at(i).principal_ == value.first && roots.at(i).rights_ == rights(value.second)); ++i; }
    };
    collect({{1,1},{2,2},{4,3}}); // Ownership persists independently of ACL rows.
    CHECK(MockSchemaService::grant_object(*manager,1001,1,3,packed(1,1)) == OB_SUCCESS);
    collect({{1,1},{2,2},{4,3}}); // Object grants are edges, never independent roots.
    CHECK(MockSchemaService::grant_database(*manager,3,"native_db",OB_PRIV_EXECUTE|OB_PRIV_GRANT) == OB_SUCCESS);
    CHECK(MockSchemaService::grant_database(*manager,4,"different_db",OB_PRIV_EXECUTE|OB_PRIV_GRANT) == OB_SUCCESS);
    collect({{1,1},{2,2},{3,1},{4,3}});
    principals[0].set_priv_set(0); principals[1].set_priv_set(OB_PRIV_SUPER);
    collect({{3,1},{4,3}}); // SUPER is not a durable grant-chain root.
    rooted.set_schema_version(43);
    CHECK(NativeRoutineRevokePlan::collect_roots(guard,rooted,snapshot,roots) == OB_STATE_NOT_MATCH && roots.empty());
    rooted.set_schema_version(42);
    snapshot.at(0).set_obj_id(1002);
    CHECK(NativeRoutineRevokePlan::collect_roots(guard,rooted,snapshot,roots) == OB_INVALID_DATA && roots.empty());
    snapshot.at(0).set_obj_id(1001);
    CHECK(snapshot.push_back(row({4,5,1,0})) == OB_SUCCESS);
    CHECK(NativeRoutineRevokePlan::collect_roots(guard,rooted,snapshot,roots) == OB_USER_NOT_EXIST && roots.empty());
    snapshot.reset(); collect({{4,3}}); // Empty object ACL still has its owner's grant options.
  }
  int examples = 0;
  const auto check = [&](std::initializer_list<Edge> edges, std::initializer_list<Request> targets,
                         std::initializer_list<std::pair<uint64_t, unsigned>> authorities,
                         Behavior behavior, int status, std::initializer_list<Edge> changed) {
    ++examples;
    ObSEArray<ObObjPriv, 4> snapshot;
    ObSEArray<NativeRoutineRevokeRequest, 4> requests;
    ObSEArray<NativeRoutineGrantRoot, 4> roots;
    ObSEArray<NativeRoutineRevokeDelta, 4> output;
    std::map<Key, ObPackedObjPriv> before, after;
    for (auto edge : edges) {
      CHECK(snapshot.push_back(row(edge)) == OB_SUCCESS);
      before[{edge.to, edge.from}] = packed(edge.rights, edge.options);
    }
    for (auto request : targets) CHECK(requests.push_back({request.from, request.to,
        rights(request.rights), request.option_only}) == OB_SUCCESS);
    for (auto root : authorities) CHECK(roots.push_back({root.first, rights(root.second)}) == OB_SUCCESS);
    for (auto edge : changed) after[{edge.to, edge.from}] = packed(edge.rights, edge.options);
    CHECK(output.push_back({999, 999, 1, 1}) == OB_SUCCESS);
    const int result = NativeRoutineRevokePlan::build(expected, snapshot, roots, requests, behavior, output);
    if (result != status) std::cerr << "revoke graph example=" << examples << " result=" << result << "/" << status << std::endl;
    CHECK(result == status);
    CHECK(output.count() == (status == OB_SUCCESS ? after.size() : 0));
    size_t i = 0;
    if (status == OB_SUCCESS) for (const auto &value : after) {
      const auto &delta = output.at(i++);
      CHECK(delta.grantee_ == value.first.first && delta.grantor_ == value.first.second);
      CHECK(delta.before_ == before.at(value.first) && delta.after_ == value.second);
    }
  };
  check({{1,2,1,1},{2,3,1,1}}, {{1,2,1}}, {{1,1}}, Behavior::CASCADE, OB_SUCCESS, {{1,2,0,0},{2,3,0,0}});
  check({{1,2,1,1},{2,3,1,1}}, {{1,2,1}}, {{1,1}}, Behavior::RESTRICT, OB_OP_NOT_ALLOW, {});
  check({{1,2,1,1},{2,3,1,1}}, {{1,2,1,true}}, {{1,1}}, Behavior::CASCADE, OB_SUCCESS, {{1,2,1,0},{2,3,0,0}});
  check({{1,2,1,1},{2,3,1,1}}, {{1,2,1,true}}, {{1,1}}, Behavior::RESTRICT, OB_OP_NOT_ALLOW, {});
  check({{1,2,1,1},{2,3,1,1}}, {{2,3,1}}, {{1,1}}, Behavior::RESTRICT, OB_SUCCESS, {{2,3,0,0}});
  check({{1,2,1,1},{2,3,1,1}}, {{1,2,1},{2,3,1}}, {{1,1}}, Behavior::RESTRICT, OB_SUCCESS, {{1,2,0,0},{2,3,0,0}});
  check({{1,2,1,1},{1,4,1,1},{4,2,1,1},{2,3,1,1}}, {{1,2,1}}, {{1,1}}, Behavior::RESTRICT, OB_SUCCESS, {{1,2,0,0}});
  check({{1,2,1,1},{1,4,1,1},{4,2,1,0},{2,3,1,1}}, {{1,2,1}}, {{1,1}}, Behavior::CASCADE, OB_SUCCESS, {{1,2,0,0},{2,3,0,0}});
  check({{1,2,1,1},{1,3,1,0},{2,3,1,1}}, {{1,2,1}}, {{1,1}}, Behavior::CASCADE, OB_SUCCESS, {{1,2,0,0},{2,3,0,0}});
  check({{1,2,3,3},{2,3,3,3}}, {{1,2,1}}, {{1,3}}, Behavior::CASCADE, OB_SUCCESS, {{1,2,2,2},{2,3,2,2}});
  check({{1,2,1,1},{4,2,2,2},{2,3,3,3}}, {{1,2,1}}, {{1,1},{4,2}}, Behavior::CASCADE, OB_SUCCESS, {{1,2,0,0},{2,3,2,2}});
  check({{1,2,1,1},{2,3,1,1},{3,2,1,1}}, {{1,2,1}}, {{1,1}}, Behavior::CASCADE, OB_SUCCESS, {{1,2,0,0},{2,3,0,0},{3,2,0,0}});
  check({{1,2,1,1},{2,3,1,1},{3,2,1,1},{4,3,1,1}}, {{1,2,1}}, {{1,1},{4,1}}, Behavior::RESTRICT, OB_SUCCESS, {{1,2,0,0}});
  check({{1,2,1,1},{2,2,1,1}}, {{1,2,1}}, {{1,1}}, Behavior::CASCADE, OB_SUCCESS, {{1,2,0,0},{2,2,0,0}});
  check({{1,1,1,1},{1,2,1,1}}, {{1,1,1,true}}, {{1,1}}, Behavior::RESTRICT, OB_SUCCESS, {{1,1,1,0}});
  check({{1,2,1,0}}, {{1,2,1,true}}, {{1,1}}, Behavior::RESTRICT, OB_SUCCESS, {});
  check({{1,2,1,1}}, {{3,2,1}}, {{1,1}}, Behavior::RESTRICT, OB_SUCCESS, {});
  check({{1,2,3,3}}, {{1,2,1,true},{1,2,1}}, {{1,3}}, Behavior::RESTRICT, OB_SUCCESS, {{1,2,2,2}});
  check({{1,2,3,3}}, {{1,2,1},{1,2,1,true}}, {{1,3}}, Behavior::RESTRICT, OB_SUCCESS, {{1,2,2,2}});
  check({{1,2,3,3},{2,3,2,0}}, {{1,2,1},{1,2,2,true}}, {{1,3}}, Behavior::CASCADE, OB_SUCCESS, {{1,2,2,0},{2,3,0,0}});
  check({{2,3,1,1},{3,2,1,1}}, {{2,3,1}}, {{1,1}}, Behavior::CASCADE, OB_STATE_NOT_MATCH, {});
  check({{1,2,1,0},{2,3,1,0}}, {{2,3,1}}, {{1,1}}, Behavior::CASCADE, OB_STATE_NOT_MATCH, {});
  check({}, {{1,2,1}}, {}, Behavior::CASCADE, OB_SUCCESS, {});
  check({{1,2,1,1}}, {{1,2,1}}, {{1,1},{1,1}}, Behavior::CASCADE, OB_INVALID_ARGUMENT, {});
  check({{1,2,1,1}}, {{0,2,1}}, {{1,1}}, Behavior::CASCADE, OB_INVALID_ARGUMENT, {});
  check({{1,2,1,1}}, {{1,2,0}}, {{1,1}}, Behavior::CASCADE, OB_INVALID_ARGUMENT, {});
  check({{1,2,1,1}}, {{1,2,1}}, {{0,1}}, Behavior::CASCADE, OB_INVALID_ARGUMENT, {});

  // Validation must reject cross-object rows, malformed option bits and
  // duplicate keys, not silently count them as independent authorization paths.
  for (int mutation = 0; mutation < 9; ++mutation) {
    ObSEArray<ObObjPriv, 4> snapshot;
    ObSEArray<NativeRoutineGrantRoot, 4> roots;
    ObSEArray<NativeRoutineRevokeRequest, 4> requests;
    ObSEArray<NativeRoutineRevokeDelta, 4> output;
    CHECK(snapshot.push_back(row({1,2,1,1})) == OB_SUCCESS);
    CHECK(roots.push_back({1, OB_PRIV_EXECUTE}) == OB_SUCCESS);
    CHECK(requests.push_back({1,2,OB_PRIV_EXECUTE,false}) == OB_SUCCESS);
    auto &value = snapshot.at(0);
    switch (mutation) {
      case 0: value.set_obj_id(1002); break;
      case 1: value.set_objtype(uint64_t(ObObjectType::TABLE)); break;
      case 2: value.set_grantee_id(0); break;
      case 3: value.set_grantor_id(OB_INVALID_ID); break;
      case 4: value.set_obj_privs(packed(1,1) ^ packed(1,0)); break;
      case 5: {
        ObPackedObjPriv bits = 0;
        CHECK(ObPrivPacker::raw_obj_priv_to_packed_info(NO_OPTION, OBJ_PRIV_ID_SELECT, bits) == OB_SUCCESS);
        value.set_obj_privs(bits); break;
      }
      case 6: { ObObjPriv duplicate; CHECK(duplicate.assign(value) == OB_SUCCESS);
        CHECK(snapshot.push_back(duplicate) == OB_SUCCESS); break; }
      case 7: value.set_col_id(OB_INVALID_ID); break;
      case 8: roots.at(0).rights_ = OB_PRIV_GRANT; break;
    }
    CHECK(output.push_back({1,2,1,0}) == OB_SUCCESS);
    CHECK(NativeRoutineRevokePlan::build(expected, snapshot, roots, requests, Behavior::CASCADE, output) ==
        (mutation == 8 ? OB_INVALID_ARGUMENT : OB_INVALID_DATA));
    CHECK(output.empty());
  }
  {
    ObSEArray<ObObjPriv, 4> snapshot;
    ObSEArray<NativeRoutineGrantRoot, 4> roots;
    ObSEArray<NativeRoutineRevokeRequest, 4> requests;
    ObSEArray<NativeRoutineRevokeDelta, 4> output;
    auto column = row({1,2,1,1}); column.set_col_id(7);
    CHECK(snapshot.push_back(column) == OB_SUCCESS && snapshot.push_back(row({2,3,1,0})) == OB_SUCCESS);
    CHECK(roots.push_back({1,OB_PRIV_EXECUTE}) == OB_SUCCESS);
    CHECK(requests.push_back({1,2,OB_PRIV_EXECUTE,false}) == OB_SUCCESS);
    CHECK(NativeRoutineRevokePlan::build(expected,snapshot,roots,requests,Behavior::CASCADE,output) == OB_STATE_NOT_MATCH);
    CHECK(snapshot.push_back(row({1,2,1,1})) == OB_SUCCESS);
    CHECK(NativeRoutineRevokePlan::build(expected,snapshot,roots,requests,Behavior::CASCADE,output) == OB_SUCCESS);
    CHECK(output.count() == 2); // The column record is neither authority nor a deletion target.
  }
  // Independent small-graph oracle: Floyd-Warshall transitive closure of grant
  // options, rather than the production adjacency-list/work-queue traversal.
  uint32_t random = 0x13498ac5;
  const auto next = [&]() { random ^= random << 13; random ^= random >> 17; random ^= random << 5; return random; };
  constexpr int nodes = 8;
  for (int sample = 0; sample < 256; ++sample) {
    unsigned initial[nodes][nodes] = {}, changed[nodes][nodes] = {};
    for (int i = 0; i + 1 < nodes; ++i) initial[i][i+1] = 15;
    for (int i = 0; i < nodes; ++i) for (int j = 0; j < nodes; ++j) {
      unsigned bits = next() & 3, options = next() & bits;
      initial[i][j] |= bits | (options << 2);
      changed[i][j] = initial[i][j];
    }
    ObSEArray<ObObjPriv, 4> snapshot;
    ObSEArray<NativeRoutineGrantRoot, 4> roots;
    ObSEArray<NativeRoutineRevokeRequest, 4> requests;
    ObSEArray<NativeRoutineRevokeDelta, 4> output;
    CHECK(roots.push_back({1, rights(3)}) == OB_SUCCESS);
    for (int i = 0; i < nodes; ++i) for (int j = 0; j < nodes; ++j) if (initial[i][j])
      CHECK(snapshot.push_back(row({uint64_t(i+1),uint64_t(j+1),initial[i][j]&3,initial[i][j]>>2})) == OB_SUCCESS);
    for (int i = 0; i < 3; ++i) {
      const unsigned from = next() % nodes, to = (sample & 1) ? (from + 1) % nodes : next() % nodes;
      const unsigned bits = (next() % 3) + 1; const bool option = next() & 1;
      CHECK(requests.push_back({from+1,to+1,rights(bits),option}) == OB_SUCCESS);
      changed[from][to] &= ~(bits << 2);
      if (!option) changed[from][to] &= ~bits;
    }
    bool dependent = false;
    for (unsigned bit : {1,2}) {
      bool paths[nodes][nodes] = {};
      for (int i = 0; i < nodes; ++i) for (int j = 0; j < nodes; ++j) paths[i][j] = changed[i][j] & (bit << 2);
      for (int k = 0; k < nodes; ++k) for (int i = 0; i < nodes; ++i) for (int j = 0; j < nodes; ++j)
        paths[i][j] = paths[i][j] || (paths[i][k] && paths[k][j]);
      for (int i = 1; i < nodes; ++i) if (!paths[0][i]) for (int j = 0; j < nodes; ++j) {
        dependent |= (changed[i][j] & bit) != 0;
        changed[i][j] &= ~(bit | (bit << 2));
      }
    }
    for (auto behavior : {Behavior::CASCADE, Behavior::RESTRICT}) {
      const int status = NativeRoutineRevokePlan::build(expected,snapshot,roots,requests,behavior,output);
      if (behavior == Behavior::RESTRICT && dependent) { CHECK(status == OB_OP_NOT_ALLOW && output.empty()); continue; }
      CHECK(status == OB_SUCCESS);
      int cursor = 0;
      for (int to = 0; to < nodes; ++to) for (int from = 0; from < nodes; ++from) if (initial[from][to] != changed[from][to]) {
        CHECK(cursor < output.count()); const auto &delta = output.at(cursor++);
        CHECK(delta.grantor_ == from+1 && delta.grantee_ == to+1);
        CHECK(delta.before_ == packed(initial[from][to]&3,initial[from][to]>>2));
        CHECK(delta.after_ == packed(changed[from][to]&3,changed[from][to]>>2));
      }
      CHECK(cursor == output.count());
    }
  }
  {
    ObSEArray<ObObjPriv, 4> snapshot;
    ObSEArray<NativeRoutineGrantRoot, 4> roots;
    ObSEArray<NativeRoutineRevokeRequest, 4> requests;
    ObSEArray<NativeRoutineRevokeDelta, 4> output;
    CHECK(roots.push_back({1,OB_PRIV_EXECUTE}) == OB_SUCCESS && requests.push_back({1,2,OB_PRIV_EXECUTE,false}) == OB_SUCCESS);
    for (uint64_t i = 1; i <= 16384; ++i) CHECK(snapshot.push_back(row({i,i+1,1,1})) == OB_SUCCESS);
    CHECK(NativeRoutineRevokePlan::build(expected,snapshot,roots,requests,Behavior::CASCADE,output) == OB_SUCCESS);
    CHECK(output.count() == 16384 && output.at(16383).grantee_ == 16385 && output.at(16383).after_ == 0);
    CHECK(NativeRoutineRevokePlan::build(expected,snapshot,roots,requests,Behavior::RESTRICT,output) == OB_OP_NOT_ALLOW && output.empty());
    CHECK(snapshot.push_back(row({1,2,1,1})) == OB_SUCCESS);
    CHECK(NativeRoutineRevokePlan::build(expected,snapshot,roots,requests,Behavior::CASCADE,output) == OB_SIZE_OVERFLOW && output.empty());
    snapshot.reset(); roots.reset();
    for (int i = 0; i < 16385; ++i) CHECK(roots.push_back({1,OB_PRIV_EXECUTE}) == OB_SUCCESS);
    CHECK(NativeRoutineRevokePlan::build(expected,snapshot,roots,requests,Behavior::CASCADE,output) == OB_SIZE_OVERFLOW && output.empty());
    roots.reset(); requests.reset();
    for (int i = 0; i < 16385; ++i) CHECK(requests.push_back({1,2,OB_PRIV_EXECUTE,false}) == OB_SUCCESS);
    CHECK(NativeRoutineRevokePlan::build(expected,snapshot,roots,requests,Behavior::CASCADE,output) == OB_SIZE_OVERFLOW && output.empty());
    requests.reset(); CHECK(requests.push_back({1,2,OB_PRIV_EXECUTE,false}) == OB_SUCCESS);
    CHECK(NativeRoutineRevokePlan::build(expected,snapshot,roots,requests,static_cast<Behavior>(99),output) == OB_INVALID_ARGUMENT && output.empty());
    for (int mutation = 0; mutation < 5; ++mutation) {
      ObRoutineInfo invalid; CHECK(invalid.assign(expected) == OB_SUCCESS);
      if (mutation == 0) invalid.set_routine_id(0);
      if (mutation == 1) invalid.set_database_id(0);
      if (mutation == 2) invalid.set_owner_id(0);
      if (mutation == 3) invalid.set_overload(-1);
      if (mutation == 4) invalid.set_schema_version(0);
      CHECK(output.push_back({1,2,1,0}) == OB_SUCCESS);
      CHECK(NativeRoutineRevokePlan::build(invalid,snapshot,roots,requests,Behavior::CASCADE,output) == OB_INVALID_ARGUMENT && output.empty());
    }
  }
  std::cout << "PASS: native REVOKE dependency planning: RESTRICT/CASCADE, alternative grant paths, independent rights, rooted cycles, malformed ACLs, 256 independent graph-oracle cases and a 16384-edge chain; no SQL/authority/commit claims" << std::endl;
}
}
