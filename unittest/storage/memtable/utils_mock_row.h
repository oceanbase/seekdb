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

#ifndef  OCEANBASE_UNITTEST_MEMTABLE_MOCK_ROW_H_
#define  OCEANBASE_UNITTEST_MEMTABLE_MOCK_ROW_H_

#include "storage/memtable/ob_memtable_interface.h"
#include "storage/ob_i_store.h"
#include "storage/access/ob_store_row_iterator.h"
#include "lib/allocator/page_arena.h"
#include "common/object/ob_object.h"
#include "common/rowkey/ob_rowkey.h"

namespace oceanbase
{
namespace unittest
{
using namespace oceanbase::common;
using namespace oceanbase::memtable;
using namespace oceanbase::storage;

class ObMtRowIterator : public ObStoreRowIterator
{
public:
  ObMtRowIterator() : cursor_(0) {}
  ~ObMtRowIterator() {}
  int get_next_row(const ObStoreRow *&row)
  {
    int ret = OB_SUCCESS;
    if (cursor_ < rows_.count()) {
      row = &rows_[cursor_++];
    } else {
      ret = OB_ITER_END;
    }
    return ret;
  }
  void reset() { cursor_ = 0; rows_.reset(); }
  void reset_iter() { cursor_ = 0; }
  void add_row(const ObStoreRow &row)
  {
    rows_.push_back(row);
  }
private:
  int64_t cursor_;
  ObSEArray<ObStoreRow, 64> rows_;
};

}
}

#endif //OCEANBASE_UNITTEST_MEMTABLE_MOCK_ROW_H_


