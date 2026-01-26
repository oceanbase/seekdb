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

#define USING_LOG_PREFIX LIB
#include "sql/executor/ob_memory_tracker.h"
#include "lib/rc/context.h"
#include "observer/omt/ob_tenant_config_mgr.h"
#include "share/rc/ob_tenant_base.h"

using namespace oceanbase::lib;

thread_local ObMemTracker ObMemTrackerGuard::mem_tracker_;



void ObMemTrackerGuard::update_mem_limit()
{
  int ret = common::OB_SUCCESS;
  int64_t hard_memory_limit = lib::get_hard_memory_limit();
  int64_t mem_quota_pct = 100;
  omt::ObTenantConfigGuard tenant_config(TENANT_CONF(MTL_ID()));
  if (OB_UNLIKELY(tenant_config.is_valid())) {
    mem_quota_pct = tenant_config->query_memory_limit_percentage;
  }
  mem_tracker_.cache_mem_limit_ = hard_memory_limit / 100 * mem_quota_pct;
}
int ObMemTrackerGuard::check_status()
{
  int ret = common::OB_SUCCESS;
  if (nullptr != mem_tracker_.mem_context_) {
    int64_t tree_mem_hold = mem_tracker_.mem_context_->tree_mem_hold();
    ++mem_tracker_.check_status_times_;
    if (0 == mem_tracker_.cache_mem_limit_
      || (mem_tracker_.check_status_times_ % UPDATE_MEM_LIMIT_THRESHOLD == 0)) {
      update_mem_limit();
    }
    if (tree_mem_hold >= mem_tracker_.cache_mem_limit_) {
      ret = OB_EXCEED_QUERY_MEM_LIMIT;
      SQL_LOG(WARN, "Exceeded memory usage limit", K(ret), K(tree_mem_hold),
              K(mem_tracker_.cache_mem_limit_));
      LOG_USER_ERROR(OB_EXCEED_QUERY_MEM_LIMIT, mem_tracker_.cache_mem_limit_, tree_mem_hold);
    }
  }
  return ret;
}
int ObMemTrackerGuard::try_check_status(int64_t check_try_times)
{
  int ret = common::OB_SUCCESS;
  if (nullptr != mem_tracker_.mem_context_
      && ((++mem_tracker_.try_check_tick_) % check_try_times == 0)) {
    ret = check_status();
  }
  return ret;
}

