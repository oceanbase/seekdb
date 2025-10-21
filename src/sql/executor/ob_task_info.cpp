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

#define USING_LOG_PREFIX SQL_EXE

#include "ob_task_info.h"
#include "sql/executor/ob_task_spliter.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase
{
namespace sql
{
ObTaskInfo::ObTaskInfo(common::ObIAllocator &allocator)
  : range_location_(allocator),
    task_split_type_(ObTaskSpliter::INVALID_SPLIT),
    task_location_(),
    pull_slice_id_(OB_INVALID_ID),
    force_save_interm_result_(false),
    slice_events_(ObModIds::OB_SQL_EXECUTOR_TASK_INFO, OB_MALLOC_NORMAL_BLOCK_SIZE),
    location_idx_(OB_INVALID_ID),
    location_idx_list_(allocator),
    root_op_(NULL),
    state_(OB_TASK_STATE_NOT_INIT),
    slice_count_pos_(allocator),
    background_(false),
    retry_times_(0),
    ts_task_send_begin_(INT64_MIN),
    ts_task_recv_done_(INT64_MIN),
    ts_result_send_begin_(INT64_MIN),
    ts_result_recv_done_(INT64_MIN),
    root_spec_(NULL)
{
}

ObTaskInfo::~ObTaskInfo()
{
  slice_events_.reset();
}


int ObTaskInfo::ObRangeLocation::assign(const ObTaskInfo::ObRangeLocation &range_loc)
{
  int ret = common::OB_SUCCESS;
  if (OB_FAIL(part_locs_.assign(range_loc.part_locs_))) {
    SQL_EXE_LOG(WARN, "copy part locs failed", K(ret), K(range_loc));
  } else {
    server_ = range_loc.server_;
  }
  return ret;
}

int ObGranuleTaskInfo::assign(const ObGranuleTaskInfo &other)
{
  int ret = OB_SUCCESS;
  if (this != &other) {
    if (OB_FAIL(ranges_.assign(other.ranges_))) {
      LOG_WARN("assign ranges_ failed", K(ret));
    } else if (OB_FAIL(ss_ranges_.assign(other.ss_ranges_))) {
      LOG_WARN("assign ss_ranges_ failed", K(ret));
    } else {
      tablet_loc_ = other.tablet_loc_;
      task_id_ = other.task_id_;
    }
  }
  return ret;
}


}
}
