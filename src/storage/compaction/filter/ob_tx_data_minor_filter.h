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

#ifndef OB_STORAGE_COMPACTION_TX_DATA_MINOR_FILTER_H_
#define OB_STORAGE_COMPACTION_TX_DATA_MINOR_FILTER_H_

#include "storage/compaction/ob_i_compaction_filter.h"
#include "share/scn.h"
namespace oceanbase
{
namespace blocksstable
{
struct ObDatumRow;
}
namespace compaction
{
class ObTxDataMinorFilter : public ObICompactionFilter
{
public:
  ObTxDataMinorFilter()
    : ObICompactionFilter(),
      is_inited_(false),
      filter_val_(),
      filter_col_idx_(0),
      max_filtered_end_scn_()
  {
    filter_val_.set_max();
    max_filtered_end_scn_.set_min();
  }
  ~ObTxDataMinorFilter() {}
  int init(const share::SCN &filter_val, const int64_t filter_col_idx);
  OB_INLINE void reset()
  {
    filter_val_.set_max();
    filter_col_idx_ = 0;
    max_filtered_end_scn_.set_min();
    is_inited_ = false;
  }
  virtual CompactionFilterType get_filter_type() const override { return TX_DATA_MINOR; }
  virtual int filter(const blocksstable::ObDatumRow &row, ObFilterRet &filter_ret) override;
  INHERIT_TO_STRING_KV("ObTxDataMinorFilter", ObICompactionFilter, K_(filter_val),
      K_(filter_col_idx), K_(max_filtered_end_scn));

public:
  share::SCN get_max_filtered_end_scn() { return max_filtered_end_scn_; }
  share::SCN get_recycle_scn() { return filter_val_; }

private:
  bool is_inited_;
  share::SCN filter_val_;
  int64_t filter_col_idx_;
  share::SCN max_filtered_end_scn_;
};

} // namespace compaction
} // namespace oceanbase

#endif // OB_STORAGE_COMPACTION_TX_DATA_MINOR_FILTER_H_
