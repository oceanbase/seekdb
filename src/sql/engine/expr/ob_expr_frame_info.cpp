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

#define USING_LOG_PREFIX SQL_ENG
#include "ob_expr_frame_info.h"
#include "native_frame_value_codec.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"
#include "sql/engine/px/ob_px_util.h"
#include "sql/ob_sql_context.h"
#include <algorithm>

namespace oceanbase
{
using namespace common;
namespace sql
{

OB_SERIALIZE_MEMBER(ObFrameInfo,
                    expr_cnt_,
                    frame_idx_,
                    frame_size_,
                    zero_init_pos_,
                    zero_init_size_);

namespace
{
constexpr int32_t NATIVE_FRAME_WIRE_VERSION = 2;
constexpr int32_t PHYSICAL_ARRAY_FRAME_WIRE_VERSION = 1;

const NativeI64PointLayout::Frame *find_native_i64_frame(
    const ObExprFrameInfo &expressions, uint32_t index)
{
  const NativeI64PointLayout *layout = expressions.native_i64_point_layout_;
  const NativeI64PointLayout::Frame *result = nullptr;
  if (layout != nullptr) {
    for (uint32_t i = 0; i < layout->frame_count; ++i) {
      if (layout->frames[i].index == index) { result = &layout->frames[i]; }
    }
  }
  return result;
}

int validate_wire_frame(const ObFrameInfo &info, int64_t frame_count);

int validate_parameter_alias_metadata(const ObExprFrameInfo &expressions,
    const ObIArray<ObFrameInfo> &group, int64_t parameter_count = -1,
    int64_t original_count = -1, bool allow_missing_aliases = false)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < group.count(); ++i) {
    for (int64_t j = 0; OB_SUCC(ret) && j < expressions.rt_exprs_.count(); ++j) {
      const ObExpr &expr = expressions.rt_exprs_.at(j);
      if (expr.frame_idx_ != group.at(i).frame_idx_ || expr.sql_type_ == nullptr) { continue; }
      const int64_t slot = expr.datum_off_ / ScalarFrameLayout::slot_size();
      if ((!allow_missing_aliases && !expr.native_parameter_alias_)
          || expr.type_ != T_QUESTIONMARK || expr.is_batch_result() || expr.eval_func_ != nullptr
          || expr.datum_off_ != ScalarFrameLayout::datum_offset(slot)
          || expr.eval_info_off_ != ScalarFrameLayout::eval_info_offset(slot)
          || slot >= group.at(i).expr_cnt_) {
        ret = OB_INVALID_ARGUMENT;
      } else if (parameter_count >= 0) {
        int64_t actual_page = 0, actual_slot = 0;
        if (expr.extra_ >= static_cast<uint64_t>(parameter_count)) { ret = OB_INVALID_ARGUMENT; }
        else {
          ScalarFrameLayout::locate_parameter(original_count, expr.extra_, actual_page, actual_slot);
          if (actual_page != i || actual_slot != slot) { ret = OB_INVALID_ARGUMENT; }
        }
      }
    }
  }
  return ret;
}

int validate_parameter_pages(const ObIArray<ObFrameInfo> &group,
    const ObIArray<char *> &pages)
{
  int ret = OB_SUCCESS;
  if (pages.count() < group.count()) { ret = OB_INVALID_ARGUMENT; }
  for (int64_t i = 0; OB_SUCC(ret) && i < group.count(); ++i) {
    if (pages.at(i) == nullptr
        || ParameterValuePage::from_handle(pages.at(i)).capacity() < group.at(i).expr_cnt_) {
      ret = OB_INVALID_ARGUMENT;
    }
  }
  return ret;
}

int validate_execution_frame_directory(const ObExprFrameInfo &expressions)
{
  int ret = OB_SUCCESS;
  const ObIArray<ObFrameInfo> *groups[] = {&expressions.const_frame_, &expressions.param_frame_,
                                          &expressions.dynamic_frame_, &expressions.datum_frame_};
  int64_t count = 0;
  for (int64_t group = 0; OB_SUCC(ret) && group < 4; ++group) {
    if (groups[group]->count() > INT64_MAX - count) { ret = OB_INVALID_ARGUMENT; }
    else { count += groups[group]->count(); }
  }
  if (expressions.const_frame_ptrs_.count() != expressions.const_frame_.count()
      || count > INT64_MAX / static_cast<int64_t>(sizeof(char *))) {
    ret = OB_INVALID_ARGUMENT;
  }
  for (int64_t group = 0; OB_SUCC(ret) && group < 4; ++group) {
    for (int64_t i = 0; OB_SUCC(ret) && i < groups[group]->count(); ++i) {
      const ObFrameInfo &info = groups[group]->at(i);
      if (OB_FAIL(validate_wire_frame(info, count))) {
      } else if (group == 0 && expressions.const_frame_ptrs_.at(i) == nullptr) {
        ret = OB_INVALID_ARGUMENT;
      }
      for (int64_t previous_group = 0; OB_SUCC(ret) && previous_group <= group; ++previous_group) {
        const int64_t limit = previous_group == group ? i : groups[previous_group]->count();
        for (int64_t previous = 0; OB_SUCC(ret) && previous < limit; ++previous) {
          if (groups[previous_group]->at(previous).frame_idx_ == info.frame_idx_) {
            ret = OB_INVALID_ARGUMENT;
          }
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    ret = validate_parameter_alias_metadata(expressions, expressions.param_frame_);
  }
  return ret;
}

int validate_native_i64_layout_impl(const ObExprFrameInfo &expressions)
{
  const NativeI64PointLayout *layout = expressions.native_i64_point_layout_;
  if (layout == nullptr) {
    for (int64_t i = 0; i < expressions.rt_exprs_.count(); ++i) {
      if (expressions.rt_exprs_.at(i).native_i64_layout_ != nullptr) { return OB_ERR_UNEXPECTED; }
    }
    return OB_SUCCESS;
  }
  if (layout->frame_count == 0 || layout->frame_count > NativeI64PointLayout::LANE_COUNT
      || expressions.is_mark_serialize()) { return OB_ERR_UNEXPECTED; }
  const int64_t prefix = expressions.const_frame_.count() + expressions.param_frame_.count()
      + expressions.dynamic_frame_.count();
  for (int64_t i = 0; i < NativeI64PointLayout::KEY_COUNT; ++i) {
    if (layout->parameter_indexes[i] < 0) { return OB_ERR_UNEXPECTED; }
  }
  for (int64_t i = 0; i < NativeI64PointLayout::LANE_COUNT; ++i) {
    if (layout->expr_indexes[i] >= expressions.rt_exprs_.count()) { return OB_ERR_UNEXPECTED; }
    const ObExpr &expr = expressions.rt_exprs_.at(layout->expr_indexes[i]);
    if (expr.native_i64_layout_ != &layout->lanes[i] || expr.sql_type_ == nullptr
        || expr.native_parameter_alias_ || expr.res_buf_len_ != 0
        || expr.dyn_buf_header_offset_ != 0
        || (i < NativeI64PointLayout::KEY_COUNT ? expr.type_ != T_REF_COLUMN : expr.type_ != T_OP_EQ)
        || layout->lanes[i].capacity == 0
        || (!expr.is_batch_result() && layout->lanes[i].capacity != 1)
        || find_native_i64_frame(expressions, expr.frame_idx_) == nullptr) { return OB_ERR_UNEXPECTED; }
    for (int64_t j = 0; j < i; ++j) {
      if (layout->expr_indexes[j] == layout->expr_indexes[i]) { return OB_ERR_UNEXPECTED; }
    }
  }
  for (uint32_t f = 0; f < layout->frame_count; ++f) {
    const auto &frame = layout->frames[f];
    if (frame.index < prefix || frame.index - prefix >= expressions.datum_frame_.count()
        || expressions.datum_frame_.at(frame.index - prefix).frame_idx_ != frame.index
        || expressions.datum_frame_.at(frame.index - prefix).frame_size_ != frame.generic_size) {
      return OB_ERR_UNEXPECTED;
    }
    for (uint32_t previous = 0; previous < f; ++previous) {
      if (layout->frames[previous].index == frame.index) { return OB_ERR_UNEXPECTED; }
    }
    for (int mode = 0; mode < 2; ++mode) {
      struct Region { uint64_t begin; uint64_t end; } regions[1 + 5 * NativeI64PointLayout::LANE_COUNT];
      int64_t count = 1, lanes = 0;
      regions[0] = {0, sizeof(NativeI64FrameHeader)};
      const uint64_t bytes = mode == 0 ? frame.generic_size : frame.compact_size;
      for (int64_t i = 0; i < expressions.rt_exprs_.count(); ++i) {
        const ObExpr &expr = expressions.rt_exprs_.at(i);
        if (expr.frame_idx_ != frame.index) { continue; }
        const NativeI64ExprLayout *lane = expr.native_i64_layout_;
        if (lane == nullptr || lanes == NativeI64PointLayout::LANE_COUNT) { return OB_ERR_UNEXPECTED; }
        bool owned = false;
        for (int64_t k = 0; k < NativeI64PointLayout::LANE_COUNT; ++k) {
          owned |= layout->expr_indexes[k] == i && lane == &layout->lanes[k];
        }
        if (!owned) { return OB_ERR_UNEXPECTED; }
        ++lanes;
        const uint64_t value_off = mode == 0 ? expr.datum_off_ : lane->value_off;
        const uint64_t eval_off = mode == 0 ? expr.eval_info_off_ : lane->eval_info_off;
        const uint64_t flags_off = mode == 0 ? expr.eval_flags_off_ : lane->eval_flags_off;
        const uint64_t skip_off = mode == 0 ? expr.pvt_skip_off_ : lane->pvt_skip_off;
        const uint64_t bitmap = ObBitVector::memory_size(lane->capacity);
        if (value_off % 8 != 0 || eval_off % alignof(ObEvalInfo) != 0
            || (expr.is_batch_result() && (flags_off % 8 != 0 || skip_off % 8 != 0))
            || (mode == 0 && expr.is_batch_result()
                && expr.pvt_skip_off_ != expr.datum_off_ + sizeof(Datum) * lane->capacity)) {
          return OB_ERR_UNEXPECTED;
        }
        regions[count++] = {value_off, value_off + (mode == 0 ? sizeof(Datum) : sizeof(int64_t)) * lane->capacity};
        regions[count++] = {eval_off, eval_off + sizeof(ObEvalInfo)};
        if (expr.is_batch_result()) {
          regions[count++] = {flags_off, flags_off + bitmap};
          regions[count++] = {skip_off, skip_off + bitmap};
        }
        if (mode != 0) { regions[count++] = {lane->state_off, uint64_t(lane->state_off) + lane->capacity}; }
      }
      if (lanes == 0 || bytes % 8 != 0) { return OB_ERR_UNEXPECTED; }
      for (int64_t i = 0; i < count; ++i) {
        if (regions[i].end < regions[i].begin || regions[i].end > bytes) { return OB_ERR_UNEXPECTED; }
        for (int64_t j = 0; j < i; ++j) {
          if (regions[i].begin < regions[j].end && regions[j].begin < regions[i].end) { return OB_ERR_UNEXPECTED; }
        }
      }
    }
  }
  for (int64_t i = 0; i < expressions.rt_exprs_.count(); ++i) {
    const ObExpr &expr = expressions.rt_exprs_.at(i);
    if (expr.native_i64_layout_ != nullptr && find_native_i64_frame(expressions, expr.frame_idx_) == nullptr) {
      return OB_ERR_UNEXPECTED;
    }
  }
  return validate_execution_frame_directory(expressions);
}

void initialize_native_i64_frame(const ObExprFrameInfo &expressions,
    const NativeI64PointLayout::Frame &description, char *frame, bool compact)
{
  MEMSET(frame, 0, compact ? description.compact_size : description.generic_size);
  new (frame) NativeI64FrameHeader{NativeI64FrameHeader::MAGIC, NativeI64FrameHeader::VERSION,
                                  static_cast<uint8_t>(compact), 0};
  for (int64_t i = 0; i < NativeI64PointLayout::LANE_COUNT; ++i) {
    const ObExpr &expr = expressions.rt_exprs_.at(expressions.native_i64_point_layout_->expr_indexes[i]);
    if (expr.frame_idx_ != description.index) { continue; }
    const NativeI64ExprLayout &lane = *expr.native_i64_layout_;
    for (uint32_t row = 0; row < lane.capacity; ++row) {
      if (compact) {
        new (frame + lane.value_off + sizeof(int64_t) * row) int64_t(0);
        new (frame + lane.state_off + row) uint8_t(static_cast<uint8_t>(Datum::State::Null));
      } else { new (frame + expr.datum_off_ + sizeof(Datum) * row) Datum(); }
    }
    new (frame + (compact ? lane.eval_info_off : expr.eval_info_off_)) ObEvalInfo{};
  }
}

bool wire_expr_selected(const ObExprFrameInfo &expressions, int64_t index, uint32_t frame_index)
{
  const ObExpr &expr = expressions.rt_exprs_.at(index);
  return expr.frame_idx_ == frame_index && (expr.type_ != T_INVALID || expr.sql_type_ != nullptr)
      && (!expressions.is_mark_serialize()
          || (index < expressions.ser_expr_marks_.count() && expressions.ser_expr_marks_.at(index)));
}

bool first_wire_expr(const ObExprFrameInfo &expressions, int64_t index, uint32_t frame_index)
{
  bool first = wire_expr_selected(expressions, index, frame_index);
  for (int64_t i = 0; first && i < index; ++i) {
    if (wire_expr_selected(expressions, i, frame_index)
        && expressions.rt_exprs_.at(i).datum_off_ == expressions.rt_exprs_.at(index).datum_off_) {
      first = false;
    }
  }
  return first;
}

int validate_wire_frame(const ObFrameInfo &info, int64_t frame_count)
{
  // Partial plans intentionally retain the original expr count and zero-init
  // range while shrinking frame_size_. Only actual selected Exprs define the
  // readable/writable regions below; neither historical field is a byte span.
  return frame_count < 0 || info.frame_idx_ >= frame_count || info.frame_size_ > UINT32_MAX
      || (info.expr_cnt_ > 0 && info.frame_size_ == 0)
      ? OB_DESERIALIZE_ERROR : OB_SUCCESS;
}

int wire_expr_extent(const ObFrameInfo &info, const ObExpr &expr, uint64_t &count)
{
  count = 1;
  int ret = OB_SUCCESS;
  if (expr.sql_type_ == nullptr) { ret = OB_ERR_UNEXPECTED; }
  else {
    uint32_t capacity = 0;
    if (OB_SUCCESS != expr.get_generic_frame_value_capacity(capacity)) {
      ret = OB_DESERIALIZE_ERROR;
    } else { count = capacity; }
  }
  if (OB_SUCC(ret) && (expr.datum_off_ % alignof(Datum) != 0
      || expr.datum_off_ > info.frame_size_
      || count > (info.frame_size_ - expr.datum_off_) / sizeof(Datum)
      || expr.eval_info_off_ % alignof(ObEvalInfo) != 0
      || expr.eval_info_off_ > info.frame_size_
      || sizeof(ObEvalInfo) > info.frame_size_ - expr.eval_info_off_
      || (expr.eval_info_off_ < expr.datum_off_ + count * sizeof(Datum)
          && expr.eval_info_off_ + sizeof(ObEvalInfo) > expr.datum_off_)
      || (expr.res_buf_len_ > 0 && (expr.res_buf_off_ > info.frame_size_
          || count > (info.frame_size_ - expr.res_buf_off_) / expr.res_buf_len_)))) {
    ret = OB_DESERIALIZE_ERROR;
  }
  if (OB_SUCC(ret) && expr.is_batch_result()) {
    const int64_t bitmap = ObBitVector::memory_size(count);
    if (expr.pvt_skip_off_ > info.frame_size_ || bitmap > info.frame_size_ - expr.pvt_skip_off_
        || expr.eval_flags_off_ > info.frame_size_ || bitmap > info.frame_size_ - expr.eval_flags_off_) {
      ret = OB_DESERIALIZE_ERROR;
    }
  }
  if (OB_SUCC(ret) && ObDynReserveBuf::supported(expr.datum_meta_.type_)
      && (expr.res_buf_len_ > 0 || expr.is_batch_result())) {
    if (expr.dyn_buf_header_offset_ % alignof(ObDynReserveBuf) != 0
        || expr.dyn_buf_header_offset_ > info.frame_size_
        || count > (info.frame_size_ - expr.dyn_buf_header_offset_) / sizeof(ObDynReserveBuf)) {
      ret = OB_DESERIALIZE_ERROR;
    }
  }
  return ret;
}

int wire_record_count(const ObFrameInfo &info, const ObExprFrameInfo &expressions,
                      bool no_data, int64_t &records)
{
  int ret = OB_SUCCESS;
  records = 0;
  for (int64_t i = 0; OB_SUCC(ret) && i < expressions.rt_exprs_.count(); ++i) {
    if (wire_expr_selected(expressions, i, info.frame_idx_)) {
      const ObExpr &expr = expressions.rt_exprs_.at(i);
      uint64_t count = 0;
      if (OB_FAIL(wire_expr_extent(info, expr, count))) {
      } else if (!no_data && count != 1) { ret = OB_NOT_SUPPORTED; }
      else {
        if (first_wire_expr(expressions, i, info.frame_idx_)) { records += no_data ? 0 : 1; }
        for (int64_t j = 0; OB_SUCC(ret) && j < i; ++j) {
          const ObExpr &other = expressions.rt_exprs_.at(j);
          if (wire_expr_selected(expressions, j, info.frame_idx_)) {
            uint64_t other_count = 0;
            if (OB_FAIL(wire_expr_extent(info, other, other_count))) {
            } else if (other.datum_off_ == expr.datum_off_) {
              if (other.eval_info_off_ != expr.eval_info_off_ || other_count != count
                  || !other.get_sql_type().equals(expr.get_sql_type())) { ret = OB_DESERIALIZE_ERROR; }
            } else if ((other.datum_off_ < expr.datum_off_ + count * sizeof(Datum)
                        && expr.datum_off_ < other.datum_off_ + other_count * sizeof(Datum))
                || (other.eval_info_off_ < expr.datum_off_ + count * sizeof(Datum)
                    && expr.datum_off_ < other.eval_info_off_ + sizeof(ObEvalInfo))
                || (expr.eval_info_off_ < other.datum_off_ + other_count * sizeof(Datum)
                    && other.datum_off_ < expr.eval_info_off_ + sizeof(ObEvalInfo))
                || (other.eval_info_off_ < expr.eval_info_off_ + sizeof(ObEvalInfo)
                    && expr.eval_info_off_ < other.eval_info_off_ + sizeof(ObEvalInfo))) {
              ret = OB_DESERIALIZE_ERROR;
            }
          }
        }
      }
    }
  }
  return ret;
}

int prepare_parameter_aliases(const ObExprFrameInfo &expressions,
    const ObIArray<ObFrameInfo> &group, const ObIArray<char *> &pages,
    int64_t parameter_count = -1, int64_t original_count = -1,
    bool restore_private_aliases = false)
{
  int ret = validate_parameter_pages(group, pages);
  if (OB_SUCC(ret)) {
    ret = validate_parameter_alias_metadata(expressions, group, parameter_count, original_count,
        restore_private_aliases);
  }
  // Only a newly decoded, unpublished receiver plan may restore legacy wire
  // flags. Ordinary allocation validates shared cached expressions read-only.
  if (OB_SUCC(ret) && restore_private_aliases) {
    for (int64_t j = 0; j < expressions.rt_exprs_.count(); ++j) {
      ObExpr &expr = expressions.rt_exprs_.at(j);
      for (int64_t i = 0; expr.sql_type_ != nullptr && i < group.count(); ++i) {
        if (expr.frame_idx_ == group.at(i).frame_idx_) { expr.native_parameter_alias_ = true; break; }
      }
    }
  }
  return ret;
}

// Ordinary execution needs the same layout checks as the wire codec, but does
// not need to rediscover every preceding alias for each expression. This
// worklist is private to one allocation call and never becomes plan state.
struct FrameInitCell
{
  int64_t expr_index_;
  uint64_t count_;
  uint32_t datum_off_;
  uint32_t eval_info_off_;
  bool first_;
};

struct FrameInitInterval
{
  uint64_t begin_;
  uint64_t end_;
};

int initialize_execution_frame(const ObFrameInfo &info,
    const ObExprFrameInfo &expressions, char *frame, ObIAllocator &exec_owner)
{
  int ret = OB_SUCCESS;
  int extent_ret = OB_SUCCESS;
  int64_t selected = 0;
  ModulePageAllocator scratch_allocator(exec_owner);
  ObSEArray<FrameInitCell, 16> cells(0, scratch_allocator);
  ObSEArray<FrameInitInterval, 32> intervals(0, scratch_allocator);
  for (int64_t i = 0; OB_SUCC(ret) && i < expressions.rt_exprs_.count(); ++i) {
    if (wire_expr_selected(expressions, i, info.frame_idx_)) {
      if (selected == INT64_MAX / static_cast<int64_t>(sizeof(FrameInitCell))) {
        ret = OB_SIZE_OVERFLOW;
      } else { ++selected; }
    }
  }
  if (OB_SUCC(ret)) { ret = cells.reserve(selected); }
  for (int64_t i = 0; OB_SUCC(ret) && extent_ret == OB_SUCCESS
      && i < expressions.rt_exprs_.count(); ++i) {
    if (wire_expr_selected(expressions, i, info.frame_idx_)) {
      const ObExpr &expr = expressions.rt_exprs_.at(i);
      uint64_t count = 0;
      extent_ret = wire_expr_extent(info, expr, count);
      if (extent_ret == OB_SUCCESS) {
        const FrameInitCell cell = {i, count, expr.datum_off_, expr.eval_info_off_, true};
        ret = cells.push_back(cell);
      }
    }
  }
  if (OB_SUCC(ret) && cells.count() > 1) {
    std::sort(cells.get_data(), cells.get_data() + cells.count(),
        [](const FrameInitCell &left, const FrameInitCell &right) {
          return left.datum_off_ < right.datum_off_
              || (left.datum_off_ == right.datum_off_ && left.expr_index_ < right.expr_index_);
        });
  }
  int64_t unique = 0;
  for (int64_t i = 0; OB_SUCC(ret) && i < cells.count();) {
    const FrameInitCell &first = cells.at(i);
    ++unique;
    int64_t j = i + 1;
    for (; OB_SUCC(ret) && j < cells.count() && cells.at(j).datum_off_ == first.datum_off_; ++j) {
      FrameInitCell &alias = cells.at(j);
      if (alias.eval_info_off_ != first.eval_info_off_ || alias.count_ != first.count_
          || !expressions.rt_exprs_.at(first.expr_index_).get_sql_type().equals(
              expressions.rt_exprs_.at(alias.expr_index_).get_sql_type())) {
        ret = OB_DESERIALIZE_ERROR;
      } else { alias.first_ = false; }
    }
    i = j;
  }
  if (OB_SUCC(ret) && unique > INT64_MAX / (2 * static_cast<int64_t>(sizeof(FrameInitInterval)))) {
    ret = OB_SIZE_OVERFLOW;
  } else if (OB_SUCC(ret)) { ret = intervals.reserve(2 * unique); }
  for (int64_t i = 0; OB_SUCC(ret) && i < cells.count(); ++i) {
    const FrameInitCell &cell = cells.at(i);
    if (cell.first_) {
      // wire_expr_extent already checked the arithmetic, individual bounds,
      // and each expression's own Datum/EvalInfo non-overlap. Only these two
      // regions participate in the existing cross-expression overlap policy.
      const FrameInitInterval datum = {cell.datum_off_, cell.datum_off_ + cell.count_ * sizeof(Datum)};
      const FrameInitInterval eval = {cell.eval_info_off_, cell.eval_info_off_ + sizeof(ObEvalInfo)};
      if (OB_FAIL(intervals.push_back(datum))) {
      } else { ret = intervals.push_back(eval); }
    }
  }
  if (OB_SUCC(ret) && intervals.count() > 1) {
    std::sort(intervals.get_data(), intervals.get_data() + intervals.count(),
        [](const FrameInitInterval &left, const FrameInitInterval &right) {
          return left.begin_ < right.begin_ || (left.begin_ == right.begin_ && left.end_ < right.end_);
        });
    uint64_t end = intervals.at(0).end_;
    for (int64_t i = 1; OB_SUCC(ret) && i < intervals.count(); ++i) {
      if (intervals.at(i).begin_ < end) { ret = OB_DESERIALIZE_ERROR; }
      else { end = intervals.at(i).end_; }
    }
  }
  // The old loop rejects an invalid earlier pair before reaching a later
  // extent failure. Check only the successfully validated prefix above, then
  // propagate that later failure if the prefix itself was valid.
  if (OB_SUCC(ret)) { ret = extent_ret; }
  if (OB_SUCC(ret) && (info.frame_size_ > INT64_MAX || (info.frame_size_ > 0 && frame == nullptr))) {
    ret = OB_INVALID_ARGUMENT;
  }
  if (OB_SUCC(ret)) {
    if (cells.count() > 1) {
      std::sort(cells.get_data(), cells.get_data() + cells.count(),
          [](const FrameInitCell &left, const FrameInitCell &right) {
            return left.expr_index_ < right.expr_index_;
          });
    }
    // Every fallible check and scratch allocation precedes the first write.
    if (info.frame_size_ > 0) { MEMSET(frame, 0, info.frame_size_); }
    for (int64_t i = 0; i < cells.count(); ++i) {
      const FrameInitCell &cell = cells.at(i);
      if (cell.first_) {
        for (uint64_t row = 0; row < cell.count_; ++row) {
          new (frame + cell.datum_off_ + row * sizeof(Datum)) Datum();
        }
        new (frame + cell.eval_info_off_) ObEvalInfo{};
      }
    }
  }
  return ret;
}

int add_wire_size(int64_t increment, int64_t &size)
{
  return increment < 0 || __builtin_add_overflow(size, increment, &size) ? OB_SIZE_OVERFLOW : OB_SUCCESS;
}

int64_t parameter_page_slots(int64_t page, int64_t count, int64_t original)
{
  const int64_t original_pages = ScalarFrameLayout::frame_count(original);
  const int64_t local_page = page < original_pages ? page : page - original_pages;
  const int64_t local_count = page < original_pages ? original : count - original;
  return std::min(ScalarFrameLayout::slots_per_frame(),
                  local_count - local_page * ScalarFrameLayout::slots_per_frame());
}
} // namespace

int NativeFrameCodec::metadata_size(const ObIArray<ObFrameInfo> &group, int64_t &size)
{
  int ret = OB_SUCCESS;
  int64_t candidate = 8;
  for (int64_t i = 0; OB_SUCC(ret) && i < group.count(); ++i) {
    ret = add_wire_size(group.at(i).get_serialize_size(), candidate);
  }
  if (OB_SUCC(ret)) { size = candidate; }
  return ret;
}

int NativeFrameCodec::serialize_metadata(const ObIArray<ObFrameInfo> &group,
    char *buf, int64_t length, int64_t &pos)
{
  int64_t bytes = 0;
  int ret = metadata_size(group, bytes);
  if (OB_FAIL(ret)) {
  } else if (buf == nullptr || pos < 0 || pos > length) { ret = OB_INVALID_ARGUMENT; }
  else if (bytes > length - pos) { ret = OB_BUF_NOT_ENOUGH; }
  else {
    int64_t cursor = pos;
    ret = serialization::encode_i64(buf, length, cursor, group.count());
    for (int64_t i = 0; OB_SUCC(ret) && i < group.count(); ++i) {
      ret = group.at(i).serialize(buf, length, cursor);
    }
    if (OB_SUCC(ret)) { pos = cursor; }
  }
  return ret;
}

int NativeFrameCodec::deserialize_metadata(const char *buf, int64_t length, int64_t &pos,
    ObIArray<ObFrameInfo> &group, int64_t frame_count)
{
  int ret = OB_SUCCESS;
  int64_t cursor = pos, count = 0;
  ObSEArray<ObFrameInfo, 4> candidate;
  if (buf == nullptr || pos < 0 || pos > length || length - pos < 8) { ret = OB_DESERIALIZE_ERROR; }
  else if (OB_FAIL(serialization::decode_i64(buf, length, cursor, &count))) {
  } else if (count < 0 || count > frame_count || count > (length - cursor) / 6) { ret = OB_DESERIALIZE_ERROR; }
  else if (OB_FAIL(candidate.reserve(count))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
      ObFrameInfo info;
      if (OB_FAIL(info.deserialize(buf, length, cursor))) {
      } else if (OB_FAIL(validate_wire_frame(info, frame_count))) {
      } else {
        for (int64_t j = 0; OB_SUCC(ret) && j < candidate.count(); ++j) {
          if (candidate.at(j).frame_idx_ == info.frame_idx_) { ret = OB_DESERIALIZE_ERROR; }
        }
        if (OB_SUCC(ret)) { ret = candidate.push_back(info); }
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(group.reserve(candidate.count()))) {
    } else if (OB_SUCC(ret) && OB_FAIL(group.assign(candidate))) {
    } else if (OB_SUCC(ret)) { pos = cursor; }
  }
  return ret;
}

int NativeFrameCodec::group_size(bool plan_part, const ObIArray<ObFrameInfo> &group,
    const ObExprFrameInfo &expressions, char *const *frames, int64_t frame_count,
    bool no_data, int64_t &size)
{
  int ret = expressions.validate_native_i64_layout();
  int64_t candidate = 5;
  if (OB_SUCC(ret) && plan_part) {
    int64_t bytes = 0;
    if (OB_FAIL(metadata_size(group, bytes))) {
    } else { ret = add_wire_size(bytes, candidate); }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < group.count(); ++i) {
    const ObFrameInfo &info = group.at(i);
    int64_t records = 0;
    if (OB_FAIL(validate_wire_frame(info, frame_count))) {
    } else if (frames == nullptr || (info.frame_size_ > 0 && frames[info.frame_idx_] == nullptr)) {
      ret = OB_INVALID_ARGUMENT;
    } else if (find_native_i64_frame(expressions, info.frame_idx_) != nullptr) {
      const auto &header = *reinterpret_cast<const NativeI64FrameHeader *>(frames[info.frame_idx_]);
      if (header.magic != NativeI64FrameHeader::MAGIC || header.version != NativeI64FrameHeader::VERSION
          || header.reserved != 0 || header.compact > 1) { ret = OB_ERR_UNEXPECTED; }
      else if (header.compact != 0 && !no_data) {
        // Full native payload wire is not part of this local SELECT slice.
        // Cursor/PL executes the real generic alternative before reaching here.
        ret = OB_NOT_SUPPORTED;
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(wire_record_count(info, expressions, no_data, records))) {
    } else if (OB_FAIL(add_wire_size(8, candidate))) {
    } else if (!no_data) {
      for (int64_t j = 0; OB_SUCC(ret) && j < expressions.rt_exprs_.count(); ++j) {
        if (first_wire_expr(expressions, j, info.frame_idx_)) {
          const ObExpr &expr = expressions.rt_exprs_.at(j);
          const Datum &value = *reinterpret_cast<const Datum *>(frames[info.frame_idx_] + expr.datum_off_);
          int64_t bytes = 0;
          if (OB_FAIL(NativeFrameValueCodec::size(expr.get_sql_type(), value, bytes))) {
          } else if (OB_FAIL(add_wire_size(12, candidate))) {
          } else { ret = add_wire_size(bytes, candidate); }
        }
      }
    }
    for (int64_t j = 0; OB_SUCC(ret) && j < i; ++j) {
      if (group.at(j).frame_idx_ == info.frame_idx_) { ret = OB_DESERIALIZE_ERROR; }
    }
  }
  if (OB_SUCC(ret)) { size = candidate; }
  return ret;
}

int NativeFrameCodec::serialize_group(char *buf, int64_t length, int64_t &pos, bool plan_part,
    const ObIArray<ObFrameInfo> &group, const ObExprFrameInfo &expressions,
    char *const *frames, int64_t frame_count, bool no_data)
{
  int64_t bytes = 0;
  int ret = group_size(plan_part, group, expressions, frames, frame_count, no_data, bytes);
  if (OB_FAIL(ret)) { return ret; }
  if (buf == nullptr || pos < 0 || pos > length) { return OB_INVALID_ARGUMENT; }
  if (bytes > length - pos) { return OB_BUF_NOT_ENOUGH; }
  int64_t cursor = pos;
  if (OB_FAIL(serialization::encode_i32(buf, length, cursor, NATIVE_FRAME_WIRE_VERSION))) {
  } else if (OB_FAIL(serialization::encode_i8(buf, length, cursor, no_data ? 1 : 0))) {
  } else if (plan_part) { ret = serialize_metadata(group, buf, length, cursor); }
  for (int64_t i = 0; OB_SUCC(ret) && i < group.count(); ++i) {
    const ObFrameInfo &info = group.at(i);
    int64_t records = 0;
    if (OB_FAIL(wire_record_count(info, expressions, no_data, records))) {
    } else if (OB_FAIL(serialization::encode_i64(buf, length, cursor, records))) {
    } else if (!no_data) {
      for (int64_t j = 0; OB_SUCC(ret) && j < expressions.rt_exprs_.count(); ++j) {
        if (first_wire_expr(expressions, j, info.frame_idx_)) {
          const ObExpr &expr = expressions.rt_exprs_.at(j);
          const char *frame = frames[info.frame_idx_];
          const ObEvalInfo &eval = *reinterpret_cast<const ObEvalInfo *>(frame + expr.eval_info_off_);
          const Datum &value = *reinterpret_cast<const Datum *>(frame + expr.datum_off_);
          if (OB_FAIL(serialization::encode_i64(buf, length, cursor, j))) {
          } else if (OB_FAIL(serialization::encode_i16(buf, length, cursor, eval.flag_))) {
          } else if (OB_FAIL(serialization::encode_i16(buf, length, cursor, eval.cnt_))) {
          } else { ret = NativeFrameValueCodec::encode(expr.get_sql_type(), value, buf, length, cursor); }
        }
      }
    }
  }
  if (OB_SUCC(ret)) { pos = cursor; }
  return ret;
}

int NativeFrameCodec::initialize_frame(const ObFrameInfo &info,
    const ObExprFrameInfo &expressions, char *frame)
{
  int64_t unused = 0;
  int ret = expressions.validate_native_i64_layout();
  if (OB_SUCC(ret)) { ret = wire_record_count(info, expressions, true, unused); }
  if (OB_SUCC(ret) && (info.frame_size_ > INT64_MAX || (info.frame_size_ > 0 && frame == nullptr))) {
    ret = OB_INVALID_ARGUMENT;
  }
  if (OB_SUCC(ret)) {
    if (info.frame_size_ > 0) { MEMSET(frame, 0, info.frame_size_); }
    if (find_native_i64_frame(expressions, info.frame_idx_) != nullptr) {
      // A decoded frame always has the ordinary wire representation. A local
      // clone may still own both plan descriptions, so publish its generic tag.
      new (frame) NativeI64FrameHeader{NativeI64FrameHeader::MAGIC,
          NativeI64FrameHeader::VERSION, 0, 0};
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < expressions.rt_exprs_.count(); ++i) {
      if (first_wire_expr(expressions, i, info.frame_idx_)) {
        const ObExpr &expr = expressions.rt_exprs_.at(i);
        uint64_t count = 0;
        if (OB_FAIL(wire_expr_extent(info, expr, count))) {
        } else {
          for (uint64_t row = 0; row < count; ++row) {
            new (frame + expr.datum_off_ + row * sizeof(Datum)) Datum();
          }
          new (frame + expr.eval_info_off_) ObEvalInfo{};
        }
      }
    }
  }
  return ret;
}

int NativeFrameCodec::deserialize_group(const char *buf, int64_t length, int64_t &pos,
    ObIAllocator &owner, bool plan_part, const ObIArray<ObFrameInfo> &group,
    const ObExprFrameInfo &expressions, ObIArray<char *> *pointers,
    char **frames, int64_t frame_count, bool no_data)
{
  int ret = OB_SUCCESS;
  int64_t cursor = pos;
  int32_t version = 0;
  int8_t empty = 0;
  ObSEArray<ObFrameInfo, 4> decoded_group;
  ObSEArray<char *, 4> decoded_frames;
  if (buf == nullptr || pos < 0 || pos > length || length - pos < 5 || frame_count < 0
      || (frame_count > 0 && frames == nullptr)) { return OB_DESERIALIZE_ERROR; }
  if (OB_FAIL(serialization::decode_i32(buf, length, cursor, &version))) {
  } else if (OB_FAIL(serialization::decode_i8(buf, length, cursor, &empty))) {
  } else if ((version != NATIVE_FRAME_WIRE_VERSION && version != PHYSICAL_ARRAY_FRAME_WIRE_VERSION)
             || empty != (no_data ? 1 : 0)) {
    ret = OB_DESERIALIZE_ERROR;
  } else if (plan_part) { ret = deserialize_metadata(buf, length, cursor, decoded_group, frame_count); }
  else { ret = decoded_group.assign(group); }
  for (int64_t i = 0; OB_SUCC(ret) && i < decoded_group.count(); ++i) {
    const ObFrameInfo &info = decoded_group.at(i);
    int64_t records = 0, expected = 0;
    char *frame = nullptr;
    if (OB_FAIL(validate_wire_frame(info, frame_count))) {
    } else if (frames[info.frame_idx_] != nullptr || length - cursor < 8) { ret = OB_DESERIALIZE_ERROR; }
    else if (OB_FAIL(wire_record_count(info, expressions, no_data, expected))) {
    } else if (OB_FAIL(serialization::decode_i64(buf, length, cursor, &records))) {
    } else if (records != expected) { ret = OB_DESERIALIZE_ERROR; }
    else if (info.frame_size_ > 0 && (frame = static_cast<char *>(owner.alloc(info.frame_size_))) == nullptr) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (OB_FAIL(initialize_frame(info, expressions, frame))) {
    } else if (!no_data) {
      for (int64_t j = 0; OB_SUCC(ret) && j < expressions.rt_exprs_.count(); ++j) {
        if (first_wire_expr(expressions, j, info.frame_idx_)) {
          const ObExpr &expr = expressions.rt_exprs_.at(j);
          int64_t index = 0;
          int16_t flags = 0, count = 0;
          const SqlType *type = nullptr;
          Datum value;
          if (length - cursor < 12) { ret = OB_DESERIALIZE_ERROR; }
          else if (OB_FAIL(serialization::decode_i64(buf, length, cursor, &index))) {
          } else if (index != j) { ret = OB_DESERIALIZE_ERROR; }
          else if (OB_FAIL(serialization::decode_i16(buf, length, cursor, &flags))) {
          } else if (OB_FAIL(serialization::decode_i16(buf, length, cursor, &count))) {
          } else if (OB_FAIL(NativeFrameValueCodec::decode(buf, length, cursor, owner, type, value, version))) {
          } else if (!type->equals(expr.get_sql_type())) { ret = OB_DESERIALIZE_ERROR; }
          else {
            *reinterpret_cast<Datum *>(frame + expr.datum_off_) = value;
            ObEvalInfo &eval = *reinterpret_cast<ObEvalInfo *>(frame + expr.eval_info_off_);
            eval.flag_ = static_cast<uint16_t>(flags); eval.cnt_ = static_cast<uint16_t>(count);
            // Reference bytes now belong to the receiving owner, not a copied
            // sender res_buf. The other logical evaluation bits are retained.
            eval.point_to_frame_ = 0;
          }
        }
      }
    }
    for (int64_t j = 0; OB_SUCC(ret) && j < i; ++j) {
      if (decoded_group.at(j).frame_idx_ == info.frame_idx_) { ret = OB_DESERIALIZE_ERROR; }
    }
    if (OB_SUCC(ret)) { ret = decoded_frames.push_back(frame); }
  }
  if (OB_SUCC(ret) && pointers != nullptr) { ret = pointers->reserve(decoded_frames.count()); }
  if (OB_SUCC(ret) && plan_part) {
    ObIArray<ObFrameInfo> &output = const_cast<ObIArray<ObFrameInfo> &>(group);
    if (OB_SUCC(ret = output.reserve(decoded_group.count()))) { ret = output.assign(decoded_group); }
  }
  if (OB_SUCC(ret) && pointers != nullptr) { ret = pointers->assign(decoded_frames); }
  if (OB_SUCC(ret)) {
    for (int64_t i = 0; i < decoded_group.count(); ++i) { frames[decoded_group.at(i).frame_idx_] = decoded_frames.at(i); }
    pos = cursor;
  }
  return ret;
}

int NativeFrameCodec::parameter_size(const ObPhysicalPlanCtx &plan, int64_t &size)
{
  int ret = OB_SUCCESS;
  int64_t candidate = 28; // version, count, original count, sparse group count
  const int64_t count = plan.get_parameter_count();
  if (!plan.native_parameters_ready() || plan.get_original_param_cnt() < 0
      || plan.get_original_param_cnt() > count) { return OB_INVALID_ARGUMENT; }
  for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
    NativeTypedDatum value;
    DatumParameterMetadata metadata;
    int64_t bytes = 0;
    if (OB_FAIL(plan.capture_native_parameter(i, value, metadata))) {
    } else if (OB_FAIL(NativeFrameValueCodec::size(*value.type_, value.datum_, bytes))) {
    } else if (OB_FAIL(add_wire_size(4 + NativeFrameValueCodec::metadata_size(), candidate))) {
    } else { ret = add_wire_size(bytes, candidate); }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < plan.get_native_group_count(); ++i) {
    NativeParameterGroup group;
    int64_t bytes = 0;
    if (OB_FAIL(plan.capture_native_group(i, group))) {
    } else if (OB_FAIL(NativeFrameValueCodec::size(*group.value_.type_, group.value_.datum_, bytes))) {
    } else if (OB_FAIL(add_wire_size(8 + NativeFrameValueCodec::metadata_size(), candidate))) {
    } else { ret = add_wire_size(bytes, candidate); }
  }
  if (OB_SUCC(ret)) { size = candidate; }
  return ret;
}

int NativeFrameCodec::serialize_parameters(const ObPhysicalPlanCtx &plan,
    char *buf, int64_t length, int64_t &pos)
{
  int64_t bytes = 0;
  int ret = parameter_size(plan, bytes);
  if (OB_FAIL(ret)) { return ret; }
  if (buf == nullptr || pos < 0 || pos > length) { return OB_INVALID_ARGUMENT; }
  if (bytes > length - pos) { return OB_BUF_NOT_ENOUGH; }
  int64_t cursor = pos;
  if (OB_FAIL(serialization::encode_i32(buf, length, cursor, NATIVE_FRAME_WIRE_VERSION))) {
  } else if (OB_FAIL(serialization::encode_i64(buf, length, cursor, plan.get_parameter_count()))) {
  } else if (OB_FAIL(serialization::encode_i64(buf, length, cursor, plan.get_original_param_cnt()))) {
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < plan.get_parameter_count(); ++i) {
    NativeTypedDatum value;
    DatumParameterMetadata metadata;
    const ObEvalInfo *eval = plan.get_parameter_eval_info(i);
    if (eval == nullptr) { ret = OB_ERR_UNEXPECTED; }
    else if (OB_FAIL(plan.capture_native_parameter(i, value, metadata))) {
    } else if (OB_FAIL(serialization::encode_i16(buf, length, cursor, eval->flag_))) {
    } else if (OB_FAIL(serialization::encode_i16(buf, length, cursor, eval->cnt_))) {
    } else if (OB_FAIL(NativeFrameValueCodec::encode_metadata(metadata, buf, length, cursor))) {
    } else { ret = NativeFrameValueCodec::encode(*value.type_, value.datum_, buf, length, cursor); }
  }
  if (OB_SUCC(ret)) { ret = serialization::encode_i64(buf, length, cursor, plan.get_native_group_count()); }
  for (int64_t i = 0; OB_SUCC(ret) && i < plan.get_native_group_count(); ++i) {
    NativeParameterGroup group;
    if (OB_FAIL(plan.capture_native_group(i, group))) {
    } else if (OB_FAIL(serialization::encode_i64(buf, length, cursor, group.index_))) {
    } else if (OB_FAIL(NativeFrameValueCodec::encode_metadata(group.attributes_, buf, length, cursor))) {
    } else { ret = NativeFrameValueCodec::encode(*group.value_.type_, group.value_.datum_, buf, length, cursor); }
  }
  if (OB_SUCC(ret)) { pos = cursor; }
  return ret;
}

int NativeFrameCodec::deserialize_parameters(const char *buf, int64_t length, int64_t &pos,
    ObIAllocator &owner, ObIArray<char *> &pages,
    ObIArray<const SqlType *> &types, int64_t &original_count,
    ObIArray<DatumParameterMetadata> *metadata, ObIArray<NativeParameterGroup> *groups,
    int32_t *wire_version)
{
  int ret = OB_SUCCESS;
  int64_t cursor = pos, count = 0, original = 0;
  int32_t version = 0;
  ObSEArray<char *, 4> candidate_pages;
  ObSEArray<const SqlType *, 16> candidate_types;
  ObSEArray<DatumParameterMetadata, 8> candidate_metadata;
  ObSEArray<NativeParameterGroup, 2> candidate_groups;
  if (buf == nullptr || pos < 0 || pos > length || length - pos < 20) { return OB_DESERIALIZE_ERROR; }
  if (OB_FAIL(serialization::decode_i32(buf, length, cursor, &version))) {
  } else if (OB_FAIL(serialization::decode_i64(buf, length, cursor, &count))) {
  } else if (OB_FAIL(serialization::decode_i64(buf, length, cursor, &original))) {
  } else if ((version != NATIVE_FRAME_WIRE_VERSION && version != PHYSICAL_ARRAY_FRAME_WIRE_VERSION)
      || original < 0 || count < original || count > (length - cursor) / 11
      || count > INT64_MAX - ScalarFrameLayout::slots_per_frame()) { ret = OB_DESERIALIZE_ERROR; }
  else {
    const int64_t page_count = ScalarFrameLayout::frame_count(original)
        + ScalarFrameLayout::frame_count(count - original);
    if (OB_FAIL(candidate_pages.reserve(page_count))) {
    } else if (OB_FAIL(candidate_types.reserve(count))) {
    } else if (version >= 2 && OB_FAIL(candidate_metadata.reserve(count))) {
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < page_count; ++i) {
      const int64_t slots = i < ScalarFrameLayout::frame_count(original)
          ? parameter_page_slots(i, count, original) : ScalarFrameLayout::slots_per_frame();
      ParameterValuePage *page = nullptr;
      if (OB_FAIL(ParameterValuePage::allocate(owner, slots, page))) {
      } else { ret = candidate_pages.push_back(reinterpret_cast<char *>(page)); }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
      int64_t page = 0, slot = 0;
      ScalarFrameLayout::locate_parameter(original, i, page, slot);
      ParameterValuePage &frame = ParameterValuePage::from_handle(candidate_pages.at(page));
      Datum value;
      ObEvalInfo &eval = frame.eval_info(slot);
      int16_t flags = 0, rows = 0;
      const SqlType *type = nullptr;
      DatumParameterMetadata attributes;
      if (length - cursor < 4) { ret = OB_DESERIALIZE_ERROR; }
      else if (OB_FAIL(serialization::decode_i16(buf, length, cursor, &flags))) {
      } else if (OB_FAIL(serialization::decode_i16(buf, length, cursor, &rows))) {
      } else if (version >= 2 && OB_FAIL(NativeFrameValueCodec::decode_metadata(buf, length, cursor, attributes))) {
      } else if (OB_FAIL(NativeFrameValueCodec::decode(buf, length, cursor, owner, type, value, version))) {
      } else if (OB_FAIL(frame.assign(slot, value, *type, owner))) {
      } else if (OB_FAIL(candidate_types.push_back(type))) {
      } else if (version >= 2 && OB_FAIL(candidate_metadata.push_back(attributes))) {
      } else {
        eval.flag_ = static_cast<uint16_t>(flags); eval.cnt_ = static_cast<uint16_t>(rows);
        eval.point_to_frame_ = 0;
      }
    }
    if (OB_SUCC(ret) && version >= 2) {
      int64_t group_count = 0;
      if (OB_FAIL(serialization::decode_i64(buf, length, cursor, &group_count))) {
      } else if (group_count < 0 || group_count > count || group_count > (length - cursor) / 39) {
        ret = OB_DESERIALIZE_ERROR;
      } else { ret = candidate_groups.reserve(group_count); }
      for (int64_t i = 0; OB_SUCC(ret) && i < group_count; ++i) {
        NativeParameterGroup group;
        if (OB_FAIL(serialization::decode_i64(buf, length, cursor, &group.index_))) {
        } else if (group.index_ < 0 || group.index_ >= count) { ret = OB_DESERIALIZE_ERROR; }
        else if (OB_FAIL(NativeFrameValueCodec::decode_metadata(buf, length, cursor, group.attributes_))) {
        } else if (OB_FAIL(NativeFrameValueCodec::decode(buf, length, cursor, owner,
            group.value_.type_, group.value_.datum_, version))) {
        } else if (!group.value_.type_->legacy_meta().is_ext_sql_array()) { ret = OB_DESERIALIZE_ERROR; }
        for (int64_t j = 0; OB_SUCC(ret) && j < candidate_groups.count(); ++j) {
          if (candidate_groups.at(j).index_ == group.index_) { ret = OB_DESERIALIZE_ERROR; }
        }
        if (OB_SUCC(ret)) { ret = candidate_groups.push_back(group); }
      }
    }
    // Reserve every output before any externally visible publication.
    if (OB_SUCC(ret) && OB_FAIL(pages.reserve(candidate_pages.count()))) {
    } else if (OB_SUCC(ret) && OB_FAIL(types.reserve(candidate_types.count()))) {
    } else if (OB_SUCC(ret) && metadata != nullptr && OB_FAIL(metadata->reserve(candidate_metadata.count()))) {
    } else if (OB_SUCC(ret) && groups != nullptr && OB_FAIL(groups->reserve(candidate_groups.count()))) {
    } else if (OB_SUCC(ret)) {
      const int a = pages.assign(candidate_pages);
      const int b = types.assign(candidate_types);
      const int c = metadata == nullptr ? OB_SUCCESS : metadata->assign(candidate_metadata);
      const int d = groups == nullptr ? OB_SUCCESS : groups->assign(candidate_groups);
      OB_ASSERT(a == OB_SUCCESS && b == OB_SUCCESS && c == OB_SUCCESS && d == OB_SUCCESS);
      UNUSED(a); UNUSED(b); UNUSED(c); UNUSED(d);
      original_count = original;
      if (wire_version != nullptr) { *wire_version = version; }
      pos = cursor;
    }
  }
  return ret;
}

int NativeFrameCodec::bind_parameter_pages(const ObIArray<ObFrameInfo> &group,
    const ObIArray<char *> &pages, int64_t parameter_count, int64_t original_count,
    char **frames, int64_t frame_count, const ObExprFrameInfo *expressions,
    bool restore_private_aliases)
{
  int ret = OB_SUCCESS;
  if (original_count < 0 || parameter_count < original_count || group.count() > pages.count()
      || frame_count < 0 || (frame_count > 0 && frames == nullptr)) { return OB_INVALID_ARGUMENT; }
  for (int64_t i = 0; OB_SUCC(ret) && i < group.count(); ++i) {
    const ObFrameInfo &info = group.at(i);
    const int64_t slots = parameter_page_slots(i, parameter_count, original_count);
    if (OB_FAIL(validate_wire_frame(info, frame_count))) {
    } else if (slots <= 0 || info.expr_cnt_ > slots || info.frame_size_ > slots * ScalarFrameLayout::slot_size()
               || frames[info.frame_idx_] != nullptr || pages.at(i) == nullptr) { ret = OB_DESERIALIZE_ERROR; }
    else {
      for (int64_t j = 0; OB_SUCC(ret) && j < i; ++j) {
        if (group.at(j).frame_idx_ == info.frame_idx_) { ret = OB_DESERIALIZE_ERROR; }
      }
    }
  }
  if (OB_SUCC(ret) && expressions != nullptr) {
    ret = prepare_parameter_aliases(*expressions, group, pages, parameter_count, original_count,
        restore_private_aliases);
  }
  if (OB_SUCC(ret)) {
    for (int64_t i = 0; i < group.count(); ++i) { frames[group.at(i).frame_idx_] = pages.at(i); }
  }
  return ret;
}

int NativeFrameCodec::clone_constant_frames(const ObExprFrameInfo &source,
    ObIAllocator &owner, ObIArray<char *> &destination)
{
  int ret = OB_SUCCESS;
  ObSEArray<char *, 4> candidate;
  if (source.const_frame_.count() != source.const_frame_ptrs_.count()) { return OB_INVALID_ARGUMENT; }
  for (int64_t i = 0; OB_SUCC(ret) && i < source.const_frame_.count(); ++i) {
    const ObFrameInfo &info = source.const_frame_.at(i);
    char *frame = nullptr;
    int64_t records = 0;
    if (OB_FAIL(validate_wire_frame(info, source.const_frame_.count()))) {
    } else if (OB_FAIL(wire_record_count(info, source, false, records))) {
    } else if (source.const_frame_ptrs_.at(i) == nullptr) { ret = OB_INVALID_ARGUMENT; }
    else if ((frame = static_cast<char *>(owner.alloc(info.frame_size_))) == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
    else if (OB_FAIL(initialize_frame(info, source, frame))) {
    } else {
      for (int64_t j = 0; OB_SUCC(ret) && j < source.rt_exprs_.count(); ++j) {
        if (first_wire_expr(source, j, info.frame_idx_)) {
          const ObExpr &expr = source.rt_exprs_.at(j);
          const Datum &value = *reinterpret_cast<const Datum *>(source.const_frame_ptrs_.at(i) + expr.datum_off_);
          Datum &copy = *reinterpret_cast<Datum *>(frame + expr.datum_off_);
          if (expr.is_batch_result()) { ret = OB_ERR_UNEXPECTED; }
          else if (OB_FAIL(ObExpr::clone_native_value(expr.get_sql_type(), value, owner, copy))) {
          } else {
            ObEvalInfo &eval = *reinterpret_cast<ObEvalInfo *>(frame + expr.eval_info_off_);
            eval = *reinterpret_cast<const ObEvalInfo *>(source.const_frame_ptrs_.at(i) + expr.eval_info_off_);
            eval.point_to_frame_ = 0;
          }
        }
      }
      if (OB_SUCC(ret)) { ret = candidate.push_back(frame); }
    }
  }
  if (OB_SUCC(ret)) { ret = destination.assign(candidate); }
  return ret;
}

int ObExprFrameInfo::assign(const ObExprFrameInfo &other,
                            common::ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  need_ctx_cnt_ = other.need_ctx_cnt_;
  native_i64_point_layout_ = nullptr;
  native_i64_layout_validated_ = false;

  if (OB_FAIL(rt_exprs_.assign(other.rt_exprs_))) {
  } else if (OB_FAIL(const_frame_.assign(other.const_frame_))) {
  } else if (OB_FAIL(param_frame_.assign(other.param_frame_))) {
  } else if (OB_FAIL(dynamic_frame_.assign(other.dynamic_frame_))) {
  } else if (OB_FAIL(datum_frame_.assign(other.datum_frame_))) {
  } else if (OB_FAIL(NativeFrameCodec::clone_constant_frames(other, allocator, const_frame_ptrs_))) {
  } else {
    if (OB_SUCC(ret)) {
      // deep copy extra info & inner function ptrs
      for (int i = 0; OB_SUCC(ret) && i < other.rt_exprs_.count(); i++) {
        rt_exprs_.at(i).native_i64_layout_ = nullptr;
        if (other.rt_exprs_.at(i).type_ == T_INVALID && other.rt_exprs_.at(i).sql_type_ == nullptr) {
          continue; // genuine ObEmptyExpr placeholder in a partial PX plan
        }
        if (OB_FAIL(rt_exprs_.at(i).clone_prepared_metadata_from(other.rt_exprs_.at(i), allocator))) {
          break;
        }
        if (ObExprExtraInfoFactory::is_registered(other.rt_exprs_.at(i).type_)) {
          if (OB_ISNULL(other.rt_exprs_.at(i).extra_info_)) {
            // do nothing
          } else if (OB_FAIL(other.rt_exprs_.at(i).extra_info_->deep_copy(allocator,
                                                                other.rt_exprs_.at(i).type_,
                                                                rt_exprs_.at(i).extra_info_))) {
          }
        }

        // copy inner function ptrs
        if (OB_SUCC(ret) && other.rt_exprs_.at(i).inner_func_cnt_ > 0) {
          int64_t func_cnt = other.rt_exprs_.at(i).inner_func_cnt_;
          void *funcs_buf = NULL;
          if (OB_ISNULL(funcs_buf = allocator.alloc(sizeof(void *) * func_cnt))) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
          } else {
            MEMCPY(funcs_buf, other.rt_exprs_.at(i).inner_functions_, func_cnt * sizeof(void *));
            rt_exprs_.at(i).inner_functions_ = (void **)funcs_buf;
          }
        }
      } // end for
    }
  }
  // allocate args memory for all rt exprs
  if (OB_SUCC(ret)) {
    for (int i = 0; OB_SUCC(ret) && i < rt_exprs_.count(); i++) {
      if (rt_exprs_.at(i).arg_cnt_ > 0) {
        ObExpr **arg_buf = NULL;
        int64_t buf_size = rt_exprs_.at(i).arg_cnt_ * sizeof(ObExpr *);
        if (OB_ISNULL(arg_buf = (ObExpr **)allocator.alloc(buf_size))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
        } else {
          MEMSET(arg_buf, 0, buf_size);
          rt_exprs_.at(i).args_ = arg_buf;
        }
      }

      // allocate parents memory for all rt exprs
      if (OB_SUCC(ret) && rt_exprs_.at(i).parent_cnt_ > 0) {
        ObExpr **parent_buf = NULL;
        int64_t buf_size = rt_exprs_.at(i).parent_cnt_ * sizeof(ObExpr *);
        if (OB_ISNULL(parent_buf = (ObExpr **)allocator.alloc(buf_size))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
        } else {
          MEMSET(parent_buf, 0, buf_size);
          rt_exprs_.at(i).parents_ = parent_buf;
        }
      }
    } // end for
  }
  // replace arg ptrs and parent ptrs for all rt exprs
  for (int i = 0; OB_SUCC(ret) && i < other.rt_exprs_.count(); i++) {
    if (other.rt_exprs_.at(i).arg_cnt_ > 0) {
      for (int j = 0; OB_SUCC(ret) && j < other.rt_exprs_.at(i).arg_cnt_; j++) {
        int64_t pos = other.rt_exprs_.at(i).args_[j] - &(other.rt_exprs_.at(0));
        if (pos >= rt_exprs_.count() || pos < 0) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("rt expr pos is invalid", K(pos), KP(&other.rt_exprs_.at(0)),
                                             KP(other.rt_exprs_.at(i).args_[j]));
        } else {
          rt_exprs_.at(i).args_[j] = &rt_exprs_.at(pos);
        }
      } // end for
    }
    if (other.rt_exprs_.at(i).parent_cnt_ > 0) {
      for (int j = 0; OB_SUCC(ret) && j < other.rt_exprs_.at(i).parent_cnt_; j++) {
        int64_t pos = other.rt_exprs_.at(i).parents_[j] - &(other.rt_exprs_.at(0));
        if (pos >= rt_exprs_.count() || pos < 0) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("rt expr pos is invalid", K(pos), KP(&other.rt_exprs_.at(0)),
                                             KP(other.rt_exprs_.at(i).args_[j]));
        } else {
          rt_exprs_.at(i).parents_[j] = &rt_exprs_.at(pos);
        }
      } // end for
    }
  } // end for

  if (OB_SUCC(ret) && other.native_i64_point_layout_ != nullptr) {
    if (OB_FAIL(other.validate_native_i64_layout())) {
    } else {
      auto *copy = static_cast<NativeI64PointLayout *>(allocator.alloc(sizeof(NativeI64PointLayout)));
      if (copy == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
      else {
        new (copy) NativeI64PointLayout(*other.native_i64_point_layout_);
        for (int64_t i = 0; i < NativeI64PointLayout::LANE_COUNT; ++i) {
          rt_exprs_.at(copy->expr_indexes[i]).native_i64_layout_ = &copy->lanes[i];
        }
        native_i64_point_layout_ = copy;
        ret = validate_and_mark_native_i64_layout();
      }
    }
  }
  return ret;
}

int ObExprFrameInfo::validate_native_i64_layout() const
{
  return validate_native_i64_layout_impl(*this);
}

int ObExprFrameInfo::validate_and_mark_native_i64_layout()
{
  native_i64_layout_validated_ = false;
  const int ret = validate_native_i64_layout_impl(*this);
  if (ret == OB_SUCCESS && native_i64_point_layout_ != nullptr) {
    native_i64_layout_validated_ = true;
  }
  return ret;
}

int ObExprFrameInfo::choose_native_i64_layout(const ObExecContext &exec_ctx, bool &compact) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(native_i64_point_layout_ != nullptr && !native_i64_layout_validated_)) {
    return OB_ERR_UNEXPECTED;
  }
  bool candidate = native_i64_point_layout_ != nullptr;
  const ObPhysicalPlanCtx *plan = exec_ctx.get_physical_plan_ctx();
  const ObSqlCtx *sql_ctx = exec_ctx.get_sql_ctx();
  candidate = candidate && plan != nullptr && plan->native_parameters_ready()
      && plan->get_native_group_count() == 0 && sql_ctx != nullptr
      && !sql_ctx->is_cursor_ && !sql_ctx->is_from_pl_;
  for (int64_t i = 0; candidate && i < NativeI64PointLayout::KEY_COUNT; ++i) {
    const int64_t index = native_i64_point_layout_->parameter_indexes[i];
    int64_t value = 0;
    Datum::State state = Datum::State::Null;
    candidate = plan->read_native_bigint_parameter(index, value, state);
  }
  compact = candidate;
  return ret;
}

