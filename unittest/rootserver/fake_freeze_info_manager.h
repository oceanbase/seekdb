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

#include "rootserver/freeze/ob_major_merge_info_manager.h"

namespace oceanbase
{
namespace rootserver
{
class FakeFreezeInfoManager : public ObMajorMergeInfoManager
{
public:
  FakeFreezeInfoManager();
  virtual ~FakeFreezeInfoManager() {}

  void set_leader_cluster(bool is_primary_cluster) { is_primary_cluster_ = is_primary_cluster; }
  bool is_primary_cluster() const;

private:
  bool is_primary_cluster_;
};
} // namespace rootserver
} // namespace oceanbase
