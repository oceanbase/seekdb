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

#include "share/plugin/ob_plugin_catalog.h"
#include "share/plugin/ob_plugin_sql_catalog.h"
#include "catalog_sql_namespace_fixture.h"

// Actual dependency admission/binder/blocker code, controlled SQL transport.
// This verifies identities, lock-query ordering and transaction ownership, not
// SQL locking, commit/rollback isolation or the Root DDL writer in a server.
namespace native_routine_dependency_test {
using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::share::plugin;

struct Fixture {
  ExtensionVersionRows transport;
  ObPluginCatalog catalog;
  ObPluginSqlConnection connection{&transport};
  bool provider_exists = true, unfinished = false, edge_visible = false;
  int64_t desired = static_cast<int64_t>(ObPluginDesiredState::ACTIVE);
  int64_t actual = static_cast<int64_t>(ObPluginState::ACTIVE);
  int64_t provider_generation = 7, function_generation = 7, edge_generation = 29;
  int function_rows = 1, edge_rows = 1, durable_targets = 1;
  uint64_t expected_generation = 7;
  std::string error;

  Fixture()
  {
    transport.active = true;
    transport.write_status = OB_SUCCESS;
    CHECK(catalog.init(&transport) == OB_SUCCESS);
    transport.on_read = [this](ExtensionVersionRows &f) {
      const bool diagnostic = f.sql.find("SELECT tenant_id,database_id,extension_id,extension_name") == 0 ||
          f.sql.find("SELECT consumer_kind,consumer_id,consumer_plugin_id") == 0;
      CHECK(diagnostic ? !f.active : f.active);
      f.rows.clear();
      ExtensionVersionRows::Row row;
      int count = 1;
      if (f.sql.find("SELECT desired_state,actual_state,generation FROM oceanbase.__all_plugin_package WHERE") == 0) {
        CHECK(f.sql.find("plugin_id='org.seekdb.gis' FOR UPDATE") != std::string::npos);
        if (!provider_exists) return;
        row.integers = {{0, desired}, {1, actual}, {2, provider_generation}};
      } else if (f.sql.find("SELECT plugin_id,relative_path") == 0) {
        CHECK(f.sql.find("plugin_id='org.seekdb.gis' FOR UPDATE") != std::string::npos);
        if (!provider_exists) return;
        for (int column : {4, 5, 6, 7, 8, 9, 10, 11, 12, 15, 16, 20, 21}) row.integers[column] = 0;
        for (int column : {0, 1, 2, 3, 13, 14, 17, 18, 19}) row.strings[column] = "";
        row.strings[0] = "org.seekdb.gis";
        row.integers[10] = desired; row.integers[11] = actual;
        row.integers[12] = provider_generation;
      } else if (f.sql.find("SELECT generation FROM oceanbase.__all_sql_extension_function") == 0) {
        CHECK(f.sql.find("function_id='org.seekdb.gis.function.st_area'") != std::string::npos);
        CHECK(f.sql.find("plugin_id='org.seekdb.gis' AND generation=7 AND kind=" +
              std::to_string(SEEKDB_PLUGIN_EXTENSION_FUNCTION) + " FOR UPDATE") != std::string::npos);
        count = function_rows; row.integers[0] = function_generation;
      } else if (f.sql.find("SELECT state FROM oceanbase.__all_plugin_operation") == 0) {
        CHECK(f.sql.find("plugin_id='org.seekdb.gis'") != std::string::npos);
        if (!unfinished) return;
        row.integers[0] = static_cast<int64_t>(ObPluginCatalogOperationState::PROMOTE_PENDING);
      } else if (f.sql.find("SELECT COUNT(*) FROM oceanbase.__all_plugin_extension") == 0) {
        CHECK(f.sql.find("plugin_id='org.seekdb.gis' AND generation=7 AND "
                         "object_id='org.seekdb.gis.function.st_area'") != std::string::npos);
        row.integers[0] = durable_targets;
      } else if (f.sql.find("SELECT provider_generation FROM oceanbase.__all_plugin_dependency") == 0) {
        CHECK(f.sql.find("consumer_kind=2 AND consumer_id='routine.310001'") != std::string::npos);
        CHECK(f.sql.find("consumer_plugin_id='' AND consumer_generation=0") != std::string::npos);
        CHECK(f.sql.find("provider_plugin_id='org.seekdb.gis' AND dependency_kind=1 "
                         "AND dependency_id='org.seekdb.gis.function.st_area'") != std::string::npos);
        CHECK(f.sql.find("AND required_capabilities=0 AND optional=0 FOR UPDATE") != std::string::npos);
        count = edge_rows; row.integers[0] = edge_generation;
      } else if (f.sql.find("SELECT tenant_id,database_id,extension_id,extension_name") == 0) {
        return; // A directly declared routine need not belong to an extension.
      } else if (f.sql.find("SELECT consumer_kind,consumer_id,consumer_plugin_id") == 0) {
        if (!edge_visible) return;
        row.integers = {{0, 2}, {3, 0}, {4, 1}, {6, 0}};
        row.strings = {{1, "routine.310001"}, {2, ""}, {5, "org.seekdb.gis.function.st_area"}};
      } else CHECK(false);
      for (int i = 0; i < count; ++i) f.rows.push_back(row);
    };
    transport.affected_rows = [this](const std::string &sql) {
      if (sql.find("INSERT INTO oceanbase.__all_plugin_dependency") == 0) {
        CHECK(sql.find("VALUES(2,'routine.310001','',0,'org.seekdb.gis',7,1,"
                       "'org.seekdb.gis.function.st_area',0,0,0,0,0,0,0,0,0,0,0,0)") != std::string::npos);
        edge_visible = true;
      } else {
        CHECK(sql.find("DELETE FROM oceanbase.__all_plugin_dependency") == 0);
        CHECK(sql.find("consumer_kind=2 AND consumer_id='routine.310001' AND consumer_plugin_id='' "
                       "AND consumer_generation=0 AND provider_plugin_id='org.seekdb.gis' AND provider_generation=" +
                       std::to_string(edge_generation)) != std::string::npos);
        CHECK(sql.find("dependency_id='org.seekdb.gis.function.st_area' AND service_abi_major=0") != std::string::npos);
        edge_visible = false;
      }
      return int64_t{1};
    };
  }
  int mutate(bool add, uint64_t routine = 310001, const std::string &module = "org.seekdb.gis",
             const std::string &implementation = "org.seekdb.gis.function.st_area")
  {
    const auto reads = transport.queries.size(), writes = transport.written.size();
    const int status = catalog.mutate_routine_dependency(connection, module, implementation, routine, add, error,
                                                         add ? expected_generation : 0);
    // These reads/writes can run on a user's borrowed session. Parse each
    // generated statement and reject unqualified catalog table references.
    for (size_t i = reads; i < transport.queries.size(); ++i) catalog_sql_namespace_test::check(transport.queries[i]);
    for (size_t i = writes; i < transport.written.size(); ++i) catalog_sql_namespace_test::check(transport.written[i]);
    CHECK(transport.starts == 0 && transport.ends == 0); // Caller owns transaction.
    return status;
  }
};

inline void run()
{
  {
    Fixture f;
    CHECK(f.mutate(true) == OB_SUCCESS);
    CHECK(f.transport.queries.size() == 5 && f.transport.writes == 1);
    CHECK(f.transport.queries[0].find("SELECT desired_state") == 0);
    CHECK(f.transport.queries[1].find("SELECT generation") == 0);
    CHECK(f.transport.active && f.error.empty());
    // Simulated outer transaction completion, not an SQL commit. Management
    // catalog APIs take a mutex and must run outside the borrowed writer tx.
    f.transport.active = false;
    std::vector<ObPluginRestrictBlocker> blockers;
    CHECK(f.catalog.list_restrict_blockers("org.seekdb.gis", blockers) == OB_SUCCESS);
    CHECK(blockers.size() == 1 && blockers[0].consumer_id_ == "routine.310001");
    CHECK(blockers[0].consumer_kind_ == ObPluginDependencyConsumerKind::USER_OBJECT);
    CHECK(blockers[0].dependency_id_ == "org.seekdb.gis.function.st_area");
    f.provider_exists = false; // DROP doesn't load code or require active package.
    const auto before = f.transport.queries.size();
    f.transport.active = true;
    CHECK(f.mutate(false) == OB_SUCCESS);
    CHECK(f.transport.queries.size() == before + 1 && f.transport.writes == 2);
    f.transport.active = false;
    CHECK(f.catalog.list_restrict_blockers("org.seekdb.gis", blockers) == OB_SUCCESS && blockers.empty());
  }
  for (bool add : {false, true}) {
    Fixture f;
    f.transport.active = false;
    CHECK(f.mutate(add) == OB_STATE_NOT_MATCH && f.transport.queries.empty());
    f.transport.active = true;
    for (uint64_t id : {uint64_t{0}, uint64_t{OB_INVALID_ID}})
      CHECK(f.mutate(add, id) == OB_INVALID_ARGUMENT);
    for (const std::string &id : {std::string{}, std::string("../gis"), std::string("gis\0suffix", 10),
                                  std::string(SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES + 1, 'x')}) {
      CHECK(f.mutate(add, 310001, id) == OB_INVALID_ARGUMENT);
      CHECK(f.mutate(add, 310001, "org.seekdb.gis", id) == OB_INVALID_ARGUMENT);
    }
    CHECK(f.transport.queries.empty() && f.transport.written.empty());
  }
  for (int failure = 0; failure < 11; ++failure) {
    Fixture f;
    int expected = OB_STATE_NOT_MATCH;
    switch (failure) {
      case 0: f.provider_exists = false; expected = OB_ENTRY_NOT_EXIST; break;
      case 1: f.desired = static_cast<int64_t>(ObPluginDesiredState::DISABLED); break;
      case 2: f.actual = static_cast<int64_t>(ObPluginState::STOPPED); break;
      case 3: f.provider_generation = 0; break;
      case 4: f.provider_generation = -1; break;
      case 5: f.function_rows = 0; expected = OB_ENTRY_NOT_EXIST; break;
      case 6: f.function_rows = 2; expected = OB_INVALID_DATA; break;
      case 7: f.function_generation = 0; expected = OB_INVALID_DATA; break;
      case 8: f.function_generation = 8; break;
      case 9: f.unfinished = true; expected = OB_EAGAIN; break;
      case 10: f.durable_targets = 0; break;
    }
    CHECK(f.mutate(true) == expected && !f.error.empty() && f.transport.written.empty());
  }
  for (int failure = 0; failure < 7; ++failure) {
    Fixture f;
    int expected = OB_INVALID_DATA;
    switch (failure) {
      case 0: f.edge_rows = 0; expected = OB_ENTRY_NOT_EXIST; break;
      case 1: f.edge_rows = 2; break;
      case 2: f.edge_generation = 0; break;
      case 3: f.edge_generation = -1; break;
      case 4: f.transport.read_status = OB_TIMEOUT; expected = OB_TIMEOUT; break;
      case 5: f.transport.close_status = OB_TIMEOUT; expected = OB_TIMEOUT; break;
      case 6: f.transport.fail_next_at = 1; expected = OB_TIMEOUT; break;
    }
    CHECK(f.mutate(false) == expected && f.transport.written.empty());
  }
  for (bool add : {false, true}) {
    Fixture f;
    f.transport.write_status = OB_TIMEOUT;
    CHECK(f.mutate(add) == OB_TIMEOUT && f.transport.writes == 1 && f.transport.active);
  }
  for (bool add : {false, true}) {
    for (int column = 0; column < (add ? 3 : 1); ++column) {
      Fixture f;
      f.transport.fail_field = column;
      CHECK(f.mutate(add) == OB_ERR_NULL_VALUE && f.transport.writes == 0);
    }
  }
  {
    Fixture f;
    f.transport.affected_rows = [](const std::string &) { return int64_t{0}; };
    CHECK(f.mutate(false) == OB_ENTRY_NOT_EXIST);
    CHECK(f.mutate(true) == OB_SUCCESS); // Checked, idempotent native rebinding.
  }
  for (uint64_t generation : {uint64_t{0}, uint64_t{8}, UINT64_MAX}) {
    Fixture f;
    f.expected_generation = generation;
    CHECK(f.mutate(true) == (generation == 8 ? OB_STATE_NOT_MATCH : OB_INVALID_ARGUMENT));
    CHECK(f.transport.writes == 0);
    CHECK(f.transport.queries.size() == (generation == 8 ? 1 : 0));
  }
  {
    Fixture f;
    f.transport.write_status = OB_ENTRY_EXIST; // Actual INSERT error, not a zero-row success.
    CHECK(f.mutate(true) == OB_ENTRY_EXIST);
  }
}
} // namespace native_routine_dependency_test
