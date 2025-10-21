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

#ifndef _OB_SERVER_EVENT_HISTORY_TABLE_OPERATOR_H
#define _OB_SERVER_EVENT_HISTORY_TABLE_OPERATOR_H 1
#include "share/ob_event_history_table_operator.h"

namespace oceanbase
{
namespace observer
{
class ObAllServerEventHistoryTableOperator: public share::ObEventHistoryTableOperator
{
public:
  virtual ~ObAllServerEventHistoryTableOperator() {}

  int init(common::ObMySQLProxy &proxy, const common::ObAddr &self_addr);

  virtual int async_delete() override;

  static ObAllServerEventHistoryTableOperator &get_instance();
private:
  ObAllServerEventHistoryTableOperator() {}
  DISALLOW_COPY_AND_ASSIGN(ObAllServerEventHistoryTableOperator);
};

} // end namespace observer
} // end namespace oceanbase

#define SERVER_EVENT_INSTANCE (::oceanbase::observer::ObAllServerEventHistoryTableOperator::get_instance())
#define SERVER_EVENT_ADD(args...)                                         \
  SERVER_EVENT_INSTANCE.add_event<false>(args)
#define SERVER_EVENT_SYNC_ADD(args...)                                         \
  SERVER_EVENT_INSTANCE.sync_add_event(args)
#define SERVER_EVENT_ADD_WITH_RETRY(args...)                                         \
  SERVER_EVENT_INSTANCE.add_event_with_retry(args)

#endif /* _OB_SERVER_EVENT_HISTORY_TABLE_OPERATOR_H */