int ObExprFrameInfo::pre_alloc_exec_memory(ObExecContext &exec_ctx, ObIAllocator *allocator) const
{
  int ret = OB_SUCCESS;
  uint64_t frame_cnt = 0;
  char **frames = NULL;
  ObPhysicalPlanCtx *phy_ctx = NULL;
  bool compact_i64 = false;
  if (NULL == (phy_ctx = exec_ctx.get_physical_plan_ctx())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(choose_native_i64_layout(exec_ctx, compact_i64))) {
  } else if (OB_FAIL(alloc_frame_after_layout_validation(allocator != NULL ? *allocator : exec_ctx.get_allocator(),
                                 phy_ctx->get_param_frame_ptrs(),
                                 frame_cnt,
                                 frames, compact_i64))) {
  } else {
    exec_ctx.set_frame_cnt(frame_cnt);
    exec_ctx.set_frames(frames);
  }

  return ret;
}

// Const pages belong to the plan, opaque parameter page handles to PlanCtx, and only
// dynamic/batch pages are newly allocated here. Construct each Datum at the
// actual Expr offset; a vector frame is not an array of scalar slots.
int ObExprFrameInfo::alloc_frame(ObIAllocator &exec_allocator,
                                 const ObIArray<char *> &param_frame_ptrs,
                                 uint64_t &frame_cnt, char **&frames, bool compact_i64) const
{
  int ret = validate_native_i64_layout();
  if (OB_SUCC(ret)) {
    ret = alloc_frame_after_layout_validation(exec_allocator, param_frame_ptrs,
                                              frame_cnt, frames, compact_i64);
  }
  return ret;
}

