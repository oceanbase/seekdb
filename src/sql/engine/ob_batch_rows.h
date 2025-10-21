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

#ifndef OCEANBASE_ENGINE_OB_BATCH_ROWS_H_
#define OCEANBASE_ENGINE_OB_BATCH_ROWS_H_

#include "lib/utility/ob_print_utils.h"
#include "lib/oblog/ob_log.h"
#include "sql/engine/ob_bit_vector.h"

namespace oceanbase
{
namespace sql
{

// operator return batch rows description
struct ObBatchRows
{
  ObBatchRows() : skip_(NULL), size_(0), end_(false), all_rows_active_(false) {}

  ObBatchRows(ObBitVector &skip, int64_t size, bool all_rows_active) :
    skip_(&skip), size_(size), end_(false), all_rows_active_(all_rows_active)
  {}

  DECLARE_TO_STRING;

  int copy(const ObBatchRows *src)
  {
    int ret = common::OB_SUCCESS;
    size_ = src->size_;
    all_rows_active_ = src->all_rows_active_;
    end_ = src->end_;
    // TODO qubin.qb: use shallow copy instead
    skip_->deep_copy(*(src->skip_), size_);
    return ret;
  }

  void set_skip(int64_t idx) {
    skip_->set(idx);
    all_rows_active_ = false;
  }

  void reset_skip(const int64_t size) {
    skip_->reset(size);
    all_rows_active_ = true;
  }

  void set_all_rows_active(bool v) {
    all_rows_active_ = v;
  }

public:
  //TODO shengle set skip and all_rows_active_ in private members
  // bit vector for filtered rows
  ObBitVector *skip_;
  // batch size
  int64_t size_;
  // iterate end
  bool end_;
  bool all_rows_active_; // means all rows in batch not skip
};

} // end namespace sql
} // end namespace oceanbase

#endif // OCEANBASE_ENGINE_OB_BATCH_ROWS_H_

