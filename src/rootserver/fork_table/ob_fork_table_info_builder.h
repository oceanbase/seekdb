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

#ifndef OB_FORK_TABLE_INFO_BUILDER_H
#define OB_FORK_TABLE_INFO_BUILDER_H

#include "lib/hash/ob_hashmap.h"
#include "share/ob_fork_table_util.h" // ObForkTableInfo/ObForkTabletInfo/ObForkTableUtil
#include "share/schema/ob_schema_getter_guard.h"

namespace oceanbase
{
namespace rootserver
{

// Helper to build fork-table related metadata for table/tablet creation.
// It owns a dest_table_id -> ObForkTableInfo mapping and provides per-partition fork tablet info generation.
class ObForkTableInfoBuilder
{
public:
  explicit ObForkTableInfoBuilder(const uint64_t tenant_id)
    : tenant_id_(tenant_id), inited_(false), fork_table_infos_()
  {}
  ~ObForkTableInfoBuilder();

  ObForkTableInfoBuilder(const ObForkTableInfoBuilder &) = delete;
  ObForkTableInfoBuilder &operator=(const ObForkTableInfoBuilder &) = delete;

  bool has_fork_table() const { return inited_ && fork_table_infos_.size() > 0; }

  int init_with_fork_table_info(
      const share::ObForkTableInfo &main_fork_table_info,
      const common::ObIArray<uint64_t> &dest_table_ids,
      share::schema::ObSchemaGetterGuard &schema_guard);

  int build_fork_tablet_infos(
      const common::ObIArray<const share::schema::ObTableSchema*> &schemas,
      const int64_t part_idx,
      const int64_t subpart_idx,
      share::schema::ObSchemaGetterGuard &schema_guard,
      common::ObIArray<share::ObForkTabletInfo> &fork_tablet_infos);

private:
  int generate_fork_table_infos_(
      const share::ObForkTableInfo &main_fork_table_info,
      const common::ObIArray<uint64_t> &dest_table_ids,
      share::schema::ObSchemaGetterGuard &schema_guard,
      hash::ObHashMap<uint64_t, share::ObForkTableInfo> &fork_table_infos);
  int generate_fork_tablet_info_(
      const int64_t part_idx,
      const int64_t subpart_idx,
      const share::ObForkTableInfo &fork_table_info,
      share::schema::ObSchemaGetterGuard &schema_guard,
      share::ObForkTabletInfo &fork_tablet_info);

private:
  const uint64_t tenant_id_;
  bool inited_;
  hash::ObHashMap<uint64_t, share::ObForkTableInfo> fork_table_infos_;
};

} // rootserver
} // oceanbase

#endif /* OB_FORK_TABLE_INFO_BUILDER_H */