int ObExprFrameInfo::alloc_frame_after_layout_validation(ObIAllocator &exec_allocator,
                                  const ObIArray<char *> &param_frame_ptrs,
                                  uint64_t &frame_cnt, char **&frames, bool compact_i64) const
{
  int ret = OB_SUCCESS;
  const bool trusted_native_layout = native_i64_layout_validated_;
  if (compact_i64 && native_i64_point_layout_ == nullptr) { return OB_INVALID_ARGUMENT; }
  const uint64_t count = const_frame_.count() + param_frame_.count()
      + dynamic_frame_.count() + datum_frame_.count();
  char **candidate = nullptr;
  if (const_frame_ptrs_.count() != const_frame_.count() || param_frame_ptrs.count() < param_frame_.count()
      || count > INT64_MAX / sizeof(char *)) { return OB_INVALID_ARGUMENT; }
  if (trusted_native_layout && OB_FAIL(validate_parameter_pages(param_frame_, param_frame_ptrs))) {
    return ret;
  }
  if (count > 0) {
    candidate = static_cast<char **>(exec_allocator.alloc(count * sizeof(char *)));
    if (candidate == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
    else if (!trusted_native_layout) { MEMSET(candidate, 0, count * sizeof(char *)); }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < const_frame_.count(); ++i) {
    const ObFrameInfo &info = const_frame_.at(i);
    if (!trusted_native_layout && OB_FAIL(validate_wire_frame(info, count))) {
    } else if (!trusted_native_layout
        && (candidate[info.frame_idx_] != nullptr || const_frame_ptrs_.at(i) == nullptr)) {
      ret = OB_INVALID_ARGUMENT;
    } else { candidate[info.frame_idx_] = const_frame_ptrs_.at(i); }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < param_frame_.count(); ++i) {
    const ObFrameInfo &info = param_frame_.at(i);
    if (!trusted_native_layout && OB_FAIL(validate_wire_frame(info, count))) {
    } else if (!trusted_native_layout
        && (candidate[info.frame_idx_] != nullptr || param_frame_ptrs.at(i) == nullptr)) {
      ret = OB_INVALID_ARGUMENT;
    } else { candidate[info.frame_idx_] = param_frame_ptrs.at(i); }
  }
  const ObIArray<ObFrameInfo> *groups[] = {&dynamic_frame_, &datum_frame_};
  for (int64_t group = 0; OB_SUCC(ret) && group < 2; ++group) {
    for (int64_t i = 0; OB_SUCC(ret) && i < groups[group]->count(); ++i) {
      const ObFrameInfo &info = groups[group]->at(i);
      if (!trusted_native_layout && OB_FAIL(validate_wire_frame(info, count))) {
      } else if (!trusted_native_layout && candidate[info.frame_idx_] != nullptr) { ret = OB_INVALID_ARGUMENT; }
      else {
        const auto *description = find_native_i64_frame(*this, info.frame_idx_);
        const int64_t bytes = description != nullptr && compact_i64
            ? description->compact_size : info.frame_size_;
        char *frame = static_cast<char *>(exec_allocator.alloc(bytes));
        if (frame == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
        else if (description != nullptr) {
          initialize_native_i64_frame(*this, *description, frame, compact_i64);
          candidate[info.frame_idx_] = frame;
        }
        else if (OB_FAIL(initialize_execution_frame(info, *this, frame, exec_allocator))) {
        } else { candidate[info.frame_idx_] = frame; }
      }
    }
  }
  if (OB_SUCC(ret) && !trusted_native_layout) {
    ret = prepare_parameter_aliases(*this, param_frame_, param_frame_ptrs);
  }
  if (OB_SUCC(ret)) { frames = candidate; frame_cnt = count; }
  return ret;
}

OB_SERIALIZE_MEMBER(ObEmptyExpr);

int ObExprFrameInfo::get_expr_idx_in_frame(ObExpr *expr, int64_t &expr_idx) const
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(expr)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    const ObIArray<ObExpr> *array = &rt_exprs_;
    int64_t idx = 0;
    if (OB_UNLIKELY(NULL == array || array->empty() || expr < &array->at(0)
                    || (idx = expr - &array->at(0) + 1) > array->count())) {
      ret = OB_ERR_UNEXPECTED;
      SQL_LOG(WARN, "expr not in array", K(ret), KP(array), KP(idx), KP(expr));
    } else {
      expr_idx = idx;
    }
  }
  return ret;
}

