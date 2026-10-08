// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Real reservation/operator/schema SQL generation, controlled ID/version and
// failing SQL transport. Does not exercise the durable ID sequence or commit.
#ifndef SEEKDB_TEST_ROUTINE_ID_RESERVATION_FIXTURE_H_
#define SEEKDB_TEST_ROUTINE_ID_RESERVATION_FIXTURE_H_
#include "observer/schema/ob_schema_service_sql_impl.h"
#include "rootserver/pl_ddl/ob_pl_ddl_operator.h"
#include "routine_overlay_guard_fixture.h"
#include <type_traits>

namespace routine_reservation_test {
using namespace oceanbase::common;
using namespace oceanbase::share::schema;
using namespace oceanbase::rootserver;

class VersionService final : public MockSchemaService
{
public:
  ~VersionService() override { schema_service_ = nullptr; }
  void bind_sql(ObSchemaService &service) { schema_service_ = &service; }
  int get_runtime_refreshed_schema_version(int64_t &version, bool = false) const override
  { version = 42; return OB_SUCCESS; }
};

class IdentityService final : public ObSchemaServiceSQLImpl
{
public:
  IdentityService(ObMySQLProxy &proxy, ObMultiVersionSchemaService &service)
    : ObSchemaServiceSQLImpl(nullptr, proxy, service) {}
  uint64_t next_ = 311234;
  int allocations_ = 0, versions_ = 0, allocation_status_ = OB_SUCCESS, version_status_ = OB_SUCCESS;
  bool throw_allocation_ = false;
  int fetch_new_sys_pl_object_id(uint64_t &id) override {
    ++allocations_;
    CHECK(id == OB_INVALID_ID);
    id = next_++;
    if (throw_allocation_) throw std::bad_alloc();
    return allocation_status_;
  }
  int gen_new_schema_version(int64_t refreshed, int64_t &version) override {
    CHECK(refreshed == 42);
    ++versions_;
    version = 43;
    return version_status_;
  }
};

class FailedWrite final : public ObMySQLTransaction
{
public:
  int writes_ = 0;
  std::string sql_;
  bool is_started() const override { return true; }
  int write(const char *sql, int32_t, int64_t &affected) override {
    ++writes_;
    sql_ = sql;
    affected = 0;
    return OB_TIMEOUT;
  }
};

inline void run()
{
  static_assert(!std::is_copy_constructible_v<RoutineIdReservation>);
  static_assert(!std::is_copy_assignable_v<RoutineIdReservation>);
  static_assert(std::is_nothrow_move_constructible_v<RoutineIdReservation>);
  static_assert(std::is_nothrow_move_assignable_v<RoutineIdReservation>);
  auto versions = std::make_unique<VersionService>();
  ObMySQLProxy proxy;
  auto service = std::make_unique<IdentityService>(proxy, *versions);
  auto other = std::make_unique<IdentityService>(proxy, *versions);
  versions->bind_sql(*service);
  ObRoutineInfo source;
  source.set_database_id(100);
  source.set_owner_id(123);
  source.set_package_id(OB_INVALID_ID);
  source.set_overload(0);
  source.set_subprogram_id(0);
  source.set_routine_type(ROUTINE_FUNCTION_TYPE);
  CHECK(source.set_routine_name(ObString::make_string("ext_reserved")) == OB_SUCCESS);
  CHECK(source.set_routine_body(ObString::make_string("RETURN 1")) == OB_SUCCESS);
  RoutineIdReservation token;
  uint64_t id = 5;
  CHECK(token.id() == OB_INVALID_ID);
  CHECK(token.take(*service, source, id) == OB_STATE_NOT_MATCH && id == OB_INVALID_ID);
  CHECK(RoutineIdReservation::reserve(*service, source, token) == OB_SUCCESS);
  const auto first = token.id();
  CHECK(first == 311234 && source.get_routine_id() == OB_INVALID_ID);
  CHECK(RoutineIdReservation::reserve(*service, source, token) == OB_INIT_TWICE);
  CHECK(token.id() == first && service->allocations_ == 1);
  RoutineIdReservation moved(std::move(token));
  CHECK(token.id() == OB_INVALID_ID && moved.id() == first);
  token = std::move(moved);
  CHECK(moved.id() == OB_INVALID_ID && token.id() == first);
  source.set_routine_id(first);
  CHECK(source.set_routine_body(ObString::make_string("RETURN 2")) == OB_SUCCESS);
  CHECK(token.take(*service, source, id) == OB_SUCCESS && id == first);
  CHECK(token.take(*service, source, id) == OB_STATE_NOT_MATCH && id == OB_INVALID_ID);

  // Mismatches burn a token, and never allocate a replacement implicitly.
  for (int field = 0; field < 8; ++field) {
    CHECK(RoutineIdReservation::reserve(*service, source, token) == OB_SUCCESS);
    ObRoutineInfo changed;
    CHECK(changed.assign(source) == OB_SUCCESS);
    changed.set_routine_id(token.id());
    switch (field) {
      case 0: changed.set_database_id(101); break;
      case 1: changed.set_owner_id(124); break;
      case 2: changed.set_routine_type(ROUTINE_PROCEDURE_TYPE); break;
      case 3: CHECK(changed.set_routine_name(ObString::make_string("EXT_RESERVED")) == OB_SUCCESS); break;
      case 4: changed.set_package_id(10); break;
      case 5: changed.set_overload(1); break;
      case 6: changed.set_routine_id(999); break;
      default: break;
    }
    const int allocations = service->allocations_;
    CHECK(token.take(field == 7 ? *other : *service, changed, id) == OB_STATE_NOT_MATCH);
    CHECK(id == OB_INVALID_ID && token.id() == OB_INVALID_ID && service->allocations_ == allocations);
  }
  for (int field = 0; field < 7; ++field) {
    ObRoutineInfo invalid;
    CHECK(invalid.assign(source) == OB_SUCCESS);
    switch (field) {
      case 0: invalid.set_database_id(0); break;
      case 1: invalid.set_owner_id(OB_INVALID_ID); break;
      case 2: invalid.set_routine_type(INVALID_ROUTINE_TYPE); break;
      case 3: CHECK(invalid.set_routine_name(ObString()) == OB_SUCCESS); break;
      case 4: invalid.set_package_id(10); break;
      case 5: invalid.set_overload(1); break;
      case 6: {
        const std::string large(OB_MAX_ROUTINE_NAME_BINARY_LENGTH + 1, 'x');
        CHECK(invalid.set_routine_name(ObString(large.size(), large.data())) == OB_SUCCESS);
        break;
      }
    }
    const int allocations = service->allocations_;
    CHECK(RoutineIdReservation::reserve(*service, invalid, token) == OB_INVALID_ARGUMENT);
    CHECK(token.id() == OB_INVALID_ID && service->allocations_ == allocations);
  }
  service->allocation_status_ = OB_TIMEOUT;
  CHECK(RoutineIdReservation::reserve(*service, source, token) == OB_TIMEOUT && token.id() == OB_INVALID_ID);
  service->allocation_status_ = OB_SUCCESS;
  service->throw_allocation_ = true;
  CHECK(RoutineIdReservation::reserve(*service, source, token) == OB_ALLOCATE_MEMORY_FAILED);
  CHECK(token.id() == OB_INVALID_ID);
  service->throw_allocation_ = false;
  for (const auto bad : {uint64_t{0}, static_cast<uint64_t>(INT64_MAX) + 1}) {
    service->next_ = bad;
    CHECK(RoutineIdReservation::reserve(*service, source, token) == OB_INVALID_DATA);
    CHECK(token.id() == OB_INVALID_ID);
  }
  service->next_ = 312345;
  CHECK(RoutineIdReservation::reserve(*service, source, token) == OB_SUCCESS);
  source.set_routine_id(token.id());
  RoutineSchemaOverlay overlay;
  CHECK(overlay.stage(source) == OB_SUCCESS);
  CHECK(source.set_routine_name(ObString::make_string("changed_after_staging")) == OB_SUCCESS);
  const ObRoutineInfo *staged = nullptr;
  bool handled = false;
  CHECK(overlay.lookup(token.id(), handled, staged) == OB_SUCCESS && handled && staged);
  ObRoutineInfo written;
  CHECK(written.assign(*staged) == OB_SUCCESS);
  FailedWrite transaction;
  ObErrorInfo errors;
  ObSEArray<ObDependencyInfo, 1> dependencies;
  ObPLDDLOperator ddl(*versions, proxy);
  const int allocations = service->allocations_;
  CHECK(ddl.create_routine(written, transaction, errors, dependencies, nullptr, &token) == OB_TIMEOUT);
  CHECK(service->allocations_ == allocations && service->versions_ == 1);
  CHECK(token.id() == OB_INVALID_ID && written.get_routine_id() == staged->get_routine_id());
  CHECK(written.get_routine_id() == 312345 && written.get_schema_version() == 43);
  CHECK(transaction.writes_ == 1 && transaction.sql_.find("__all_routine") != std::string::npos);
  CHECK(transaction.sql_.find("routine_id") != std::string::npos && transaction.sql_.find("312345") != std::string::npos);
  CHECK(ddl.create_routine(written, transaction, errors, dependencies, nullptr, &token) == OB_STATE_NOT_MATCH);
  CHECK(service->allocations_ == allocations && service->versions_ == 1 && transaction.writes_ == 1);
  // A version failure also burns the ID; no SQL is attempted.
  CHECK(RoutineIdReservation::reserve(*service, written, token) == OB_SUCCESS);
  written.set_routine_id(token.id());
  service->version_status_ = OB_TIMEOUT;
  CHECK(ddl.create_routine(written, transaction, errors, dependencies, nullptr, &token) == OB_TIMEOUT);
  CHECK(token.id() == OB_INVALID_ID && transaction.writes_ == 1);
  // The ordinary path still ignores a caller-supplied numeric routine ID.
  service->version_status_ = OB_SUCCESS;
  const auto next = service->next_;
  written.set_routine_id(999);
  CHECK(ddl.create_routine(written, transaction, errors, dependencies, nullptr) == OB_TIMEOUT);
  CHECK(written.get_routine_id() == next && transaction.writes_ == 2);
}
} // namespace routine_reservation_test
#endif
