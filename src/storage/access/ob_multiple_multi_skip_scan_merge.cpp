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

#include "ob_multiple_multi_skip_scan_merge.h"

namespace oceanbase
{
namespace storage
{

ObMultipleMultiSkipScanMerge::ObMultipleMultiSkipScanMerge()
  : cur_range_idx_(0),
    ranges_(nullptr),
    skip_scan_ranges_(nullptr)
{
}

ObMultipleMultiSkipScanMerge::~ObMultipleMultiSkipScanMerge()
{
  reset();
}

int ObMultipleMultiSkipScanMerge::init(
    ObTableAccessParam &param,
    ObTableAccessContext &context,
    ObGetTableParam &get_table_param)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObMultipleSkipScanMerge::init(param, context, get_table_param))) {
    STORAGE_LOG(WARN, "Fail to init ObMultipleSkipScanMerge", K(ret), K(context), K(get_table_param));
  }
  return ret;
}

void ObMultipleMultiSkipScanMerge::reset()
{
  cur_range_idx_ = 0;
  ranges_ = nullptr;
  skip_scan_ranges_ = nullptr;
  ObMultipleSkipScanMerge::reset();
}

void ObMultipleMultiSkipScanMerge::reuse()
{
  cur_range_idx_ = 0;
  ranges_ = nullptr;
  skip_scan_ranges_ = nullptr;
  ObMultipleSkipScanMerge::reuse();

}


int ObMultipleMultiSkipScanMerge::inner_get_next_row(blocksstable::ObDatumRow &row)
{
  int ret = OB_SUCCESS;
  while (OB_SUCC(ret)) {
    if (OB_FAIL(ObMultipleSkipScanMerge::inner_get_next_row(row))) {
      if (OB_UNLIKELY(OB_ITER_END != ret && OB_PUSHDOWN_STATUS_CHANGED != ret)) {
        STORAGE_LOG(WARN, "Fail to inner get next row", K(ret), K(cur_range_idx_));
      } else if (OB_ITER_END == ret) {
        if (++cur_range_idx_ < ranges_->count()) {
          ret = OB_SUCCESS;
          ObMultipleSkipScanMerge::reuse();
          if (OB_FAIL(ObMultipleSkipScanMerge::open(ranges_->at(cur_range_idx_), skip_scan_ranges_->at(cur_range_idx_)))) {
            STORAGE_LOG(WARN, "Fail to open cur range", K(ret), K(cur_range_idx_));
          }
        }
      }
    } else {
      STORAGE_LOG(DEBUG, "get next row", K(row));
      break;
    }
  }
  return ret;
}

int ObMultipleMultiSkipScanMerge::inner_get_next_rows()
{
  int ret = OB_SUCCESS;
  while (OB_SUCC(ret)) {
    if (OB_FAIL(ObMultipleSkipScanMerge::inner_get_next_rows())) {
      if (OB_UNLIKELY(OB_ITER_END != ret && OB_PUSHDOWN_STATUS_CHANGED != ret)) {
        STORAGE_LOG(WARN, "Fail to inner get next row", K(ret), K(cur_range_idx_));
      } else if (OB_ITER_END == ret) {
        if (++cur_range_idx_ < ranges_->count()) {
          ret = OB_SUCCESS;
          ObMultipleSkipScanMerge::reuse();
          if (OB_FAIL(ObMultipleSkipScanMerge::open(ranges_->at(cur_range_idx_), skip_scan_ranges_->at(cur_range_idx_)))) {
            STORAGE_LOG(WARN, "Fail to open cur range", K(ret), K(cur_range_idx_));
          }
        }
      }
    } else {
      break;
    }
  }
  return ret;
}

}
}
