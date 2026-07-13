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
    skip_scan_ranges_(),
    allocator_(nullptr),
    status_(EMPTY),
    enable_new_false_range_(false),
    is_inited_(false)
{
  rowkeys_.set_attr(ObMemAttr("TScanRowkeys"));
  ranges_.set_attr(ObMemAttr("TScanRanges"));
  skip_scan_ranges_.set_attr(ObMemAttr("TScanSSRanges"));
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
    RESET_SCAN_RANGES(skip_scan_ranges_);

    for (int64_t i = 0; i < rowkeys_.count(); i++) {
      if (!rowkeys_.at(i).is_static_rowkey()) {
        allocator_->free(const_cast<ObStorageDatum *>(rowkeys_.at(i).datums_));
      }
    }
  }
  rowkeys_.reset();
  ranges_.reset();
  skip_scan_ranges_.reset();
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
      if (scan_param.use_index_skip_scan()) {
        ret = OB_ERR_UNEXPECTED;
        STORAGE_LOG(WARN, "Index skip scan can only be used in scan", K(ret));
      } else if (OB_FAIL(init_rowkeys(scan_param.key_ranges_, scan_param.scan_flag_, datum_utils))) {
        STORAGE_LOG(WARN, "Failed to init rowkeys", K(ret));
      }
    } else if (scan_param.use_index_skip_scan()) {
      if (OB_FAIL(init_ranges_in_skip_scan(
          scan_param.key_ranges_, scan_param.ss_key_ranges_, scan_param.scan_flag_, datum_utils))) {
        STORAGE_LOG(WARN, "Failed to init skip scan ranges", K(ret));
      }
    } else if (OB_FAIL(init_ranges(scan_param.key_ranges_, scan_param.scan_flag_, datum_utils))) {
      STORAGE_LOG(WARN, "Failed to init ranges", K(ret));
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
      STORAGE_LOG(WARN, "Failed to push back range", K(ret));
    } else if (OB_FAIL(init_ranges(ranges, scan_flag, nullptr))) {
      STORAGE_LOG(WARN, "Failed to init ranges", K(ret));
    }
  } else if (OB_ISNULL(simple_batch.ranges_)) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "Invalid simple batch ranges", K(ret), K(simple_batch));
  } else if (OB_FAIL(init_ranges(*simple_batch.ranges_, scan_flag, nullptr))) {
    STORAGE_LOG(WARN, "Failed to init ranges", K(ret));
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
    STORAGE_LOG(WARN, "Failed to compare range keys", K(ret), K(range));
  } else {
    is_false = (cmp > 0) || (0 == cmp && (!range.border_flag_.inclusive_start() || !range.border_flag_.inclusive_end()));
    if (is_false) {
      STORAGE_LOG(DEBUG, "chaser debug always false range", K(ret), K(range), K(range.border_flag_));
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
        STORAGE_LOG(WARN, "Failed to check range", K(ret), K(ranges.at(i)));
      } else if (is_false) {
      } else if (OB_FAIL(datum_rowkey.from_rowkey(rowkey, *allocator_))) {
        STORAGE_LOG(WARN, "Failed to convert rowkey", K(ret));
      } else if (FALSE_IT(datum_rowkey.set_group_idx(ranges.at(i).get_group_id()))) {
      } else if (OB_FAIL(rowkeys_.push_back(datum_rowkey))) {
        STORAGE_LOG(WARN, "Failed to push rowkey", K(ret));
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
      STORAGE_LOG(WARN, "Failed to push whole range", K(ret));
    }
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < ranges.count(); i++) {
      ObDatumRange datum_range;
      bool is_false = false;
      if (OB_FAIL(always_false(ranges.at(i), is_false))) {
        STORAGE_LOG(WARN, "Failed to check range", K(ret), K(ranges.at(i)));
      } else if (is_false) {
      } else if (OB_FAIL(datum_range.from_range(ranges.at(i), *allocator_, enable_new_false_range_))) {
        STORAGE_LOG(WARN, "Failed to convert range", K(ret));
      } else if (OB_FAIL(ranges_.push_back(datum_range))) {
        STORAGE_LOG(WARN, "Failed to push range", K(ret));
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

int ObTableScanRange::init_ranges_in_skip_scan(
    const common::ObIArray<common::ObNewRange> &ranges,
    const common::ObIArray<common::ObNewRange> &skip_scan_ranges,
    const common::ObQueryFlag &scan_flag,
    const blocksstable::ObStorageDatumUtils *datum_utils)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(nullptr == allocator_
      || ranges.count() != skip_scan_ranges.count()
      || ranges.empty())) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "Invalid skip scan ranges", K(ret), K(ranges.count()), K(skip_scan_ranges.count()));
  } else {
    common::ObSEArray<ObSkipScanWrappedRange, DEFAULT_RANGE_CNT> wrapped_ranges;
    for (int64_t i = 0; OB_SUCC(ret) && i < ranges.count(); i++) {
      ObSkipScanWrappedRange wrapped_range;
      bool is_false = false;
      if (OB_FAIL(always_false(ranges.at(i), is_false))) {
        STORAGE_LOG(WARN, "Failed to check range", K(ret), K(ranges.at(i)));
      } else if (is_false) {
      } else if (OB_FAIL(wrapped_range.datum_range_.from_range(
          ranges.at(i), *allocator_, enable_new_false_range_))) {
        STORAGE_LOG(WARN, "Failed to convert range", K(ret));
      } else if (OB_FAIL(wrapped_range.datum_skip_range_.from_range(
          skip_scan_ranges.at(i), *allocator_, enable_new_false_range_))) {
        STORAGE_LOG(WARN, "Failed to convert skip range", K(ret));
      } else if (OB_FAIL(wrapped_ranges.push_back(wrapped_range))) {
        STORAGE_LOG(WARN, "Failed to push range", K(ret));
      }
    }
    if (OB_SUCC(ret) && wrapped_ranges.count() > 1
        && nullptr != datum_utils && scan_flag.is_support_sort_scan()) {
      ObDatumComparor<ObSkipScanWrappedRange> comparor(
          *datum_utils, ret, scan_flag.is_reverse_scan());
      lib::ob_sort(wrapped_ranges.begin(), wrapped_ranges.end(), comparor);
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < wrapped_ranges.count(); i++) {
      if (OB_FAIL(ranges_.push_back(wrapped_ranges.at(i).datum_range_))) {
        STORAGE_LOG(WARN, "Failed to push range", K(ret));
      } else if (OB_FAIL(skip_scan_ranges_.push_back(wrapped_ranges.at(i).datum_skip_range_))) {
        STORAGE_LOG(WARN, "Failed to push skip range", K(ret));
      }
    }
    if (OB_SUCC(ret) && ranges_.empty()) {
      status_ = EMPTY;
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
