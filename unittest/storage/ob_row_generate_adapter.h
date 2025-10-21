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

#ifndef UNITTEST_STORAGE_OB_ROW_GENERATE_ADAPTER_H_
#define UNITTEST_STORAGE_OB_ROW_GENERATE_ADAPTER_H_


#include "common/object/ob_object.h"
#include "storage/ob_i_store.h"
#include "common/row/ob_row_store.h"
#include "lib/hash/ob_hashmap.h"
#include "lib/time/ob_time_utility.h"
#include "lib/string/ob_string.h"
#include "storage/ob_i_store.h"
#include "blocksstable/ob_row_generate.h"

namespace oceanbase
{

using namespace blocksstable;

namespace common
{


class ObMockIterWithLimit : public ObNewRowIterator {
public:
  ObMockIterWithLimit() : need_row_count_(0), got_row_count_(0) {};
  ~ObMockIterWithLimit() {};

  void set_need_row_count(int64_t count) {
    need_row_count_ = count;
  }

  int64_t get_need_row_count() {
    return need_row_count_;
  }

  bool is_iter_end() {
    return need_row_count_ == got_row_count_;
  }

protected:

  void advance_iter() {
      ++got_row_count_;
    }

  int64_t need_row_count_;
  int64_t got_row_count_;
};

class ObRowGenerateAdapter : public ObMockIterWithLimit{

public:
  ObRowGenerateAdapter() {};
  ~ObRowGenerateAdapter() {};

  virtual int get_next_row(ObNewRow *&row)
  {
    int ret = OB_SUCCESS;
    if (is_iter_end()) {
      ret = OB_ITER_END;
    } else {
      generate_.get_next_row(new_row_);
      row = &new_row_.row_val_;
      advance_iter();
    }
    return ret;
  }

  int init(const share::schema::ObTableSchema &src_schema, ObIAllocator &buf)
  {
    new_row_.row_val_.count_ = src_schema.get_column_count();
    new_row_.row_val_.cells_ = static_cast<ObObj *>(buf.alloc(new_row_.row_val_.count_ * sizeof(ObObj)));
    return generate_.init(src_schema);
  }

  virtual void reset()
  {
    generate_.reset();
  }

private:
  ObRowGenerate generate_;
  ObStoreRow new_row_;
};

} // namespace unittest
} // namespace oceanbase
#endif // OCEANBASE_UNITTEST_MOCK_ITERATOR_H_


