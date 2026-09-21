// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#ifndef SEEKDB_TEST_ROUTINE_CATALOG_TRANSACTION_FIXTURE_H_
#define SEEKDB_TEST_ROUTINE_CATALOG_TRANSACTION_FIXTURE_H_
#include "share/schema/routine_catalog_transaction.h"
#include "share/schema/routine_catalog_savepoint.h"
#include "data_plane/transaction/ob_tx_seq.h"

namespace routine_catalog_transaction_test {
using namespace oceanbase::common;
using namespace oceanbase::share::schema;
using oceanbase::transaction::ObTxSEQ;
inline void run()
{
  // Production Rust queue + C++ drain bridge. The eviction effect is controlled;
  // no fixture claims this is a real cache/data-transaction concurrency test.
  for (int scenario = 0; scenario < 8; ++scenario) {
    RoutineInvalidationQueue queue(1, 1);
    CHECK(queue.valid());
    auto journal = std::make_unique<RoutineCatalogTransaction>(771);
    CHECK(journal->admit_ddl(771, ObTxSEQ(10, 0), 17) == OB_SUCCESS);
    CHECK(journal->record_schema_version(771, ObTxSEQ(10, 0), 600) == OB_SUCCESS);
    CHECK(journal->record_invalidation(771, ObTxSEQ(10, 0), 100, 900) == OB_SUCCESS);
    CHECK(queue.reserve(*journal, 771) == OB_STATE_NOT_MATCH);
    uint64_t version = 0, operations = 0;
    CHECK(journal->begin_prepare(771, version, operations) == OB_SUCCESS);
    CHECK(queue.reserve(*journal, 772) == OB_STATE_NOT_MATCH);
    CHECK(queue.reserve(*journal, 771) == OB_SUCCESS);
    CHECK(queue.reserve(*journal, 771) == OB_STATE_NOT_MATCH);
    struct Evictor final : IRoutineCacheEvictor {
      int calls = 0, checks = 0, scenario;
      explicit Evictor(int scenario) : scenario(scenario) {}
      int check_schema_version(int64_t version) override {
        CHECK(version == (scenario == 2 ? 600 : 601)); ++checks;
        return scenario == 7 && checks == 1 ? OB_EAGAIN : OB_SUCCESS;
      }
      int evict(uint64_t db, uint64_t id) override {
        CHECK(db == 100 && id == 900); ++calls;
        if (calls == 1) {
          if (scenario == 3) return OB_TIMEOUT;
          if (scenario == 4) throw std::bad_alloc();
          if (scenario == 5) throw 1;
        }
        return OB_SUCCESS;
      }
    } evictor(scenario);
    uint32_t processed = 99;
    CHECK(queue.process(evictor, 0, processed) == OB_INVALID_ARGUMENT && processed == 0);
    CHECK(queue.process(evictor, 65, processed) == OB_INVALID_ARGUMENT && processed == 0);
    CHECK(queue.process(evictor, 1, processed) == OB_SUCCESS && processed == 0 && evictor.calls == 0);
    if (scenario == 6) queue.close(); // Existing promise survives admission close.
    if (scenario == 1) {
      CHECK(journal->finish(771, false) == OB_SUCCESS);
    } else if (scenario != 2) {
      CHECK(journal->record_end_sign(771, 601) == OB_SUCCESS);
      CHECK(journal->complete_prepare(771, 601, OB_SUCCESS) == OB_SUCCESS);
      CHECK(journal->finish(771, true) == OB_SUCCESS);
      uint64_t count = 99;
      CHECK(journal->invalidation_count(771, count) == OB_SUCCESS && count == 0);
    }
    journal.reset(); // Queue never owns/dereferences the session or journal.
    const int result[] = {OB_SUCCESS, OB_SUCCESS, OB_SUCCESS, OB_TIMEOUT,
      OB_ALLOCATE_MEMORY_FAILED, OB_ERR_UNEXPECTED, OB_SUCCESS, OB_EAGAIN};
    CHECK(queue.process(evictor, 1, processed) == result[scenario]);
    CHECK(processed == (scenario == 0 || scenario == 2 || scenario == 6 ? 1 : 0));
    if ((scenario >= 3 && scenario <= 5) || scenario == 7) {
      CHECK(evictor.calls == (scenario == 7 ? 0 : 1));
      CHECK(queue.process(evictor, 1, processed) == OB_SUCCESS && processed == 1);
      CHECK(evictor.calls == (scenario == 7 ? 1 : 2));
    }
    const auto calls = evictor.calls;
    CHECK(queue.process(evictor, 1, processed) == OB_SUCCESS && processed == 0 && evictor.calls == calls);
  }
  {
    RoutineCatalogTransaction journal(771);
    CHECK(journal.record_invalidation(771, ObTxSEQ::INVL(), 100, 900) == OB_INVALID_ARGUMENT);
    CHECK(journal.record_invalidation(771, ObTxSEQ(10, 1), 100, 900) == OB_NOT_SUPPORTED);
    CHECK(journal.record_invalidation(771, ObTxSEQ(10, 0), 100, 900) == OB_STATE_NOT_MATCH);
    CHECK(journal.admit_ddl(771, ObTxSEQ(10, 0), 17) == OB_SUCCESS);
    CHECK(journal.record_schema_version(771, ObTxSEQ(10, 0), 600) == OB_SUCCESS);
    CHECK(journal.record_invalidation(772, ObTxSEQ(10, 0), 100, 900) == OB_STATE_NOT_MATCH);
    CHECK(journal.record_invalidation(771, ObTxSEQ(10, 0), OB_INVALID_ID, 900) == OB_INVALID_ARGUMENT);
    CHECK(journal.record_invalidation(771, ObTxSEQ(10, 0), 100, 900) == OB_SUCCESS);
    uint64_t count = 0, ticket = 9, db = 9, id = 9;
    CHECK(journal.invalidation_count(771, count) == OB_SUCCESS && count == 1);
    CHECK(journal.peek_invalidation(771, ticket, db, id) == OB_STATE_NOT_MATCH);
    CHECK(ticket == 0 && db == 0 && id == 0);
    CHECK(journal.rollback(771, ObTxSEQ(10, 0)) == OB_SUCCESS);
    CHECK(journal.invalidation_count(771, count) == OB_SUCCESS && count == 0);
    CHECK(journal.prepare_commit(771) == OB_SUCCESS);
    CHECK(journal.finish(771, true) == OB_SUCCESS);
    CHECK(journal.peek_invalidation(771, ticket, db, id) == OB_SUCCESS);
    CHECK(ticket == 0 && db == 0 && id == 0);
    CHECK(journal.ack_invalidation(771, 1) == OB_STATE_NOT_MATCH);
  }
  // Exercise the same host/Rust coordinator used by session commit. Host SQL
  // effects are controlled; this is sequencing/error evidence, not DB commit.
  for (int scenario = 0; scenario < 14; ++scenario) {
    RoutineCatalogTransaction journal(771);
    if (scenario != 13) CHECK(journal.admit_ddl(771, ObTxSEQ(10, 0), 17) == OB_SUCCESS);
    if (scenario != 12) CHECK(journal.record_schema_version(771, ObTxSEQ(10, 0), 600) == OB_SUCCESS);
    struct Host final : ICatalogCommitHost {
      RoutineCatalogTransaction &journal;
      int scenario, checks = 0, preparations = 0, closes = 0;
      Host(RoutineCatalogTransaction &journal, int scenario) : journal(journal), scenario(scenario) {}
      int check_transaction() override {
        ++checks;
        CHECK(journal.check_preparing(771) == OB_SUCCESS);
        if ((scenario == 10 && checks == 1) || (scenario == 11 && checks == 2)) throw 1;
        return (scenario == 3 && checks == 1) || (scenario == 4 && checks == 2)
            ? OB_TRANS_INVALID_STATE : OB_SUCCESS;
      }
      int prepare(int64_t version, int64_t epoch, int64_t &prepared) override {
        ++preparations;
        CHECK(version == 600 && epoch == 17 && closes == 0);
        CHECK(journal.record_schema_version(771, ObTxSEQ(20, 0), 601) == OB_STATE_NOT_MATCH);
        if (scenario == 5) throw std::bad_alloc();
        if (scenario == 1 || scenario == 7) return OB_TIMEOUT;
        if (scenario != 8) CHECK(journal.record_end_sign(771, 601) == OB_SUCCESS);
        prepared = scenario == 9 ? 602 : 601;
        return OB_SUCCESS;
      }
      int close() override {
        ++closes;
        CHECK(journal.check_preparing(771) == OB_SUCCESS);
        if (scenario == 6) throw std::bad_alloc();
        return scenario == 2 || scenario == 7 ? OB_ERR_UNEXPECTED : OB_SUCCESS;
      }
    } host(journal, scenario);
    const int expected[] = {OB_SUCCESS, OB_TIMEOUT, OB_ERR_UNEXPECTED, OB_TRANS_INVALID_STATE,
      OB_TRANS_INVALID_STATE, OB_ALLOCATE_MEMORY_FAILED, OB_ALLOCATE_MEMORY_FAILED, OB_TIMEOUT,
      OB_STATE_NOT_MATCH, OB_STATE_NOT_MATCH, OB_ERR_UNEXPECTED, OB_ERR_UNEXPECTED,
      OB_SUCCESS, OB_STATE_NOT_MATCH};
    CHECK(journal.prepare_commit(771, host) == expected[scenario]);
    if (scenario == 12 || scenario == 13) {
      CHECK(host.checks == 0 && host.preparations == 0 && host.closes == 0);
    } else {
      CHECK(host.closes == 1);
      CHECK(host.preparations == (scenario == 3 || scenario == 10 ? 0 : 1));
      const bool second_check = scenario == 0 || scenario == 4 || scenario == 8 || scenario == 9 || scenario == 11;
      CHECK(host.checks == (second_check ? 2 : 1));
    }
    if (scenario == 0 || scenario == 12) {
      uint64_t version = 0, count = 0;
      CHECK(journal.schema_state(771, version, count) == OB_SUCCESS);
      CHECK(version == (scenario == 0 ? 601 : 0) && count == (scenario == 0 ? 2 : 0));
      const auto calls = host.checks + host.preparations + host.closes;
      CHECK(journal.prepare_commit(771, host) == OB_STATE_NOT_MATCH);
      CHECK(host.checks + host.preparations + host.closes == calls);
      CHECK(journal.finish(771, true) == OB_SUCCESS);
    } else {
      const auto calls = host.checks + host.preparations + host.closes;
      CHECK(journal.prepare_commit(771, host) != OB_SUCCESS);
      CHECK(host.checks + host.preparations + host.closes == calls);
      CHECK(journal.finish(771, true) != OB_SUCCESS);
      (void)journal.finish(771, false);
    }
  }
  auto privileges = std::make_shared<RoutinePrivilegeOverlay>(100, 123);
  auto schema = std::make_shared<RoutineSchemaOverlay>(privileges);
  ObRoutineInfo routine;
  routine.set_database_id(100); routine.set_routine_id(9001); routine.set_owner_id(123);
  routine.set_schema_version(42); routine.set_package_id(OB_INVALID_ID); routine.set_overload(0);
  routine.set_routine_type(ROUTINE_FUNCTION_TYPE);
  CHECK(routine.set_routine_name(ObString::make_string("journal_value")) == OB_SUCCESS);
  CHECK(routine.set_routine_body(ObString::make_string("RETURN 1")) == OB_SUCCESS);
  const auto stage = [&](uint64_t id, bool automatic) {
    const ObRoutineInfo *old = nullptr; bool handled = false;
    CHECK(schema->lookup(100, OB_INVALID_ID, routine.get_routine_name(), 0,
        ROUTINE_FUNCTION_TYPE, handled, old) == OB_SUCCESS);
    if (old) {
      CHECK(schema->erase(100, old->get_routine_name(), ROUTINE_FUNCTION_TYPE,
          old->get_routine_id()) == OB_SUCCESS);
      CHECK(privileges->record_drop(*old) == OB_SUCCESS);
    }
    routine.set_routine_id(id);
    CHECK(schema->stage(routine) == OB_SUCCESS);
    CHECK(privileges->record_create(routine, automatic) == OB_SUCCESS);
  };
  const auto current = [&](uint64_t id, ObPrivSet rights) {
    const ObRoutineInfo *found = nullptr; bool handled = false;
    CHECK(schema->lookup(100, OB_INVALID_ID, routine.get_routine_name(), 0,
        ROUTINE_FUNCTION_TYPE, handled, found) == OB_SUCCESS);
    CHECK(handled == (id != 0) && (found ? found->get_routine_id() : 0) == id);
    bool granted = false; ObPrivSet bits = 0;
    CHECK(privileges->lookup(100, routine.get_routine_name(), ROUTINE_FUNCTION_TYPE,
        123, handled, found, granted, bits) == OB_SUCCESS);
    CHECK(bits == rights && granted == (id != 0));
    return found;
  };
  const ObPrivSet rights = OB_PRIV_EXECUTE | OB_PRIV_ALTER_ROUTINE;
  const ObTxSEQ before_plugin_use(2, 0), first(10, 0), inner(20, 0), later(30, 0);
  const ObRoutineInfo *borrowed = nullptr;
  {
    RoutineCatalogTransaction tx(12345); CHECK(tx.valid());
    CHECK(tx.record(12345, ObTxSEQ(), schema, privileges) == OB_INVALID_ARGUMENT);
    CHECK(tx.record(12345, ObTxSEQ(10, 1), schema, privileges) == OB_NOT_SUPPORTED);
    auto other = std::make_shared<RoutinePrivilegeOverlay>(100, 123);
    CHECK(tx.record(12345, first, schema, other) == OB_INVALID_ARGUMENT);
    CHECK(tx.record(12345, first, schema, privileges) == OB_SUCCESS); stage(9001, true);
    borrowed = current(9001, rights);
    CHECK(tx.record(12345, inner, schema, privileges) == OB_SUCCESS);
    CHECK(schema->erase(100, routine.get_routine_name(), ROUTINE_FUNCTION_TYPE, 9001) == OB_SUCCESS);
    CHECK(privileges->record_drop(routine) == OB_SUCCESS);
    // Separate changes after the same data barrier are undone newest-first.
    CHECK(tx.record(12345, inner, schema, privileges) == OB_SUCCESS); stage(9002, false);
    current(9002, 0);
    CHECK(tx.rollback(54321, inner) == OB_STATE_NOT_MATCH); current(9002, 0);
    CHECK(tx.rollback(12345, inner) == OB_SUCCESS); CHECK(current(9001, rights) == borrowed);
    CHECK(tx.rollback(12345, inner) == OB_SUCCESS); current(9001, rights);
    CHECK(tx.record(12345, later, schema, privileges) == OB_SUCCESS); stage(9003, true);
    CHECK(tx.rollback(12345, inner) == OB_SUCCESS); current(9001, rights);
    // A savepoint predating lazy journal construction still restores base view.
    CHECK(tx.rollback(12345, before_plugin_use) == OB_SUCCESS); current(0, 0);
    CHECK(borrowed->get_routine_id() == 9001 && borrowed->get_routine_body() == routine.get_routine_body());
    CHECK(tx.record(12345, first, schema, privileges) == OB_STATE_NOT_MATCH);
    CHECK(tx.record(12345, ObTxSEQ(40, 0), schema, privileges) == OB_SUCCESS); stage(9004, true);
    CHECK(tx.finish(12345, true) == OB_STATE_NOT_MATCH);
    CHECK(tx.prepare_commit(12345) == OB_SUCCESS);
    CHECK(tx.record(12345, ObTxSEQ(50, 0), schema, privileges) == OB_STATE_NOT_MATCH);
    CHECK(tx.finish(12345, true) == OB_SUCCESS); current(9004, rights);
    CHECK(tx.rollback(12345, before_plugin_use) == OB_STATE_NOT_MATCH);
  }
  current(9004, rights); // Verified commit releases marks; it does not publish.
  {
    RoutineCatalogTransaction tx(555); CHECK(tx.valid());
    CHECK(tx.record(555, first, schema, privileges) == OB_SUCCESS); stage(9005, true);
    // Model unknown durable outcome: destroy PRIVATE marks, do not assert DB abort.
  }
  current(9004, rights);
  {
    RoutineCatalogTransaction tx(556); CHECK(tx.valid());
    CHECK(tx.record(556, first, schema, privileges) == OB_SUCCESS); stage(9006, true);
    CHECK(tx.finish(556, false) == OB_SUCCESS); current(9004, rights);
  }
  {
    // A foreign view rollback breaks ancestry. Journal returns the original
    // host error, rejects new mutation/commit, consumes each mark exactly once.
    RoutineCatalogSavepoint outer(schema, privileges);
    stage(9007, true);
    RoutineCatalogTransaction tx(557); CHECK(tx.valid());
    CHECK(tx.record(557, first, schema, privileges) == OB_SUCCESS); stage(9008, true);
    CHECK(outer.rollback() == OB_SUCCESS); current(9004, rights);
    CHECK(tx.rollback(557, first) == OB_STATE_NOT_MATCH);
    CHECK(tx.record(557, later, schema, privileges) == OB_STATE_NOT_MATCH);
    CHECK(tx.finish(557, true) == OB_STATE_NOT_MATCH);
    CHECK(tx.finish(557, false) == OB_STATE_NOT_MATCH); current(9004, rights);
  }
}
}
#endif
