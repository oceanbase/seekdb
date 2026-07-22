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
#include "share/rc/ob_module_provider.h"
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
  {
    if (OB_FAIL(update_wa_percentage())) {
      LOG_WARN("update work area memory failed", K(ret));
      // Ignore this error code since successive operations
      // shouldn't relay on it.
      ret = OB_SUCCESS;
    }
    if (OB_FAIL(update_ctx_memory_throttle())) {
      LOG_WARN("update context memory throttle failed", K(ret));
      ret = OB_SUCCESS;
    }
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
            LOG_ERROR("set_limit failed", K(ret), K(ctx_id), K(limit));
          }
        }
      }
    }
  }
  return ret;
}

int ObServerDutyTask::read_wa_percentage(int64_t &pctg)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  const ObSysVarSchema *var_schema = NULL;
  ObObj value;
  if (OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema service is null");
  } else if (OB_FAIL(GCTX.schema_service_->get_runtime_schema_guard(schema_guard))) {
    LOG_WARN("get schema guard failed", K(ret));
  } else if (OB_FAIL(schema_guard.get_system_variable(SYS_VAR_OB_SQL_WORK_AREA_PERCENTAGE, var_schema))) {
    LOG_WARN("get runtime system variable failed", K(ret));
  } else if (OB_ISNULL(var_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("var_schema is null");
  } else if (OB_FAIL(var_schema->get_value(&allocator_, NULL, value))) {
    LOG_WARN("get value from var_schema failed", K(ret), K(*var_schema));
  } else if (OB_FAIL(value.get_int(pctg))) {
    LOG_WARN("get int from value failed", K(ret), K(value));
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
      ObSqlMemoryManager *sql_mem_mgr = share::g_mp->sql_memory_manager();
      if (OB_UNLIKELY(nullptr == sql_mem_mgr)) {
        LOG_WARN("sql memory manager is null");
      } else if (OB_FAIL(sql_mem_mgr->calculate_global_bound_size())) {
        LOG_WARN("failed to calculate global bound size", K(ret));
      }
    }
  }
}

int ObServerDutyTask::update_wa_percentage()
{
  int ret = OB_SUCCESS;
  int64_t wa_pctg = 0;
  if (OB_FAIL(read_wa_percentage(wa_pctg))) {
    LOG_WARN("read work area percentage failed", K(ret));
  } else if (wa_pctg < 0 || wa_pctg > 100) {
    LOG_WARN("work area memroy percentage "
             "shouldn't greater than 100 or be negative",
             K(wa_pctg));
  } else {
    auto allocator = lib::ObMallocAllocator::get_instance()->get_ctx_allocator(
        common::ObCtxIds::WORK_AREA);
    if (allocator != nullptr) {
      if (OB_FAIL(lib::set_wa_limit(wa_pctg))) {
        LOG_WARN("set work area memory failed",
                 K(wa_pctg), K(ret));
      } else {
        LOG_INFO("set work area memory",
                 K(wa_pctg),
                 "limit", allocator->get_limit(),
                 K(ret));
       }
     }
  }
  return ret;
}

}  // observer
}  // oceanbase
