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

#ifndef _OB_CLUSTER_EVENT_HISTORY_TABLE_OPERATOR_H
#define _OB_CLUSTER_EVENT_HISTORY_TABLE_OPERATOR_H
#include "share/ob_event_history_table_operator.h"
#include "share/ob_cluster_version.h"

namespace oceanbase
{
namespace observer
{
class ObAllClusterEventHistoryTableOperator: public share::ObEventHistoryTableOperator
{
public:
  virtual ~ObAllClusterEventHistoryTableOperator() {}

  int init(common::ObMySQLProxy &proxy);

  virtual int async_delete() override;

  static ObAllClusterEventHistoryTableOperator &get_instance();
private:
  ObAllClusterEventHistoryTableOperator() {}
  DISALLOW_COPY_AND_ASSIGN(ObAllClusterEventHistoryTableOperator);
};

} // end namespace observer
} // end namespace oceanbase

#define CLUSTER_EVENT_INSTANCE (::oceanbase::observer::ObAllClusterEventHistoryTableOperator::get_instance())

#define CLUSTER_EVENT_SYNC_ADD(args...) \
  if (OB_SUCC(ret)) { \
    uint64_t data_version = 0; \
    if (OB_FAIL(GET_MIN_DATA_VERSION(OB_SYS_TENANT_ID, data_version))) { \
      SHARE_LOG(WARN, "fail to get data version", KR(ret), "tenant_id", OB_SYS_TENANT_ID); \
    } else if (OB_FAIL(CLUSTER_EVENT_INSTANCE.sync_add_event(args))) { \
      SHARE_LOG(WARN, "fail to sync add event", KR(ret)); \
    } \
  }

#endif /* _OB_CLUSTER_EVENT_HISTORY_TABLE_OPERATOR_H */
