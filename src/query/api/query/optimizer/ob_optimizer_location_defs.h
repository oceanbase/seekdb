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

#ifndef OCEANBASE_QUERY_OPTIMIZER_LOCATION_DEFS_H_
#define OCEANBASE_QUERY_OPTIMIZER_LOCATION_DEFS_H_

#include "common/row/ob_row.h"
#include "lib/container/ob_se_array.h"
#include "lib/hash/ob_hashmap.h"
#include "lib/hash_func/murmur_hash.h"
#include "lib/utility/ob_unify_serialize.h"

namespace oceanbase
{
namespace sql
{

// Runtime location data shared by SQL planning, plan cache, and execution.
// These value types intentionally live outside the Optimizer implementation
// target so consumers do not need an Optimizer header just to hold its output.
class ObPartIdRowMapManager
{
public:
  ObPartIdRowMapManager()
    : manager_(), part_idx_(common::OB_INVALID_INDEX) {}
  typedef common::ObSEArray<int64_t, 12> ObRowIdList;
  struct MapEntry
  {
  public:
    MapEntry(): list_() { }
    TO_STRING_KV(K_(list));
    int assign(const MapEntry &entry);
  public:
    ObRowIdList list_;
  };
  typedef common::ObSEArray<MapEntry, 1> ObPartRowManager;
  const ObRowIdList* get_row_id_list(int64_t part_index);
  void reset() { manager_.reset(); part_idx_ = common::OB_INVALID_INDEX; }
  int64_t get_part_count() const { return manager_.count(); }
  int64_t get_part_idx() const { return part_idx_; }
  void set_part_idx(int64_t part_idx) { part_idx_ = part_idx; }
  const MapEntry &at(int64_t i) const { return manager_.at(i); }
  common::ObNewRow &get_part_row() { return part_row_; }
  TO_STRING_KV(K_(manager), K_(part_idx));
private:
  ObPartRowManager manager_;
  int64_t part_idx_; // Used for parameter passing only.
  common::ObNewRow part_row_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObPartIdRowMapManager);
};

struct TableLocationKey
{
  uint64_t table_id_;
  uint64_t ref_table_id_;

  bool operator==(const TableLocationKey &other) const
  {
    return table_id_ == other.table_id_ && ref_table_id_ == other.ref_table_id_;
  }

  uint64_t hash() const
  {
    uint64_t hash_ret = 0;
    hash_ret = common::murmurhash(&table_id_, sizeof(uint64_t), hash_ret);
    hash_ret = common::murmurhash(&ref_table_id_, sizeof(uint64_t), hash_ret);
    return hash_ret;
  }

  TO_STRING_KV(K_(table_id), K_(ref_table_id));
};

typedef common::ObSEArray<uint64_t, 8> TabletIdArray;

struct GroupPWJTabletIdInfo
{
  OB_UNIS_VERSION(1);
public:
  TO_STRING_KV(K_(group_id), K_(tablet_id_array));
  int64_t group_id_{0};
  TabletIdArray tablet_id_array_;
};

typedef common::hash::ObHashMap<uint64_t,
                                TabletIdArray,
                                common::hash::NoPthreadDefendMode> PWJTabletIdMap;
typedef common::hash::ObHashMap<uint64_t,
                                GroupPWJTabletIdInfo,
                                common::hash::NoPthreadDefendMode> GroupPWJTabletIdMap;

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_QUERY_OPTIMIZER_LOCATION_DEFS_H_