OB_DEF_SERIALIZE(ObExprFrameInfo)
{
  int ret = OB_SUCCESS;
  OB_UNIS_ENCODE(need_ctx_cnt_);
  ObIArray<ObExpr> *seri_arr_bak = ObExpr::get_serialize_array();
  ObExpr::get_serialize_array() = const_cast<ObArray<ObExpr> *>(&rt_exprs_);
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(serialization::encode_i32(buf, buf_len, pos, rt_exprs_.count()))) {
  } else if (nullptr == ObExpr::get_serialize_array()) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    LOG_TRACE("get serialize array", K(ObExpr::get_serialize_array()), K(rt_exprs_.count()));
    for (int64_t i = 0; i < rt_exprs_.count() && OB_SUCC(ret); ++i) {
      const ObExpr &expr = rt_exprs_.at(i);
      if (OB_FAIL(expr.serialize(buf, buf_len, pos))) {
      }
    }
  }
  OB_UNIS_ENCODE(const_frame_ptrs_.count());
  OZ(ObPxTreeSerializer::serialize_frame_info<true>(buf, buf_len, pos, const_frame_,
                                              *this,
                                              const_frame_ptrs_.get_data(),
                                              const_frame_ptrs_.count()));
  OB_UNIS_ENCODE(param_frame_);
  OB_UNIS_ENCODE(datum_frame_);
  OB_UNIS_ENCODE(dynamic_frame_);
  ObExpr::get_serialize_array() = seri_arr_bak;

  return ret;
}

