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
#include "native_routine_grant_plan_fixture.h"
#include "share/schema/routine_catalog_savepoint.h"

// Private snapshot projection composed with the real pure DCL planners. The
// host admission, SQL locks, version allocator and transaction are not modeled.
namespace native_routine_acl_snapshot_test {
using namespace native_routine_revoke_test;
using SnapshotKey = std::tuple<uint64_t,uint64_t,uint64_t>;
using Image = std::map<SnapshotKey,ObPackedObjPriv>;

inline Image image(const ObIArray<ObObjPriv> &snapshot)
{
  Image result;
  SnapshotKey previous{}; bool first = true;
  for (int64_t i = 0; i < snapshot.count(); ++i) {
    const auto &row = snapshot.at(i);
    const SnapshotKey key{row.get_grantee_id(),row.get_grantor_id(),row.get_col_id()};
    CHECK(first || previous < key); first = false; previous = key;
    CHECK(row.get_obj_id() == 1001 && row.get_objtype() == uint64_t(ObObjectType::FUNCTION));
    CHECK(result.emplace(key,row.get_obj_privs()).second);
  }
  return result;
}

inline void run(const ObRoutineInfo &prototype)
{
  ObRoutineInfo expected; CHECK(expected.assign(prototype) == OB_SUCCESS); expected.set_owner_id(1);
  CHECK(expected.set_native_binding(ObString::make_string("org.seekdb.gis"),
      ObString::make_string("org.seekdb.gis.area"),1) == OB_SUCCESS);
  auto privileges = std::make_shared<RoutinePrivilegeOverlay>(100,1);
  auto schema = std::make_shared<RoutineSchemaOverlay>(privileges);
  CHECK(schema->stage(expected) == OB_SUCCESS);
  CHECK(privileges->record_create(expected,true) == OB_SUCCESS);
  ObSEArray<ObObjPriv,4> base, projected, saved;
  // A historical function grant and an independent column group. The private
  // zero overrides only its exact grantor key, never the column or another grantor.
  CHECK(base.push_back(row({9,8,1,0})) == OB_SUCCESS);
  auto column = row({2,3,1,1}); column.set_col_id(1);
  CHECK(base.push_back(column) == OB_SUCCESS);
  CHECK(privileges->record_object_change(expected,9,8,43,packed(1,0),0,1) == OB_SUCCESS);
  CHECK(privileges->merge_object_snapshot(expected,base,projected) == OB_SUCCESS);
  Image initial{{{1,1,OBJ_LEVEL_FOR_TAB_PRIV},packed(3,0)},{{3,2,1},packed(1,1)}};
  CHECK(image(projected) == initial);
  CHECK(saved.assign(projected) == OB_SUCCESS);
  {
    RoutineCatalogSavepoint mark(schema,privileges); CHECK(mark.valid());
    CHECK(privileges->record_object_change(expected,1,2,44,0,packed(1,1),1) == OB_SUCCESS);
    CHECK(privileges->record_object_change(expected,2,3,45,0,packed(1,1),1) == OB_SUCCESS);
    CHECK(privileges->record_object_change(expected,2,4,46,0,packed(1,0),1) == OB_SUCCESS);
    CHECK(privileges->merge_object_snapshot(expected,base,projected) == OB_SUCCESS);
    Image granted = initial;
    granted[{2,1,OBJ_LEVEL_FOR_TAB_PRIV}] = packed(1,1);
    granted[{3,2,OBJ_LEVEL_FOR_TAB_PRIV}] = packed(1,1);
    granted[{4,2,OBJ_LEVEL_FOR_TAB_PRIV}] = packed(1,0);
    CHECK(image(projected) == granted && image(saved) == initial);
    // An in-place copy is supported, and its owned rows survive later changes.
    CHECK(privileges->merge_object_snapshot(expected,projected,projected) == OB_SUCCESS);
    CHECK(image(projected) == granted);
    ObSEArray<NativeRoutineGrantRoot,4> roots;
    ObSEArray<NativeRoutineRevokeRequest,4> requests;
    ObSEArray<NativeRoutineRevokeDelta,4> deltas;
    CHECK(roots.push_back({1,rights(3)}) == OB_SUCCESS);
    CHECK(requests.push_back({1,2,OB_PRIV_EXECUTE,false}) == OB_SUCCESS);
    CHECK(NativeRoutineRevokePlan::build(expected,projected,roots,requests,Behavior::RESTRICT,deltas) == OB_OP_NOT_ALLOW);
    CHECK(deltas.empty()); // Private delegation is a real dependency, not lost by OR-ing rights.
    CHECK(NativeRoutineRevokePlan::build(expected,projected,roots,requests,Behavior::CASCADE,deltas) == OB_SUCCESS);
    CHECK(deltas.count() == 3);
    int64_t version = 47;
    for (const auto &delta : deltas)
      CHECK(privileges->record_object_change(expected,delta.grantor_,delta.grantee_,version++,
          delta.before_,delta.after_,1) == OB_SUCCESS);
    CHECK(image(projected) == granted); // Prior snapshot remains owned and immutable.
    CHECK(privileges->merge_object_snapshot(expected,base,projected) == OB_SUCCESS && image(projected) == initial);
    ObSEArray<NativeRoutineGrantRequest,4> grants;
    ObSEArray<NativeRoutineGrantDelta,4> grant_deltas;
    CHECK(grants.push_back({1,2,OB_PRIV_EXECUTE,false}) == OB_SUCCESS);
    CHECK(NativeRoutineGrantPlan::build(expected,projected,grants,grant_deltas) == OB_SUCCESS);
    CHECK(grant_deltas.count() == 1 && grant_deltas.at(0).before_ == 0 && grant_deltas.at(0).after_ == packed(1,0));
    CHECK(mark.rollback() == OB_SUCCESS);
    CHECK(privileges->merge_object_snapshot(expected,base,projected) == OB_SUCCESS && image(projected) == initial);
  }
  // Invalid base data cannot hide behind a private override. Errors clear the
  // previous output without changing the recorded private state.
  for (int mutation = 0; mutation < 11; ++mutation) {
    ObSEArray<ObObjPriv,4> bad;
    CHECK(bad.push_back(row({9,8,1,0})) == OB_SUCCESS);
    ObRoutineInfo current; CHECK(current.assign(expected) == OB_SUCCESS);
    int status = OB_INVALID_DATA;
    switch (mutation) {
      case 0: bad.at(0).set_obj_id(1002); break;
      case 1: bad.at(0).set_objtype(uint64_t(ObObjectType::PROCEDURE)); break;
      case 2: bad.at(0).set_grantee_id(0); break;
      case 3: bad.at(0).set_grantor_id(OB_INVALID_ID); break;
      case 4: bad.at(0).set_col_id(OB_INVALID_ID); break;
      case 5: CHECK(bad.push_back(bad.at(0)) == OB_SUCCESS); break;
      case 6: bad.at(0).set_obj_privs(packed(1,1)^packed(1,0)); break;
      case 7: bad.at(0).set_obj_privs(~ObPackedObjPriv{0}); break;
      case 8: current.set_owner_id(2); status = OB_STATE_NOT_MATCH; break;
      case 9: current.set_schema_version(41); status = OB_STATE_NOT_MATCH; break;
      case 10: current.set_database_id(101); status = OB_INVALID_ARGUMENT; break;
    }
    CHECK(projected.assign(saved) == OB_SUCCESS);
    CHECK(privileges->merge_object_snapshot(current,bad,projected) == status && projected.empty());
    CHECK(privileges->merge_object_snapshot(expected,base,projected) == OB_SUCCESS && image(projected) == initial);
    CHECK(privileges->merge_object_snapshot(current,bad,bad) == status && bad.empty());
  }
  {
    ObSEArray<ObObjPriv,4> full;
    auto bounded = std::make_shared<RoutinePrivilegeOverlay>(100,1);
    for (uint64_t i = 0; i < RoutinePrivilegeOverlay::MAX_IDENTITIES; ++i)
      CHECK(full.push_back(row({1,100+i,1,0})) == OB_SUCCESS);
    // Insert sorts before the deletion: bound applies to the final union.
    CHECK(bounded->record_object_change(expected,1,2,43,0,packed(1,0),1) == OB_SUCCESS);
    CHECK(bounded->record_object_change(expected,1,16483,44,packed(1,0),0,1) == OB_SUCCESS);
    CHECK(bounded->merge_object_snapshot(expected,full,projected) == OB_SUCCESS && projected.count() == 16384);
    CHECK(projected.at(0).get_grantee_id() == 2 && projected.at(16383).get_grantee_id() == 16482);
    CHECK(bounded->record_object_change(expected,1,3,45,0,packed(1,0),1) == OB_SUCCESS);
    CHECK(bounded->merge_object_snapshot(expected,full,projected) == OB_SIZE_OVERFLOW && projected.empty());
    CHECK(full.push_back(row({1,99999,1,0})) == OB_SUCCESS);
    CHECK(bounded->merge_object_snapshot(expected,full,projected) == OB_SIZE_OVERFLOW && projected.empty());
  }
  privileges->retire();
  CHECK(projected.assign(saved) == OB_SUCCESS);
  CHECK(privileges->merge_object_snapshot(expected,base,projected) == OB_STATE_NOT_MATCH && projected.empty());
  std::cout << "PASS: full private ACL snapshot, exact grantor/column keys, owned and aliased output, GRANT/REVOKE planning with private delegation, CASCADE/RESTRICT, savepoint restoration, 11 malformed/stale inputs, retirement and final-union bounds; no SQL/commit claims" << std::endl;
}
}
