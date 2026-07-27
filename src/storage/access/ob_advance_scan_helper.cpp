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

#define USING_LOG_PREFIX STORAGE
#include "ob_advance_scan_helper.h"
#include "storage/blocksstable/ob_micro_block_row_scanner.h"

namespace oceanbase
{
using namespace blocksstable;
namespace storage
{
ObAdvanceScanHelper::ObAdvanceScanHelper(const ObStorageDatumUtils &datum_utils)
  : range_alloc_("ADVANCE_RANGE", OB_MALLOC_NORMAL_BLOCK_SIZE),
    is_inited_(false),
    left_border_reached_(false),
    micro_start_(-1),
    micro_last_(-1),
    micro_current_(-1),
    datum_utils_(datum_utils),
    complete_range_(),
    range_datums_(nullptr),
    read_info_(nullptr),
    stmt_alloc_(nullptr)
{
}

ObAdvanceScanHelper::~ObAdvanceScanHelper()
{
  if (OB_LIKELY(nullptr != range_datums_ && nullptr != stmt_alloc_)) {
    stmt_alloc_->free(range_datums_);
  }
}

void ObAdvanceScanHelper::reset()
{
  is_inited_ = false;
  left_border_reached_ = false;
  micro_start_ = -1;
  micro_last_ = -1;
  micro_current_ = -1;
  complete_range_.reset();
  if (OB_LIKELY(nullptr != range_datums_ && nullptr != stmt_alloc_)) {
    stmt_alloc_->free(range_datums_);
  }
  range_datums_ = nullptr;
  read_info_ = nullptr;
  stmt_alloc_ = nullptr;
  range_alloc_.reset();
}

int ObAdvanceScanHelper::init(
    const bool is_reverse_scan,
    const ObDatumRange &scan_range,
    const ObITableReadInfo &read_info,
    common::ObIAllocator &stmt_allocator)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", KR(ret), KP(this), K(lbt()));
  } else if (OB_UNLIKELY(is_reverse_scan)) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("reverse scan is not supported", KR(ret), K(is_reverse_scan));
  } else if (OB_FAIL(complete_range_.deep_copy(scan_range, range_alloc_))) {
    LOG_WARN("failed to deep copy scan range", KR(ret));
  } else {
    read_info_ = &read_info;
    stmt_alloc_ = &stmt_allocator;
    is_inited_ = true;
    LOG_TRACE("[ADVANCE SCAN] success to init", KR(ret), K(*this));
  }
  return ret;
}

int ObAdvanceScanHelper::switch_info(
    const bool is_reverse_scan,
    const ObDatumRange &scan_range,
    const ObITableReadInfo &read_info,
    common::ObIAllocator &stmt_allocator)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret), KP(this), K(lbt()));
  } else if (OB_UNLIKELY(is_reverse_scan)) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("reverse scan is not supported", KR(ret), K(is_reverse_scan));
  } else if (OB_UNLIKELY(read_info_ != &read_info || stmt_alloc_ != &stmt_allocator)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected argument in rescan", KR(ret),
             KP_(read_info), KP(&read_info), KP_(stmt_alloc), KP(&stmt_allocator), K(lbt()));
  } else if (OB_FAIL(complete_range_.deep_copy(scan_range, range_alloc_))) {
    LOG_WARN("failed to deep copy scan range", KR(ret));
  } else {
    LOG_TRACE("[ADVANCE SCAN] success to switch info", KR(ret), K(*this));
  }
  return ret;
}

