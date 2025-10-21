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

#ifndef _OB_SERIES_ADAPTER_ITER_H
#define _OB_SERIES_ADAPTER_ITER_H

#include "ob_hbase_cell_iter.h"

namespace oceanbase
{   
namespace table
{

class ObAdapterCellCompare
{
public:
  ObAdapterCellCompare() = default;
  virtual ~ObAdapterCellCompare() = default;

  virtual int compare(const common::ObNewRow *lhs, const common::ObNewRow *rhs, int &cmp_ret);
  virtual bool operator()(const common::ObNewRow *lhs, const common::ObNewRow *rhs);

  int get_error_code() const noexcept { return result_code_; }

protected:
  int result_code_ = OB_SUCCESS;
};

class ObHbaseSeriesCellIter : public ObHbaseCellIter {
public:
  ObHbaseSeriesCellIter();
  virtual ~ObHbaseSeriesCellIter() {}
  int rescan(ObHbaseRescanParam &rescan_param);
  int get_next_cell(ObNewRow *&row) override;
  ObNewRange &get_origin_range() { return  origin_range_; }

private:
  int init();
  int get_next_cell(ObNewRow *&row, uint8_t depth = 0);
  int check_left_border(ObNewRow &row, bool &left_inclusive);
  int check_right_border(ObNewRow &row, bool &right_inclusive);
  int is_in_range(ObNewRow &row, bool &in_range);
  int convert_series_to_normal(ObNewRow &series_row, ObIArray<ObNewRow *> &normal_rows);
  bool same_kt(ObNewRow &json_row);
  int handle_json_row(ObNewRow &series_row);

private:
  common::ObArenaAllocator copy_alloc_;
  hash::ObHashSet<ObString, common::hash::NoPthreadDefendMode> unique_qualifier_;
  ObAdapterCellCompare compare_;
  ObBinaryHeap<ObNewRow *, ObAdapterCellCompare, 16> rows_heap_;
  ObNewRow *next_row_;
  bool is_inited_;
  ObNewRange origin_range_;
  ObString now_k_;
  int64_t now_t_;
  bool iter_is_end_;


  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObHbaseSeriesCellIter);
};

} // end of namespace table
} // end of namespace oceanbase

#endif
