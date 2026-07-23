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

#include "fake_zone_merge_manager.h"

namespace oceanbase
{
namespace rootserver
{
using namespace oceanbase::share;
using namespace oceanbase::common;

int FakeZoneMergeManager::add_zone_merge_info(const ObZoneMergeInfo& zone_merge_info)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(zone_merge_info_.assign(zone_merge_info))) {
    LOG_WARN("fail to assign zone merge info", K(ret), K(zone_merge_info));
  }
  return ret;
}

int FakeZoneMergeManager::update_zone_merge_info(const ObZoneMergeInfo& zone_merge_info)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(zone_merge_info_.assign(zone_merge_info))) {
    LOG_WARN("fail to assign zone merge info", K(ret), K(zone_merge_info));
  }
  return ret;
}

int FakeZoneMergeManager::set_global_merge_info(const ObGlobalMergeInfo &global_merge_info)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(global_merge_info_.assign(global_merge_info))) {
    LOG_WARN("fail to assign global merge info", K(ret), K(global_merge_info));
  }
  return ret;
}

} // namespace rootserver
} // namespace oceanbase
