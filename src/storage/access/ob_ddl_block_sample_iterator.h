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

#ifndef OCEANBASE_STORAGE_OB_DDL_BLOCK_SAMPLE_ITERATOR_H
#define OCEANBASE_STORAGE_OB_DDL_BLOCK_SAMPLE_ITERATOR_H

#include "storage/ob_i_store.h"
#include "ob_i_sample_iterator.h"
#include "ob_multiple_scan_merge.h"
#include "storage/blocksstable/index_block/ob_index_block_tree_cursor.h"
#include "storage/access/ob_block_sample_iterator.h"

namespace oceanbase
{
namespace storage
{

class ObDDLBlockSampleIterator : public ObBlockSampleIterator
{
public:
  explicit ObDDLBlockSampleIterator(const common::SampleInfo &sample_info) :
    ObBlockSampleIterator(sample_info), is_opened_(false), reservoir_() { }
  virtual ~ObDDLBlockSampleIterator() = default;
  int open(ObMultipleScanMerge &scan_merge,
           ObTableAccessContext &access_ctx,
           const blocksstable::ObDatumRange &range,
           ObGetTableParam &get_table_param,
           const bool is_reverse_scan);
  virtual void reuse() override;
  virtual void reset() override;
  virtual int get_next_row(blocksstable::ObDatumRow *&row) override;
  virtual int get_next_rows(int64_t &count, int64_t capacity) override;

protected:
  virtual int open_range(blocksstable::ObDatumRange &range) override;

private:
  int reservoir_block_sample();

private:
  bool is_opened_;
  ObArray<blocksstable::ObDatumRange *> reservoir_;
};

}
}

#endif /* OCEANBASE_STORAGE_OB_DDL_BLOCK_SAMPLE_ITERATOR_H */
