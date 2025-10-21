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

#include "ob_server_event_history_table_operator.h"
namespace oceanbase
{
namespace observer
{
using namespace common;
using namespace share;

int ObAllServerEventHistoryTableOperator::init(common::ObMySQLProxy &proxy, const common::ObAddr &self_addr)
{
  int ret = OB_SUCCESS;
  const bool is_rs_event = false;
  const bool is_server_event = true;
  set_addr(self_addr, is_rs_event, is_server_event);
  if (OB_FAIL(ObEventHistoryTableOperator::init(proxy))) {
  } else {
    set_event_table(share::OB_ALL_SERVER_EVENT_HISTORY_TNAME);
  }
  return ret;
}

ObAllServerEventHistoryTableOperator &ObAllServerEventHistoryTableOperator::get_instance()
{
  static ObAllServerEventHistoryTableOperator instance;
  return instance;
}
int ObAllServerEventHistoryTableOperator::async_delete()
{
  int ret = OB_SUCCESS;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
    SHARE_LOG(WARN, "not init", KR(ret));
  } else if (OB_FAIL(default_async_delete())) {
    SHARE_LOG(WARN, "failed to default async delete", KR(ret));
  }
  return ret;
}
} // end namespace observer
} // end namespace oceanbase
