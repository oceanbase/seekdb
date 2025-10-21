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

#ifndef OCEANBASE_UNITTEST_MOCK_MERGE_ITERATOR_H_
#define OCEANBASE_UNITTEST_MOCK_MERGE_ITERATOR_H_

#include "mock_ob_iterator.h"
#include "storage/access/ob_vector_store.h"

namespace oceanbase
{
using namespace storage;
namespace common
{

class ObMockScanMergeIterator : public storage::ObStoreRowIterator
{
public:
  ObMockScanMergeIterator(int64_t count)
      : current_(0),
      end_(count - 1),
      vector_store_(nullptr),
      datum_infos_(nullptr),
      read_info_(nullptr)
  {}
  virtual ~ObMockScanMergeIterator() {};
  int get_next_row(const storage::ObStoreRow *&row);
  int init(const ObVectorStore *vector_store,
           common::ObIAllocator &alloc,
           const ObITableReadInfo &read_info);
  int reset_scanner();
  bool end_of_block() {
    return current_ == -1 ||
        current_ > end_;
  }
  void reset()
  {
    row_.reset();
    sstable_row_.reset();
  }

public:
  int64_t current_;
  int64_t end_;
  ObDatumRow row_;
  const storage::ObVectorStore *vector_store_;
  const ObIArray<blocksstable::ObSqlDatumInfo > *datum_infos_;
  const ObITableReadInfo *read_info_;
  ObStoreRow sstable_row_;
};

} // namespace unittest
} // namespace oceanbase
#endif // OCEANBASE_UNITTEST_MOCK_MERGE_ITERATOR_H_

