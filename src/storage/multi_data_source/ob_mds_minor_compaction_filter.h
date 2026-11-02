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

#ifndef OCEANBASE_STORAGE_OB_MDS_MINOR_COMPACTION_FILTER
#define OCEANBASE_STORAGE_OB_MDS_MINOR_COMPACTION_FILTER

#include "storage/compaction/ob_i_compaction_filter.h"

namespace oceanbase
{
namespace storage
{
namespace mds
{
struct MdsDumpKVStorageAdapter;
}
class ObMdsMinorFilter : public compaction::ObICompactionFilter
{
public:
  ObMdsMinorFilter();
  virtual ~ObMdsMinorFilter() = default;
  int init(
    const int64_t last_major_snapshot,
    const int64_t multi_version_start);
  void reset()
  {
    last_major_snapshot_ = 0;
    truncate_filter_snapshot_ = 0;
    is_inited_ = false;
  }
  virtual CompactionFilterType get_filter_type() const override { return MDS_MINOR_FILTER_DATA; }
  virtual int filter(const blocksstable::ObDatumRow &row, ObFilterRet &filter_ret) override;
  INHERIT_TO_STRING_KV("ObICompactionFilter", ObICompactionFilter, "filter_name", "ObMdsMinorFilter", K_(is_inited),
      K_(last_major_snapshot), K_(truncate_filter_snapshot));
private:
  int filter_medium_info(
    const blocksstable::ObDatumRow &row,
    const mds::MdsDumpKVStorageAdapter &kv_adapter,
    ObFilterRet &filter_ret);
  int filter_truncate_info(
    const blocksstable::ObDatumRow &row,
    const mds::MdsDumpKVStorageAdapter &kv_adapter,
    ObFilterRet &filter_ret);
private:
  bool is_inited_;
  int64_t last_major_snapshot_;
  int64_t truncate_filter_snapshot_;
  ObArenaAllocator allocator_;
};

class ObCrossLSMdsMinorFilter : public compaction::ObICompactionFilter
{
public:
  ObCrossLSMdsMinorFilter();
  virtual ~ObCrossLSMdsMinorFilter() = default;
public:
  virtual int filter(const blocksstable::ObDatumRow &row, ObFilterRet &filter_ret) override;
  virtual CompactionFilterType get_filter_type() const override { return MDS_MINOR_CROSS_LS; }
};
} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_OB_MDS_MINOR_COMPACTION_FILTER
