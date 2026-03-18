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

#define USING_LOG_PREFIX SHARE_SCHEMA
#include "ob_standby_schema_refresh_trigger.h"
#include "share/inner_table/ob_inner_table_schema_constants.h"  // OB_ALL_DDL_OPERATION_TNAME
#include "lib/oblog/ob_log.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/mysqlclient/ob_mysql_result.h"  // ObMySQLResult
#include "share/schema/ob_schema_getter_guard.h"  // ObSchemaGetterGuard
#include "share/ob_all_tenant_info.h"  // ObAllTenantInfo, ObAllTenantInfoProxy
#include "observer/ob_service.h"  // ObService

namespace oceanbase
{
namespace share
{
namespace schema
{

int ObStandbySchemaRefreshTrigger::init()
{
  int ret = OB_SUCCESS;

  if (is_inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", KR(ret));
  } else if (OB_FAIL(ObTenantThreadHelper::create("StandbySchem",
        lib::TGDefIDs::SimpleLSService, *this))) {
    LOG_WARN("failed to create STANDBY_SCHEMA_REFRESH_TRIGGER", KR(ret));
  } else if (OB_FAIL(ObTenantThreadHelper::start())) {
    LOG_WARN("failed to start STANDBY_SCHEMA_REFRESH_TRIGGER", KR(ret));
  } else {
    is_inited_ = true;
    LOG_INFO("ObStandbySchemaRefreshTrigger init success");
  }

  return ret;
}

void ObStandbySchemaRefreshTrigger::destroy()
{
  LOG_INFO("ObStandbySchemaRefreshTrigger destroy");
  ObTenantThreadHelper::destroy();
  is_inited_ = false;
}

void ObStandbySchemaRefreshTrigger::do_work()
{
  LOG_INFO("ObStandbySchemaRefreshTrigger thread start");
  int ret = OB_SUCCESS;

  if (OB_FAIL(check_inner_stat_())) {
    LOG_WARN("inner stat error", KR(ret), K_(is_inited));
  } else {
    while (!has_set_stop()) {
      ObCurTraceId::init(GCONF.self_addr_);
      if (OB_FAIL(submit_tenant_refresh_schema_task_())) {
        LOG_WARN("submit_tenant_refresh_schema_task_ failed", KR(ret));
      }

      idle(DEFAULT_IDLE_TIME);
    }
  }

  LOG_INFO("ObStandbySchemaRefreshTrigger thread end");
}

int ObStandbySchemaRefreshTrigger::check_inner_stat_()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  }
  return ret;
}

int ObStandbySchemaRefreshTrigger::submit_tenant_refresh_schema_task_()
{
  int ret = OB_SUCCESS;
  ObAllTenantInfo tenant_info;

  // Check if tenant is standby and in normal status
  if (OB_FAIL(ObAllTenantInfoProxy::load_tenant_info(false, tenant_info))) {
    LOG_WARN("fail to load tenant info", KR(ret));
  } else if (!tenant_info.is_standby() || !tenant_info.is_normal_status()) {
    // Not standby or not normal status, skip schema refresh
    // Thread keeps running but doesn't do actual work
    if (REACH_THREAD_TIME_INTERVAL(5 * 1000 * 1000)) {
      LOG_DEBUG("tenant is not standby or not normal status, skip schema refresh",
                K(tenant_info.get_tenant_role()), K(tenant_info.get_switchover_status()));
    }
  } else {
    // Tenant is standby and in normal status, proceed with schema refresh
    int64_t schema_version = OB_INVALID_VERSION;
    ObRefreshSchemaStatus schema_status;
    ObSchemaStatusProxy *schema_status_proxy = GCTX.schema_status_proxy_;
    if (OB_ISNULL(schema_status_proxy)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("schema_status_proxy is null", KR(ret));
    } else if (OB_FAIL(schema_status_proxy->get_refresh_schema_status(OB_SYS_TENANT_ID, schema_status))) {
      LOG_WARN("failed to get tenant refresh schema status", KR(ret));
    } else if (OB_FAIL(GCTX.schema_service_->get_schema_version_in_inner_table(*GCTX.sql_proxy_,
          schema_status, schema_version))) {
      LOG_WARN("fail to get latest schema version in inner table", K(ret));
    } else if (OB_FAIL(GCTX.ob_service_->submit_async_refresh_schema_task(
                         OB_SYS_TENANT_ID, schema_version))) {
      LOG_WARN("fail to submit async refresh schema task",
               KR(ret), K(schema_version));
    }
  }

  return ret;
}

} // namespace schema
} // namespace share
} // namespace oceanbase
