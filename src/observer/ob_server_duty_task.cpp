/*
 * Copyright (c) 2025 OceanBase.
 *
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

#define USING_LOG_PREFIX SERVER
#include "ob_server_duty_task.h"
#include "share/rc/ob_server_runtime.h"
#include "sql/engine/ob_sql_memory_manager.h"
#include "observer/omt/ob_server_runtime.h"

using namespace oceanbase::common;

namespace oceanbase {
using namespace share;
using namespace share::schema;
using namespace sql;
namespace observer {

ObServerDutyTask::ObServerDutyTask()
  : allocator_(ObModIds::OB_DUTY_TASK)
{
}

void ObServerDutyTask::runTimerTask()
{
  allocator_.reset_remain_one_page();
  update_runtime_settings();
}

int ObServerDutyTask::schedule(common::ObTimer &timer)
{
  return timer.schedule(*this, SCHEDULE_PERIOD, true);
}

void ObServerDutyTask::update_runtime_settings()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(update_ctx_memory_throttle())) {
  }
}

int ObServerDutyTask::update_ctx_memory_throttle()
{
  int ret = OB_SUCCESS;
  {
    ObCtxMemoryLimitChecker checker;
    uint64_t ctx_id = 0;
    int64_t limit = 0;
    ObMallocAllocator *alloc = ObMallocAllocator::get_instance();
    if (!checker.check(GCONF._ctx_memory_limit, ctx_id, limit)) {
      // do nothing
    } else {
      if ('\0' == GCONF._ctx_memory_limit[0]) {
        ctx_id = ObCtxIds::MAX_CTX_ID;
        limit = INT64_MAX; // empty str means no limit, and not care ctx_id.
      }
      for (int i = 0; i < ObCtxIds::MAX_CTX_ID; i++) {
        if (ObCtxIds::WORK_AREA == i ||
            ObCtxIds::META_OBJ_CTX_ID == i) {
          continue;
        }
        auto allocator = alloc->get_ctx_allocator(i);
        if (OB_NOT_NULL(allocator)) {
          if (OB_FAIL(allocator->set_limit(ctx_id == i ? limit : INT64_MAX))) {
          }
        }
      }
    }
  }
  return ret;
}

//////////////////////////////////////////////////////////////////////////////////////////

int ObSqlMemoryTimerTask::schedule(common::ObTimer &timer)
{
  return timer.schedule(*this, SCHEDULE_PERIOD, true);
}

void ObSqlMemoryTimerTask::runTimerTask()
{
  int ret = OB_SUCCESS;
  {
    SERVER_MODULE_SCOPE {
      ObSqlMemoryManager *sql_mem_mgr = ::oceanbase::share::server_service<::oceanbase::sql::ObSqlMemoryManager>();
      if (OB_UNLIKELY(nullptr == sql_mem_mgr)) {
        LOG_WARN("sql memory manager is null");
      } else if (OB_FAIL(sql_mem_mgr->calculate_global_bound_size())) {
      }
    }
  }
}

}  // observer
}  // oceanbase