int ObAdvanceScanHelper::advance_scan(const ObDatumRange &scan_range)
{
  int ret = OB_SUCCESS;
  int cmp_ret = 0;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret), KP(this), K(lbt()));
  } else if (OB_FAIL(scan_range.end_key_.compare(complete_range_.end_key_, datum_utils_, cmp_ret, false/*compare_datum_cnt*/))) {
    LOG_WARN("failed to compare end_key_", KR(ret), K(scan_range), K_(complete_range));
  } else if (cmp_ret != 0) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("endkey is not same", KR(ret), K(cmp_ret), K(scan_range), K_(complete_range));
  } else if (OB_FAIL(scan_range.start_key_.compare(complete_range_.start_key_, datum_utils_, cmp_ret))) {
    LOG_WARN("failed to compare start_key_", KR(ret), K(scan_range), K_(complete_range));
  } else if (cmp_ret <= 0) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("new startkey is not greater", KR(ret), K(cmp_ret), K(scan_range), K_(complete_range));
  } else if (FALSE_IT(range_alloc_.reuse())) {
  } else if (OB_FAIL(complete_range_.deep_copy(scan_range, range_alloc_))) {
    LOG_WARN("failed to deep copy scan range", KR(ret));
  } else {
    left_border_reached_ = false;
  }
  LOG_DEBUG("[ADVANCE SCAN] advance scan", KR(ret), K(scan_range), K_(complete_range), KPC(this));
  return ret;
}

int ObAdvanceScanHelper::filter_index_node(
    ObMicroIndexInfo &index_info,
    ObAdvanceScanState &prev_state,
    ObAdvanceScanState &state)
{
  int ret = OB_SUCCESS;
  bool state_determined = false;
  const ObCommonDatumRowkey &endkey = index_info.endkey_;
  LOG_DEBUG("[ADVANCE SCAN] try skip data by endkey", K(prev_state), K(state), K(endkey), K_(complete_range),
            KPC(this), K(lbt()));
  if (left_border_reached_) {
    LOG_DEBUG("[ADVANCE SCAN] left border reached, no need to skip", K(prev_state), K(state),
              K(endkey), K_(complete_range), KPC(this));
  } else {
    int64_t left_ne_pos = -1;
    int left_cmp_ret = 0;
    const bool cmp_datum_cnt = true;
    const ObDatumRowkey &left_border = complete_range_.start_key_;
    const ObBorderFlag &border_flag = complete_range_.border_flag_;
    if (OB_UNLIKELY(!complete_range_.is_valid())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected complete range", KR(ret), K(prev_state), K(state), K_(complete_range), K(endkey), KPC(this));
    } else if (OB_FAIL(endkey.compare(left_border, datum_utils_, left_cmp_ret, cmp_datum_cnt))) {
      LOG_WARN("failed to compare left border", KR(ret), K(prev_state), K(state), K(endkey), K_(complete_range), KPC(this));
    } else if (left_cmp_ret < 0 || (0 == left_cmp_ret && !border_flag.inclusive_start())) {
      state_determined = true;
      state.set_state(ObAdvanceScanNodeState::BEFORE_RANGE);
      LOG_TRACE("[ADVANCE SCAN] node is before range", K(prev_state), K(state), K(endkey), K_(complete_range), KPC(this));
    }
    LOG_DEBUG("[ADVANCE SCAN] check left border", KR(ret), K(left_cmp_ret), K(left_ne_pos),
              K(prev_state), K(state), K(endkey), K_(complete_range), KPC(this));
  }
  if (OB_SUCC(ret) && !state_determined) {
    state.set_state(ObAdvanceScanNodeState::MAY_OVERLAP_RANGE);
  }
  prev_state = state;
  return ret;
}