OB_DEF_DESERIALIZE(ObExprFrameInfo)
{
  int ret = OB_SUCCESS;
  native_i64_point_layout_ = nullptr;
  native_i64_layout_validated_ = false;
  int32_t expr_cnt = 0;
  OB_UNIS_DECODE(need_ctx_cnt_);
  ObIArray<ObExpr> &exprs = const_cast<ObArray<ObExpr> &>(rt_exprs_);
  ObIArray<ObExpr> *seri_arr_bak = ObExpr::get_serialize_array();
  ObExpr::get_serialize_array() = &exprs;
  if (OB_FAIL(ret)) {
  } else if (data_len - pos < 4) {
    ret = OB_DESERIALIZE_ERROR;
  } else if (OB_FAIL(serialization::decode_i32(buf, data_len, pos, &expr_cnt))) {
  } else if (expr_cnt < 0 || expr_cnt > data_len - pos) {
    ret = OB_DESERIALIZE_ERROR;
  } else if (OB_FAIL(exprs.prepare_allocate(expr_cnt))) {
  } else {
    for (int64_t i = 0; i < expr_cnt && OB_SUCC(ret); ++i) {
      ObExpr &expr = exprs.at(i);
      if (OB_FAIL(expr.deserialize(buf, data_len, pos))) {
      }
    }
  }

  int64_t const_frame_cnt = 0;
  char **frames = NULL;
  OB_UNIS_DECODE(const_frame_cnt);
  if (OB_SUCC(ret) && (const_frame_cnt < 0 || const_frame_cnt > INT64_MAX / sizeof(char *)
      || const_frame_cnt > data_len - pos)) {
    ret = OB_DESERIALIZE_ERROR;
  }
  if (OB_SUCC(ret) && 0 != const_frame_cnt) {
    CK(OB_NOT_NULL(frames = static_cast<char**>(
             allocator_.alloc(sizeof(char*) * const_frame_cnt))));
    OX(MEMSET(frames, 0, sizeof(char*) * const_frame_cnt));
  }
  OZ(ObPxTreeSerializer::deserialize_frame_info<true>(buf, data_len, pos,
     allocator_, const_frame_, *this, &const_frame_ptrs_, frames, const_frame_cnt));

  OB_UNIS_DECODE(param_frame_);
  OB_UNIS_DECODE(datum_frame_);
  OB_UNIS_DECODE(dynamic_frame_);
  ObExpr::get_serialize_array() = seri_arr_bak;
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObExprFrameInfo)
{
  int ret = OB_SUCCESS;
  int64_t len = 0;
  OB_UNIS_ADD_LEN(need_ctx_cnt_);
  ObIArray<ObExpr> &exprs = const_cast<ObArray<ObExpr> &>(rt_exprs_);
  int32_t expr_cnt = exprs.count();
  ObIArray<ObExpr> *seri_arr_bak = ObExpr::get_serialize_array();
  ObExpr::get_serialize_array() = &exprs;
  len += serialization::encoded_length_i32(expr_cnt);
  for (int64_t i = 0; i < exprs.count(); ++i) {
    ObExpr &expr = exprs.at(i);
    len += expr.get_serialize_size();
  }

  OB_UNIS_ADD_LEN(const_frame_ptrs_.count());
  len += ObPxTreeSerializer::get_serialize_frame_info_size<true>(
         const_frame_, *this, const_frame_ptrs_.get_data(), const_frame_ptrs_.count());

  OB_UNIS_ADD_LEN(param_frame_);
  OB_UNIS_ADD_LEN(datum_frame_);
  OB_UNIS_ADD_LEN(dynamic_frame_);
  ObExpr::get_serialize_array() = seri_arr_bak;

  return len;
}

