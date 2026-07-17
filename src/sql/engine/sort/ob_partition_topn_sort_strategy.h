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

#ifndef OCEANBASE_SQL_ENGINE_SORT_OB_PARTITION_TOPN_SORT_STRATEGY_H_
#define OCEANBASE_SQL_ENGINE_SORT_OB_PARTITION_TOPN_SORT_STRATEGY_H_

#include "sql/engine/sort/ob_i_sort_strategy.h"
#include "sql/engine/sort/ob_sort_resource_manager.h"

namespace oceanbase
{
namespace sql
{

template <typename SortImpl>
class ObPartitionTopNSortStrategy : public ObISortStrategy
{
public:
  ObPartitionTopNSortStrategy()
    : ctx_(nullptr), sort_impl_(nullptr), res_mgr_(nullptr), topn_cnt_(0)
  {
  }

  ~ObPartitionTopNSortStrategy()
  {
    reset();
  }

  int init(ObSortVecOpContext &ctx) override
  {
    int ret = OB_SUCCESS;
    ctx_ = &ctx;
    topn_cnt_ = ctx.topn_cnt_;
    is_inited_ = true;
    return ret;
  }

  int add_batch(const ObBatchRows &input_brs, bool &sort_need_dump) override
  {
    int ret = OB_SUCCESS;
    sort_need_dump = false;
    return ret;
  }

  int add_batch(const ObBatchRows &input_brs, const uint16_t selector[], const int64_t size) override
  {
    int ret = OB_SUCCESS;
    return ret;
  }

  int sort() override
  {
    int ret = OB_SUCCESS;
    return ret;
  }

  int get_next_batch(const int64_t max_cnt, int64_t &read_rows) override
  {
    int ret = OB_SUCCESS;
    read_rows = 0;
    return ret;
  }

  int add_batch_stored_row(int64_t &row_size, const ObCompactRow **sk_rows,
                           const ObCompactRow **addon_rows) override
  {
    int ret = OB_SUCCESS;
    return ret;
  }

  int64_t get_extra_size(bool is_sort_key) override
  {
    return 0;
  }

  int rewind() override
  {
    int ret = OB_SUCCESS;
    return ret;
  }

  void reset() override
  {
    sort_impl_ = nullptr;
    res_mgr_ = nullptr;
    is_inited_ = false;
  }

  OB_INLINE int64_t get_topn_count() const { return topn_cnt_; }

private:
  ObSortVecOpContext *ctx_;
  SortImpl *sort_impl_;
  ObSQLSortResourceManager *res_mgr_;
  int64_t topn_cnt_;
};

} // end namespace sql
} // end namespace oceanbase

#endif /* OCEANBASE_SQL_ENGINE_SORT_OB_PARTITION_TOPN_SORT_STRATEGY_H_ */
