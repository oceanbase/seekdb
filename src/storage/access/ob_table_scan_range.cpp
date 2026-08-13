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

#include "ob_table_scan_range.h"

namespace oceanbase
{
using namespace share;
using namespace common;
using namespace blocksstable;
namespace storage
{

ObTableScanRange::ObTableScanRange()
  : rowkeys_(),
    ranges_(),
    allocator_(nullptr),
    status_(EMPTY),
    enable_new_false_range_(false),
    is_inited_(false)
{
  rowkeys_.set_attr(ObMemAttr("TScanRowkeys"));
  ranges_.set_attr(ObMemAttr("TScanRanges"));
}

void ObTableScanRange::reset()
{
#define RESET_SCAN_RANGES(RANGES)                                                    \
do {                                                                                 \
  for (int64_t i = 0; i < RANGES.count(); i++) {                                     \
    ObDatumRange &range = RANGES.at(i);                                              \
    if (!range.get_start_key().is_static_rowkey()) {                                 \
      allocator_->free(const_cast<ObStorageDatum *>(range.get_start_key().datums_)); \
    }                                                                                \
    if (!range.get_end_key().is_static_rowkey()) {                                   \
      allocator_->free(const_cast<ObStorageDatum *>(range.get_end_key().datums_));   \
    }                                                                                \
  }                                                                                  \
} while(0)                                                                           \

  if (nullptr != allocator_) {
    RESET_SCAN_RANGES(ranges_);

    for (int64_t i = 0; i < rowkeys_.count(); i++) {
      if (!rowkeys_.at(i).is_static_rowkey()) {
        allocator_->free(const_cast<ObStorageDatum *>(rowkeys_.at(i).datums_));
      }
    }
  }
  rowkeys_.reset();
  ranges_.reset();
  allocator_ = nullptr;
  status_ = EMPTY;
  enable_new_false_range_ = false;
  is_inited_ = false;
}

int ObTableScanRange::init(ObTableScanParam &scan_param)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    STORAGE_LOG(WARN, "ObTableScanRange is inited twice", K(ret), K(*this));
  } else if (OB_UNLIKELY(!scan_param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "Invalid scan param", K(ret), K(scan_param));
  } else {
    allocator_ = scan_param.scan_allocator_;
    status_ = scan_param.is_get_ ? GET : SCAN;
    enable_new_false_range_ = scan_param.enable_new_false_range_;
    const ObStorageDatumUtils *datum_utils =
        &scan_param.table_param_->get_read_info().get_datum_utils();
    if (OB_UNLIKELY(!datum_utils->is_valid())) {
      ret = OB_ERR_UNEXPECTED;
      STORAGE_LOG(WARN, "Invalid datum utils", K(ret), KPC(scan_param.table_param_));
    } else if (scan_param.is_get_) {
      if (OB_FAIL(init_rowkeys(scan_param.key_ranges_, scan_param.scan_flag_, datum_utils))) {
      }
    } else if (OB_FAIL(init_ranges(scan_param.key_ranges_, scan_param.scan_flag_, datum_utils))) {
    }
    if (OB_SUCC(ret)) {
      is_inited_ = true;
    }
  }
  return ret;
}

int ObTableScanRange::init(
    ObTableScanParam &scan_param,
    const ObSimpleBatch &simple_batch,
    ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  ObQueryFlag scan_flag;
  scan_flag.scan_order_ = ObQueryFlag::Forward;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    STORAGE_LOG(WARN, "ObTableScanRange is already inited", K(ret), K(*this));
  } else if (OB_UNLIKELY(!simple_batch.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "Invalid simple batch", K(ret), K(simple_batch));
  } else if (FALSE_IT(allocator_ = &allocator)) {
  } else if (simple_batch.type_ == ObSimpleBatch::T_SCAN) {
    ObSEArray<ObNewRange, 1> ranges;
    if (OB_ISNULL(simple_batch.range_)) {
      ret = OB_INVALID_ARGUMENT;
      STORAGE_LOG(WARN, "Invalid simple batch range", K(ret), K(simple_batch));
    } else if (OB_FAIL(ranges.push_back(*simple_batch.range_))) {
    } else if (OB_FAIL(init_ranges(ranges, scan_flag, nullptr))) {
    }
  } else if (OB_ISNULL(simple_batch.ranges_)) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "Invalid simple batch ranges", K(ret), K(simple_batch));
  } else if (OB_FAIL(init_ranges(*simple_batch.ranges_, scan_flag, nullptr))) {
  }
  if (OB_SUCC(ret)) {
    status_ = ranges_.empty() ? EMPTY : SCAN;
    enable_new_false_range_ = scan_param.enable_new_false_range_;
    is_inited_ = true;
  }
  return ret;
}