int ObPreCalcExprFrameInfo::assign(const ObPreCalcExprFrameInfo &other,
                                   common::ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(ObExprFrameInfo::assign(other, allocator))) {
  } else if (OB_FAIL(pre_calc_rt_exprs_.prepare_allocate(other.pre_calc_rt_exprs_.count()))) {
  }
  for (int i = 0; OB_SUCC(ret) && i < other.pre_calc_rt_exprs_.count(); i++) {
    int64_t pos = other.pre_calc_rt_exprs_.at(i) - &(other.rt_exprs_.at(0));
    if (pos >= rt_exprs_.count() || pos < 0) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("rt expr pos is invalid", K(pos), KP(&other.rt_exprs_.at(0)),
                                         KP(other.pre_calc_rt_exprs_.at(i)));
    } else {
      pre_calc_rt_exprs_.at(i) = &rt_exprs_.at(pos);
    }
  } // for end

  return ret;
}

int ObPreCalcExprFrameInfo::eval(ObExecContext &exec_ctx,
                                 ObIArray<DatumParameter> &res_datum_params)
{
  int ret = OB_SUCCESS;
  res_datum_params.reuse();
  if (pre_calc_rt_exprs_.empty()) {
    /* do nothing */
  } else if (OB_FAIL(pre_alloc_exec_memory(exec_ctx))) {
  } else if (OB_FAIL(exec_ctx.init_expr_op(need_ctx_cnt_))) {
  } else if (OB_FAIL(do_normal_eval(exec_ctx, res_datum_params))) {
  }
  // reset expr op anyway
  exec_ctx.reset_expr_op();
  return ret;
}

