// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#ifndef SEEKDB_TEST_CATALOG_COMMIT_PREPARATION_FIXTURE_H_
#define SEEKDB_TEST_CATALOG_COMMIT_PREPARATION_FIXTURE_H_
#include "borrowed_sql_transaction_fixture.h"
#include "catalog_sql_namespace_fixture.h"
#include "rootserver/catalog_commit_preparation.h"
#include "common/ob_timeout_ctx.h"
#include "share/ob_global_stat_proxy.h"
#include "share/schema/ob_ddl_epoch.h"

namespace catalog_commit_preparation_test {
using namespace oceanbase::common;
using namespace oceanbase::share::schema;
using oceanbase::rootserver::CatalogCommitPreparation;
using oceanbase::transaction::ObTxSEQ;

inline void check_epoch_sql()
{
  using oceanbase::share::ObGlobalStatProxy;
  using oceanbase::share::SCN;
  // Exercise the actual epoch manager and scalar SQL generator, not an
  // overridden epoch hook. The rows/transport are controlled, not a live DB.
  for (int scenario = 0; scenario < 7; ++scenario) {
    ExtensionVersionRows rows;
    rows.core_value_column = true;
    rows.rows[0].dependency = scenario == 4 ? "invalid" : "7";
    if (scenario == 2) rows.read_status = OB_TIMEOUT;
    if (scenario == 3) rows.rows.clear();
    if (scenario == 5) rows.fail_field = 0;
    if (scenario == 6) rows.rows.push_back(rows.rows[0]);
    rows.on_read = [](ExtensionVersionRows &rows) {
      catalog_sql_namespace_test::check(rows.sql);
      CHECK(rows.sql == "SELECT column_value FROM oceanbase.__all_core_table WHERE "
          "TABLE_NAME = '__all_global_stat' AND COLUMN_NAME = 'ddl_epoch' FOR UPDATE");
    };
    borrowed_sql_transaction_test::Guard guard;
    BorrowedSQLTransaction borrowed(rows, guard);
    ObDDLEpochMgr epoch;
    ObMySQLProxy proxy;
    auto service = std::make_unique<routine_reservation_test::VersionService>();
    CHECK(epoch.init(&proxy, service.get()) == OB_SUCCESS);
    const int expected[] = {OB_SUCCESS, OB_RS_NOT_MASTER, OB_TIMEOUT, OB_ITER_END,
        OB_INVALID_DATA, OB_ERR_NULL_VALUE, OB_ERR_UNEXPECTED};
    CHECK(epoch.check_and_lock_ddl_epoch(borrowed, scenario == 1 ? 8 : 7) == expected[scenario]);
    CHECK(rows.reads == 1 && rows.writes == 0 && rows.starts == 0 && rows.ends == 0);
    // A transport failure poisons the borrowed adapter; semantic checks made
    // after a successful read do not change its transport status.
    CHECK(borrowed.status() == (scenario == 2 ? OB_TIMEOUT : OB_SUCCESS));
  }
  // The same proxy has direct scalar reads/updates that bypass CoreTableProxy.
  // Cover every such SQL generator, preserving its lock and monotonic clauses.
  ExtensionVersionRows rows;
  rows.read_status = OB_TIMEOUT;
  rows.write_status = OB_SUCCESS;
  rows.on_read = [](ExtensionVersionRows &rows) { catalog_sql_namespace_test::check(rows.sql); };
  SCN scn;
  CHECK(ObGlobalStatProxy::get_snapshot_gc_scn(rows, scn) == OB_TIMEOUT);
  CHECK(rows.sql.find("FOR UPDATE") == std::string::npos);
  CHECK(ObGlobalStatProxy::select_snapshot_gc_scn_for_update(rows, scn) == OB_TIMEOUT);
  CHECK(rows.sql.find("FOR UPDATE") != std::string::npos);
  CHECK(ObGlobalStatProxy::select_snapshot_gc_scn_for_update_nowait(rows, scn) == OB_TIMEOUT);
  CHECK(rows.sql.find("FOR UPDATE NOWAIT") != std::string::npos);
  int64_t lsn = 0;
  for (bool locked : {false, true}) {
    CHECK(ObGlobalStatProxy::get_change_stream_refresh_scn(rows, locked, scn) == OB_TIMEOUT);
    CHECK((rows.sql.find("FOR UPDATE") != std::string::npos) == locked);
    CHECK(ObGlobalStatProxy::get_change_stream_min_dep_lsn(rows, locked, lsn) == OB_TIMEOUT);
    CHECK((rows.sql.find("FOR UPDATE") != std::string::npos) == locked);
  }
  int64_t affected = 0;
  CHECK(ObGlobalStatProxy::update_snapshot_gc_scn(rows, SCN::min_scn(), affected) == OB_SUCCESS && affected == 1);
  CHECK(ObGlobalStatProxy::advance_change_stream_refresh_scn(rows, SCN::min_scn(), affected) == OB_SUCCESS && affected == 1);
  CHECK(ObGlobalStatProxy::advance_change_stream_min_dep_lsn(rows, 9, affected) == OB_SUCCESS && affected == 1);
  CHECK(rows.reads == 7 && rows.writes == 3 && rows.starts == 0 && rows.ends == 0);
  for (const auto &sql : rows.written) {
    catalog_sql_namespace_test::check(sql);
    CHECK(sql.find("AND column_value < ") != std::string::npos);
  }
}

inline void run()
{
  check_epoch_sql();
  {
    auto service = std::make_unique<routine_reservation_test::VersionService>();
    CHECK(service->request_schema_refresh(0) == OB_INVALID_ARGUMENT);
    CHECK(service->request_schema_refresh(-1) == OB_INVALID_ARGUMENT);
    CHECK(service->request_schema_refresh(601) == OB_INNER_STAT_ERROR);
  }
  for (int scenario = 0; scenario < 12; ++scenario) {
    std::vector<int> events;
    borrowed_sql_transaction_test::Guard guard;
    borrowed_sql_transaction_test::Client client(guard), fresh(guard);
    BorrowedSQLTransaction borrowed(client, guard);
    auto service = std::make_unique<routine_reservation_test::VersionService>();
    struct Admission final : oceanbase::rootserver::CatalogDDLAdmission {
      int scenario;
      std::vector<int> &events;
      borrowed_sql_transaction_test::Guard &guard;
      Admission(ObMultiVersionSchemaService &service, ObMySQLTransaction &transaction,
          ObISQLClient &fresh, int scenario, std::vector<int> &events, borrowed_sql_transaction_test::Guard &guard)
          : CatalogDDLAdmission(service, transaction, fresh), scenario(scenario), events(events), guard(guard) {}
      int capture_epoch(int64_t &epoch) override {
        events.push_back(1);
        if (scenario == 10) throw std::bad_alloc();
        epoch = scenario == 4 ? 0 : 7;
        return scenario == 3 ? OB_ENTRY_NOT_EXIST : OB_SUCCESS;
      }
      int lock(int64_t timeout) override {
        CHECK(timeout > 0); events.push_back(2);
        return scenario == 5 ? OB_TIMEOUT : OB_SUCCESS;
      }
      int read_version(int64_t &version) override {
        events.push_back(3); version = scenario == 7 ? 601 : 600;
        if (scenario == 8) guard.error = OB_TRANS_INVALID_STATE;
        return scenario == 6 ? OB_TIMEOUT : OB_SUCCESS;
      }
    } admission(*service, borrowed, scenario == 11 ? static_cast<ObISQLClient &>(borrowed) : fresh,
        scenario, events, guard);
    ObTimeoutCtx parent;
    const int64_t deadline = ObTimeUtility::current_time() + 60000000;
    CHECK(parent.set_abs_timeout(scenario == 9 ? 1 : deadline) == OB_SUCCESS);
    if (scenario == 2) guard.error = OB_TRANS_INVALID_STATE;
    int64_t epoch = 99;
    const int expected[] = {OB_SUCCESS, OB_INVALID_ARGUMENT, OB_STATE_NOT_MATCH, OB_ENTRY_NOT_EXIST,
      OB_ERR_UNEXPECTED, OB_TIMEOUT, OB_TIMEOUT, OB_EAGAIN, OB_STATE_NOT_MATCH, OB_TIMEOUT,
      OB_ALLOCATE_MEMORY_FAILED, OB_INVALID_ARGUMENT};
    CHECK(admission.acquire(scenario == 1 ? 0 : 600, deadline, epoch) == expected[scenario]);
    CHECK(epoch == (scenario == 0 ? 7 : 0));
    CHECK(&ObTimeoutCtx::get_ctx() == &parent); // TLS frame unwound on all paths, including exception.
    if (scenario == 0 || scenario == 6 || scenario == 7 || scenario == 8) CHECK(events == std::vector<int>({1, 2, 3}));
    else if (scenario == 5) CHECK(events == std::vector<int>({1, 2}));
    else if (scenario == 3 || scenario == 4 || scenario == 10) CHECK(events == std::vector<int>({1}));
    else CHECK(events.empty());
    const auto count = events.size(); epoch = 99;
    CHECK(admission.acquire(600, deadline, epoch) == OB_INIT_TWICE && epoch == 0 && events.size() == count);
    CHECK(client.calls == 0 && fresh.calls == 0); // Effects above are controlled, never actual DB proof.
  }
  // Real prepare sequencing, version allocation adapter, DDL SQL generation,
  // borrowed transport and Rust ledger. SQL/MDS/watermark effects are controlled;
  // this does not prove a durable transaction, epoch lock, or schema publication.
  for (int scenario = 0; scenario < 15; ++scenario) {
    std::vector<std::string> events;
    struct Client final : ObMySQLTransaction {
      std::vector<std::string> &events;
      int failure = OB_SUCCESS, starts = 0, ends = 0;
      explicit Client(std::vector<std::string> &events) : events(events) {}
      int write(const char *sql, int32_t, int64_t &rows) override {
        catalog_sql_namespace_test::check(sql);
        CHECK(std::string(sql).find("oceanbase.__all_ddl_operation") != std::string::npos);
        events.emplace_back("end-sign"); rows = 1; return failure;
      }
      int read(ReadResult &, const char *sql, int32_t) override {
        catalog_sql_namespace_test::check(sql);
        CHECK(std::string(sql).find("oceanbase.__all_core_table") != std::string::npos);
        CHECK(std::string(sql).find("FOR UPDATE") != std::string::npos);
        events.emplace_back("watermark-read"); return OB_TIMEOUT;
      }
      int start(ObISQLClient *, bool, int32_t) override { ++starts; return OB_NOT_SUPPORTED; }
      int start(ObISQLClient *, const int64_t &, bool) override { ++starts; return OB_NOT_SUPPORTED; }
      int end(bool) override { ++ends; return OB_NOT_SUPPORTED; }
    } client(events);
    RoutineCatalogTransaction journal(123);
    struct Recorder final : ICatalogOperationRecorder {
      RoutineCatalogTransaction &journal;
      std::vector<std::string> &events;
      int failure = OB_SUCCESS;
      bool preparing = false;
      Recorder(RoutineCatalogTransaction &journal, std::vector<std::string> &events)
          : journal(journal), events(events) {}
      int check_schema_operation() const override {
        return preparing ? journal.check_preparing(123) : OB_SUCCESS;
      }
      int finish_schema_operation(int64_t version, int sql_result) override {
        CHECK(sql_result == OB_SUCCESS); events.emplace_back("record");
        return failure != OB_SUCCESS ? failure : preparing ? journal.record_end_sign(123, version)
            : journal.record_schema_version(123, ObTxSEQ(10, 0), version);
      }
    } recorder(journal, events);
    borrowed_sql_transaction_test::Guard guard;
    BorrowedSQLTransaction transaction(client, guard, &recorder);
    auto service = std::make_unique<routine_reservation_test::VersionService>();
    ObMySQLProxy proxy;
    routine_version_test::Allocator allocator(proxy, *service);
    if (scenario != 12) service->bind_sql(allocator);
    class Preparation final : public CatalogCommitPreparation {
    public:
      std::vector<std::string> &events;
      int signal_error = OB_SUCCESS, watermark_error = OB_SUCCESS;
      int64_t watermark = 0;
      bool real_watermark = false;
      Preparation(ObMultiVersionSchemaService &service, ObMySQLTransaction &transaction,
          std::vector<std::string> &events)
          : CatalogCommitPreparation(service, transaction), events(events) {}
      int register_signal() override { events.emplace_back("signal"); return signal_error; }
      int write_watermark(int64_t version) override {
        events.emplace_back("watermark"); watermark = version;
        return real_watermark ? CatalogCommitPreparation::write_watermark(version) : watermark_error;
      }
    } preparation(*service, transaction, events);
    const bool changed = scenario != 1;
    const bool end_sign = scenario != 2;
    const int64_t last = scenario == 4 ? -1 : scenario == 11 ? 0 : 601;
    if (changed && last > 0 && end_sign)
      CHECK(journal.admit_ddl(123, ObTxSEQ(1, 0), 7) == OB_SUCCESS);
    if (changed && last > 0)
      CHECK(journal.record_schema_version(123, ObTxSEQ(1, 0), last) == OB_SUCCESS);
    // Caller finalization requires an end-sign. Preserve the separate legacy
    // Root/no-end-sign/bootstrap tests without pretending they use that protocol.
    recorder.preparing = changed && last > 0 && end_sign;
    if (recorder.preparing) {
      uint64_t version = 99, operations = 99;
      CHECK(journal.begin_prepare(123, version, operations) == OB_SUCCESS);
      CHECK(version == static_cast<uint64_t>(last) && operations == 1);
      CHECK(journal.record_schema_version(123, ObTxSEQ(10, 0), 999) == OB_STATE_NOT_MATCH);
    }
    if (scenario == 3) guard.error = OB_TRANS_INVALID_STATE;
    if (scenario == 5) allocator.fail_at_ = 1;
    if (scenario == 6) allocator.next_ = last;
    if (scenario == 7) allocator.throw_at_ = 1;
    if (scenario == 8) client.failure = OB_TIMEOUT;
    if (scenario == 9) preparation.signal_error = OB_TIMEOUT;
    if (scenario == 10) preparation.watermark_error = OB_TIMEOUT;
    if (scenario == 13) recorder.failure = OB_ALLOCATE_MEMORY_FAILED;
    if (scenario == 14) preparation.real_watermark = true;
    const int expected[] = {OB_SUCCESS, OB_SUCCESS, OB_SUCCESS, OB_STATE_NOT_MATCH,
      OB_INVALID_ARGUMENT, OB_TIMEOUT, OB_ERR_UNEXPECTED, OB_ALLOCATE_MEMORY_FAILED,
      OB_TIMEOUT, OB_TIMEOUT, OB_TIMEOUT, OB_SUCCESS, OB_ERR_UNEXPECTED, OB_ALLOCATE_MEMORY_FAILED, OB_TIMEOUT};
    auto *tsi = GET_TSI(TSILastOper); CHECK(tsi);
    const auto previous = tsi->last_operation_schema_version_;
    tsi->last_operation_schema_version_ = 9999; // Must never be a prepare input.
    int64_t prepared_version = 99;
    CHECK(preparation.prepare(last, changed, end_sign, prepared_version) == expected[scenario]);
    CHECK(tsi->last_operation_schema_version_ == 9999);
    tsi->last_operation_schema_version_ = previous;
    const int64_t expected_version = scenario == 0 || scenario == 11 ? 1001 : scenario == 2 ? 601 : 0;
    CHECK(prepared_version == expected_version);
    if (scenario == 1) CHECK(events == std::vector<std::string>({"signal"}));
    else if (scenario == 2) CHECK(events == std::vector<std::string>({"signal", "watermark"}));
    else if (scenario == 0 || scenario == 10 || scenario == 11)
      CHECK(events == std::vector<std::string>({"end-sign", "record", "signal", "watermark"}));
    else if (scenario == 8) CHECK(events == std::vector<std::string>({"end-sign"}));
    else if (scenario == 9) CHECK(events == std::vector<std::string>({"end-sign", "record", "signal"}));
    else if (scenario == 13) CHECK(events == std::vector<std::string>({"end-sign", "record"}));
    else if (scenario == 14) CHECK(events == std::vector<std::string>({"end-sign", "record", "signal", "watermark", "watermark-read"}));
    else CHECK(events.empty());
    CHECK(client.starts == 0 && client.ends == 0);
    const auto size = events.size();
    prepared_version = 99;
    CHECK(preparation.prepare(last, changed, end_sign, prepared_version) == OB_INIT_TWICE);
    CHECK(prepared_version == 0 && events.size() == size);
    if (recorder.preparing) {
      CHECK(journal.rollback(123, ObTxSEQ(10, 0)) == OB_STATE_NOT_MATCH);
      CHECK(journal.prepare_commit(123) == OB_STATE_NOT_MATCH); // Cannot bypass MDS/watermark result.
      CHECK(journal.complete_prepare(123, expected_version, expected[scenario]) == expected[scenario]);
      if (expected[scenario] == OB_SUCCESS) {
        uint64_t version = 99, operations = 99;
        CHECK(journal.schema_state(123, version, operations) == OB_SUCCESS && version == 1001 && operations == 2);
      } else CHECK(journal.finish(123, true) != OB_SUCCESS);
      // VERIFIED outcome is modeled, not proof of actual SQL/MDS rollback.
      CHECK(journal.finish(123, expected[scenario] == OB_SUCCESS) == expected[scenario]);
    } else {
      CHECK(journal.rollback(123, ObTxSEQ(10, 0)) == OB_SUCCESS);
      uint64_t version = 99, operations = 99;
      CHECK(journal.schema_state(123, version, operations) == OB_SUCCESS);
      CHECK(version == (changed && last > 0 ? static_cast<uint64_t>(last) : 0));
      CHECK(operations == (changed && last > 0 ? 1 : 0));
    }
  }
  ObMySQLTransaction inactive;
  CHECK(CatalogCommitPreparation::register_transaction_signal(inactive) == OB_INVALID_ARGUMENT);
  borrowed_sql_transaction_test::Guard guard;
  borrowed_sql_transaction_test::Client client(guard);
  BorrowedSQLTransaction active(client, guard);
  CHECK(CatalogCommitPreparation::register_transaction_signal(active) == OB_ERR_UNEXPECTED);
  CHECK(client.calls == 0 && client.connections == 1); // No connection means no SQL fallback.
  CHECK(oceanbase::rootserver::CatalogDDLAdmission::lock_transaction(active, false, 0) == OB_TIMEOUT);
  CHECK(client.connections == 1);
  CHECK(oceanbase::rootserver::CatalogDDLAdmission::lock_transaction(active, false, 1000) == OB_ERR_UNEXPECTED);
  CHECK(client.calls == 0 && client.connections == 2);
}
}
#endif
