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

#ifndef _OB_SHARE_RESOURCE_PLAN_OB_RESOURCE_PLAN_MANAGER_H_
#define _OB_SHARE_RESOURCE_PLAN_OB_RESOURCE_PLAN_MANAGER_H_

#include "common/data_buffer.h"
#include "lib/string/ob_string.h"
#include "share/resource_manager/ob_resource_plan_info.h"

namespace oceanbase
{
namespace common
{
class ObString;
}
namespace share
{
class ObResourcePlanManager
{
public:
  ObResourcePlanManager() : background_quota_(INT32_MAX)
  {}
  virtual ~ObResourcePlanManager() = default;
  int init();
  int refresh_global_background_cpu();
  int64_t to_string(char *buf, const int64_t len) const;
private:
  int32_t background_quota_;
  /* variables */
  DISALLOW_COPY_AND_ASSIGN(ObResourcePlanManager);
};
}
}
#endif /* _OB_SHARE_RESOURCE_PLAN_OB_RESOURCE_PLAN_MANAGER_H_ */
//// end of header file