OB_INLINE int ObPreCalcExprFrameInfo::do_normal_eval(ObExecContext &exec_ctx,
                                                     ObIArray<DatumParameter> &res_datum_params)
{
  int ret = OB_SUCCESS;
  ObEvalCtx eval_ctx(exec_ctx);
  Datum *res_datum = NULL;
  DatumParameter datum_param;
  ObExpr *rt_expr = NULL;
  for (int64_t i = 0; OB_SUCC(ret) && i < pre_calc_rt_exprs_.count(); ++i) {
    if (OB_ISNULL(rt_expr = pre_calc_rt_exprs_.at(i))) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(rt_expr->eval(eval_ctx, res_datum))) {
    } else {
      if (OB_FAIL(capture_datum_parameter(*rt_expr, eval_ctx, datum_param))) {
      } else if (rt_expr->obj_meta_.has_lob_header()) {
        datum_param.set_result_flag(HAS_LOB_HEADER_FLAG);
      }
      if (OB_SUCC(ret) && OB_FAIL(res_datum_params.push_back(datum_param))) {
      }
    } // else end
  } // for end
  return ret;
}

OB_NOINLINE int ObPreCalcExprFrameInfo::do_batch_stmt_eval(ObExecContext &exec_ctx,
                                                           ObIArray<DatumParameter> &res_datum_params)
{
  int ret = OB_SUCCESS;
  ObEvalCtx eval_ctx(exec_ctx);
  Datum *res_datum = NULL;
  DatumParameter datum_param;
  int64_t group_cnt = exec_ctx.get_sql_ctx()->get_batch_params_count();
  DatumParameterArray *datum_array = nullptr;
  //construct sql array obj
  for (int64_t i = 0; OB_SUCC(ret) && i < pre_calc_rt_exprs_.count(); ++i) {
    ObExpr *rt_expr = NULL;
    if (OB_ISNULL(rt_expr = pre_calc_rt_exprs_.at(i))) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_ISNULL(datum_array = DatumParameterArray::alloc(exec_ctx.get_allocator(), group_cnt))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      datum_array->element_.set_obj_type(rt_expr->datum_meta_.type_);
      datum_array->element_.set_collation_type(rt_expr->datum_meta_.cs_type_);
      datum_array->element_.set_scale(rt_expr->datum_meta_.scale_);
      const SqlTypeClass &dst_tc = sql_type_class(rt_expr->datum_meta_.type_);
      if (ObStringTC == dst_tc || ObTextTC == dst_tc) {
        datum_array->element_.set_length_semantics(rt_expr->datum_meta_.length_semantics_);
      } else {
        datum_array->element_.set_precision(rt_expr->datum_meta_.precision_);
      }
    }
    if (OB_SUCC(ret)) {
      SqlTypeMeta array_meta;
      array_meta.set_ext();
      array_meta.set_extend_type(T_EXT_SQL_ARRAY);
      datum_param.set_type_meta(array_meta);
      if (OB_FAIL(datum_param.set_array_address(datum_array, exec_ctx.get_allocator()))) {
      } else if (OB_FAIL(res_datum_params.push_back(datum_param))) {
      }
    } // else end
  } // for end
  //replace array param to the real param and eval the pre calc expr
  for (int64_t group_id = 0; OB_SUCC(ret) && group_id < group_cnt; ++group_id) {
    if (OB_FAIL(exec_ctx.get_physical_plan_ctx()->replace_batch_param_datum(group_id, 0,
                              exec_ctx.get_physical_plan_ctx()->get_parameter_count()))) {
    }
    //params of each group need to clear the datum evaluted flags before calc the pre_calc_expr
    //otherwise, the result of pre_calc_expr will be the result of the last group param
    clear_datum_evaluted_flags(exec_ctx.get_frames());
    for (int64_t i = 0; OB_SUCC(ret) && i < pre_calc_rt_exprs_.count(); ++i) {
      ObExpr *rt_expr = pre_calc_rt_exprs_.at(i);
      datum_array = res_datum_params.at(i).get_array<DatumParameterArray>();
      if (OB_FAIL(rt_expr->eval(eval_ctx, res_datum))) {
      } else {
        NativeTypedDatum borrowed, owned;
        DatumParameterMetadata attributes;
        if (OB_FAIL(capture_datum_parameter(*rt_expr, eval_ctx, borrowed, attributes))) {
        } else if (OB_FAIL(clone_native_parameter_value(borrowed, exec_ctx.get_allocator(), owned))) {
        } else {
          if (rt_expr->obj_meta_.has_lob_header()) { attributes.res_flags_ |= HAS_LOB_HEADER_FLAG; }
          ret = datum_array->set_native_element_borrowed_type(group_id,
              owned.datum_, *owned.type_, attributes, exec_ctx.get_allocator());
        }
      }
    }
  }
  return ret;
}

