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
// Focused entrypoint for the same writer fixture used by kernel_script.cpp.
// No auxiliary DSO build, server, SQL isolation or durable commit is simulated.
#include "sql/resolver/ob_resolver_utils.h"
#include "sql/resolver/ddl/extension_routine_batch.h"
#include "sql/ob_sql_context.h"
#include "sql/session/ob_sql_session_info.h"
#include "sql/pl/ob_pl.h"
#include "sql/engine/ob_physical_plan.h"
#include "sql/ob_sql_init.h"
#include "share/rc/ob_module_provider.h"
#include "share/schema/ob_priv_sql_service.h"
#include "share/ob_ddl_common.h"
#include "rootserver/pl_ddl/ob_pl_ddl_service.h"
#include "rootserver/ob_ddl_service.h"
#include "rootserver/pl_ddl/native_routine_privilege_transaction.h"
#include <cstdlib>
#include <iostream>
#include "catalog_version_fixture.h"

#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << ": " << #expr << std::endl; std::abort(); } } while (false)
#include "routine_version_reservation_fixture.h"
#include "routine_catalog_transaction_fixture.h"
#include "routine_catalog_writer_fixture.h"
#include "native_routine_placement_fixture.h"
#include "routine_extension_install_fixture.h"
#include "routine_ddl_invalidation_fixture.h"

static void native_privilege_transaction_boundary()
{
  using namespace oceanbase::common;
  for (int scenario = 0; scenario < 14; ++scenario) {
    struct Transaction final : ObMySQLTransaction {
      explicit Transaction(int scenario) : scenario(scenario), active(scenario == 10) {}
      int scenario, starts = 0, ends = 0;
      bool active, commit = false;
      std::vector<std::string> events;
      bool is_started() const override { return active; }
      int start(ObISQLClient *, const int64_t &version, bool snapshot) override {
        ++starts; events.push_back("start"); CHECK(version == 42 && !snapshot);
        active = scenario != 2 && scenario != 11;
        return scenario == 2 || scenario == 3 ? OB_TIMEOUT : OB_SUCCESS;
      }
      int end(bool value) override {
        CHECK(active); ++ends; commit = value; active = false;
        events.push_back(value ? "commit" : "rollback");
        return scenario == 5 || scenario == 6 ? OB_TIMEOUT : OB_SUCCESS;
      }
    } transaction(scenario);
    ObMySQLProxy proxy;
    int batches = 0, publications = 0;
    const int result = oceanbase::rootserver::execute_native_routine_privilege_transaction(
        transaction, proxy, scenario == 13 ? 0 : 42,
        [&](int64_t &changed) {
          CHECK(transaction.active); ++batches; transaction.events.push_back("batch");
          changed = scenario == 1 ? 0 : scenario == 12 ? -1 : 49;
          if (scenario == 8) throw std::bad_alloc();
          if (scenario == 9) throw 1;
          return scenario == 4 || scenario == 5 ? OB_USER_NOT_EXIST : OB_SUCCESS;
        }, [&] {
          CHECK(!transaction.active && transaction.ends == 1 && transaction.commit);
          ++publications; transaction.events.push_back("publish");
          return scenario == 7 ? OB_TIMEOUT : OB_SUCCESS;
        });
    const int expected[] = {OB_SUCCESS, OB_SUCCESS, OB_TIMEOUT, OB_TIMEOUT, OB_USER_NOT_EXIST,
        OB_USER_NOT_EXIST, OB_TIMEOUT, OB_TIMEOUT, OB_ALLOCATE_MEMORY_FAILED, OB_ERR_UNEXPECTED,
        OB_STATE_NOT_MATCH, OB_STATE_NOT_MATCH, OB_ERR_UNEXPECTED, OB_INVALID_ARGUMENT};
    CHECK(result == expected[scenario]);
    const bool started = scenario != 10 && scenario != 13;
    const bool applied = started && scenario != 2 && scenario != 3 && scenario != 11;
    const bool ended = started && scenario != 2 && scenario != 11;
    const bool commit = scenario == 0 || scenario == 1 || scenario == 6 || scenario == 7;
    const bool published = scenario == 0 || scenario == 7;
    CHECK(transaction.starts == int(started) && batches == int(applied) &&
        transaction.ends == int(ended) && transaction.commit == commit && publications == int(published));
    std::vector<std::string> sequence;
    if (started) sequence.push_back("start");
    if (applied) sequence.push_back("batch");
    if (ended) sequence.push_back(commit ? "commit" : "rollback");
    if (published) sequence.push_back("publish");
    CHECK(transaction.events == sequence);
  }
  std::cout << "PASS: 14 native DCL transaction boundary scenarios: single batch/start/end, admission and write rollback, commit failure without replay/publication, no-op and publication failure; controlled transaction, not live commit" << std::endl;
}

int main()
{
  using namespace oceanbase::common;
  using namespace oceanbase::sql;
  OB_LOGGER.set_file_name("native_routine_writer.log", true);
  OB_LOGGER.set_enable_async_log(false);
  OB_LOGGER.set_log_level("WARN");
  CHECK(ObCharset::init_charset() == OB_SUCCESS);
  CHECK(init_sql_factories() == OB_SUCCESS);
  CHECK(oceanbase::share::ObSysVariables::init_default_values() == OB_SUCCESS);
  CHECK(ObBasicSessionInfo::init_sys_vars_cache_base_values() == OB_SUCCESS);
  native_privilege_transaction_boundary();
  routine_ddl_invalidation_test::run();
  native_routine_placement_test::run();
  routine_catalog_writer_test::run();
  routine_extension_install_test::run();
  std::cout << "PASS: 136 routine writer scenarios (68 cases at native slots 0 and 7), including pre-reserved native owner grants and intervening version allocation, token/policy mismatch, fresh-ACL admission, catalog/module identity, transactional ACL cleanup, all automatic-grant SQL failures and borrowed rollback; controlled SQL/provider, no live-server claims" << std::endl;
}
