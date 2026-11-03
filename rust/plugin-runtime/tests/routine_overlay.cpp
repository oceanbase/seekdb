// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Production schema value types + owned overlay. No guard/SQL/session fixture.
#include "share/schema/routine_schema_overlay.h"
#include "lib/charset/ob_charset.h"
#include <cstdlib>
#include <iostream>

#define CHECK(expr) do { if (!(expr)) { std::cerr << __LINE__ << ": " << #expr << std::endl; std::abort(); } } while (false)
using namespace oceanbase::common;
using namespace oceanbase::share::schema;

static std::vector<char> wire(const ObRoutineInfo &routine)
{
  std::vector<char> bytes(routine.get_serialize_size());
  int64_t position = 0;
  CHECK(routine.serialize(bytes.data(), bytes.size(), position) == OB_SUCCESS);
  CHECK(position == static_cast<int64_t>(bytes.size()));
  return bytes;
}

int main()
{
  CHECK(ObCharset::init_charset() == OB_SUCCESS);
  RoutineSchemaOverlay overlay;
  ObRoutineInfo source;
  source.set_database_id(100);
  source.set_routine_id(1001);
  source.set_owner_id(123);
  source.set_package_id(OB_INVALID_ID);
  source.set_overload(0);
  source.set_subprogram_id(0);
  source.set_routine_type(ROUTINE_FUNCTION_TYPE);
  source.set_schema_version(42);
  CHECK(source.set_routine_name(ObString::make_string("ext_value")) == OB_SUCCESS);
  CHECK(source.set_routine_body(ObString::make_string("RETURN 1")) == OB_SUCCESS);
  CHECK(source.set_comment(ObString::make_string("first version")) == OB_SUCCESS);
  ObRoutineParam parameter;
  parameter.set_routine_id(1001);
  parameter.set_sequence(1);
  parameter.set_subprogram_id(0);
  parameter.set_param_position(1);
  parameter.set_param_level(0);
  parameter.set_param_type(ObIntType);
  parameter.set_schema_version(42);
  CHECK(parameter.set_param_name(ObString::make_string("arg")) == OB_SUCCESS);
  CHECK(source.add_routine_param(parameter) == OB_SUCCESS);
  auto original = wire(source);
  CHECK(overlay.stage(source) == OB_SUCCESS && overlay.record_count() == 1);
  const auto lookup_name = [&](uint64_t db, const char *name, ObRoutineType type, bool expected_handled) {
    const ObRoutineInfo *found = &source;
    bool handled = !expected_handled;
    CHECK(overlay.lookup(db, OB_INVALID_ID, ObString::make_string(name), 0, type, handled, found) == OB_SUCCESS);
    CHECK(handled == expected_handled);
    if (!handled) CHECK(found == nullptr);
    return found;
  };
  const auto lookup_id = [&](uint64_t id, bool expected_handled) {
    const ObRoutineInfo *found = &source;
    bool handled = !expected_handled;
    CHECK(overlay.lookup(id, handled, found) == OB_SUCCESS && handled == expected_handled);
    if (!handled) CHECK(found == nullptr);
    return found;
  };
  const ObRoutineInfo *first = lookup_name(100, "EXT_VALUE", ROUTINE_FUNCTION_TYPE, true);
  CHECK(first != &source && wire(*first) == original && lookup_id(1001, true) == first);
  lookup_name(101, "ext_value", ROUTINE_FUNCTION_TYPE, false);
  lookup_name(100, "ext_value", ROUTINE_PROCEDURE_TYPE, false);
  lookup_name(100, "missing", ROUTINE_FUNCTION_TYPE, false);
  lookup_id(9999, false);
  // ALTER deep-copies the new full state, retaining all previous borrowed views.
  CHECK(source.set_comment(ObString::make_string("second version")) == OB_SUCCESS);
  CHECK(source.set_routine_body(ObString::make_string("RETURN 2")) == OB_SUCCESS);
  CHECK(overlay.stage(source) == OB_SUCCESS);
  const auto *second = lookup_id(1001, true);
  CHECK(second != first && wire(*second) == wire(source) && wire(*first) == original);
  const auto second_wire = wire(*second);
  source.reset();
  CHECK(wire(*second) == second_wire && wire(*first) == original);
  // Name and ID tombstones agree, including case-folded lookups.
  CHECK(overlay.erase(100, ObString::make_string("EXT_VALUE"), ROUTINE_FUNCTION_TYPE, 1001) == OB_SUCCESS);
  CHECK(lookup_name(100, "ext_value", ROUTINE_FUNCTION_TYPE, true) == nullptr);
  CHECK(lookup_id(1001, true) == nullptr && wire(*first) == original);
  CHECK(overlay.stage(*second) == OB_STATE_NOT_MATCH); // never recycle a deleted ID
  CHECK(source.assign(*second) == OB_SUCCESS);
  source.set_routine_id(1002);
  CHECK(overlay.stage(source) == OB_SUCCESS);
  CHECK(lookup_name(100, "ext_value", ROUTINE_FUNCTION_TYPE, true)->get_routine_id() == 1002);
  CHECK(lookup_id(1001, true) == nullptr);
  // Wrong identity cannot erase/replace the new live object or mutate indices.
  const size_t retained = overlay.record_count();
  const int64_t retained_bytes = overlay.schema_bytes();
  CHECK(overlay.erase(100, ObString::make_string("ext_value"), ROUTINE_FUNCTION_TYPE, 1001) == OB_STATE_NOT_MATCH);
  source.set_routine_id(1003);
  CHECK(overlay.stage(source) == OB_STATE_NOT_MATCH);
  source.set_routine_id(1002);
  CHECK(source.set_routine_name(ObString::make_string("renamed")) == OB_SUCCESS);
  CHECK(overlay.stage(source) == OB_STATE_NOT_MATCH);
  CHECK(overlay.record_count() == retained && overlay.schema_bytes() == retained_bytes);
  lookup_id(1003, false);
  lookup_name(100, "renamed", ROUTINE_FUNCTION_TYPE, false);
  // Functions, procedures and databases are separate names; IDs remain global.
  CHECK(source.set_routine_name(ObString::make_string("ext_value")) == OB_SUCCESS);
  source.set_routine_type(ROUTINE_PROCEDURE_TYPE);
  source.set_routine_id(2001);
  CHECK(overlay.stage(source) == OB_SUCCESS);
  CHECK(lookup_name(100, "ext_value", ROUTINE_PROCEDURE_TYPE, true)->get_routine_id() == 2001);
  source.set_database_id(101);
  source.set_routine_id(2002);
  CHECK(overlay.stage(source) == OB_SUCCESS);
  CHECK(lookup_name(101, "ext_value", ROUTINE_PROCEDURE_TYPE, true)->get_routine_id() == 2002);
  // A base-schema removal can be recorded without copying the old body.
  CHECK(overlay.erase(100, ObString::make_string("base_only"), ROUTINE_FUNCTION_TYPE, 3001) == OB_SUCCESS);
  CHECK(lookup_name(100, "BASE_ONLY", ROUTINE_FUNCTION_TYPE, true) == nullptr);
  CHECK(lookup_id(3001, true) == nullptr);
  for (uint64_t package : {uint64_t{10}, OB_INVALID_ID}) {
    const ObRoutineInfo *found = first;
    bool handled = true;
    const uint64_t overload = package == OB_INVALID_ID ? 1 : 0;
    CHECK(overlay.lookup(100, package, ObString::make_string("ext_value"), overload,
                         ROUTINE_FUNCTION_TYPE, handled, found) == OB_SUCCESS);
    CHECK(!handled && found == nullptr);
  }
  for (uint64_t invalid : {uint64_t{0}, OB_INVALID_ID, uint64_t{1} << 63}) {
    const ObRoutineInfo *found = first;
    bool handled = true;
    CHECK(overlay.lookup(invalid, handled, found) == OB_INVALID_ARGUMENT && !handled && found == nullptr);
    CHECK(overlay.erase(100, ObString::make_string("ext_value"), ROUTINE_FUNCTION_TYPE, invalid) == OB_INVALID_ARGUMENT);
    source.set_routine_id(invalid);
    CHECK(overlay.stage(source) == OB_INVALID_ARGUMENT);
  }
  // Capacity failure retains all old tombstones and leaves no new index entry.
  RoutineSchemaOverlay full;
  for (size_t i = 0; i < RoutineSchemaOverlay::MAX_RECORDS; ++i) {
    const auto name = std::to_string(i);
    CHECK(full.erase(100, ObString(name.size(), name.data()), ROUTINE_FUNCTION_TYPE, i + 1) == OB_SUCCESS);
  }
  CHECK(full.erase(100, ObString::make_string("overflow"), ROUTINE_FUNCTION_TYPE, 99999) == OB_SIZE_OVERFLOW);
  CHECK(full.record_count() == RoutineSchemaOverlay::MAX_RECORDS);
  bool handled = true;
  const ObRoutineInfo *found = first;
  CHECK(full.lookup(99999, handled, found) == OB_SUCCESS && !handled && found == nullptr);
  CHECK(full.lookup(1, handled, found) == OB_SUCCESS && handled && found == nullptr);
  {
    // The bound is binary length, not a mistaken ASCII-only character limit.
    std::string name;
    for (int i = 0; i < 60; ++i) name += "函数";
    CHECK(source.assign(*second) == OB_SUCCESS);
    source.set_routine_id(5001);
    CHECK(source.set_routine_name(ObString(name.size(), name.data())) == OB_SUCCESS);
    CHECK(overlay.stage(source) == OB_SUCCESS);
    found = nullptr; handled = false;
    CHECK(overlay.lookup(100, OB_INVALID_ID, ObString(name.size(), name.data()), 0,
                         ROUTINE_FUNCTION_TYPE, handled, found) == OB_SUCCESS);
    CHECK(handled && found->get_routine_id() == 5001);
    const auto retained_count = overlay.record_count();
    source.set_package_id(10);
    CHECK(overlay.stage(source) == OB_INVALID_ARGUMENT);
    source.set_package_id(OB_INVALID_ID);
    source.set_overload(1);
    CHECK(overlay.stage(source) == OB_INVALID_ARGUMENT);
    source.set_overload(0);
    const std::string oversized_name(OB_MAX_ROUTINE_NAME_BINARY_LENGTH + 1, 'n');
    CHECK(source.set_routine_name(ObString(oversized_name.size(), oversized_name.data())) == OB_SUCCESS);
    CHECK(overlay.stage(source) == OB_INVALID_ARGUMENT);
    CHECK(overlay.record_count() == retained_count);
  }
  {
    CHECK(source.assign(*second) == OB_SUCCESS);
    source.set_routine_id(6001);
    CHECK(source.set_routine_name(ObString::make_string("oversized_body")) == OB_SUCCESS);
    const std::string body(RoutineSchemaOverlay::MAX_SCHEMA_BYTES, 'x');
    CHECK(source.set_routine_body(ObString(body.size(), body.data())) == OB_SUCCESS);
    const auto count_before = overlay.record_count();
    const auto bytes_before = overlay.schema_bytes();
    CHECK(overlay.stage(source) == OB_SIZE_OVERFLOW);
    CHECK(overlay.record_count() == count_before && overlay.schema_bytes() == bytes_before);
    lookup_id(6001, false);
  }
  return 0;
}
