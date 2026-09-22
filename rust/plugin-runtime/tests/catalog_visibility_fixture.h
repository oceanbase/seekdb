// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#ifndef SEEKDB_TEST_CATALOG_VISIBILITY_FIXTURE_H_
#define SEEKDB_TEST_CATALOG_VISIBILITY_FIXTURE_H_
#include "session_catalog_view_fixture.h"
#include <thread>

namespace catalog_visibility_test {
using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::share::schema;
using oceanbase::transaction::ObTxDesc;
using oceanbase::transaction::ObTxSEQ;
using oceanbase::transaction::SessionCatalogTestAccess;

class Service final : public MockSchemaService {
public:
  int64_t visible = 42;
  int read_error = OB_SUCCESS;
  int get_runtime_refreshed_schema_version(int64_t &version, bool = false) const override
  { version = visible; return read_error; }
};

inline void run()
{
  // Real client fence and enqueue entrypoints, but no initialized backend:
  // waiting/enqueueing fails deterministically instead of racing a scheduler.
  // Advancing visible models the refresh worker, NOT actual schema publication.
  auto service = std::make_unique<Service>();
  CHECK(service->get_plugin_catalog_commit_version() == 0);
  CHECK(service->refresh_schema_for_client(0) == OB_SUCCESS);
  CHECK(service->publish_plugin_catalog_commit(0) == OB_INVALID_ARGUMENT);
  CHECK(service->publish_plugin_catalog_commit(-1) == OB_INVALID_ARGUMENT);
  CHECK(service->get_plugin_catalog_commit_version() == 0);
  CHECK(service->request_schema_refresh(900) == OB_INNER_STAT_ERROR);
  CHECK(service->get_plugin_catalog_commit_version() == 0); // An enqueue request is not a commit.
  CHECK(service->publish_plugin_catalog_commit(601) == OB_INNER_STAT_ERROR);
  CHECK(service->get_plugin_catalog_commit_version() == 601); // Queue failure retains the fence.
  ObSQLSessionInfo observer;
  CHECK(observer.get_last_ddl_schema_version() == 0);
  CHECK(service->refresh_schema_for_client(observer.get_last_ddl_schema_version()) == OB_INNER_STAT_ERROR);
  CHECK(service->publish_plugin_catalog_commit(600) == OB_INNER_STAT_ERROR);
  CHECK(service->get_plugin_catalog_commit_version() == 601);
  service->visible = 601;
  CHECK(service->refresh_schema_for_client(observer.get_last_ddl_schema_version()) == OB_SUCCESS);
  CHECK(service->refresh_schema_for_client(602) == OB_INNER_STAT_ERROR); // Session fence still applies.
  service->visible = 602;
  CHECK(service->refresh_schema_for_client(602) == OB_SUCCESS);
  service->read_error = OB_TIMEOUT;
  CHECK(service->refresh_schema_for_client(0) == OB_TIMEOUT);
  service->read_error = OB_SUCCESS;
  // Out-of-order commit callbacks cannot regress the shared watermark.
  std::thread first([&] { CHECK(service->publish_plugin_catalog_commit(800) == OB_INNER_STAT_ERROR); });
  std::thread second([&] { CHECK(service->publish_plugin_catalog_commit(700) == OB_INNER_STAT_ERROR); });
  first.join(); second.join();
  CHECK(service->get_plugin_catalog_commit_version() == 800);
  CHECK(service->destroy() == OB_SUCCESS);
  CHECK(service->get_plugin_catalog_commit_version() == 0);

  // Actual session completion + real Rust journal. Data outcomes are supplied,
  // not durable commits. Only verified nonempty commits may publish a fence.
  for (int scenario = 0; scenario < 7; ++scenario) {
    auto backend = std::make_unique<Service>();
    ObSQLSessionInfo writer, reader;
    ObTxDesc tx;
    SessionCatalogTestAccess::active(tx);
    struct Binding {
      ObSQLSessionInfo &session;
      ObMultiVersionSchemaService *previous = GCTX.schema_service_;
      Binding(ObSQLSessionInfo &session, ObTxDesc &tx, ObMultiVersionSchemaService &backend) : session(session) {
        session.get_tx_desc() = &tx; GCTX.schema_service_ = &backend;
      }
      ~Binding() {
        session.discard_plugin_catalog_transaction(); session.get_tx_desc() = nullptr;
        GCTX.schema_service_ = previous;
      }
    } binding(writer, tx, *backend);
    std::shared_ptr<RoutineSchemaOverlay> schema;
    std::shared_ptr<RoutinePrivilegeOverlay> privileges;
    std::shared_ptr<RoutineCatalogTransaction> journal;
    CHECK(writer.prepare_plugin_catalog_view(ObTxSEQ(10, 0), schema, privileges, journal) == OB_SUCCESS);
    if (scenario == 5) {
      CHECK(journal->prepare_commit(771) == OB_SUCCESS);
    } else {
      CHECK(journal->admit_ddl(771, ObTxSEQ(10, 0), 7) == OB_SUCCESS);
      CHECK(journal->record_schema_version(771, ObTxSEQ(10, 0), 600) == OB_SUCCESS);
      uint64_t version = 0, operations = 0;
      CHECK(journal->begin_prepare(771, version, operations) == OB_SUCCESS);
      CHECK(journal->record_end_sign(771, 601) == OB_SUCCESS);
      CHECK(journal->complete_prepare(771, 601, OB_SUCCESS) == OB_SUCCESS);
    }
    const int results[] = {OB_SUCCESS, OB_TRANS_COMMITED, OB_SUCCESS,
        OB_TRANS_ROLLBACKED, OB_TIMEOUT, OB_SUCCESS, OB_SUCCESS};
    if (scenario == 6) SessionCatalogTestAccess::active(tx, 772);
    CHECK(writer.complete_plugin_catalog_transaction(771, results[scenario], scenario == 2)
        == (scenario == 6 ? OB_TRANS_INVALID_STATE : OB_SUCCESS));
    const int64_t expected = scenario < 2 ? 601 : 0;
    CHECK(backend->get_plugin_catalog_commit_version() == expected);
    CHECK(writer.get_last_ddl_schema_version() == expected);
    CHECK(!writer.has_plugin_catalog_transaction());
    CHECK(reader.get_last_ddl_schema_version() == 0);
    CHECK(backend->refresh_schema_for_client(reader.get_last_ddl_schema_version())
        == (expected ? OB_INNER_STAT_ERROR : OB_SUCCESS));
    backend->visible = 601;
    CHECK(backend->refresh_schema_for_client(reader.get_last_ddl_schema_version()) == OB_SUCCESS);
  }
}
}
#endif
