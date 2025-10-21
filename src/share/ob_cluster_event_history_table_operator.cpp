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

#include "ob_cluster_event_history_table_operator.h"
namespace oceanbase
{
namespace observer
{
using namespace common;
using namespace share;

int ObAllClusterEventHistoryTableOperator::init(common::ObMySQLProxy &proxy)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObEventHistoryTableOperator::init(proxy))) {
    SHARE_LOG(WARN, "fail to init event history table operator", KR(ret));
  } else {
    set_event_table(share::OB_ALL_CLUSTER_EVENT_HISTORY_TNAME);
  }
  return ret;
}

ObAllClusterEventHistoryTableOperator &ObAllClusterEventHistoryTableOperator::get_instance()
{
  static ObAllClusterEventHistoryTableOperator instance;
  return instance;
}

int ObAllClusterEventHistoryTableOperator::async_delete()
{
  return OB_NOT_SUPPORTED;
}
} // end namespace observer
} // end namespace oceanbase
