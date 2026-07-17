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

#define USING_LOG_PREFIX SERVER

#include "observer/table_load/ob_table_load_partition_location.h"
#include "observer/ob_server.h"
#include "observer/table_load/ob_table_load_utils.h"

namespace oceanbase
{
namespace observer
{
using namespace common;
using namespace table;

int ObTableLoadPartitionLocation::init_partition_location(
    const ObIArray<ObTableLoadPartitionId> &partition_ids,
    const ObIArray<ObTableLoadPartitionId> &target_partition_ids,
    ObTableLoadPartitionLocation &partition_location,
    ObTableLoadPartitionLocation &target_partition_location)
{
  int ret = OB_SUCCESS;
  partition_location.reset();
  target_partition_location.reset();
  if (OB_UNLIKELY(partition_ids.count() != target_partition_ids.count())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("source and target partition count differ", K(ret), K(partition_ids), K(target_partition_ids));
  } else if (OB_FAIL(partition_location.init(partition_ids))) {
    LOG_WARN("init source partitions failed", K(ret));
  } else if (OB_FAIL(target_partition_location.init(target_partition_ids))) {
    LOG_WARN("init target partitions failed", K(ret));
  }
  return ret;
}

int ObTableLoadPartitionLocation::init(const ObIArray<ObTableLoadPartitionId> &partition_ids)
{
  int ret = OB_SUCCESS;
  ObArray<ObTableLoadTabletId> tablet_ids;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("table load local partitions init twice", K(ret));
  } else if (OB_UNLIKELY(partition_ids.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("empty table load partitions", K(ret));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < partition_ids.count(); ++i) {
    if (OB_FAIL(tablet_ids.push_back(ObTableLoadTabletId(partition_ids.at(i))))) {
      LOG_WARN("add local tablet failed", K(ret), K(i));
    }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(ObTableLoadUtils::deep_copy(tablet_ids,
                                                 local_info_.partition_id_array_,
                                                 allocator_))) {
    LOG_WARN("copy local tablets failed", K(ret));
  } else {
    local_info_.addr_ = ObServer::get_instance().get_self();
    is_inited_ = true;
  }
  return ret;
}

int ObTableLoadPartitionLocation::get_local_info(LocalInfo &info) const
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("table load local partitions not initialized", K(ret));
  } else {
    info = local_info_;
  }
  return ret;
}

}  // namespace observer
}  // namespace oceanbase
