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

#include "ob_tenant_event_history_table_operator.h"

namespace oceanbase
{
namespace rootserver
{
using namespace common;
using namespace share;
int ObTenantEventHistoryTableOperator::init(common::ObMySQLProxy &proxy,
                                        const common::ObAddr &self_addr)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObEventHistoryTableOperator::init(proxy))) {
    LOG_WARN("fail to init event history table operator", KR(ret));
  } else {
    const bool is_rs_event = false;
    const bool is_server_event = false;
    set_addr(self_addr, is_rs_event, is_server_event);
    set_event_table(share::OB_ALL_TENANT_EVENT_HISTORY_TNAME);
  }
  return ret;
}
ObTenantEventHistoryTableOperator &ObTenantEventHistoryTableOperator::get_instance()
{
  static ObTenantEventHistoryTableOperator instance;
  return instance;
}
int ObTenantEventHistoryTableOperator::async_delete()
{
  return OB_NOT_SUPPORTED;
}
}//end namespace rootserver
}//end namespace oceanbase
