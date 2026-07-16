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

#ifndef OCEANBASE_SQL_ENGINE_SORT_OB_I_SORT_STRATEGY_H_
#define OCEANBASE_SQL_ENGINE_SORT_OB_I_SORT_STRATEGY_H_

#include "lib/allocator/ob_allocator.h"
#include "lib/ob_errno.h"

namespace oceanbase
{
namespace sql
{

class ObSortVecOpContext;
struct ObBatchRows;
struct ObCompactRow;

class ObISortStrategy
{
public:
  explicit ObISortStrategy() : is_inited_(false) {}
  virtual ~ObISortStrategy() {}

  virtual int init(ObSortVecOpContext &ctx) = 0;
  virtual int sort() = 0;
  virtual int add_batch(const ObBatchRows &input_brs, bool &sort_need_dump) = 0;
  virtual int add_batch(const ObBatchRows &input_brs, const uint16_t selector[], const int64_t size) = 0;
  virtual int get_next_batch(const int64_t max_cnt, int64_t &read_rows) = 0;
  virtual int add_batch_stored_row(int64_t &row_size, const ObCompactRow **sk_rows, const ObCompactRow **addon_rows) = 0;
  virtual int64_t get_extra_size(bool is_sort_key) = 0;
  virtual int rewind() = 0;
  virtual void reset() = 0;

  OB_INLINE bool is_inited() const { return is_inited_; }

protected:
  bool is_inited_;
};

} // end namespace sql
} // end namespace oceanbase

#endif /* OCEANBASE_SQL_ENGINE_SORT_OB_I_SORT_STRATEGY_H_ */
