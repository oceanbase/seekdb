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

#ifndef OCEANBASE_SQL_ENGINE_SORT_OB_SORT_VEC_DUMP_STRATEGY_H_
#define OCEANBASE_SQL_ENGINE_SORT_OB_SORT_VEC_DUMP_STRATEGY_H_

#include <utility>
#include "lib/container/ob_array.h"
#include "sql/engine/basic/ob_compact_row.h"

namespace oceanbase
{
namespace sql
{

template <typename Store_Row, bool has_addon>
class ObNormalDumpStrategy
{
public:
  ObNormalDumpStrategy(common::ObIArray<Store_Row *> &rows,
                       common::ObIArray<Store_Row *> &ties_array,
                       const RowMeta &sk_row_meta)
    : row_pos_(0),
      ties_array_pos_(0),
      rows_(rows),
      ties_array_(ties_array),
      sk_row_meta_(sk_row_meta)
  {}

  int operator()(const Store_Row *&sk_row, const Store_Row *&addon_row)
  {
    int ret = OB_SUCCESS;
    if (row_pos_ >= rows_.count() && ties_array_pos_ >= ties_array_.count()) {
      ret = OB_ITER_END;
    } else if (row_pos_ < rows_.count()) {
      sk_row = rows_.at(row_pos_++);
      if (has_addon) {
        addon_row = sk_row->get_addon_ptr(sk_row_meta_);
      }
    } else {
      sk_row = ties_array_.at(ties_array_pos_++);
      if (has_addon) {
        addon_row = sk_row->get_addon_ptr(sk_row_meta_);
      }
    }
    return ret;
  }

private:
  int64_t row_pos_;
  int64_t ties_array_pos_;
  common::ObIArray<Store_Row *> &rows_;
  common::ObIArray<Store_Row *> &ties_array_;
  const RowMeta &sk_row_meta_;
};

template <typename Store_Row, bool has_addon, typename HeapNextFunc>
class ObIMMSDumpStrategy
{
public:
  ObIMMSDumpStrategy(HeapNextFunc &&heap_next,
                     const RowMeta &sk_row_meta,
                     common::ObIArray<Store_Row *> &sorted_dumped_rows,
                     const bool is_topn_sort,
                     const bool is_topn_filter_enabled)
    : heap_next_(std::forward<HeapNextFunc>(heap_next)),
      sk_row_meta_(sk_row_meta),
      sorted_dumped_rows_(sorted_dumped_rows),
      is_topn_sort_(is_topn_sort),
      is_topn_filter_enabled_(is_topn_filter_enabled)
  {}

  int operator()(const Store_Row *&sk_row, const Store_Row *&addon_row)
  {
    int ret = OB_SUCCESS;
    if (OB_FAIL(heap_next_(sk_row))) {
      if (OB_ITER_END != ret) {
        SQL_ENG_LOG(WARN, "get row from memory heap failed", K(ret));
      }
    } else {
      if (has_addon) {
        addon_row = sk_row->get_addon_ptr(sk_row_meta_);
      }
      if (is_topn_sort_ && is_topn_filter_enabled_) {
        ret = sorted_dumped_rows_.push_back(const_cast<Store_Row *>(sk_row));
      }
    }
    return ret;
  }

private:
  HeapNextFunc heap_next_;
  const RowMeta &sk_row_meta_;
  common::ObIArray<Store_Row *> &sorted_dumped_rows_;
  bool is_topn_sort_;
  bool is_topn_filter_enabled_;
};

template <typename Store_Row, bool has_addon, typename PartTopNProvider>
class ObPartitionTopNDumpStrategy
{
public:
  ObPartitionTopNDumpStrategy(PartTopNProvider &provider, int64_t &node_idx, int64_t &row_idx)
    : provider_(provider), node_idx_(node_idx), row_idx_(row_idx)
  {}

  int operator()(const Store_Row *&sk_row, const Store_Row *&addon_row)
  {
    return provider_.part_topn_node_next(node_idx_, row_idx_, sk_row, addon_row);
  }

private:
  PartTopNProvider &provider_;
  int64_t &node_idx_;
  int64_t &row_idx_;
};

} // namespace sql
} // namespace oceanbase

#endif /* OCEANBASE_SQL_ENGINE_SORT_OB_SORT_VEC_DUMP_STRATEGY_H_ */
