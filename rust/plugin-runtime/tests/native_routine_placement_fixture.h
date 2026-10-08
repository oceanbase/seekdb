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
#include "share/schema/native_routine_create_slot.h"
#include "routine_version_reservation_fixture.h"

namespace native_routine_placement_test {
using namespace oceanbase::common;
using namespace oceanbase::share::schema;
using namespace oceanbase::rootserver;

inline void declaration(ObRoutineInfo &routine, uint64_t id, int64_t slot, ObObjType input)
{
  routine.set_database_id(100); routine.set_owner_id(123); routine.set_routine_id(id);
  routine.set_schema_version(42); routine.set_package_id(OB_INVALID_ID); routine.set_subprogram_id(0);
  routine.set_overload(slot); routine.set_routine_type(ROUTINE_FUNCTION_TYPE);
  CHECK(routine.set_routine_name("place_value") == OB_SUCCESS);
  CHECK(routine.set_native_binding(ObString::make_string("org.seekdb.test"), ObString::make_string("value"), 1) == OB_SUCCESS);
  for (int i = 0; i < 2; ++i) {
    ObRoutineParam parameter;
    parameter.set_sequence(i); parameter.set_param_position(i); parameter.set_subprogram_id(0);
    parameter.set_param_level(0); parameter.set_param_type(i == 0 ? ObDoubleType : input);
    parameter.set_in_sp_param_flag();
    CHECK(routine.add_routine_param(parameter) == OB_SUCCESS);
  }
}

inline void run()
{
  ObRoutineInfo a, b, incoming, changed;
  declaration(a, 1001, 7, ObIntType);
  declaration(b, 1002, 900, ObDoubleType);
  declaration(incoming, OB_INVALID_ID, 12345, ObGeometryType);
  ObSEArray<const ObRoutineInfo *, 4> family;
  int64_t slot = 99;
  CHECK(NativeRoutineCreateSlot::select(incoming, family, slot) == OB_SUCCESS && slot == 0);
  CHECK(family.push_back(&a) == OB_SUCCESS && family.push_back(&b) == OB_SUCCESS);
  CHECK(NativeRoutineCreateSlot::select(incoming, family, slot) == OB_SUCCESS && slot == 901);
  std::swap(family.at(0), family.at(1));
  CHECK(NativeRoutineCreateSlot::select(incoming, family, slot) == OB_SUCCESS && slot == 901);
  CHECK(NativeRoutineCreateSlot::select(a, family, slot) == OB_ERR_SP_ALREADY_EXISTS && slot == -1);
  for (int mutation = 0; mutation < 8; ++mutation) {
    CHECK(changed.assign(a) == OB_SUCCESS);
    if (mutation == 0) changed.set_overload(900);
    if (mutation == 1) changed.set_routine_id(1002);
    if (mutation == 2) changed.set_database_id(101);
    if (mutation == 3) changed.set_schema_version(0);
    if (mutation == 4) changed.set_overload(-1);
    if (mutation == 5) changed.set_overload(INT64_MAX);
    if (mutation == 6) CHECK(changed.set_native_binding(ObString(), ObString(), 0) == OB_SUCCESS);
    if (mutation == 7) changed.get_routine_params().at(1)->set_param_type(ObDoubleType);
    family.at(1) = &changed;
    const int expected = mutation == 5 ? OB_SIZE_OVERFLOW : mutation == 6 ? OB_ERR_SP_ALREADY_EXISTS : OB_STATE_NOT_MATCH;
    CHECK(NativeRoutineCreateSlot::select(incoming, family, slot) == expected && slot == -1);
  }
  family.reset();
  for (int i = 0; i < NativeRoutineCreateSlot::MAX_FAMILY; ++i) CHECK(family.push_back(&a) == OB_SUCCESS);
  CHECK(NativeRoutineCreateSlot::select(incoming, family, slot) == OB_SIZE_OVERFLOW && slot == -1);

  // Real guard/private-family lookup sees preceding declarations and DROP;
  // placement itself never stages or allocates a catalog identity.
  auto manager = std::make_unique<ObSchemaMgr>();
  auto service = std::make_unique<routine_reservation_test::VersionService>();
  CHECK(manager->init() == OB_SUCCESS);
  CHECK(MockSchemaService::set_name_case_mode(*manager, OB_ORIGIN_AND_INSENSITIVE) == OB_SUCCESS);
  ObSchemaGetterGuard guard;
  CHECK(MockSchemaService::bind(guard, *service, *manager) == OB_SUCCESS);
  auto privileges = std::make_shared<RoutinePrivilegeOverlay>();
  auto view = std::make_shared<RoutineSchemaOverlay>(privileges);
  CHECK(guard.attach_routine_overlay(view) == OB_SUCCESS && view->stage(a) == OB_SUCCESS);
  CHECK(NativeRoutineCreateSlot::assign(guard, b) == OB_SUCCESS && b.get_overload() == 8);
  CHECK(view->stage(b) == OB_SUCCESS);
  CHECK(NativeRoutineCreateSlot::assign(guard, incoming) == OB_SUCCESS && incoming.get_overload() == 9);
  {
    RoutineCatalogSavepoint savepoint(view, privileges);
    CHECK(view->erase(100, b.get_routine_name(), ROUTINE_FUNCTION_TYPE, b.get_routine_id(), b.get_overload()) == OB_SUCCESS);
    CHECK(NativeRoutineCreateSlot::assign(guard, incoming) == OB_SUCCESS && incoming.get_overload() == 8);
    CHECK(savepoint.rollback() == OB_SUCCESS);
  }
  CHECK(NativeRoutineCreateSlot::assign(guard, incoming) == OB_SUCCESS && incoming.get_overload() == 9);

  ObMySQLProxy proxy;
  routine_version_test::Allocator allocator(proxy, *service);
  service->bind_sql(allocator);
  routine_version_test::Transaction transaction;
  for (int64_t chosen : {0, 7}) for (int mutation = 0; mutation < 5; ++mutation) {
    CHECK(changed.assign(a) == OB_SUCCESS); changed.set_overload(chosen);
    RoutineIdReservation id;
    CHECK(RoutineIdReservation::reserve(allocator, changed, id) == OB_SUCCESS);
    changed.set_routine_id(id.id());
    RoutineVersionReservation version;
    CHECK(RoutineVersionReservation::reserve(*service, transaction, changed, nullptr, version) == OB_SUCCESS);
    changed.set_schema_version(version.version());
    if (mutation == 1) changed.set_overload(chosen + 1);
    if (mutation == 2) changed.get_routine_params().at(1)->set_param_type(ObDoubleType);
    if (mutation == 3) CHECK(changed.set_native_binding(ObString(), ObString(), 0) == OB_SUCCESS);
    if (mutation == 4) changed.get_routine_params().at(0)->set_param_type(ObIntType); // Result is not input identity.
    const int expected = mutation == 0 || mutation == 4 ? OB_SUCCESS : OB_STATE_NOT_MATCH;
    const int allocated = allocator.calls_, ids = allocator.ids_;
    uint64_t result_id = 0;
    int64_t result_version = 0, parameters = 0;
    CHECK(id.take(allocator, changed, result_id) == expected);
    CHECK(version.take(*service, transaction, changed, nullptr, result_version, parameters) == expected);
    CHECK(id.id() == OB_INVALID_ID && version.version() == OB_INVALID_VERSION);
    CHECK(allocator.calls_ == allocated && allocator.ids_ == ids);
    CHECK(id.take(allocator, changed, result_id) == OB_STATE_NOT_MATCH);
    CHECK(version.take(*service, transaction, changed, nullptr, result_version, parameters) == OB_STATE_NOT_MATCH);
  }
  for (int mutation = 0; mutation < 3; ++mutation) {
    CHECK(changed.assign(a) == OB_SUCCESS);
    if (mutation == 1) changed.set_overload(8);
    if (mutation == 2) changed.get_routine_params().at(1)->set_param_type(ObDoubleType);
    RoutineVersionReservation version;
    const int count = allocator.calls_;
    CHECK(RoutineVersionReservation::reserve(*service, transaction, changed, &a, version) ==
        (mutation == 0 ? OB_SUCCESS : OB_INVALID_ARGUMENT));
    if (mutation != 0) CHECK(allocator.calls_ == count && version.version() == OB_INVALID_VERSION);
    else {
      changed.set_schema_version(version.version());
      int64_t result = 0, parameters = 0;
      CHECK(version.take(*service, transaction, changed, &a, result, parameters) == OB_SUCCESS);
      CHECK(parameters > 42 && result > parameters);
      CHECK(RoutineVersionReservation::reserve_drop(*service, transaction, changed, version) == OB_SUCCESS);
      CHECK(version.take_drop(*service, transaction, changed, result) == OB_SUCCESS);
    }
  }
  std::cout << "PASS: native slot allocation, duplicate/corrupt family rejection, private CREATE/DROP rollback and slot/input-pinned ID/version/ALTER/DROP reservations; no live sequence or commit claims" << std::endl;
}
}