int ObAdvanceScanHelper::seek_to_range(
    ObIMicroBlockRowScanner &micro_scanner,
    ObMicroIndexInfo &index_info,
    const bool first)
{
  int ret = OB_SUCCESS;
  const bool is_left_border = index_info.is_left_border();
  const bool is_right_border = index_info.is_right_border();
  ObAdvanceScanState &state = index_info.advance_scan_state_;
  LOG_DEBUG("[ADVANCE SCAN] seek in micro block", K(first), K(state), K(micro_scanner), KPC(this));
  if (state.is_before_range()) {
    micro_scanner.skip_to_end();
  } else if (left_border_reached_ && !first) {
    state.set_state(ObAdvanceScanNodeState::BEFORE_RANGE);
  } else if (OB_UNLIKELY(!micro_scanner.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected scanner state", KR(ret), K(state), K(micro_scanner), KPC(this));
  } else if (first || -1 == micro_current_) { // -1 == micro_current_ means forward scan in an already opened micro block
    if (OB_FAIL(micro_scanner.end_of_block())) {
      if (OB_UNLIKELY(OB_ITER_END != ret)) {
        LOG_WARN("failed to check end of block", KR(ret), K(micro_scanner));
      } else {
        ret = OB_SUCCESS;
        state.set_state(ObAdvanceScanNodeState::BEFORE_RANGE);
        LOG_DEBUG("[ADVANCE SCAN] micro scanner reachs end", K(state), K(first));
      }
    } else {
      micro_start_ = micro_scanner.get_current_pos();
      micro_last_ = micro_scanner.get_last_pos();
      micro_current_ = micro_start_;
      LOG_DEBUG("[ADVANCE SCAN] micro scanner start", K(micro_start_), K(micro_last_), K(micro_current_), KPC(this));
    }
  }
  if (OB_SUCC(ret) && !left_border_reached_ && !state.is_before_range()) {
    bool has_data = false;
    bool range_covered = false;
    if (OB_FAIL(micro_scanner.skip_to_range(micro_start_, micro_last_, complete_range_, is_left_border, is_right_border,
                                            micro_current_, has_data, range_covered))) {
      LOG_WARN("failed to seek to range", KR(ret), K(micro_scanner), KPC(this));
    } else if (has_data) {
      micro_start_ = MAX(micro_start_, micro_current_);
      left_border_reached_ = true;
    } else {
      micro_start_ = MAX(micro_start_, micro_current_);
      state.set_state(ObAdvanceScanNodeState::BEFORE_RANGE);
    }
    LOG_DEBUG("[ADVANCE SCAN] micro scanner seek to range", KR(ret), K(has_data), K(range_covered), K(state), KPC(this));
  }
  return ret;
}

int ObAdvanceScanHelperFactory::build_advance_scan_helper(
    const ObTableIterParam &iter_param,
    ObTableAccessContext &access_ctx,
    const ObDatumRange *range,
    ObAdvanceScanHelper *&advance_scan_helper)
{
  int ret = OB_SUCCESS;
  const bool is_reverse_scan = access_ctx.query_flag_.is_reverse_scan();
  const ObITableReadInfo *read_info = iter_param.get_read_info();
  ObIAllocator &stmt_allocator = *access_ctx.stmt_allocator_;

  if (OB_UNLIKELY(!iter_param.is_advance_scan())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument to build advance scan helper", KR(ret), K(iter_param), K(lbt()));
  } else if (OB_UNLIKELY(nullptr == range || !range->is_valid() || nullptr == read_info)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument to build advance scan helper", KPC(range), KP(read_info));
  } else if (nullptr != advance_scan_helper) {
    if (OB_FAIL(advance_scan_helper->switch_info(is_reverse_scan, *range, *read_info, stmt_allocator))) {
      LOG_WARN("failed to switch advance scan helper", KR(ret));
    }
  } else if (OB_ISNULL(advance_scan_helper = OB_NEWx(ObAdvanceScanHelper, &stmt_allocator, read_info->get_datum_utils()))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to alloc advance scan helper", KR(ret));
  } else if (OB_FAIL(advance_scan_helper->init(is_reverse_scan, *range, *read_info, stmt_allocator))) {
    LOG_WARN("failed to init advance scan helper", KR(ret));
  }
  if (OB_FAIL(ret) && nullptr != advance_scan_helper) {
    advance_scan_helper->~ObAdvanceScanHelper();
    stmt_allocator.free(advance_scan_helper);
    advance_scan_helper = nullptr;
  }
  return ret;
}

void ObAdvanceScanHelperFactory::destroy_advance_scan_helper(ObAdvanceScanHelper *&advance_scan_helper)
{
  if (nullptr != advance_scan_helper) {
    ObIAllocator *stmt_allocator = advance_scan_helper->get_stmt_alloc();
    advance_scan_helper->~ObAdvanceScanHelper();
    if (nullptr != stmt_allocator) {
      stmt_allocator->free(advance_scan_helper);
    }
    advance_scan_helper = nullptr;
  }
}

}
}