int ObTableScanRange::always_false(const common::ObNewRange &range, bool &is_false)
{
  int ret = OB_SUCCESS;
  int cmp = 0;

  if (OB_LIKELY(enable_new_false_range_)) {
    is_false = false;
  } else if (OB_FAIL(range.get_start_key().compare(range.get_end_key(), cmp))) {
  } else {
    is_false = (cmp > 0) || (0 == cmp && (!range.border_flag_.inclusive_start() || !range.border_flag_.inclusive_end()));
    if (is_false) {
    }
  }
  return ret;
}

int ObTableScanRange::init_rowkeys(
    const common::ObIArray<common::ObNewRange> &ranges,
    const common::ObQueryFlag &scan_flag,
    const blocksstable::ObStorageDatumUtils *datum_utils)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(nullptr == allocator_)) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "Invalid argument to init rowkeys", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < ranges.count(); i++) {
      ObDatumRowkey datum_rowkey;
      const ObRowkey &rowkey = ranges.at(i).get_start_key();
      bool is_false = false;
      if (OB_FAIL(always_false(ranges.at(i), is_false))) {
      } else if (is_false) {
      } else if (OB_FAIL(datum_rowkey.from_rowkey(rowkey, *allocator_))) {
      } else if (FALSE_IT(datum_rowkey.set_group_idx(ranges.at(i).get_group_id()))) {
      } else if (OB_FAIL(rowkeys_.push_back(datum_rowkey))) {
      }
    }
    if (OB_SUCC(ret)) {
      if (rowkeys_.empty()) {
        status_ = EMPTY;
      } else if (rowkeys_.count() > 1 && nullptr != datum_utils && scan_flag.is_support_sort_scan()) {
        ObDatumComparor<ObDatumRowkey> comparor(*datum_utils, ret, scan_flag.is_reverse_scan());
        lib::ob_sort(rowkeys_.begin(), rowkeys_.end(), comparor);
      }
    }
  }
  return ret;
}

int ObTableScanRange::init_ranges(
    const common::ObIArray<common::ObNewRange> &ranges,
    const common::ObQueryFlag &scan_flag,
    const blocksstable::ObStorageDatumUtils *datum_utils)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(nullptr == allocator_)) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "Invalid argument to init ranges", K(ret), K(allocator_));
  } else if (ranges.empty() && !enable_new_false_range_) {
    ObDatumRange datum_range;
    datum_range.set_whole_range();
    if (OB_FAIL(ranges_.push_back(datum_range))) {
    }
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < ranges.count(); i++) {
      ObDatumRange datum_range;
      bool is_false = false;
      if (OB_FAIL(always_false(ranges.at(i), is_false))) {
      } else if (is_false) {
      } else if (OB_FAIL(datum_range.from_range(ranges.at(i), *allocator_, enable_new_false_range_))) {
      } else if (OB_FAIL(ranges_.push_back(datum_range))) {
      }
    }
    if (OB_SUCC(ret)) {
      if (ranges_.empty()) {
        status_ = EMPTY;
      } else if (ranges_.count() > 1 && nullptr != datum_utils && scan_flag.is_support_sort_scan()) {
        ObDatumComparor<ObDatumRange> comparor(*datum_utils, ret, scan_flag.is_reverse_scan());
        lib::ob_sort(ranges_.begin(), ranges_.end(), comparor);
      }
    }
  }
  return ret;
}

int ObTableScanRange::get_query_iter_type(ObQRIterType &iter_type) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_valid())) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "not inited");
  } else {
    iter_type = T_INVALID_ITER_TYPE;
    if (is_get()) {
      if (get_rowkeys().count() == 1) {
        iter_type = T_SINGLE_GET;
      } else {
        iter_type = T_MULTI_GET;
      }
    } else if (get_ranges().count() == 1) {
      iter_type = T_SINGLE_SCAN;
    } else {
      iter_type = T_MULTI_SCAN;
    }
  }
  return ret;
}
} // namespace storage
} // namespace oceanbase
