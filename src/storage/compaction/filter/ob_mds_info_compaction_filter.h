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
#ifndef OB_STORAGE_COMPACTION_MDS_INFO_COMPACTION_FILTER_H_
#define OB_STORAGE_COMPACTION_MDS_INFO_COMPACTION_FILTER_H_
#include "storage/compaction/ob_i_compaction_filter.h"
#include "storage/compaction/ob_mds_filter_info.h"
#include "storage/truncate_info/ob_truncate_info_array.h"
#include "storage/truncate_info/ob_truncate_partition_filter.h"
namespace oceanbase
{
namespace storage
{
struct ObMdsInfoDistinctMgr;
}
namespace compaction
{
class ObMdsInfoCompactionFilter final : public ObICompactionFilter
{
public:
  ObMdsInfoCompactionFilter()
    : ObICompactionFilter(),
      truncate_filter_(),
      is_inited_(false)
  {}
  virtual ~ObMdsInfoCompactionFilter() { reset(); }
  void reset()
  {
    is_inited_ = false;
  }
  int init(
    common::ObIAllocator &allocator,
    const ObTabletID &tablet_id,
    const int64_t schema_rowkey_cnt,
    const ObIArray<ObColDesc> &cols_desc,
    const storage::ObMdsInfoDistinctMgr &truncate_info_mgr);
  virtual int filter(
      const blocksstable::ObDatumRow &row,
      ObFilterRet &filter_ret) override;
  virtual CompactionFilterType get_filter_type() const override { return MDS_IN_MEDIUM_INFO; }
  INHERIT_TO_STRING_KV("ObMdsInfoCompactionFilter", ObICompactionFilter, K_(truncate_filter));
private:
  storage::ObTruncatePartitionFilter truncate_filter_;
  bool is_inited_;
};

} // namespace compaction
} // namespace oceanbase

#endif // OB_STORAGE_COMPACTION_MDS_INFO_COMPACTION_FILTER_H_
