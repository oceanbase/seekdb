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

#include "common/ob_tablet_id.h"
#include "lib/container/ob_se_array.h"

namespace oceanbase
{
namespace sql
{
class DASRelatedTabletMap;

// A local access target. Partition identity is needed for pruning and
// partition-wise joins; replica and server selection are not needed in seekdb.
class TabletAccessTarget
{
public:
  TabletAccessTarget()
      : partition_id_(common::OB_INVALID_INDEX), first_level_part_id_(common::OB_INVALID_INDEX) {}
  int assign(const TabletAccessTarget &other)
  {
    *this = other;
    return common::OB_SUCCESS;
  }
  void set_tablet_info(common::ObTabletID tablet_id,
                      common::ObPartID part_id,
                      common::ObPartID first_level_part_id)
  {
    tablet_id_ = tablet_id;
    partition_id_ = part_id;
    first_level_part_id_ = first_level_part_id;
  }
  bool is_valid() const
  {
    return common::OB_INVALID_INDEX != partition_id_ && tablet_id_.is_valid();
  }
  int64_t get_partition_id() const { return partition_id_; }
  int64_t get_first_level_part_id() const { return first_level_part_id_; }
  common::ObTabletID get_tablet_id() const { return tablet_id_; }

  TO_STRING_KV(K_(partition_id), K_(first_level_part_id), K_(tablet_id));

private:
  int64_t partition_id_;
  // Only valid for subpartitioned tables.
  int64_t first_level_part_id_;
  common::ObTabletID tablet_id_;
};

using TabletAccessTargetIArray = common::ObIArray<TabletAccessTarget>;
using TabletAccessTargetArray = common::ObSEArray<TabletAccessTarget, 2, common::ModulePageAllocator, true>;

// Targets for one table reference, including aliases and local index accesses.
// This is an optimizer/plan-matching value, not mutable DAS execution state.
class TableAccessTargets
{
public:
  TableAccessTargets();
  int assign(const TableAccessTargets &other);
  void set_table_identity(uint64_t table_ref_id, uint64_t ref_table_id);
  int replace_local_index_targets(DASRelatedTabletMap &map, common::ObTableID ref_table_id);
  uint64_t get_table_ref_id() const { return table_ref_id_; }
  uint64_t get_ref_table_id() const { return ref_table_id_; }
  const TabletAccessTargetIArray &get_tablets() const { return tablet_targets_; }
  TabletAccessTargetIArray &get_tablets_for_update() { return tablet_targets_; }
  int64_t get_tablet_count() const { return tablet_targets_.count(); }

  TO_STRING_KV(K_(table_ref_id), K_(ref_table_id), K_(tablet_targets));

private:
  // The statement's table reference ID, possibly a generated alias ID.
  uint64_t table_ref_id_;
  uint64_t ref_table_id_;
  TabletAccessTargetArray tablet_targets_;
  DISALLOW_COPY_AND_ASSIGN(TableAccessTargets);
};
} // namespace sql
} // namespace oceanbase
