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
#include "sql/resolver/ddl/extension_routine_batch.h"
#include <vector>

// Real RPC codecs and owning mixed-operation container. No DCL execution or
// SQL transaction is simulated. The supplied native declaration came from the
// actual CREATE resolver in the GIS fixture.
inline void verify_extension_dcl_batch(const oceanbase::share::schema::ObRoutineInfo &routine)
{
  using namespace oceanbase::common;
  using namespace oceanbase::share;
  using namespace oceanbase::share::schema;
  using namespace oceanbase::sql;
  using namespace oceanbase::obcall;
  using Operation = ExtensionRoutineUpdateBatch::Operation;
  using Kind = Operation::Kind;
  const auto encode = [](const auto &arg) {
    std::vector<char> bytes(arg.get_serialize_size()); int64_t position = 0;
    CHECK(arg.serialize(bytes.data(), bytes.size(), position) == OB_SUCCESS);
    CHECK(position == int64_t(bytes.size()));
    return bytes;
  };
  ExtensionRoutineUpdateBatch batch;
  std::vector<char> expected_grant, expected_revoke;
  {
    ObGrantArg grant;
    ObRevokeRoutineArg revoke;
    ObCreateRoutineArg create;
    ObDropRoutineArg drop;
    std::string database = "native_db", user = "package_reader", host = "localhost";
    std::string audit = "private DCL audit text must never appear in operation diagnostics";
    ObSEArray<uint64_t, 4> roles;
    for (uint64_t role : {125, 124, 125}) CHECK(roles.push_back(role) == OB_SUCCESS);
    CHECK(grant.native_target_.assign(routine, true) == OB_SUCCESS);
    CHECK(revoke.native_target_.assign(routine, true) == OB_SUCCESS);
    CHECK(grant.native_target_.bind_actor(123, roles) == OB_SUCCESS);
    CHECK(revoke.native_target_.bind_actor(123, roles) == OB_SUCCESS);
    grant.priv_level_ = OB_PRIV_ROUTINE_LEVEL; grant.object_type_ = ObObjectType::FUNCTION;
    grant.object_id_ = routine.get_routine_id(); grant.table_ = routine.get_routine_name();
    grant.db_ = ObString(database.size(), database.data()); grant.grantor_id_ = 123;
    grant.priv_set_ = OB_PRIV_EXECUTE | OB_PRIV_ALTER_ROUTINE | OB_PRIV_GRANT;
    grant.option_ = GRANT_OPTION;
    CHECK(grant.users_passwd_.push_back(ObString(user.size(), user.data())) == OB_SUCCESS);
    CHECK(grant.users_passwd_.push_back(ObString()) == OB_SUCCESS);
    CHECK(grant.hosts_.push_back(ObString(host.size(), host.data())) == OB_SUCCESS);
    revoke.db_ = grant.db_; revoke.routine_ = grant.table_; revoke.grantor_id_ = 123;
    revoke.obj_id_ = grant.object_id_; revoke.obj_type_ = uint64_t(ObObjectType::FUNCTION);
    revoke.priv_set_ = OB_PRIV_EXECUTE; revoke.grant_option_only_ = true;
    revoke.revoke_behavior_ = ObRevokeRoutineArg::REVOKE_CASCADE;
    ObSEArray<uint64_t, 4> grantees;
    for (uint64_t grantee : {202, 201, 202}) CHECK(grantees.push_back(grantee) == OB_SUCCESS);
    CHECK(revoke.set_native_grantees(grantees) == OB_SUCCESS);
    for (ObDDLArg *arg : {static_cast<ObDDLArg *>(&grant), static_cast<ObDDLArg *>(&revoke)}) {
      arg->ddl_stmt_str_ = ObString(audit.size(), audit.data());
      arg->task_id_ = 987;
      CHECK(arg->based_schema_object_infos_.push_back(ObBasedSchemaObjectInfo(
          routine.get_routine_id(), ROUTINE_SCHEMA, routine.get_schema_version())) == OB_SUCCESS);
    }
    CHECK(grant.is_valid() && revoke.is_valid());
    expected_grant = encode(grant); expected_revoke = encode(revoke);
    ObSEArray<Operation, 4> input;
    CHECK(input.push_back({Kind::CREATE, &create}) == OB_SUCCESS);
    CHECK(input.push_back({Kind::GRANT, nullptr, nullptr, &grant}) == OB_SUCCESS);
    CHECK(input.push_back({Kind::REVOKE, nullptr, nullptr, nullptr, &revoke}) == OB_SUCCESS);
    CHECK(input.push_back({Kind::DROP, nullptr, &drop}) == OB_SUCCESS);
    CHECK(input.push_back({Kind::GRANT, nullptr, nullptr, &grant}) == OB_SUCCESS);
    CHECK(batch.assign(input) == OB_SUCCESS);
    // Original containers and strings die before checking any owned view.
    std::fill(database.begin(), database.end(), 'x'); std::fill(user.begin(), user.end(), 'x');
    std::fill(host.begin(), host.end(), 'x'); std::fill(audit.begin(), audit.end(), 'x');
  }
  for (int pass = 0; pass < 2; ++pass) {
    CHECK(batch.operations().count() == 5);
    const Kind expected[] = {Kind::CREATE, Kind::GRANT, Kind::REVOKE, Kind::DROP, Kind::GRANT};
    for (int i = 0; i < 5; ++i) {
      const auto &operation = batch.operations().at(i);
      CHECK(operation.has_valid_shape() && operation.kind_ == expected[i]);
      CHECK(operation.is_schema_change() == (i == 0 || i == 3));
      if (operation.grant_arg_) CHECK(encode(*operation.grant_arg_) == expected_grant);
      if (operation.revoke_arg_) CHECK(encode(*operation.revoke_arg_) == expected_revoke);
      char diagnostics[512]{};
      const int64_t size = operation.to_string(diagnostics, sizeof(diagnostics));
      CHECK(size > 0 && std::string(diagnostics, size).find("private") == std::string::npos);
    }
    const auto &grant = *batch.operations().at(1).grant_arg_;
    const auto &revoke = *batch.operations().at(2).revoke_arg_;
    CHECK(grant.native_target_.check(&routine) == OB_SUCCESS && grant.native_target_.actor_id_ == 123);
    CHECK(grant.native_target_.enabled_roles_.count() == 2 && grant.native_target_.enabled_roles_.at(0) == 124);
    CHECK(grant.users_passwd_.at(0) == ObString::make_string("package_reader"));
    CHECK(revoke.native_grantees_.count() == 2 && revoke.native_grantees_.at(0) == 201);
    CHECK(revoke.user_id_ == OB_INVALID_ID && revoke.grant_option_only_);
    CHECK(revoke.revoke_behavior_ == ObRevokeRoutineArg::REVOKE_CASCADE);
    CHECK(batch.assign(batch.operations()) == OB_SUCCESS);
  }
  // Every kind/pointer combination, including multiple simultaneous payloads.
  ObCreateRoutineArg create; ObDropRoutineArg drop; ObGrantArg grant; ObRevokeRoutineArg revoke;
  for (Kind kind : {Kind::INVALID, Kind::CREATE, Kind::DROP, Kind::ALTER, Kind::GRANT, Kind::REVOKE,
                    static_cast<Kind>(255)}) for (int mask = 0; mask < 16; ++mask) {
    Operation operation{kind, mask & 1 ? &create : nullptr, mask & 2 ? &drop : nullptr,
        mask & 4 ? &grant : nullptr, mask & 8 ? &revoke : nullptr};
    const bool valid = ((kind == Kind::CREATE || kind == Kind::ALTER) && mask == 1) ||
        (kind == Kind::DROP && mask == 2) || (kind == Kind::GRANT && mask == 4) ||
        (kind == Kind::REVOKE && mask == 8);
    CHECK(operation.has_valid_shape() == valid);
    if (!valid) {
      ObSEArray<Operation, 2> input;
      CHECK(input.push_back({Kind::GRANT, nullptr, nullptr, &grant}) == OB_SUCCESS);
      CHECK(batch.assign(input) == OB_SUCCESS);
      CHECK(input.push_back(operation) == OB_SUCCESS);
      CHECK(batch.assign(input) == OB_INVALID_ARGUMENT && batch.operations().empty());
    }
  }
  // The common total-byte budget applies to DCL, not just schema payloads.
  std::string large(ExtensionRoutineUpdateBatch::MAX_WIRE_BYTES / 2, 'x');
  grant.ddl_stmt_str_ = ObString(large.size(), large.data());
  ObSEArray<Operation, 2> input;
  CHECK(input.push_back({Kind::GRANT, nullptr, nullptr, &grant}) == OB_SUCCESS);
  CHECK(batch.assign(input) == OB_SUCCESS);
  CHECK(input.push_back({Kind::GRANT, nullptr, nullptr, &grant}) == OB_SUCCESS);
  CHECK(batch.assign(input) == OB_SIZE_OVERFLOW && batch.operations().empty());
  input.reset(); grant.ddl_stmt_str_.reset();
  CHECK(input.push_back({Kind::GRANT, nullptr, nullptr, &grant}) == OB_SUCCESS);
  CHECK(batch.assign(input) == OB_SUCCESS);
  // A malformed bound target must not leave a preceding successful operation.
  grant.native_target_.resolved_ = true;
  CHECK(batch.assign(input) == OB_INVALID_ARGUMENT && batch.operations().empty());
  std::cout << "PASS: ordered DDL/DCL owned wire, native identity/actor/roles/recipients/options, self-copy, "
      "112 shape cases, failure clearing and total byte limit; no package DCL execution claims" << std::endl;
}
