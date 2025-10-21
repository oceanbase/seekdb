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

#ifndef OCEANBASE_SHARE_DEADLOCK_OB_DEADLOCK_TRANS_SERVICE_
#define OCEANBASE_SHARE_DEADLOCK_OB_DEADLOCK_TRANS_SERVICE_
#include "lib/mysqlclient/ob_mysql_proxy.h"
#include "ob_deadlock_detector_common_define.h"
#include "lib/container/ob_iarray.h"
#include "share/ob_event_history_table_operator.h"

namespace oceanbase
{
namespace share
{
namespace detector
{

class  ObDeadLockInnerTableService
{
public:
  static int insert(const ObDetectorInnerReportInfo &inner_info,
                    int64_t sequence,
                    int64_t size,
                    int64_t current_ts);
  static int insert_all(const common::ObIArray<ObDetectorInnerReportInfo> &infos);

  class ObDeadLockEventHistoryTableOperator : public share::ObEventHistoryTableOperator
  {
  public:
    virtual ~ObDeadLockEventHistoryTableOperator() {}
    virtual int async_delete() override;
    static ObDeadLockEventHistoryTableOperator &get_instance();
  private:
    ObDeadLockEventHistoryTableOperator() {};
    DISALLOW_COPY_AND_ASSIGN(ObDeadLockEventHistoryTableOperator);
  };
private:
  friend class ObDeadLockEventHistoryTableOperator;
};

#define DEALOCK_EVENT_INSTANCE (::oceanbase::share::detector::\
        ObDeadLockInnerTableService::\
        ObDeadLockEventHistoryTableOperator::get_instance())

}// namespace detector
}// namespace share
}// namespace oceanbase
#endif
