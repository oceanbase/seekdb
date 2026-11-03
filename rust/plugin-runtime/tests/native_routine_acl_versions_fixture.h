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
#include "rootserver/pl_ddl/native_routine_acl_version_reservation.h"
#include "native_routine_grant_fixture.h"
#include "native_routine_revoke_fixture.h"
#include <type_traits>

namespace native_routine_acl_versions_test {
using namespace native_routine_revoke_test;
using Reservation = NativeRoutineAclVersionReservation;
using Kind = Reservation::Kind;

inline void run(const ObRoutineInfo &prototype)
{
  static_assert(!std::is_copy_constructible_v<Reservation> && !std::is_copy_assignable_v<Reservation>);
  static_assert(std::is_nothrow_move_constructible_v<Reservation> && std::is_nothrow_move_assignable_v<Reservation>);
  for (int scenario = 0; scenario < 38; ++scenario) {
    auto service = std::make_unique<native_routine_grant_test::Service>();
    auto other_service = std::make_unique<native_routine_grant_test::Service>();
    ObMySQLProxy proxy;
    auto versions = std::make_unique<native_routine_grant_test::Versions>(proxy,*service);
    auto replacement = std::make_unique<native_routine_grant_test::Versions>(proxy,*service);
    service->bind_sql(*versions); other_service->bind_sql(*versions);
    ExtensionVersionRows transaction, other_transaction;
    transaction.active = other_transaction.active = true;
    ObRoutineInfo expected; CHECK(expected.assign(prototype) == OB_SUCCESS); expected.set_overload(77);
    CHECK(expected.set_native_binding(ObString::make_string("org.seekdb.gis"),
        ObString::make_string("org.seekdb.gis.area"),1) == OB_SUCCESS);
    oceanbase::obcall::NativeRoutinePrivilegeTarget target;
    ObSEArray<uint64_t,4> roles; CHECK(roles.push_back(124) == OB_SUCCESS);
    CHECK(target.assign(expected,true) == OB_SUCCESS && target.bind_actor(123,roles) == OB_SUCCESS);
    Kind kind = scenario == 1 ? Kind::REVOKE : Kind::GRANT;
    ObSEArray<NativeRoutineAclChange,4> changes;
    for (uint64_t grantee : {126,127}) CHECK(changes.push_back({123,grantee,
        scenario == 1 ? packed(1,0) : 0,scenario == 1 ? 0 : packed(1,0)}) == OB_SUCCESS);
    int reserve_status = OB_SUCCESS;
    switch (scenario) {
      case 24: changes.at(0).grantee_ = 0; reserve_status = OB_INVALID_ARGUMENT; break;
      case 25: std::swap(changes.at(0),changes.at(1)); reserve_status = OB_INVALID_ARGUMENT; break;
      case 26: changes.at(1) = changes.at(0); reserve_status = OB_INVALID_ARGUMENT; break;
      case 27: changes.at(0).after_ = packed(1,1)^packed(1,0); reserve_status = OB_INVALID_ARGUMENT; break;
      case 28: changes.at(0).before_ = packed(3,0); reserve_status = OB_INVALID_ARGUMENT; break;
      case 29: kind = static_cast<Kind>(99); reserve_status = OB_INVALID_ARGUMENT; break;
      case 30: changes.reset(); reserve_status = OB_INVALID_ARGUMENT; break;
      case 31: transaction.active = false; reserve_status = OB_STATE_NOT_MATCH; break;
      case 32: service->bind_sql(*versions); versions->fixed = true; reserve_status = OB_STATE_NOT_MATCH; break;
      case 33: versions->fail_at = 2; reserve_status = OB_TIMEOUT; break;
      case 34: versions->throw_at = 2; reserve_status = OB_ALLOCATE_MEMORY_FAILED; break;
      case 35: target.clear_actor(); reserve_status = OB_INVALID_ARGUMENT; break;
      case 36: changes.at(0).grantor_ = OB_INVALID_ID; reserve_status = OB_INVALID_ARGUMENT; break;
      case 37: kind = Kind::REVOKE; reserve_status = OB_INVALID_ARGUMENT; break;
    }
    Reservation token;
    CHECK(token.count() == 0 && token.version_at(0) == OB_INVALID_VERSION);
    CHECK(Reservation::reserve(*service,transaction,target,kind,changes,token) == reserve_status);
    CHECK(transaction.reads == 0 && transaction.writes == 0 && transaction.starts == 0 && transaction.ends == 0);
    if (reserve_status != OB_SUCCESS) {
      CHECK(token.count() == 0 && token.version_at(0) == OB_INVALID_VERSION);
      CHECK(versions->calls == (scenario >= 32 && scenario <= 34 ? 2 : 0));
      continue;
    }
    CHECK(token.count() == 2 && token.version_at(0) == 5001 && token.version_at(1) == 5002);
    CHECK(token.version_at(-1) == OB_INVALID_VERSION && token.version_at(2) == OB_INVALID_VERSION);
    CHECK(Reservation::reserve(*service,transaction,target,kind,changes,token) == OB_INIT_TWICE);
    CHECK(versions->calls == 2 && token.count() == 2);
    if (scenario == 2) {
      // A later script operation reserves a newer version before this plan is
      // executed. Taking this plan must retain 5001/5002, not allocate 5004+.
      int64_t later = 0;
      CHECK(service->gen_new_schema_version(later) == OB_SUCCESS && later == 5003);
    }
    Reservation moved(std::move(token)); CHECK(token.count() == 0);
    token = std::move(moved); CHECK(moved.count() == 0);
    switch (scenario) {
      case 4: service->bind_sql(*replacement); break;
      case 6: transaction.active = false; break;
      case 7: CHECK(target.bind_actor(125,roles) == OB_SUCCESS); break;
      case 8: target.enabled_roles_.reset(); break;
      case 9: target.routine_.set_routine_id(1002); break;
      case 10: target.routine_.set_schema_version(43); break;
      case 11: target.routine_.set_owner_id(999); break;
      case 12: target.routine_.set_overload(78); break;
      case 13: CHECK(target.routine_.set_routine_name(ObString::make_string("changed")) == OB_SUCCESS); break;
      case 14: CHECK(target.routine_.set_native_binding(ObString::make_string("org.seekdb.gis"),
          ObString::make_string("org.seekdb.gis.other"),1) == OB_SUCCESS); break;
      case 15: target.routine_.get_routine_params().at(0)->set_param_type(ObVarcharType); break;
      case 16: kind = Kind::REVOKE; break;
      case 17: changes.at(0).grantor_ = 125; break;
      case 18: changes.at(0).grantee_ = 128; break;
      case 19: changes.at(0).before_ = packed(1,0); break;
      case 20: changes.at(0).after_ = packed(1,1); break;
      case 21: std::swap(changes.at(0),changes.at(1)); break;
      case 22: changes.pop_back(); break;
      case 23: target.signature_qualified_ = false; break;
    }
    ObSEArray<int64_t,4> taken; CHECK(taken.push_back(999) == OB_SUCCESS);
    const int ret = token.take(scenario == 3 ? *other_service : *service,
        scenario == 5 ? other_transaction : transaction,target,kind,changes,taken);
    CHECK(ret == (scenario < 3 ? OB_SUCCESS : OB_STATE_NOT_MATCH));
    CHECK(token.count() == 0 && versions->calls == (scenario == 2 ? 3 : 2) && replacement->calls == 0);
    if (ret == OB_SUCCESS) CHECK(taken.count() == 2 && taken.at(0) == 5001 && taken.at(1) == 5002);
    else CHECK(taken.empty());
    CHECK(token.take(*service,transaction,target,kind,changes,taken) == OB_STATE_NOT_MATCH && taken.empty());
    CHECK(transaction.reads == 0 && transaction.writes == 0 && transaction.starts == 0 && transaction.ends == 0);
  }
  std::cout << "PASS: 38 native ACL version reservation scenarios: owned one-shot target/actor/role/plan binding, service/transaction mismatch, ordered real versions, move, failed allocation and no take-time allocation or SQL" << std::endl;
  for (int scenario = 0; scenario < 10; ++scenario) {
    auto service = std::make_unique<native_routine_grant_test::Service>();
    auto other_service = std::make_unique<native_routine_grant_test::Service>();
    ObMySQLProxy proxy;
    auto versions = std::make_unique<native_routine_grant_test::Versions>(proxy, *service);
    service->bind_sql(*versions); other_service->bind_sql(*versions);
    ExtensionVersionRows transaction, other_transaction;
    transaction.active = other_transaction.active = true;
    ObRoutineInfo routine; CHECK(routine.assign(prototype) == OB_SUCCESS);
    CHECK(routine.set_native_binding(ObString::make_string("org.seekdb.gis"),
        ObString::make_string("org.seekdb.gis.area"), 1) == OB_SUCCESS);
    Reservation token;
    if (scenario == 8) routine.set_owner_id(0);
    if (scenario == 9) versions->fail_at = 1;
    const int reserved = Reservation::reserve_create_owner(*service, transaction, routine, token);
    CHECK(reserved == (scenario == 8 ? OB_INVALID_ARGUMENT : scenario == 9 ? OB_TIMEOUT : OB_SUCCESS));
    if (reserved != OB_SUCCESS) {
      CHECK(token.count() == 0 && versions->calls == (scenario == 9 ? 1 : 0));
    } else {
      CHECK(token.count() == 1 && token.version_at(0) == 5001);
      CHECK(Reservation::reserve_create_owner(*service, transaction, routine, token) == OB_INIT_TWICE);
      if (scenario == 3) transaction.active = false;
      if (scenario == 4) routine.set_owner_id(routine.get_owner_id() + 1);
      if (scenario == 5) routine.set_overload(routine.get_overload() + 1);
      if (scenario == 6) routine.set_schema_version(routine.get_schema_version() + 1);
      if (scenario == 7) routine.set_routine_id(0); // Failure before generic take must consume too.
      int64_t taken = 99;
      const int ret = token.take_create_owner(scenario == 1 ? *other_service : *service,
          scenario == 2 ? other_transaction : transaction, routine, taken);
      CHECK(ret == (scenario == 0 ? OB_SUCCESS : scenario == 7 ? OB_INVALID_ARGUMENT : OB_STATE_NOT_MATCH));
      CHECK(taken == (scenario == 0 ? 5001 : OB_INVALID_VERSION));
      CHECK(token.count() == 0 && versions->calls == 1);
      CHECK(token.take_create_owner(*service, transaction, routine, taken) != OB_SUCCESS);
      CHECK(taken == OB_INVALID_VERSION);
    }
    CHECK(transaction.reads == 0 && transaction.writes == 0 && transaction.starts == 0 && transaction.ends == 0);
  }
  std::cout << "PASS: 10 native CREATE owner ACL reservation scenarios: exact one-shot implicit grant, service/transaction/identity mismatch, invalid target and allocation failure; no SQL/authorization/commit" << std::endl;
}
}