void ObPreCalcExprFrameInfo::clear_datum_evaluted_flags(char **frames)
{
  int64_t datum_frame_idx = const_frame_ptrs_.count() + param_frame_.count() + dynamic_frame_.count();
  for (int64_t i = 0; i < datum_frame_.count(); ++i) {
    char *cur_frame = frames[datum_frame_idx + i];
    for (int64_t j = 0; j < datum_frame_.at(i).expr_cnt_; ++j) {
      ObEvalInfo *eval_info = reinterpret_cast<ObEvalInfo *>(cur_frame + ScalarFrameLayout::eval_info_offset(j));
      eval_info->clear_evaluated_flag();
    }
  }
}

int ObPreCalcExprFrameInfo::eval_expect_err(ObExecContext &exec_ctx,
                                            bool &all_eval_err)
{
  int ret = OB_SUCCESS;
  all_eval_err = true;
  if (pre_calc_rt_exprs_.empty()) {
    /* do nothing */
  } else if (OB_FAIL(pre_alloc_exec_memory(exec_ctx))) {
  } else if (OB_FAIL(exec_ctx.init_expr_op(need_ctx_cnt_))) {
  } else {
    ObEvalCtx eval_ctx(exec_ctx);
    Datum *res_datum = NULL;
    for (int64_t i = 0; OB_SUCC(ret) && all_eval_err && i < pre_calc_rt_exprs_.count(); ++i) {
      if (OB_ISNULL(pre_calc_rt_exprs_.at(i))) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_LIKELY(OB_SUCCESS != pre_calc_rt_exprs_.at(i)->eval(eval_ctx, res_datum))) {
        // eval error is expected
      } else {
        all_eval_err = false;
      }// else end
    } // for end
    // reset expr op anyway
    exec_ctx.reset_expr_op();
  }
  return ret;
}

ObTempExprCtx::~ObTempExprCtx()
{
  if (expr_op_ctx_store_ != NULL) {
    ObExprOperatorCtx **it = expr_op_ctx_store_;
    ObExprOperatorCtx **it_end = &expr_op_ctx_store_[expr_op_size_];
    for (; it != it_end; ++it) {
      if (NULL != (*it)) {
        (*it)->~ObExprOperatorCtx();
      }
    }
    expr_op_ctx_store_ = NULL;
    expr_op_size_ = 0;
    frame_cnt_ = 0;
  }
}

int ObTempExpr::eval_datum(ObExecContext &exec_ctx, ObIAllocator &result_allocator,
                            Datum &result) const
{
  int ret = OB_SUCCESS;
  Datum *res_datum = nullptr;
  ObTempExprCtx *temp_expr_ctx = nullptr;
  result.set_null();
  if (OB_UNLIKELY(!idx_col_arr_.empty() || expr_idx_ < 0 || expr_idx_ >= rt_exprs_.count())) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    OZ(exec_ctx.get_temp_expr_eval_ctx(*this, temp_expr_ctx));
    CK(OB_NOT_NULL(temp_expr_ctx));
    OX(ObSQLUtils::clear_expr_eval_flags(rt_exprs_.at(expr_idx_), *temp_expr_ctx));
    if (OB_SUCC(ret)) {
      ObTempExprCtxReplaceGuard exec_ctx_backup_guard(exec_ctx, *temp_expr_ctx);
      OZ(rt_exprs_.at(expr_idx_).eval(*temp_expr_ctx, res_datum));
      CK(OB_NOT_NULL(res_datum));
      OZ(ObExpr::clone_native_value(rt_exprs_.at(expr_idx_).get_sql_type(),
          *res_datum, result_allocator, result));
      if (!exec_ctx.use_temp_expr_ctx_cache()) {
        temp_expr_ctx->~ObTempExprCtx();
      }
    }
  }
  return ret;
}

int ObTempExpr::eval_datum(ObExecContext &exec_ctx, const ObIArray<DatumValue> &row,
                            ObIAllocator &result_allocator, Datum &result) const
{
  int ret = OB_SUCCESS;
  ObTempExprCtx *temp_expr_ctx = nullptr;
  if (OB_UNLIKELY(expr_idx_ < 0 || expr_idx_ >= rt_exprs_.count())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(exec_ctx.get_temp_expr_eval_ctx(*this, temp_expr_ctx))) {
  } else if (OB_ISNULL(temp_expr_ctx)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    // A noncached context owns expression operator resources even if filling
    // the input frame or evaluating its root fails.
    struct ContextCleanup {
      ObTempExprCtx *context_;
      bool destroy_;
      ~ContextCleanup() { if (destroy_) { context_->~ObTempExprCtx(); } }
    } cleanup{temp_expr_ctx, !exec_ctx.use_temp_expr_ctx_cache()};
    ObSQLUtils::clear_expr_eval_flags(rt_exprs_.at(expr_idx_), *temp_expr_ctx);
    if (OB_FAIL(row_to_frame(row, *temp_expr_ctx))) {
    } else {
      ObTempExprCtxReplaceGuard guard(exec_ctx, *temp_expr_ctx);
      Datum *evaluated = nullptr;
      Datum owned;
      if (OB_FAIL(rt_exprs_.at(expr_idx_).eval(*temp_expr_ctx, evaluated))) {
      } else if (OB_ISNULL(evaluated)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(ObExpr::clone_native_value(rt_exprs_.at(expr_idx_).get_sql_type(),
          *evaluated, result_allocator, owned))) {
      } else {
        result = owned;
      }
    }
  }
  return ret;
}

namespace {
int export_temp_expr_result(const SqlType &type, const Datum &source,
                            ObIAllocator &owner, common::ObDatum &destination)
{
  const DatumCopyPlan plan(type);
  int64_t bytes = 0;
  int ret = plan.legacy_payload_size(source, bytes);
  if (OB_SUCC(ret)) {
    char *buffer = bytes == 0 ? nullptr : static_cast<char *>(owner.alloc(bytes));
    if (bytes > 0 && buffer == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
    else {
      int64_t position = 0;
      ret = plan.export_legacy(source, destination, buffer, bytes, position);
    }
  }
  return ret;
}
} // namespace

int ObTempExpr::eval_datum(ObExecContext &exec_ctx, ObIAllocator &result_allocator,
                          common::ObDatum &result) const
{
  Datum native;
  int ret = eval_datum(exec_ctx, result_allocator, native);
  if (OB_SUCC(ret)) {
    ret = export_temp_expr_result(rt_exprs_.at(expr_idx_).get_sql_type(), native, result_allocator, result);
  }
  return ret;
}

int ObTempExpr::eval_datum(ObExecContext &exec_ctx, const ObIArray<DatumValue> &row,
                          ObIAllocator &result_allocator, common::ObDatum &result) const
{
  Datum native;
  int ret = eval_datum(exec_ctx, row, result_allocator, native);
  if (OB_SUCC(ret)) {
    ret = export_temp_expr_result(rt_exprs_.at(expr_idx_).get_sql_type(), native, result_allocator, result);
  }
  return ret;
}

int ObTempExpr::row_to_frame(const ObIArrayWrap<DatumValue> &row,
                              ObTempExprCtx &temp_expr_ctx) const
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < idx_col_arr_.count(); ++i) {
    const RowIdxColumnPair &column = idx_col_arr_.at(i);
    if (column.idx_ < 0 || column.idx_ >= row.count()
        || column.expr_pos_ < 0 || column.expr_pos_ >= rt_exprs_.count()) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      const DatumValue &value = row.at(column.idx_);
      ObExpr &expr = rt_exprs_.at(column.expr_pos_);
      common::ObDatum legacy;
      char extended_payload[DATUM_EXTEND_PAYLOAD_SIZE];
      Datum input;
      if (value.get_type() != expr.datum_meta_.type_ && !value.is_null()) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(value.to_row_datum(legacy, extended_payload))) {
      } else if (OB_FAIL(expr.get_datum_copy_plan().import_legacy(legacy, input))) {
      } else if (OB_FAIL(expr.deep_copy_datum(temp_expr_ctx, input))) {
      } else {
        Datum &frame = expr.locate_expr_datum(temp_expr_ctx);
        if (is_lob_storage(value.get_type())) {
          ret = ob_adjust_lob_datum(temp_expr_ctx.exec_ctx_, frame, value.get_meta(),
              expr.obj_meta_, temp_expr_ctx.exec_ctx_.get_allocator(), frame);
        }
        if (OB_SUCC(ret)) { expr.set_evaluated_projected(temp_expr_ctx); }
      }
    }
  }
  return ret;
}

int ObTempExpr::deep_copy(ObIAllocator &allocator, ObTempExpr *&dst) const
{
  int ret = OB_SUCCESS;
  char *buf = static_cast<char *>(allocator.alloc(sizeof(ObTempExpr)));
  if (OB_ISNULL(buf)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  }
  OX(dst = new(buf)ObTempExpr(allocator));
  OZ(dst->assign(*this, allocator));
  OX(dst->expr_idx_ = expr_idx_);
  OZ(dst->idx_col_arr_.assign(this->idx_col_arr_));

  return ret;
}

OB_SERIALIZE_MEMBER(RowIdxColumnPair, idx_, expr_pos_);
OB_SERIALIZE_MEMBER((ObTempExpr, ObExprFrameInfo), expr_idx_, idx_col_arr_);
}
}
