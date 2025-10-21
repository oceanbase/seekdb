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

#define USING_LOG_PREFIX SQL_MONITOR
#include "sql/monitor/ob_phy_plan_monitor_info.h"
using namespace oceanbase::common;
namespace oceanbase
{
namespace sql
{
OB_SERIALIZE_MEMBER(ObPhyPlanMonitorInfo, request_id_, plan_id_, plan_info_, operator_infos_, execution_time_);

ObPhyPlanMonitorInfo::ObPhyPlanMonitorInfo(common::ObConcurrentFIFOAllocator &allocator)
    :allocator_(allocator),
     request_id_(OB_INVALID_ID),
     scheduler_addr_(),
     plan_id_(OB_INVALID_ID),
     execution_time_(0),
     operator_infos_(OB_MALLOC_NORMAL_BLOCK_SIZE),
     exec_trace_(false, ObLatchIds::TRACE_RECORDER_LOCK)
  {
  }

int ObPhyPlanMonitorInfo::add_operator_info(const ObPhyOperatorMonitorInfo &info)
{
  return operator_infos_.push_back(info);
}


int ObPhyPlanMonitorInfo::get_operator_info_by_index(int64_t index,
                                                     ObPhyOperatorMonitorInfo *&info)
{
  int ret = OB_SUCCESS;
  if (0 > index) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid operator index", K(index));
  } else if (index > operator_infos_.count()) {
    ret = OB_ITER_END;
    LOG_WARN("fail to get operator info", K(ret), K(index));
  } else if (index < operator_infos_.count()) {
    info = &(operator_infos_.at(index));
  }
  return ret;
}

} //namespace sql
} //namespace oceanbase
