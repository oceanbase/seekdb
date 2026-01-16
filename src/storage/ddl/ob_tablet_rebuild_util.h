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

#ifndef OCEANBASE_STORAGE_DDL_OB_TABLET_REBUILD_UTIL_H_
#define OCEANBASE_STORAGE_DDL_OB_TABLET_REBUILD_UTIL_H_

#include "lib/hash/ob_hashmap.h"
#include "lib/container/ob_array.h"
#include "common/ob_tablet_id.h"
#include "storage/ob_i_table.h"

namespace oceanbase
{
namespace common
{
class ObIAllocator;
}
namespace share
{
class SCN;
class ObLSID;
}
namespace blocksstable
{
class ObSSTableBasicMeta;
class ObSSTable;
}
namespace storage
{
class ObLSHandle;
class ObStorageSchema;
class ObTabletCreateSSTableParam;

struct ObDdlSSTableTaskKey final
{
public:
  ObDdlSSTableTaskKey() : src_sst_key_(), dest_tablet_id_() {}
  ObDdlSSTableTaskKey(const ObITable::TableKey &src_key, const common::ObTabletID &dst_tablet_id)
    : src_sst_key_(src_key), dest_tablet_id_(dst_tablet_id)
  {}
  ~ObDdlSSTableTaskKey() = default;
  uint64_t hash() const { return src_sst_key_.hash() + dest_tablet_id_.hash(); }
  int hash(uint64_t &hash_val) const { hash_val = hash(); return OB_SUCCESS; }
  bool operator==(const ObDdlSSTableTaskKey &other) const
  {
    return src_sst_key_ == other.src_sst_key_ && dest_tablet_id_ == other.dest_tablet_id_;
  }
  bool is_valid() const { return src_sst_key_.is_valid() && dest_tablet_id_.is_valid(); }
  TO_STRING_KV(K_(src_sst_key), K_(dest_tablet_id));

public:
  ObITable::TableKey src_sst_key_;
  common::ObTabletID dest_tablet_id_;
};

class ObTabletRebuildUtil final
{
public:
  // Destroy hashmap whose values are pointers allocated from the given allocator.
  template <typename KEY, typename VALUE>
  static int destroy_value_ptr_map(common::ObIAllocator &alloc,
                                   common::hash::ObHashMap<KEY, VALUE*> &map)
  {
    int ret = OB_SUCCESS;
    struct GetMapItemKeyFn final
    {
      GetMapItemKeyFn() : map_keys_(), ret_code_(OB_SUCCESS) {}
      int operator()(common::hash::HashMapPair<KEY, VALUE*> &entry)
      {
        if (OB_LIKELY(OB_SUCCESS == ret_code_)) {
          ret_code_ = map_keys_.push_back(entry.first);
        }
        return ret_code_;
      }
      common::ObArray<KEY> map_keys_;
      int ret_code_;
    } get_map_item_key_fn;

    if (map.created() && OB_FAIL(map.foreach_refactored(get_map_item_key_fn))) {
      // keep ret for caller; best-effort cleanup continues.
    }
    for (int64_t i = 0; i < get_map_item_key_fn.map_keys_.count(); i++) {
      VALUE *value = nullptr;
      const KEY &key = get_map_item_key_fn.map_keys_.at(i);
      int tmp_ret = map.erase_refactored(key, &value);
      if (OB_SUCCESS != tmp_ret && OB_SUCCESS == ret) {
        ret = tmp_ret;
      }
      if (OB_NOT_NULL(value)) {
        value->~VALUE();
        alloc.free(value);
        value = nullptr;
      }
    }
    map.destroy();
    return ret;
  }

  static int get_clipped_storage_schema_on_demand(
      common::ObIAllocator &allocator,
      common::hash::ObHashMap<ObITable::TableKey, ObStorageSchema*> &clipped_schemas_map,
      const common::ObTabletID &tablet_id,
      const blocksstable::ObSSTable &sstable,
      const ObStorageSchema &latest_schema,
      const bool try_create,
      const ObStorageSchema *&storage_schema);

  static int check_need_fill_empty_sstable(
      ObLSHandle &ls_handle,
      const bool is_minor_sstable,
      const ObITable::TableKey &table_key,
      const common::ObTabletID &dst_tablet_id,
      bool &need_fill_empty_sstable,
      share::SCN &end_scn);

  static int build_create_empty_sstable_param(
      const blocksstable::ObSSTableBasicMeta &meta,
      const ObITable::TableKey &table_key,
      const common::ObTabletID &dst_tablet_id,
      const share::SCN &end_scn,
      ObTabletCreateSSTableParam &create_sstable_param);
};

} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_DDL_OB_TABLET_REBUILD_UTIL_H_
