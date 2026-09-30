// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Real C++ -> Rust operation driver and paired-view journal. SQL/data outcomes
// are controlled effects, not real transactions or an authorization fixture.
#ifndef SEEKDB_TEST_QUERY_OPERATION_FIXTURE_H_
#define SEEKDB_TEST_QUERY_OPERATION_FIXTURE_H_
#include "share/schema/routine_catalog_transaction.h"
#include "share/schema/routine_schema_overlay.h"
#include "data_plane/transaction/ob_tx_seq.h"
#include <array>
#include <vector>

namespace query_operation_test {
using namespace oceanbase::common;
using namespace oceanbase::share::schema;
using oceanbase::transaction::ObTxSEQ;
enum Phase { PREFLIGHT = 1, PREPARE, APPLY, CLOSE, IDENTITY, DATA_UNDO, VIEW_UNDO, POISON };

struct Host final : ICatalogOperationHost {
  std::shared_ptr<RoutinePrivilegeOverlay> grants = std::make_shared<RoutinePrivilegeOverlay>();
  std::shared_ptr<RoutineSchemaOverlay> schema = std::make_shared<RoutineSchemaOverlay>(grants);
  RoutineCatalogTransaction journal{77};
  std::array<int, 9> errors{};
  std::vector<int> events;
  int throw_phase = 0;
  bool closed = false, data_undone = false;
  const ObRoutineInfo *borrowed = nullptr;
  Host() {
    CHECK(journal.valid());
    CHECK(journal.record(77, ObTxSEQ(1, 0), schema, grants) == OB_SUCCESS);
    stage(9001, "previous");
  }
  int hit(int phase) {
    events.push_back(phase);
    if (throw_phase == phase) throw std::bad_alloc();
    return errors[phase];
  }
  void stage(uint64_t id, const char *name) {
    ObRoutineInfo routine;
    routine.set_database_id(100); routine.set_owner_id(123); routine.set_routine_id(id);
    routine.set_schema_version(42); routine.set_package_id(OB_INVALID_ID); routine.set_overload(0);
    routine.set_routine_type(ROUTINE_FUNCTION_TYPE);
    CHECK(routine.set_routine_name(name) == OB_SUCCESS);
    CHECK(routine.set_routine_body("RETURN 1") == OB_SUCCESS);
    CHECK(grants->record_create(routine, true) == OB_SUCCESS);
    CHECK(schema->stage(routine) == OB_SUCCESS);
  }
  int preflight() override { return hit(PREFLIGHT); }
  int prepare() override {
    // Deliberately fail AFTER acquiring a private mark in error scenarios.
    CHECK(journal.record(77, ObTxSEQ(10, 0), schema, grants) == OB_SUCCESS);
    return hit(PREPARE);
  }
  int apply() override {
    CHECK(!closed);
    CHECK(journal.admit_ddl(77, ObTxSEQ(10, 0), 7) == OB_SUCCESS);
    CHECK(journal.record_schema_version(77, ObTxSEQ(10, 0), 42) == OB_SUCCESS);
    stage(9002, "operation");
    bool handled = false;
    CHECK(schema->lookup(9002, handled, borrowed) == OB_SUCCESS && handled && borrowed);
    return hit(APPLY); // Includes an exception after partial staging.
  }
  int close() override { closed = true; return hit(CLOSE); }
  int check_transaction() override { CHECK(closed); return hit(IDENTITY); }
  int rollback_data() override {
    CHECK(closed);
    const int ret = hit(DATA_UNDO);
    data_undone = ret == OB_SUCCESS; // Controlled confirmation, not database undo.
    return ret;
  }
  int rollback_view() override {
    CHECK(data_undone);
    const int ret = hit(VIEW_UNDO);
    return ret == OB_SUCCESS ? journal.rollback(77, ObTxSEQ(10, 0)) : ret;
  }
  int poison(int cause) override {
    CHECK(cause != OB_SUCCESS);
    CHECK(journal.fail(77, cause) == OB_SUCCESS);
    schema->retire(); grants->retire();
    return hit(POISON);
  }
  void present(uint64_t id, bool expected) {
    bool handled = false; const ObRoutineInfo *routine = nullptr;
    CHECK(schema->lookup(id, handled, routine) == OB_SUCCESS);
    CHECK(handled == expected && (routine != nullptr) == expected);
    if (expected) {
      bool granted = false; ObPrivSet bits = 0;
      CHECK(grants->lookup(100, routine->get_routine_name(), ROUTINE_FUNCTION_TYPE,
          123, handled, routine, granted, bits) == OB_SUCCESS);
      CHECK(granted && bits == (OB_PRIV_EXECUTE | OB_PRIV_ALTER_ROUTINE));
    }
  }
};

inline void run()
{
  using Outcome = CatalogOperationResult::Outcome;
  {
    Host host;
    CatalogOperationResult result;
    CHECK(run_catalog_operation(host, result) == OB_SUCCESS);
    CHECK(result.outcome_ == Outcome::APPLIED && result.failed_phase_ == 0);
    CHECK(host.events == std::vector<int>({PREFLIGHT, PREPARE, APPLY, CLOSE, IDENTITY}));
    host.present(9001, true); host.present(9002, true);
    uint64_t version = 0, operations = 0;
    CHECK(host.journal.schema_state(77, version, operations) == OB_SUCCESS && version == 42 && operations == 1);
    // Success has NOT sealed/committed the journal; caller can roll back later.
    CHECK(host.journal.rollback(77, ObTxSEQ(10, 0)) == OB_SUCCESS);
    host.present(9001, true); host.present(9002, false);
    CHECK(host.borrowed->get_routine_id() == 9002);
  }
  for (int failed : {PREFLIGHT, PREPARE, APPLY}) {
    for (bool exception : {false, true}) {
      Host host;
      host.errors[failed] = OB_ERR_NO_PRIVILEGE;
      if (exception) host.throw_phase = failed;
      CatalogOperationResult result;
      const int expected = exception ? OB_ALLOCATE_MEMORY_FAILED : OB_ERR_NO_PRIVILEGE;
      CHECK(run_catalog_operation(host, result) == expected && result.operation_error_ == expected);
      CHECK(result.failed_phase_ == static_cast<uint32_t>(failed));
      CHECK(result.outcome_ == (failed == PREFLIGHT ? Outcome::NOT_STARTED : Outcome::ROLLED_BACK));
      host.present(9001, true); host.present(9002, false);
      if (failed == PREFLIGHT) CHECK(host.events == std::vector<int>({PREFLIGHT}));
      else {
        CHECK(host.data_undone && host.closed);
        CHECK(host.events[host.events.size() - 2] == DATA_UNDO && host.events.back() == VIEW_UNDO);
      }
      CHECK(!host.schema->is_retired());
      CHECK(host.journal.record(77, ObTxSEQ(20, 0), host.schema, host.grants) == OB_SUCCESS);
      if (host.borrowed) CHECK(host.borrowed->get_routine_id() == 9002);
    }
  }
  for (int failed : {CLOSE, IDENTITY, DATA_UNDO, VIEW_UNDO}) {
    for (bool exception : {false, true}) {
      Host host;
      host.errors[APPLY] = OB_ERR_NO_PRIVILEGE;
      host.errors[failed] = OB_TIMEOUT;
      if (exception) host.throw_phase = failed;
      CatalogOperationResult result;
      CHECK(run_catalog_operation(host, result) == OB_ERR_NO_PRIVILEGE);
      CHECK(result.failed_phase_ == APPLY && result.outcome_ == Outcome::REQUIRES_ABORT);
      const int cleanup_error = exception ? OB_ALLOCATE_MEMORY_FAILED : OB_TIMEOUT;
      CHECK((failed == CLOSE ? result.close_error_ : failed == IDENTITY ? result.identity_error_ :
          failed == DATA_UNDO ? result.data_rollback_error_ : result.view_rollback_error_) == cleanup_error);
      CHECK(host.events.back() == POISON && host.schema->is_retired() && host.grants->is_retired());
      if (failed <= DATA_UNDO) CHECK(!std::count(host.events.begin(), host.events.end(), VIEW_UNDO));
      if (failed <= IDENTITY) CHECK(!std::count(host.events.begin(), host.events.end(), DATA_UNDO));
      CHECK(host.journal.fail(77, OB_ERR_UNEXPECTED) == OB_SUCCESS); // First cause persists.
      CHECK(host.journal.prepare_commit(77) == cleanup_error);
      uint64_t version = 9, operations = 9;
      CHECK(host.journal.schema_state(77, version, operations) == cleanup_error && version == 0 && operations == 0);
      CHECK(host.journal.finish(77, true) == cleanup_error);
      CHECK(host.journal.finish(77, false) == cleanup_error); // Verified abort still consumes all marks.
      CHECK(host.borrowed->get_routine_id() == 9002);
      bool handled = true; const ObRoutineInfo *routine = host.borrowed;
      CHECK(host.schema->lookup(9002, handled, routine) == OB_STATE_NOT_MATCH && !handled && !routine);
    }
  }
  {
    Host host;
    host.errors[APPLY] = OB_ERR_NO_PRIVILEGE;
    host.errors[DATA_UNDO] = OB_TIMEOUT;
    host.throw_phase = POISON;
    CatalogOperationResult result;
    CHECK(run_catalog_operation(host, result) == OB_ERR_NO_PRIVILEGE);
    CHECK(result.outcome_ == Outcome::REQUIRES_ABORT && result.poison_error_ == OB_ALLOCATE_MEMORY_FAILED);
    CHECK(host.journal.prepare_commit(77) == OB_TIMEOUT);
  }
}
}
#endif
