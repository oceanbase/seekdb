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
#ifndef OB_STORAGE_TRUNCATE_INFO_TRUNCATE_INFO_DISTINCT_MGR_H_
#define OB_STORAGE_TRUNCATE_INFO_TRUNCATE_INFO_DISTINCT_MGR_H_
#include "storage/meta_mem/ob_tablet_handle.h"
#include "storage/truncate_info/ob_truncate_info_array.h"
namespace oceanbase
{
namespace common
{
class ObIAllocator;
struct ObVersionRange;
}
namespace compaction
{
struct ObMdsFilterInfo;
}
namespace storage
{
class ObTablet;
struct ObMdsInfoDistinctMgr final
{
  ObMdsInfoDistinctMgr();
  ~ObMdsInfoDistinctMgr() { reset(); }
  int init(
    common::ObArenaAllocator &allocator,
    storage::ObTablet &tablet,
    const common::ObIArray<ObTabletHandle> *split_extra_tablet_handles,
    const common::ObVersionRange &read_version_range,
    const bool for_access);
  bool is_valid() const { return is_inited_; }
  bool empty() const { return distinct_array_.empty(); }
  void clear() {
    array_.reset();
    distinct_array_.reuse();
  }
  void reset()
  {
    is_inited_ = false;
    array_.reset();
    distinct_array_.reset();
  }
  int fill_mds_filter_info(
    common::ObIAllocator &allocator,
    compaction::ObMdsFilterInfo &mds_filter_info) const;
  int check_mds_filter_info(
    const compaction::ObMdsFilterInfo &mds_filter_info);
  int get_distinct_truncate_info_array(
    ObTruncateInfoArray &distinct_array) const;
  const ObIArray<ObTruncateInfo *> &get_distinct_truncate_info_array() const { return distinct_array_; }
  bool operator ==(const ObMdsInfoDistinctMgr &other) const = delete;
  TO_STRING_KV(K_(array), "distinct_array_cnt", distinct_array_.count(), K_(distinct_array));
private:
  int read_split_truncate_info_array(
    const common::ObIArray<ObTabletHandle> *split_extra_tablet_handles,
    const common::ObVersionRange &read_version_range,
    const bool for_access);
  int build_distinct_array(const ObVersionRange &read_version_range, const bool for_access);
private:
  ObTruncateInfoArray array_;
  ObSEArray<ObTruncateInfo *, 4> distinct_array_;
  bool is_inited_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObMdsInfoDistinctMgr);
};

} // namespace storage
} // namespace oceanbase

#endif // OB_STORAGE_TRUNCATE_INFO_TRUNCATE_INFO_DISTINCT_MGR_H_
