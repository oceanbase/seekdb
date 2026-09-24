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
#include "rootserver/pl_ddl/native_routine_grant_plan.h"
#include "native_routine_revoke_fixture.h"

// Pure ACL planning. Uses the established packed-privilege fixture helpers;
// no user authorization, SQL transport, transaction or publication is supplied.
namespace native_routine_grant_plan_test {
using namespace native_routine_revoke_test;
inline void run(const ObRoutineInfo &prototype)
{
  ObRoutineInfo expected; CHECK(expected.assign(prototype) == OB_SUCCESS); expected.set_overload(77);
  CHECK(expected.set_native_binding(ObString::make_string("org.seekdb.gis"),
      ObString::make_string("org.seekdb.gis.area"), 1) == OB_SUCCESS);
  int combinations = 0;
  for (unsigned before = 0; before < 4; ++before) for (unsigned options = 0; options < 4; ++options) {
    if (options & ~before) continue;
    for (unsigned first = 1; first < 4; ++first) for (bool first_option : {false, true})
      for (unsigned second = 1; second < 4; ++second) for (bool second_option : {false, true}) {
        ObSEArray<ObObjPriv, 4> snapshot;
        ObSEArray<NativeRoutineGrantRequest, 4> requests;
        ObSEArray<NativeRoutineGrantDelta, 4> output;
        CHECK(snapshot.push_back(row({1,2,before,options})) == OB_SUCCESS);
        CHECK(snapshot.push_back(row({3,4,3,3})) == OB_SUCCESS); // Unrequested key remains outside the delta.
        CHECK(requests.push_back({1,2,rights(first),first_option}) == OB_SUCCESS);
        CHECK(requests.push_back({1,2,rights(second),second_option}) == OB_SUCCESS);
        CHECK(requests.push_back(requests.at(0)) == OB_SUCCESS); // Idempotent duplicate, not a second write.
        CHECK(NativeRoutineGrantPlan::build(expected,snapshot,requests,output) == OB_SUCCESS);
        CHECK(output.count() == 1 && output.at(0).grantor_ == 1 && output.at(0).grantee_ == 2);
        const auto wanted_before = packed(before,options);
        const auto wanted_after = wanted_before | packed(first,first_option ? first : 0) |
            packed(second,second_option ? second : 0);
        CHECK(output.at(0).before_ == wanted_before && output.at(0).after_ == wanted_after);
        snapshot.reset(); requests.reset(); // Returned images do not borrow either input.
        CHECK(output.at(0).after_ == wanted_after);
        ++combinations;
      }
  }
  CHECK(combinations == 324);
  {
    ObSEArray<ObObjPriv, 4> snapshot;
    ObSEArray<NativeRoutineGrantRequest, 4> requests;
    ObSEArray<NativeRoutineGrantDelta, 4> output;
    auto column = row({1,2,3,3}); column.set_col_id(1);
    CHECK(snapshot.push_back(column) == OB_SUCCESS); // Column rights do not supply function rights.
    for (auto request : {NativeRoutineGrantRequest{8,9,OB_PRIV_EXECUTE},
        NativeRoutineGrantRequest{7,2,OB_PRIV_EXECUTE}, NativeRoutineGrantRequest{1,2,OB_PRIV_ALTER_ROUTINE}})
      CHECK(requests.push_back(request) == OB_SUCCESS);
    CHECK(NativeRoutineGrantPlan::build(expected,snapshot,requests,output) == OB_SUCCESS && output.count() == 3);
    CHECK(output.at(0).grantee_ == 2 && output.at(0).grantor_ == 1 && output.at(0).before_ == 0);
    CHECK(output.at(1).grantee_ == 2 && output.at(1).grantor_ == 7);
    CHECK(output.at(2).grantee_ == 9 && output.at(2).grantor_ == 8);
  }
  for (int mutation = 0; mutation < 18; ++mutation) {
    ObRoutineInfo target; CHECK(target.assign(expected) == OB_SUCCESS);
    ObSEArray<ObObjPriv, 4> snapshot;
    ObSEArray<NativeRoutineGrantRequest, 4> requests;
    ObSEArray<NativeRoutineGrantDelta, 4> output;
    CHECK(snapshot.push_back(row({1,2,1,0})) == OB_SUCCESS);
    CHECK(requests.push_back({1,2,OB_PRIV_EXECUTE,false}) == OB_SUCCESS);
    CHECK(output.push_back({99,99,0,0}) == OB_SUCCESS); // Failure must clear previous output.
    int status = mutation < 8 ? OB_INVALID_DATA : OB_INVALID_ARGUMENT;
    switch (mutation) {
      case 0: snapshot.at(0).set_obj_id(1002); break;
      case 1: snapshot.at(0).set_objtype(uint64_t(ObObjectType::PROCEDURE)); break;
      case 2: snapshot.at(0).set_grantee_id(0); break;
      case 3: snapshot.at(0).set_grantor_id(OB_INVALID_ID); break;
      case 4: snapshot.at(0).set_col_id(OB_INVALID_ID); break;
      case 5: CHECK(snapshot.push_back(snapshot.at(0)) == OB_SUCCESS); break;
      case 6: snapshot.at(0).set_obj_privs(packed(1,1) ^ packed(1,0)); break;
      case 7: snapshot.at(0).set_obj_privs(~ObPackedObjPriv{0}); break;
      case 8: requests.at(0).grantor_ = 0; break;
      case 9: requests.at(0).grantee_ = OB_INVALID_ID; break;
      case 10: requests.at(0).rights_ = 0; break;
      case 11: requests.at(0).rights_ |= OB_PRIV_GRANT; break;
      case 12: requests.reset(); break;
      case 13: target.set_routine_id(OB_INVALID_ID); break;
      case 14: target.set_schema_version(0); break;
      case 15: target.set_owner_id(0); break;
      case 16: target.set_package_id(42); break;
      case 17: CHECK(target.set_native_binding(ObString(),ObString(),0) == OB_SUCCESS); break;
    }
    CHECK(NativeRoutineGrantPlan::build(target,snapshot,requests,output) == status && output.empty());
  }
  {
    ObSEArray<ObObjPriv, 4> snapshot;
    ObSEArray<NativeRoutineGrantRequest, 4> requests;
    ObSEArray<NativeRoutineGrantDelta, 4> output;
    for (uint64_t i = 0; i < 16384; ++i)
      CHECK(requests.push_back({1,100+i,OB_PRIV_EXECUTE,false}) == OB_SUCCESS);
    CHECK(NativeRoutineGrantPlan::build(expected,snapshot,requests,output) == OB_SUCCESS && output.count() == 16384);
    CHECK(output.at(0).grantee_ == 100 && output.at(16383).grantee_ == 16483);
    CHECK(requests.push_back(requests.at(0)) == OB_SUCCESS);
    CHECK(NativeRoutineGrantPlan::build(expected,snapshot,requests,output) == OB_SIZE_OVERFLOW && output.empty());
    requests.reset(); CHECK(requests.push_back({1,2,OB_PRIV_EXECUTE,false}) == OB_SUCCESS);
    for (uint64_t i = 0; i < 16385; ++i) CHECK(snapshot.push_back(row({1,100+i,1,0})) == OB_SUCCESS);
    CHECK(NativeRoutineGrantPlan::build(expected,snapshot,requests,output) == OB_SIZE_OVERFLOW && output.empty());
  }
  std::cout << "PASS: native GRANT pure plan: 324 privilege/option combinations, duplicate coalescing, ordering, owned/no-op images, 18 malformed inputs and 16384-group bounds; no authorization or SQL" << std::endl;
}
}
