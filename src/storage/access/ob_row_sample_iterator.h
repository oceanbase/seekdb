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

#ifndef OCEANBAES_STORAGE_OB_ROW_SAMPLE_ITERATOR_H
#define OCEANBAES_STORAGE_OB_ROW_SAMPLE_ITERATOR_H

#include "share/ob_i_tablet_scan.h"
#include "storage/ob_i_store.h"
#include "ob_i_sample_iterator.h"

namespace oceanbase
{
namespace storage
{
class ObMemtableRowSampleIterator : public ObISampleIterator 
{
public:
  // must larger than 1
  static const int64_t SAMPLE_MEMTABLE_RANGE_COUNT = 10;
public:
  explicit ObMemtableRowSampleIterator(const SampleInfo &sample_info)
      : ObISampleIterator(sample_info), iterator_(nullptr), row_num_(0) {}
  virtual ~ObMemtableRowSampleIterator() {}

  int open(ObQueryRowIterator &iterator)
  {
    int ret = OB_SUCCESS;
    iterator_ = &iterator;
    row_num_ = 0;
    return ret;
  }

  virtual void reuse() { row_num_ = 0; }

  virtual void reset() override
  {
    iterator_ = nullptr;
    row_num_ = 0;
  }

  virtual int get_next_row(blocksstable::ObDatumRow *&row) override;

private:
  ObQueryRowIterator *iterator_;
  int64_t row_num_;
};

}
}



#endif /* OCEANBAES_STORAGE_OB_ROW_SAMPLE_ITERATOR_H */
