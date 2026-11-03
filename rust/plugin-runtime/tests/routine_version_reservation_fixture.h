// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Real version reservation/operator and SQL generation with controlled version
// allocation and a recording, failing transport. No durable sequence or commit.
#ifndef SEEKDB_TEST_ROUTINE_VERSION_RESERVATION_FIXTURE_H_
#define SEEKDB_TEST_ROUTINE_VERSION_RESERVATION_FIXTURE_H_
#include "routine_id_reservation_fixture.h"

namespace routine_version_test {
using namespace oceanbase::common;
using namespace oceanbase::share::schema;
using namespace oceanbase::rootserver;

class Allocator final : public ObSchemaServiceSQLImpl {
public:
  Allocator(ObMySQLProxy &proxy, ObMultiVersionSchemaService &service)
      : ObSchemaServiceSQLImpl(nullptr, proxy, service) {}
  int calls_ = 0, fail_at_ = -1, throw_at_ = -1, ids_ = 0;
  int64_t next_ = 1001;
  bool fixed_ = false;
  int gen_new_schema_version(int64_t refreshed, int64_t &version) override {
    CHECK(refreshed == 42);
    ++calls_;
    version = next_;
    if (!fixed_ && next_ < INT64_MAX) ++next_;
    if (calls_ == throw_at_) throw std::bad_alloc();
    return calls_ == fail_at_ ? OB_TIMEOUT : OB_SUCCESS;
  }
  int fetch_new_sys_pl_object_id(uint64_t &id) override {
    CHECK(id == OB_INVALID_ID);
    id = 400000 + ++ids_;
    return OB_SUCCESS;
  }
};

class Transaction final : public ObMySQLTransaction {
public:
  bool active_ = true;
  std::string fail_table_ = "__all_routine";
  std::vector<std::string> writes_;
  bool is_started() const override { return active_; }
  int write(const char *sql, int32_t, int64_t &affected) override {
    writes_.emplace_back(sql);
    affected = 1;
    return writes_.back().find(fail_table_) != std::string::npos ? OB_TIMEOUT : OB_SUCCESS;
  }
};

inline void run()
{
  static_assert(!std::is_copy_constructible_v<RoutineVersionReservation>);
  static_assert(!std::is_copy_assignable_v<RoutineVersionReservation>);
  static_assert(std::is_nothrow_move_constructible_v<RoutineVersionReservation>);
  static_assert(std::is_nothrow_move_assignable_v<RoutineVersionReservation>);
  auto service = std::make_unique<routine_reservation_test::VersionService>();
  auto other_service = std::make_unique<routine_reservation_test::VersionService>();
  ObMySQLProxy proxy;
  auto allocator = std::make_unique<Allocator>(proxy, *service);
  auto other_allocator = std::make_unique<Allocator>(proxy, *service);
  service->bind_sql(*allocator);
  other_service->bind_sql(*allocator);
  Transaction transaction, other_transaction;
  ObRoutineInfo original;
  original.set_database_id(100);
  original.set_owner_id(123);
  original.set_routine_id(311234);
  original.set_schema_version(42);
  original.set_package_id(OB_INVALID_ID);
  original.set_overload(0);
  original.set_subprogram_id(0);
  original.set_routine_type(ROUTINE_FUNCTION_TYPE);
  CHECK(original.set_routine_name(ObString::make_string("ext_versioned")) == OB_SUCCESS);
  CHECK(original.set_routine_body(ObString::make_string("RETURN 1")) == OB_SUCCESS);
  RoutineVersionReservation token;
  int64_t version = 5, parameters = 6;
  CHECK(token.version() == OB_INVALID_VERSION);
  CHECK(token.take(*service, transaction, original, nullptr, version, parameters) == OB_STATE_NOT_MATCH);
  CHECK(version == OB_INVALID_VERSION && parameters == OB_INVALID_VERSION);
  CHECK(RoutineVersionReservation::reserve(*service, transaction, original, nullptr, token) == OB_SUCCESS);
  CHECK(token.version() == 1001 && original.get_schema_version() == 42);
  CHECK(RoutineVersionReservation::reserve(*service, transaction, original, nullptr, token) == OB_INIT_TWICE);
  CHECK(allocator->calls_ == 1 && token.version() == 1001);
  RoutineVersionReservation moved(std::move(token));
  CHECK(token.version() == OB_INVALID_VERSION && moved.version() == 1001);
  token = std::move(moved);
  CHECK(moved.version() == OB_INVALID_VERSION);
  ObRoutineInfo candidate;
  CHECK(candidate.assign(original) == OB_SUCCESS);
  candidate.set_schema_version(token.version());
  CHECK(candidate.set_routine_body(ObString::make_string("RETURN 2")) == OB_SUCCESS);
  CHECK(token.take(*service, transaction, candidate, nullptr, version, parameters) == OB_SUCCESS);
  CHECK(version == 1001 && parameters == OB_INVALID_VERSION);
  CHECK(token.take(*service, transaction, candidate, nullptr, version, parameters) == OB_STATE_NOT_MATCH);

  for (int field = 0; field < 13; ++field) {
    CHECK(RoutineVersionReservation::reserve(*service, transaction, original, nullptr, token) == OB_SUCCESS);
    CHECK(candidate.assign(original) == OB_SUCCESS);
    candidate.set_schema_version(token.version());
    switch (field) {
      case 0: candidate.set_routine_id(999); break;
      case 1: candidate.set_database_id(101); break;
      case 2: candidate.set_owner_id(124); break;
      case 3: candidate.set_routine_type(ROUTINE_PROCEDURE_TYPE); break;
      case 4: CHECK(candidate.set_routine_name(ObString::make_string("EXT_VERSIONED")) == OB_SUCCESS); break;
      case 5: candidate.set_package_id(10); break;
      case 6: candidate.set_overload(1); break;
      case 7: candidate.set_schema_version(42); break;
      case 10: transaction.active_ = false; break;
      case 11: service->bind_sql(*other_allocator); break;
      default: break;
    }
    const int calls = allocator->calls_;
    CHECK(token.take(field == 8 ? *other_service : *service,
        field == 9 ? other_transaction : transaction, candidate,
        field == 12 ? &original : nullptr, version, parameters) == OB_STATE_NOT_MATCH);
    CHECK(token.version() == OB_INVALID_VERSION && version == OB_INVALID_VERSION && parameters == OB_INVALID_VERSION);
    CHECK(allocator->calls_ == calls);
    transaction.active_ = true;
    service->bind_sql(*allocator);
  }
  for (int field = 0; field < 9; ++field) {
    CHECK(candidate.assign(original) == OB_SUCCESS);
    switch (field) {
      case 0: candidate.set_routine_id(OB_INVALID_ID); break;
      case 1: candidate.set_database_id(0); break;
      case 2: candidate.set_owner_id(0); break;
      case 3: candidate.set_routine_type(INVALID_ROUTINE_TYPE); break;
      case 4: candidate.set_package_id(10); break;
      case 5: candidate.set_overload(1); break;
      case 6: CHECK(candidate.set_routine_name(ObString()) == OB_SUCCESS); break;
      case 7: transaction.active_ = false; break;
      case 8: {
        const std::string name(OB_MAX_ROUTINE_NAME_BINARY_LENGTH + 1, 'x');
        CHECK(candidate.set_routine_name(ObString(name.size(), name.data())) == OB_SUCCESS);
        break;
      }
    }
    const int calls = allocator->calls_;
    CHECK(RoutineVersionReservation::reserve(*service, transaction, candidate, nullptr, token)
        == (field == 7 ? OB_STATE_NOT_MATCH : OB_INVALID_ARGUMENT));
    CHECK(token.version() == OB_INVALID_VERSION && allocator->calls_ == calls);
    transaction.active_ = true;
  }
  // A parameterized ALTER needs two ordered real versions, never an invented
  // version-1. The input old schema remains unchanged.
  ObRoutineInfo old;
  CHECK(old.assign(original) == OB_SUCCESS);
  ObRoutineParam parameter;
  parameter.set_schema_version(41);
  CHECK(old.add_routine_param(parameter) == OB_SUCCESS);
  CHECK(candidate.assign(original) == OB_SUCCESS);
  const int64_t first = allocator->next_;
  CHECK(RoutineVersionReservation::reserve(*service, transaction, candidate, &old, token) == OB_SUCCESS);
  CHECK(token.version() == first + 1 && old.get_schema_version() == 42);
  candidate.set_schema_version(token.version());
  CHECK(token.take(*service, transaction, candidate, &old, version, parameters) == OB_SUCCESS);
  CHECK(version == first + 1 && parameters == first);
  for (int field = 0; field < 3; ++field) {
    CHECK(RoutineVersionReservation::reserve(*service, transaction, original, &old, token) == OB_SUCCESS);
    CHECK(candidate.assign(original) == OB_SUCCESS);
    candidate.set_schema_version(token.version());
    ObRoutineInfo changed;
    CHECK(changed.assign(old) == OB_SUCCESS);
    if (field == 0) changed.set_schema_version(41);
    if (field == 1) CHECK(changed.add_routine_param(parameter) == OB_SUCCESS);
    CHECK(token.take(*service, transaction, candidate, field == 2 ? nullptr : &changed,
        version, parameters) == OB_STATE_NOT_MATCH);
    CHECK(token.version() == OB_INVALID_VERSION);
  }
  allocator->fail_at_ = allocator->calls_ + 2;
  CHECK(RoutineVersionReservation::reserve(*service, transaction, original, &old, token) == OB_TIMEOUT);
  CHECK(token.version() == OB_INVALID_VERSION);
  allocator->throw_at_ = allocator->calls_ + 1;
  CHECK(RoutineVersionReservation::reserve(*service, transaction, original, nullptr, token) == OB_ALLOCATE_MEMORY_FAILED);
  CHECK(token.version() == OB_INVALID_VERSION);
  for (int64_t bad : {int64_t{0}, int64_t{-1}, int64_t{42}}) {
    allocator->next_ = bad;
    CHECK(RoutineVersionReservation::reserve(*service, transaction, original, &old, token) == OB_INVALID_DATA);
    CHECK(token.version() == OB_INVALID_VERSION);
  }
  allocator->next_ = 2001;
  allocator->fixed_ = true;
  CHECK(RoutineVersionReservation::reserve(*service, transaction, original, &old, token) == OB_INVALID_DATA);
  allocator->fixed_ = false;
  // DROP participates in statement-order allocation too: otherwise a later
  // same-name CREATE could be replayed before the old object's DROP.
  CHECK(RoutineVersionReservation::reserve_drop(*service, transaction, old, token) == OB_SUCCESS);
  const int64_t dropped = token.version();
  RoutineVersionReservation next;
  CHECK(RoutineVersionReservation::reserve(*service, transaction, original, nullptr, next) == OB_SUCCESS);
  CHECK(next.version() > dropped);
  CHECK(token.take_drop(*service, transaction, old, version) == OB_SUCCESS && version == dropped);
  CHECK(token.take_drop(*service, transaction, old, version) == OB_STATE_NOT_MATCH);
  CHECK(next.take_drop(*service, transaction, original, version) == OB_STATE_NOT_MATCH);
  CHECK(next.version() == OB_INVALID_VERSION);

  // Actual CREATE -> SQL INSERT uses both reserved ID and version without
  // allocating again. Overlay and wire copies retain that same version.
  RoutineIdReservation identity;
  CHECK(candidate.assign(original) == OB_SUCCESS);
  CHECK(RoutineIdReservation::reserve(*allocator, candidate, identity) == OB_SUCCESS);
  candidate.set_routine_id(identity.id());
  CHECK(RoutineVersionReservation::reserve(*service, transaction, candidate, nullptr, token) == OB_SUCCESS);
  const int64_t expected = token.version();
  candidate.set_schema_version(expected);
  auto overlay = std::make_shared<RoutineSchemaOverlay>();
  CHECK(overlay->stage(candidate) == OB_SUCCESS);
  const ObRoutineInfo *staged = nullptr;
  bool handled = false;
  CHECK(overlay->lookup(candidate.get_routine_id(), handled, staged) == OB_SUCCESS && handled && staged);
  oceanbase::obcall::ObCreateRoutineArg arg;
  CHECK(arg.routine_info_.assign(*staged) == OB_SUCCESS);
  oceanbase::sql::ExtensionRoutineUpdateBatch wire;
  using Op = oceanbase::share::plugin::ExtensionRoutineUpdateOperation;
  ObSEArray<Op, 1> operations;
  CHECK(operations.push_back({Op::Kind::CREATE, &arg, nullptr}) == OB_SUCCESS);
  CHECK(wire.assign(operations) == OB_SUCCESS);
  CHECK(candidate.assign(wire.operations().at(0).create_arg_->routine_info_) == OB_SUCCESS);
  const int calls = allocator->calls_;
  const int ids = allocator->ids_;
  ObPLDDLOperator ddl(*service, proxy);
  ObErrorInfo errors;
  ObSEArray<ObDependencyInfo, 1> dependencies;
  const int create_status = ddl.create_routine(candidate, transaction, errors, dependencies, nullptr, &identity, &token);
  if (create_status != OB_TIMEOUT) std::cerr << "reserved CREATE status: " << create_status
      << ", id=" << candidate.get_routine_id() << ", version=" << candidate.get_schema_version()
      << ", writes=" << transaction.writes_.size() << std::endl;
  CHECK(create_status == OB_TIMEOUT);
  CHECK(candidate.get_schema_version() == expected && allocator->calls_ == calls && allocator->ids_ == ids);
  CHECK(token.version() == OB_INVALID_VERSION && identity.id() == OB_INVALID_ID);
  CHECK(transaction.writes_.back().find(std::to_string(expected)) != std::string::npos);
  const size_t writes = transaction.writes_.size();
  CHECK(ddl.create_routine(candidate, transaction, errors, dependencies, nullptr, &identity, &token) == OB_STATE_NOT_MATCH);
  CHECK(transaction.writes_.size() == writes && allocator->calls_ == calls);

  // Actual replacement reaches old-parameter history deletion with precisely
  // the first reserved version. The transport fails there (no commit claim).
  CHECK(candidate.assign(original) == OB_SUCCESS);
  CHECK(RoutineVersionReservation::reserve(*service, transaction, candidate, &old, token) == OB_SUCCESS);
  const int64_t replace_version = token.version();
  candidate.set_schema_version(replace_version);
  transaction.fail_table_ = "__all_routine_param_history";
  const int replace_calls = allocator->calls_;
  CHECK(ddl.replace_routine(candidate, &old, transaction, errors, dependencies, nullptr, &token) == OB_TIMEOUT);
  CHECK(candidate.get_schema_version() == replace_version && allocator->calls_ == replace_calls);
  CHECK(token.version() == OB_INVALID_VERSION);
  CHECK(transaction.writes_.back().find(std::to_string(replace_version - 1)) != std::string::npos);

  // Host-created dependency metadata (not body compilation): owned wire
  // transport, the actual schema-version fence, and dependency SQL must agree
  // with the target's reserved version visible through the overlay.
  auto manager = std::make_unique<ObSchemaMgr>();
  CHECK(manager->init() == OB_SUCCESS);
  ObSchemaGetterGuard guard;
  CHECK(MockSchemaService::bind(guard, *service, *manager) == OB_SUCCESS);
  CHECK(guard.attach_routine_overlay(overlay) == OB_SUCCESS);
  oceanbase::obcall::ObCreateRoutineArg dependent;
  CHECK(dependent.routine_info_.assign(original) == OB_SUCCESS);
  dependent.routine_info_.set_routine_id(500001);
  CHECK(dependent.routine_info_.set_routine_name(ObString::make_string("ext_dependent")) == OB_SUCCESS);
  CHECK(RoutineVersionReservation::reserve(*service, transaction, dependent.routine_info_, nullptr, next) == OB_SUCCESS);
  dependent.routine_info_.set_schema_version(next.version());
  CHECK(dependent.based_schema_object_infos_.push_back(
      ObBasedSchemaObjectInfo(staged->get_routine_id(), ROUTINE_SCHEMA, expected)) == OB_SUCCESS);
  ObDependencyInfo dependency;
  dependency.set_dep_obj_type(ObObjectType::FUNCTION);
  dependency.set_ref_obj_type(ObObjectType::FUNCTION);
  dependency.set_ref_obj_id(staged->get_routine_id());
  dependency.set_ref_timestamp(expected);
  dependency.set_dep_timestamp(-1);
  dependency.set_order(0);
  CHECK(dependent.dependency_infos_.push_back(dependency) == OB_SUCCESS);
  operations.reset();
  CHECK(operations.push_back({Op::Kind::CREATE, &dependent, nullptr}) == OB_SUCCESS);
  CHECK(wire.assign(operations) == OB_SUCCESS);
  const auto &owned = *wire.operations().at(0).create_arg_;
  CHECK(owned.dependency_infos_.at(0).get_ref_timestamp() == expected);
  auto fence = std::make_unique<ObDDLService>();
  CHECK(fence->check_parallel_ddl_conflict(guard, owned) == OB_SUCCESS);
  dependent.based_schema_object_infos_.at(0).schema_version_ = expected - 1;
  CHECK(fence->check_parallel_ddl_conflict(guard, dependent) == OB_ERR_PARALLEL_DDL_CONFLICT);
  CHECK(fence->check_parallel_ddl_conflict(guard, owned) == OB_SUCCESS); // independent wire ownership
  CHECK(dependencies.assign(owned.dependency_infos_) == OB_SUCCESS);
  transaction.fail_table_ = "__all_dependency";
  CHECK(ObDependencyInfo::insert_dependency_infos(transaction, dependencies, 500001,
      next.version(), 123) == OB_TIMEOUT);
  CHECK(transaction.writes_.back().find("ref_timestamp") != std::string::npos);
  CHECK(transaction.writes_.back().find(std::to_string(expected)) != std::string::npos);
  CHECK(overlay->erase(staged->get_database_id(), staged->get_routine_name(), staged->get_routine_type(),
      staged->get_routine_id()) == OB_SUCCESS);
  CHECK(fence->check_parallel_ddl_conflict(guard, owned) == OB_ERR_PARALLEL_DDL_CONFLICT);
  CHECK(guard.reset() == OB_SUCCESS);

  oceanbase::obcall::ObCreateRoutineArg metadata;
  CHECK(metadata.routine_info_.assign(old) == OB_SUCCESS);
  ObSEArray<const oceanbase::obcall::ObCreateRoutineArg *, 1> installation_source;
  CHECK(installation_source.push_back(&metadata) == OB_SUCCESS);
  oceanbase::sql::ExtensionRoutineBatch installation;
  CHECK(installation.assign(installation_source) == OB_SUCCESS);
  operations.reset();
  CHECK(operations.push_back({Op::Kind::ALTER, &metadata, nullptr}) == OB_SUCCESS);
  CHECK(wire.assign(operations) == OB_SUCCESS);
  metadata.routine_info_.set_schema_version(9999);
  metadata.routine_info_.get_routine_params().at(0)->set_schema_version(9998);
  for (const auto *copy : {installation.args().at(0), wire.operations().at(0).create_arg_}) {
    CHECK(copy->routine_info_.get_schema_version() == 42);
    CHECK(copy->routine_info_.get_routine_params().count() == 1);
    CHECK(copy->routine_info_.get_routine_params().at(0)->get_schema_version() == 41);
  }
}
} // namespace routine_version_test
#endif
