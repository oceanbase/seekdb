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

#define USING_LOG_PREFIX RS

#include "rootserver/ob_upgrade_executor.h"
#include "share/ob_global_stat_proxy.h"
#include "share/ob_cluster_event_history_table_operator.h"//CLUSTER_EVENT_INSTANCE
#include "observer/ob_service.h"

namespace oceanbase
{
using namespace common;
using namespace common::sqlclient;
using namespace share;
using namespace share::schema;

namespace rootserver
{

int ObUpgradeProcessorExecutor::init(const uint64_t &tenant_id, common::ObMySQLProxy *sql_proxy)
{
  int ret = OB_SUCCESS;
  tenant_id_ = tenant_id;
  sql_proxy_ = sql_proxy;
  return ret;
}

int ObUpgradeProcessorExecutor::check_inner_stat_()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(sql_proxy_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("null pointer", KR(ret), KP_(sql_proxy));
  } else if (!is_valid_tenant_id(tenant_id_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid tenant id", KR(ret), K_(tenant_id));
  }
  return ret;
}

}//end rootserver
}//end oceanbase
