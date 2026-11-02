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

#ifndef OCEANBASE_STORAGE_OB_MULTIPLE_MULTI_SCAN_MERGE_
#define OCEANBASE_STORAGE_OB_MULTIPLE_MULTI_SCAN_MERGE_
#include "ob_multiple_scan_merge.h"
#include "storage/ob_storage_struct.h"


namespace oceanbase
{
namespace storage
{
class ObMultipleMultiScanMerge : public ObMultipleScanMerge
{
public:
  ObMultipleMultiScanMerge();
  virtual ~ObMultipleMultiScanMerge();
public:
  int open(const common::ObIArray<blocksstable::ObDatumRange> &ranges);
  virtual void reset();
protected:
  virtual int calc_scan_range() override;
  int inner_calc_scan_range(const ObIArray<blocksstable::ObDatumRange> *&ranges,
                            common::ObSEArray<blocksstable::ObDatumRange, 32> &cow_ranges,
                            int64_t curr_scan_index_,
                            blocksstable::ObDatumRowkey &curr_rowkey,
                            bool calc_di_base_range);
  virtual int construct_iters() override;
  virtual int inner_get_next_row(blocksstable::ObDatumRow &row);
  virtual int is_range_valid() const override;
  virtual int pause(bool& do_pause) override final;
  virtual int get_current_range(ObDatumRange& current_range) const override;
  virtual int get_range_count() const override { return ranges_->count(); }
private:
  const ObIArray<blocksstable::ObDatumRange> *ranges_;
  common::ObSEArray<blocksstable::ObDatumRange, 32> cow_ranges_;
  const ObIArray<blocksstable::ObDatumRange> *di_base_ranges_;
  common::ObSEArray<blocksstable::ObDatumRange, 32> di_base_cow_ranges_;
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObMultipleMultiScanMerge);
};

} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_OB_MULTIPLE_MULTI_SCAN_MERGE_
