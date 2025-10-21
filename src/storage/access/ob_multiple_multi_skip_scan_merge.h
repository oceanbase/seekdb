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

#ifndef OCEANBASE_STORAGE_MULTIPLE_MULTI_SKIP_SCAN_MERGE_H
#define OCEANBASE_STORAGE_MULTIPLE_MULTI_SKIP_SCAN_MERGE_H

#include "ob_multiple_skip_scan_merge.h"
namespace oceanbase
{
namespace storage
{

class ObMultipleMultiSkipScanMerge final : public ObMultipleSkipScanMerge
{
public:
  ObMultipleMultiSkipScanMerge();
  virtual ~ObMultipleMultiSkipScanMerge();
  virtual int init(
      ObTableAccessParam &param,
      ObTableAccessContext &context,
      ObGetTableParam &get_table_param) override;
  virtual void reset() override;
  virtual void reuse() override;
protected:
  virtual int inner_get_next_row(blocksstable::ObDatumRow &row) override;
  virtual int inner_get_next_rows() override;
private:
  int64_t cur_range_idx_;
  const ObIArray<blocksstable::ObDatumRange> *ranges_;
  const ObIArray<blocksstable::ObDatumRange> *skip_scan_ranges_;
};

}
}

#endif // OCEANBASE_STORAGE_MULTIPLE_MULTI_SKIP_SCAN_MERGE_H
