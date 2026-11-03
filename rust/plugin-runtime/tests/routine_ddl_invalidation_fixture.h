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
#include "rootserver/pl_ddl/routine_ddl_invalidation.h"

namespace routine_ddl_invalidation_test {
inline void run()
{
  using namespace oceanbase::common;
  using namespace oceanbase::share::schema;
  using oceanbase::rootserver::RoutineDdlInvalidation;
  for (int scenario = 0; scenario < 8; ++scenario) {
    RoutineInvalidationQueue queue(1, 4);
    CHECK(queue.valid());
    auto pending = std::make_unique<RoutineDdlInvalidation>();
    CHECK(pending->record(100, 400001, 60, 7) == OB_SUCCESS);
    CHECK(pending->record(100, 400002, 61, 7) == OB_SUCCESS);
    CHECK(pending->record(100, 400001, 62, 7) == OB_SUCCESS); // Separate idempotent ticket, as in caller journals.
    CHECK(pending->record(100, 400003, 63, 8) == OB_STATE_NOT_MATCH);
    struct Evictor final : IRoutineCacheEvictor {
      int64_t refreshed = 69; int calls = 0; bool fail = false;
      int check_schema_version(int64_t required) override {
        CHECK(required >= 70 && required <= 71);
        return refreshed < required ? OB_EAGAIN : OB_SUCCESS;
      }
      int evict(uint64_t db, uint64_t routine) override {
        CHECK(db == 100 && (routine == 400001 || routine == 400002));
        ++calls; return fail ? OB_TIMEOUT : OB_SUCCESS;
      }
    } evictor;
    uint32_t processed = 99;
    if (scenario == 1) {
      CHECK(pending->finish(false) == OB_SUCCESS); // Known rollback before preparation.
    } else {
      if (scenario == 6) queue.close();
      const int ret = pending->prepare(70, scenario == 5 ? 70 : 71,
          [&](RoutineCatalogTransaction &journal, uint64_t scope) {
            return scenario == 4 ? OB_SIZE_OVERFLOW : queue.reserve(journal, scope);
          });
      CHECK((ret == OB_SUCCESS) == (scenario != 4 && scenario != 5 && scenario != 6));
      CHECK(queue.process(evictor, 4, processed) == OB_SUCCESS && processed == 0 && evictor.calls == 0);
      if (scenario == 3) pending.reset(); // Unknown outcome: version-gated conservative eviction, not commit.
      else CHECK(pending->finish(scenario != 2 && scenario != 4 && scenario != 5 && scenario != 6) == OB_SUCCESS);
    }
    const bool deliver = scenario == 0 || scenario == 3 || scenario == 7;
    CHECK(queue.process(evictor, 4, processed) == (deliver ? OB_EAGAIN : OB_SUCCESS));
    CHECK(processed == 0 && evictor.calls == 0);
    evictor.refreshed = 71;
    if (scenario == 7) {
      evictor.fail = true;
      CHECK(queue.process(evictor, 4, processed) == OB_TIMEOUT && processed == 0 && evictor.calls == 1);
      evictor.fail = false;
    }
    CHECK(queue.process(evictor, 4, processed) == OB_SUCCESS);
    CHECK(processed == (deliver ? 3U : 0U));
    CHECK(evictor.calls == (deliver ? 3 + (scenario == 7) : 0));
    CHECK(queue.process(evictor, 4, processed) == OB_SUCCESS && processed == 0);
  }
  std::cout << "PASS: Root DDL invalidation journal and real Rust queue: private before commit, known rollback cancellation, commit/version fence, unknown outcome, reservation/seal failure, duplicate identities and retryable eviction; no SQL transport" << std::endl;
}
}
