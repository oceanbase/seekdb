// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#ifndef SEEKDB_TEST_BORROWED_SQL_TRANSACTION_FIXTURE_H_
#define SEEKDB_TEST_BORROWED_SQL_TRANSACTION_FIXTURE_H_
#include "share/schema/borrowed_sql_transaction.h"
#include "sql/engine/expr/caller_catalog_transaction.h"
#include "routine_version_reservation_fixture.h"
#include "share/schema/ob_ddl_sql_service.h"
#include "share/schema/routine_catalog_transaction.h"
#include <thread>

namespace borrowed_sql_transaction_test {
using namespace oceanbase::common;
using namespace oceanbase::share::schema;
struct Guard final : ICallerTransactionGuard {
  int error = OB_SUCCESS;
  int check() const override { return error; }
};
struct Client final : ObISQLClient {
  explicit Client(Guard &guard) : guard(guard) {}
  int calls = 0, connections = 0, failure = OB_SUCCESS, after = OB_SUCCESS;
  int64_t affected = 7;
  bool throw_allocation = false;
  Guard &guard;
  int execute() {
    ++calls;
    guard.error = after;
    if (throw_allocation) throw std::bad_alloc();
    return failure;
  }
  int read(ReadResult &, const char *, int32_t) override { return execute(); }
  int write(const char *, int32_t, int64_t &rows) override { rows = affected; return execute(); }
  int escape(const char *, int64_t, char *, int64_t, int64_t &size) override { size = 3; return execute(); }
  sqlclient::ObISQLConnection *get_connection() override { ++connections; return nullptr; }
  int acquire_connection(sqlclient::ObISQLConnectionGuard &, int32_t) override { ++connections; return OB_ERR_UNEXPECTED; }
};
inline void run()
{
  {
    Guard guard; Client client(guard);
    {
      BorrowedSQLTransaction borrowed(client, guard);
      ObMySQLTransaction &writer = borrowed;
      CHECK(writer.is_started());
      int64_t rows = 99;
      CHECK(writer.write("generated catalog DML", rows) == OB_SUCCESS && rows == 7);
      ObISQLClient::ReadResult result;
      CHECK(writer.read(result, "generated catalog read") == OB_SUCCESS);
      CHECK(client.calls == 2 && client.connections == 0);
    }
    CHECK(client.calls == 2 && client.connections == 0); // No destructor SQL/new connection.
  }
  for (int operation = 0; operation < 5; ++operation) {
    Guard guard; Client client(guard);
    BorrowedSQLTransaction borrowed(client, guard); ObMySQLTransaction &writer = borrowed;
    int ret = OB_SUCCESS;
    sqlclient::ObISQLConnectionGuard connection;
    switch (operation) {
      case 0: ret = writer.start(&client, false, 0); break;
      case 1: { const int64_t version = 42; ret = writer.start(&client, version, false); break; }
      case 2: ret = writer.end(true); break;
      case 3: ret = writer.end(false); break;
      case 4: ret = writer.acquire_connection(connection, 0); break;
    }
    CHECK(ret == OB_NOT_SUPPORTED && !writer.is_started());
    int64_t rows = 99;
    CHECK(writer.write("must not execute", rows) == OB_NOT_SUPPORTED && rows == 0);
    CHECK(client.calls == 0 && client.connections == 0 && !connection.is_valid());
  }
  for (int scenario = 0; scenario < 4; ++scenario) {
    Guard guard; Client client(guard);
    BorrowedSQLTransaction borrowed(client, guard);
    if (scenario == 0) guard.error = OB_TRANS_INVALID_STATE;
    if (scenario == 1) client.after = OB_TRANS_INVALID_STATE;
    if (scenario == 2) { client.failure = OB_TIMEOUT; client.after = OB_TRANS_INVALID_STATE; }
    if (scenario == 3) client.throw_allocation = true;
    const int expected = scenario < 2 ? OB_TRANS_INVALID_STATE : scenario == 2 ? OB_TIMEOUT : OB_ALLOCATE_MEMORY_FAILED;
    int64_t rows = 99;
    CHECK(borrowed.write("generated DML", rows) == expected && rows == 0);
    guard.error = OB_SUCCESS; client.after = OB_SUCCESS; client.failure = OB_SUCCESS;
    const auto calls = client.calls;
    CHECK(borrowed.write("must stay failed", rows) == expected && rows == 0);
    CHECK(borrowed.status() == expected && client.calls == calls);
  }
  {
    Guard guard; Client client(guard); BorrowedSQLTransaction borrowed(client, guard);
    int64_t rows = 99;
    CHECK(borrowed.write("other resource group", 1, rows) == OB_NOT_SUPPORTED && rows == 0);
    CHECK(client.calls == 0);
  }
  {
    // Real DDL operation generation + borrowed adapter + Rust journal. The
    // transport alone is controlled: no server transaction/publication claims.
    using namespace oceanbase::share::schema;
    using oceanbase::transaction::ObTxSEQ;
    auto service = std::make_unique<routine_reservation_test::VersionService>();
    ObMySQLProxy proxy;
    routine_version_test::Allocator allocator(proxy, *service);
    ObDDLSqlService ddl(allocator);
    ObSchemaOperation operation; operation.op_type_ = OB_DDL_END_SIGN;
    auto *tsi = GET_TSI(TSILastOper); CHECK(tsi);
    const auto previous = tsi->last_operation_schema_version_;
    for (int scenario = 0; scenario < 7; ++scenario) {
      RoutineCatalogTransaction journal(123);
      struct Recorder final : ICatalogOperationRecorder {
        RoutineCatalogTransaction &journal;
        int check_error = OB_SUCCESS, finish_error = OB_SUCCESS, calls = 0;
        explicit Recorder(RoutineCatalogTransaction &journal) : journal(journal) {}
        int check_schema_operation() const override { return check_error; }
        int finish_schema_operation(int64_t version, int sql_result) override {
          CHECK(sql_result == OB_SUCCESS); ++calls;
          return finish_error == OB_SUCCESS ? journal.record_schema_version(123, ObTxSEQ(10, 0), version) : finish_error;
        }
      } recorder(journal);
      Guard guard; Client client(guard); client.affected = 1;
      BorrowedSQLTransaction borrowed(client, guard, scenario == 0 ? nullptr : &recorder);
      if (scenario == 1) recorder.check_error = OB_STATE_NOT_MATCH;
      if (scenario == 2) client.failure = OB_TIMEOUT;
      if (scenario == 3) client.affected = 0;
      if (scenario == 4) recorder.finish_error = OB_ALLOCATE_MEMORY_FAILED;
      const int expected[] = {OB_NOT_SUPPORTED, OB_STATE_NOT_MATCH, OB_TIMEOUT,
        OB_ERR_UNEXPECTED, OB_ALLOCATE_MEMORY_FAILED, OB_SUCCESS, OB_INVALID_ARGUMENT};
      tsi->last_operation_schema_version_ = 4321;
      CHECK(ddl.log_nop_operation(operation, scenario == 6 ? 0 : 701, ObString(), borrowed) == expected[scenario]);
      CHECK(tsi->last_operation_schema_version_ == 4321);
      CHECK(client.calls == (scenario < 2 || scenario == 6 ? 0 : 1));
      CHECK(recorder.calls == (scenario == 4 || scenario == 5 ? 1 : 0));
      uint64_t version = 99, count = 99;
      CHECK(journal.schema_state(123, version, count) == OB_SUCCESS);
      CHECK(version == (scenario == 5 ? 701 : 0) && count == (scenario == 5 ? 1 : 0));
      if (scenario != 5) {
        int64_t rows = 99;
        CHECK(borrowed.write("must remain failed", rows) == expected[scenario] && rows == 0);
      } else {
        // New transport instance, same transaction-owned state; a lower
        // reserved version must not move its publication maximum backwards.
        BorrowedSQLTransaction next(client, guard, &recorder);
        CHECK(ddl.log_nop_operation(operation, 601, ObString(), next) == OB_SUCCESS);
        CHECK(journal.schema_state(123, version, count) == OB_SUCCESS && version == 701 && count == 2);
        // Sequential exclusive handoff of the journal, not of a live transport.
        // Each worker's unrelated legacy TSI state remains untouched.
        std::thread worker([&] {
          auto *worker_tsi = GET_TSI(TSILastOper); CHECK(worker_tsi);
          const auto old = worker_tsi->last_operation_schema_version_;
          worker_tsi->last_operation_schema_version_ = 9999;
          BorrowedSQLTransaction on_worker(client, guard, &recorder);
          CHECK(ddl.log_nop_operation(operation, 901, ObString(), on_worker) == OB_SUCCESS);
          CHECK(worker_tsi->last_operation_schema_version_ == 9999);
          worker_tsi->last_operation_schema_version_ = old;
        });
        worker.join();
        CHECK(tsi->last_operation_schema_version_ == 4321);
        CHECK(journal.schema_state(123, version, count) == OB_SUCCESS && version == 901 && count == 3);
        CHECK(journal.rollback(123, ObTxSEQ(10, 0)) == OB_SUCCESS);
        CHECK(journal.schema_state(123, version, count) == OB_SUCCESS && version == 0 && count == 0);
      }
    }
    Guard guard; Client legacy(guard); legacy.affected = 1;
    CHECK(ddl.log_nop_operation(operation, 801, ObString(), legacy) == OB_SUCCESS);
    CHECK(tsi->last_operation_schema_version_ == 801); // Existing non-borrowed path is unchanged.
    tsi->last_operation_schema_version_ = previous;
  }
}
}
#endif
