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

#include "rootserver/freeze/ob_zone_merge_manager.h"

namespace oceanbase
{
namespace rootserver
{
class FakeZoneMergeManager : public ObZoneMergeManager
{
public:
  FakeZoneMergeManager() {}
  virtual ~FakeZoneMergeManager() {}

  void set_is_loaded(const bool is_loaded) { is_loaded_ = is_loaded; }
  int add_zone_merge_info(const share::ObZoneMergeInfo& zone_merge_info);
  int update_zone_merge_info(const share::ObZoneMergeInfo& zone_merge_info);
  int set_global_merge_info(const share::ObGlobalMergeInfo &global_merge_info);

};
} // namespace rootserver
} // namespace oceanbase
