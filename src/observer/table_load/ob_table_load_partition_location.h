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

#pragma once

#include "lib/net/ob_addr.h"
#include "share/table/ob_table_load_array.h"
#include "share/table/ob_table_load_define.h"

namespace oceanbase
{
namespace observer
{

class ObTableLoadPartitionLocation
{
public:
  struct LocalInfo
  {
    common::ObAddr addr_;
    table::ObTableLoadArray<table::ObTableLoadTabletId> partition_id_array_;
    TO_STRING_KV(K_(addr), K_(partition_id_array));
  };

  ObTableLoadPartitionLocation()
    : allocator_("TLD_PL"), is_inited_(false)
  {}

  int init(const common::ObIArray<table::ObTableLoadPartitionId> &partition_ids);
  int get_local_info(LocalInfo &info) const;
  void reset()
  {
    local_info_.addr_.reset();
    local_info_.partition_id_array_.reset();
    allocator_.reset();
    is_inited_ = false;
  }

  static int init_partition_location(
      const common::ObIArray<table::ObTableLoadPartitionId> &partition_ids,
      const common::ObIArray<table::ObTableLoadPartitionId> &target_partition_ids,
      ObTableLoadPartitionLocation &partition_location,
      ObTableLoadPartitionLocation &target_partition_location);

private:
  common::ObArenaAllocator allocator_;
  LocalInfo local_info_;
  bool is_inited_;
};

}  // namespace observer
}  // namespace oceanbase
