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

#define USING_LOG_PREFIX SQL_ENG

#include "sql/engine/px/ob_granule_task_info.h"

namespace oceanbase
{
namespace sql
{

int ObGranuleTaskInfo::assign(const ObGranuleTaskInfo &other)
{
  int ret = OB_SUCCESS;
  if (this != &other) {
    if (OB_FAIL(ranges_.assign(other.ranges_))) {
    } else {
      tablet_loc_ = other.tablet_loc_;
      task_id_ = other.task_id_;
      granule_type_ = other.granule_type_;
    }
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
