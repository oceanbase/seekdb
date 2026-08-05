/*
 * Copyright (c) 2026 OceanBase.
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
#include "sql/engine/aggregate/ob_pushdown_aggregate_program.h"

#include "lib/allocator/ob_allocator.h"
#include "lib/container/ob_fixed_array.h"
#include "lib/container/ob_se_array.h"
#include "lib/ob_errno.h"
#include "share/aggregate/ob_pushdown_aggregate_protocol.h"
#include "share/datum/ob_datum_funcs.h"
#include "sql/engine/expr/ob_expr.h"

namespace oceanbase
{
namespace sql
{
ObCountPushdownAggregateProgramBase::ObCountPushdownAggregateProgramBase()
  : inputs_(), counts_(), deltas_(), state_(share::aggregate::AGG_PROGRAM_NEW)
{}

int ObCountPushdownAggregateProgramBase::init(
    const common::ObIArray<ObCountPushdownInputSpec> &inputs)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(inputs.empty())) {
    ret = OB_NOT_SUPPORTED;
  } else if (OB_FAIL(inputs_.reserve(inputs.count()))) {
    LOG_WARN("failed to reserve count pushdown inputs", K(ret), K(inputs.count()));
  } else if (OB_FAIL(counts_.reserve(inputs.count()))) {
    LOG_WARN("failed to reserve count pushdown values", K(ret), K(inputs.count()));
  } else if (OB_FAIL(deltas_.reserve(inputs.count()))) {
    LOG_WARN("failed to reserve count pushdown deltas", K(ret), K(inputs.count()));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < inputs.count(); ++i) {
    const ObCountPushdownInputSpec &input = inputs.at(i);
    if (OB_UNLIKELY(input.slot_ < 0)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid count pushdown input slot", K(ret), K(i), K(input));
    } else if (OB_FAIL(inputs_.push_back(input))) {
      LOG_WARN("failed to append count pushdown input", K(ret), K(i), K(input));
    } else if (OB_FAIL(counts_.push_back(0))) {
      LOG_WARN("failed to append count pushdown value", K(ret), K(i));
    } else if (OB_FAIL(deltas_.push_back(0))) {
      LOG_WARN("failed to append count pushdown delta", K(ret), K(i));
    }
  }
  return ret;
}

share::aggregate::ObPushdownAggregateProgramState
ObCountPushdownAggregateProgramBase::state() const
{
  return state_;
}

int ObCountPushdownAggregateProgramBase::reset_scan()
{
  for (int64_t i = 0; i < counts_.count(); ++i) {
    counts_.at(i) = 0;
    deltas_.at(i) = 0;
  }
  state_ = share::aggregate::AGG_PROGRAM_NEW;
  return OB_SUCCESS;
}

int ObCountPushdownAggregateProgramBase::validate_reduction(
    const uint32_t requested,
    const share::aggregate::ObAggregateReduction &reduction) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY((reduction.present_ & requested) != requested
                  || reduction.row_count_ < 0
                  || reduction.null_count_ < 0
                  || reduction.null_count_ > reduction.row_count_)) {
    ret = OB_ERR_UNEXPECTED;
  }
  return ret;
}

int ObCountPushdownAggregateProgramBase::can_consume(
    share::aggregate::ObIAggregateInputSegment &segment,
    bool &can_consume)
{
  int ret = OB_SUCCESS;
  can_consume = false;
  if (OB_UNLIKELY(share::aggregate::AGG_PROGRAM_NEW != state_
                  && share::aggregate::AGG_PROGRAM_CONSUMING != state_)) {
    ret = OB_STATE_NOT_MATCH;
  } else {
    can_consume = true;
  }
  for (int64_t i = 0; OB_SUCC(ret) && can_consume && i < inputs_.count(); ++i) {
    const ObCountPushdownInputSpec &input = inputs_.at(i);
    const uint32_t requested = share::aggregate::AGG_REDUCE_ROW_COUNT
        | (input.exclude_null_ ? share::aggregate::AGG_REDUCE_NULL_COUNT : 0);
    share::aggregate::ObAggregateReduction reduction;
    const int reduce_ret = segment.try_reduce(input.slot_, requested, reduction);
    if (OB_NOT_SUPPORTED == reduce_ret) {
      can_consume = false;
    } else if (OB_SUCCESS != reduce_ret) {
      ret = reduce_ret;
      LOG_WARN("failed to probe aggregate reduction", K(ret), K(input.slot_), K(requested));
    } else if (OB_FAIL(validate_reduction(requested, reduction))) {
      LOG_WARN("invalid aggregate reduction probe", K(ret), K(input.slot_), K(requested),
               K(reduction.present_), K(reduction.row_count_), K(reduction.null_count_));
    }
  }
  return ret;
}

int ObCountPushdownAggregateProgramBase::consume(
    share::aggregate::ObIAggregateInputSegment &segment)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(share::aggregate::AGG_PROGRAM_NEW != state_
                  && share::aggregate::AGG_PROGRAM_CONSUMING != state_)) {
    ret = OB_STATE_NOT_MATCH;
    LOG_WARN("aggregate program is not consumable", K(ret), K(state_));
  } else {
    for (int64_t i = 0; i < deltas_.count(); ++i) {
      deltas_.at(i) = 0;
    }
  }

  for (int64_t i = 0; OB_SUCC(ret) && i < inputs_.count(); ++i) {
    const ObCountPushdownInputSpec &input = inputs_.at(i);
    const uint32_t requested = share::aggregate::AGG_REDUCE_ROW_COUNT
        | (input.exclude_null_ ? share::aggregate::AGG_REDUCE_NULL_COUNT : 0);
    share::aggregate::ObAggregateReduction reduction;
    const int reduce_ret = segment.try_reduce(input.slot_, requested, reduction);
    int64_t delta = 0;
    if (OB_SUCCESS == reduce_ret) {
      if (OB_FAIL(validate_reduction(requested, reduction))) {
        LOG_WARN("invalid aggregate reduction", K(ret), K(input.slot_), K(requested),
                 K(reduction.present_), K(reduction.row_count_), K(reduction.null_count_));
      } else {
        delta = reduction.row_count_ - (input.exclude_null_ ? reduction.null_count_ : 0);
      }
    } else if (OB_NOT_SUPPORTED == reduce_ret) {
      if (!input.exclude_null_) {
        if (OB_UNLIKELY(segment.selection().count_ < 0)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("invalid aggregate selection count", K(ret), K(segment.selection().count_));
        } else {
          delta = segment.selection().count_;
        }
      } else {
        share::aggregate::ObAggregateValueBatchView values;
        if (OB_FAIL(segment.read_values(input.slot_, values))) {
          LOG_WARN("failed to read aggregate input values", K(ret), K(input.slot_));
        } else if (OB_UNLIKELY(values.count_ != segment.selection().count_
                               || values.count_ < 0
                               || (values.count_ > 0 && OB_ISNULL(values.datums_)))) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("invalid aggregate value batch", K(ret), K(values.count_),
                   K(segment.selection().count_), KP(values.datums_));
        } else {
          for (int64_t row_idx = 0; row_idx < values.count_; ++row_idx) {
            delta += !values.datums_[row_idx].is_null();
          }
        }
      }
    } else {
      ret = reduce_ret;
      LOG_WARN("failed to reduce aggregate input", K(ret), K(input.slot_), K(requested));
    }
    if (OB_SUCC(ret)) {
      deltas_.at(i) = delta;
    }
  }

  for (int64_t i = 0; OB_SUCC(ret) && i < counts_.count(); ++i) {
    if (OB_UNLIKELY(deltas_.at(i) > INT64_MAX - counts_.at(i))) {
      ret = OB_SIZE_OVERFLOW;
      LOG_WARN("count pushdown result overflow", K(ret), K(i), K(counts_.at(i)), K(deltas_.at(i)));
    }
  }
  if (OB_SUCC(ret)) {
    for (int64_t i = 0; i < counts_.count(); ++i) {
      counts_.at(i) += deltas_.at(i);
    }
    state_ = share::aggregate::AGG_PROGRAM_CONSUMING;
  } else {
    state_ = share::aggregate::AGG_PROGRAM_FAILED;
  }
  return ret;
}

int ObCountPushdownAggregateProgramBase::seal()
{
  int ret = OB_SUCCESS;
  if (share::aggregate::AGG_PROGRAM_NEW == state_
      || share::aggregate::AGG_PROGRAM_CONSUMING == state_) {
    state_ = share::aggregate::AGG_PROGRAM_SEALED;
  } else {
    ret = OB_STATE_NOT_MATCH;
    LOG_WARN("aggregate program cannot be sealed", K(ret), K(state_));
    state_ = share::aggregate::AGG_PROGRAM_FAILED;
  }
  return ret;
}

int ObCountPushdownAggregateProgramBase::emit(
    const int64_t max_rows,
    share::aggregate::ObAggregateEmitResult &result)
{
  int ret = OB_SUCCESS;
  result = share::aggregate::ObAggregateEmitResult();
  if (share::aggregate::AGG_PROGRAM_END == state_) {
    result.end_ = true;
  } else if (OB_UNLIKELY(max_rows <= 0
                         || (share::aggregate::AGG_PROGRAM_SEALED != state_
                             && share::aggregate::AGG_PROGRAM_EMITTING != state_))) {
    ret = OB_STATE_NOT_MATCH;
    LOG_WARN("aggregate program cannot emit", K(ret), K(max_rows), K(state_));
    state_ = share::aggregate::AGG_PROGRAM_FAILED;
  } else {
    state_ = share::aggregate::AGG_PROGRAM_EMITTING;
    if (OB_FAIL(materialize_counts(counts_))) {
      LOG_WARN("failed to materialize count pushdown result", K(ret), K(counts_));
      state_ = share::aggregate::AGG_PROGRAM_FAILED;
    } else {
      // A scalar aggregate always produces one row, including empty input.
      result.row_count_ = 1;
      result.end_ = true;
      state_ = share::aggregate::AGG_PROGRAM_END;
      LOG_DEBUG("materialized query-owned count pushdown result", K(counts_));
    }
  }
  return ret;
}

namespace
{

enum ObScalarPushdownAggregateKind : uint8_t
{
  SCALAR_AGG_COUNT_STAR = 0,
  SCALAR_AGG_COUNT_NONNULL,
  SCALAR_AGG_MIN,
  SCALAR_AGG_MAX
};

struct ObScalarPushdownAggregateSpec
{
  ObScalarPushdownAggregateSpec()
    : kind_(SCALAR_AGG_COUNT_STAR), slot_(-1), cmp_func_(nullptr), output_(nullptr)
  {}

  ObScalarPushdownAggregateKind kind_;
  share::aggregate::ObAggregateInputSlot slot_;
  common::ObDatumCmpFuncType cmp_func_;
  ObExpr *output_;
  TO_STRING_KV(K_(kind), K_(slot), KP_(output));
};

// One current/staging pair is retained for every MIN/MAX output.  Capacity
// grows geometrically and is reused across segments and reset_scan(); a failed
// assignment leaves the slot byte-for-byte unchanged.
struct ObOwnedAggregateDatum
{
  ObOwnedAggregateDatum()
    : datum_(), buffer_(nullptr), capacity_(0), has_value_(false)
  { datum_.set_null(); }

  int assign(const common::ObDatum &src, common::ObIAllocator &allocator)
  {
    int ret = OB_SUCCESS;
    char *target = buffer_;
    int64_t target_capacity = capacity_;
    const int64_t required = MAX(static_cast<int64_t>(src.len_), 1L);
    if (OB_UNLIKELY(src.is_null())) {
      ret = OB_INVALID_ARGUMENT;
    } else if (OB_UNLIKELY(src.len_ > 0 && nullptr == src.ptr_)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (required > capacity_) {
      target_capacity = MAX(64L, capacity_);
      while (target_capacity < required && target_capacity <= INT64_MAX / 2) {
        target_capacity *= 2;
      }
      if (target_capacity < required) {
        target_capacity = required;
      }
      if (OB_ISNULL(target = static_cast<char *>(allocator.alloc(target_capacity)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      }
    }
    if (OB_SUCC(ret)) {
      if (src.len_ > 0) {
        MEMCPY(target, src.ptr_, src.len_);
      }
      if (target != buffer_) {
        if (nullptr != buffer_) {
          allocator.free(buffer_);
        }
        buffer_ = target;
        capacity_ = target_capacity;
      }
      datum_.ptr_ = buffer_;
      datum_.pack_ = src.pack_;
      has_value_ = true;
    }
    return ret;
  }

  void clear_value()
  {
    datum_.set_null();
    has_value_ = false;
  }

  void release(common::ObIAllocator &allocator)
  {
    if (nullptr != buffer_) {
      allocator.free(buffer_);
    }
    buffer_ = nullptr;
    capacity_ = 0;
    clear_value();
  }

  void swap(ObOwnedAggregateDatum &other)
  {
    common::ObDatum datum = datum_;
    char *buffer = buffer_;
    const int64_t capacity = capacity_;
    const bool has_value = has_value_;
    datum_ = other.datum_;
    buffer_ = other.buffer_;
    capacity_ = other.capacity_;
    has_value_ = other.has_value_;
    other.datum_ = datum;
    other.buffer_ = buffer;
    other.capacity_ = capacity;
    other.has_value_ = has_value;
  }

  common::ObDatum datum_;
  char *buffer_;
  int64_t capacity_;
  bool has_value_;
  TO_STRING_KV(K_(datum), KP_(buffer), K_(capacity), K_(has_value));
};

struct ObScalarPushdownAggregateState
{
  ObScalarPushdownAggregateState()
    : count_(0), delta_(0), current_(), staging_(), staging_ready_(false)
  {}

  int64_t count_;
  int64_t delta_;
  ObOwnedAggregateDatum current_;
  ObOwnedAggregateDatum staging_;
  bool staging_ready_;
  TO_STRING_KV(K_(count), K_(delta), K_(current), K_(staging), K_(staging_ready));
};

class ObScalarPushdownAggregateProgram final
  : public share::aggregate::ObIPushdownAggregateProgram
{
public:
  ObScalarPushdownAggregateProgram(
      ObEvalCtx &eval_ctx,
      const bool rich_format,
      common::ObIAllocator &allocator)
    : eval_ctx_(eval_ctx),
      access_ctx_(nullptr),
      rich_format_(rich_format),
      allocator_(allocator),
      specs_(),
      states_(),
      state_(share::aggregate::AGG_PROGRAM_NEW)
  {}

  ~ObScalarPushdownAggregateProgram() override
  {
    for (int64_t i = 0; i < states_.count(); ++i) {
      states_.at(i).current_.release(allocator_);
      states_.at(i).staging_.release(allocator_);
    }
  }

  void destroy() override
  {
    common::ObIAllocator *allocator = &allocator_;
    this->~ObScalarPushdownAggregateProgram();
    allocator->free(this);
  }

  int init(const common::ObIArray<ObExpr *> &aggregate_exprs)
  {
    int ret = OB_SUCCESS;
    if (OB_UNLIKELY(aggregate_exprs.empty())) {
      ret = OB_NOT_SUPPORTED;
    } else if (OB_FAIL(eval_ctx_.get_datum_access_ctx(access_ctx_))) {
      LOG_WARN("failed to get datum access context", K(ret));
    } else if (OB_FAIL(specs_.reserve(aggregate_exprs.count()))) {
      LOG_WARN("failed to reserve scalar aggregate specs", K(ret), K(aggregate_exprs.count()));
    } else if (OB_FAIL(states_.reserve(aggregate_exprs.count()))) {
      LOG_WARN("failed to reserve scalar aggregate states", K(ret), K(aggregate_exprs.count()));
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < aggregate_exprs.count(); ++i) {
      ObExpr *expr = aggregate_exprs.at(i);
      ObScalarPushdownAggregateSpec spec;
      spec.slot_ = static_cast<share::aggregate::ObAggregateInputSlot>(i);
      spec.output_ = expr;
      if (OB_ISNULL(expr)) {
        ret = OB_NOT_SUPPORTED;
      } else if (T_FUN_COUNT == expr->type_ && 0 == expr->arg_cnt_) {
        spec.kind_ = SCALAR_AGG_COUNT_STAR;
      } else if (T_FUN_COUNT == expr->type_ && 1 == expr->arg_cnt_) {
        spec.kind_ = SCALAR_AGG_COUNT_NONNULL;
      } else if ((T_FUN_MIN == expr->type_ || T_FUN_MAX == expr->type_)
                 && 1 == expr->arg_cnt_) {
        if (expr->obj_meta_.is_lob_storage()) {
          ret = OB_NOT_SUPPORTED;
        } else if (OB_ISNULL(expr->basic_funcs_)
                   || OB_ISNULL(expr->basic_funcs_->null_first_cmp_)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("missing MIN/MAX datum comparator", K(ret), K(i), KPC(expr));
        } else {
          spec.kind_ = T_FUN_MIN == expr->type_ ? SCALAR_AGG_MIN : SCALAR_AGG_MAX;
          spec.cmp_func_ = expr->basic_funcs_->null_first_cmp_;
        }
      } else {
        ret = OB_NOT_SUPPORTED;
      }
      if (OB_SUCC(ret) && OB_FAIL(specs_.push_back(spec))) {
        LOG_WARN("failed to append scalar aggregate spec", K(ret), K(i));
      } else if (OB_SUCC(ret) && OB_FAIL(states_.push_back(ObScalarPushdownAggregateState()))) {
        LOG_WARN("failed to append scalar aggregate state", K(ret), K(i));
      }
    }
    return ret;
  }

  share::aggregate::ObPushdownAggregateProgramState state() const override
  { return state_; }

  int reset_scan() override
  {
    for (int64_t i = 0; i < states_.count(); ++i) {
      ObScalarPushdownAggregateState &agg_state = states_.at(i);
      agg_state.count_ = 0;
      agg_state.delta_ = 0;
      agg_state.current_.clear_value();
      agg_state.staging_.clear_value();
      agg_state.staging_ready_ = false;
    }
    state_ = share::aggregate::AGG_PROGRAM_NEW;
    return OB_SUCCESS;
  }

  int can_consume(
      share::aggregate::ObIAggregateInputSegment &segment,
      bool &can_consume) override
  {
    int ret = OB_SUCCESS;
    can_consume = false;
    if (OB_UNLIKELY(share::aggregate::AGG_PROGRAM_NEW != state_
                    && share::aggregate::AGG_PROGRAM_CONSUMING != state_)) {
      ret = OB_STATE_NOT_MATCH;
    } else {
      can_consume = true;
    }
    for (int64_t i = 0; OB_SUCC(ret) && can_consume && i < specs_.count(); ++i) {
      bool spec_can_consume = false;
      if (OB_FAIL(probe_spec(specs_.at(i), segment, spec_can_consume))) {
        LOG_WARN("failed to probe scalar aggregate input", K(ret), K(i));
      } else {
        can_consume = spec_can_consume;
      }
    }
    return ret;
  }

  int consume(share::aggregate::ObIAggregateInputSegment &segment) override
  {
    int ret = OB_SUCCESS;
    if (OB_UNLIKELY(share::aggregate::AGG_PROGRAM_NEW != state_
                    && share::aggregate::AGG_PROGRAM_CONSUMING != state_)) {
      ret = OB_STATE_NOT_MATCH;
    }
    for (int64_t i = 0; i < states_.count(); ++i) {
      states_.at(i).delta_ = 0;
      states_.at(i).staging_ready_ = false;
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < specs_.count(); ++i) {
      if (OB_FAIL(stage_spec(specs_.at(i), segment, states_.at(i)))) {
        LOG_WARN("failed to stage scalar aggregate input", K(ret), K(i));
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < specs_.count(); ++i) {
      if ((SCALAR_AGG_COUNT_STAR == specs_.at(i).kind_
           || SCALAR_AGG_COUNT_NONNULL == specs_.at(i).kind_)
          && OB_UNLIKELY(states_.at(i).delta_ > INT64_MAX - states_.at(i).count_)) {
        ret = OB_SIZE_OVERFLOW;
        LOG_WARN("scalar COUNT overflow", K(ret), K(i), K(states_.at(i).count_),
                 K(states_.at(i).delta_));
      }
    }
    if (OB_SUCC(ret)) {
      for (int64_t i = 0; i < specs_.count(); ++i) {
        ObScalarPushdownAggregateState &agg_state = states_.at(i);
        if (SCALAR_AGG_COUNT_STAR == specs_.at(i).kind_
            || SCALAR_AGG_COUNT_NONNULL == specs_.at(i).kind_) {
          agg_state.count_ += agg_state.delta_;
        } else if (agg_state.staging_ready_) {
          agg_state.current_.swap(agg_state.staging_);
        }
        agg_state.staging_ready_ = false;
      }
      state_ = share::aggregate::AGG_PROGRAM_CONSUMING;
    } else {
      state_ = share::aggregate::AGG_PROGRAM_FAILED;
    }
    return ret;
  }

  int seal() override
  {
    int ret = OB_SUCCESS;
    if (share::aggregate::AGG_PROGRAM_NEW == state_
        || share::aggregate::AGG_PROGRAM_CONSUMING == state_) {
      state_ = share::aggregate::AGG_PROGRAM_SEALED;
    } else {
      ret = OB_STATE_NOT_MATCH;
      state_ = share::aggregate::AGG_PROGRAM_FAILED;
    }
    return ret;
  }

  int emit(
      const int64_t max_rows,
      share::aggregate::ObAggregateEmitResult &result) override
  {
    int ret = OB_SUCCESS;
    result = share::aggregate::ObAggregateEmitResult();
    if (share::aggregate::AGG_PROGRAM_END == state_) {
      result.end_ = true;
    } else if (OB_UNLIKELY(max_rows <= 0
                           || (share::aggregate::AGG_PROGRAM_SEALED != state_
                               && share::aggregate::AGG_PROGRAM_EMITTING != state_))) {
      ret = OB_STATE_NOT_MATCH;
      state_ = share::aggregate::AGG_PROGRAM_FAILED;
    } else {
      state_ = share::aggregate::AGG_PROGRAM_EMITTING;
      if (OB_FAIL(materialize())) {
        LOG_WARN("failed to materialize scalar aggregate program", K(ret));
        state_ = share::aggregate::AGG_PROGRAM_FAILED;
      } else {
        result.row_count_ = 1;
        result.end_ = true;
        state_ = share::aggregate::AGG_PROGRAM_END;
      }
    }
    return ret;
  }

private:
  int validate_count_reduction(
      const uint32_t requested,
      const share::aggregate::ObAggregateReduction &reduction) const
  {
    return OB_UNLIKELY((reduction.present_ & requested) != requested
                       || reduction.row_count_ < 0
                       || reduction.null_count_ < 0
                       || reduction.null_count_ > reduction.row_count_)
        ? OB_ERR_UNEXPECTED
        : OB_SUCCESS;
  }

  int validate_extreme_reduction(
      const ObScalarPushdownAggregateSpec &spec,
      const share::aggregate::ObAggregateReduction &reduction,
      bool &is_exact) const
  {
    int ret = OB_SUCCESS;
    const bool is_min = SCALAR_AGG_MIN == spec.kind_;
    const uint32_t requested = is_min
        ? share::aggregate::AGG_REDUCE_MIN
        : share::aggregate::AGG_REDUCE_MAX;
    const common::ObDatum *datum = is_min ? reduction.min_ : reduction.max_;
    const bool is_prefix = is_min ? reduction.min_is_prefix_ : reduction.max_is_prefix_;
    is_exact = false;
    if (OB_UNLIKELY((reduction.present_ & requested) != requested || nullptr == datum)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      // A NULL datum is an exact all-NULL summary.  Prefix is a valid physical
      // summary, but not an exact SQL MIN/MAX result.
      is_exact = !is_prefix;
    }
    return ret;
  }

  int probe_spec(
      const ObScalarPushdownAggregateSpec &spec,
      share::aggregate::ObIAggregateInputSegment &segment,
      bool &can_consume) const
  {
    int ret = OB_SUCCESS;
    can_consume = false;
    uint32_t requested = share::aggregate::AGG_REDUCE_NONE;
    if (SCALAR_AGG_COUNT_STAR == spec.kind_) {
      requested = share::aggregate::AGG_REDUCE_ROW_COUNT;
    } else if (SCALAR_AGG_COUNT_NONNULL == spec.kind_) {
      requested = share::aggregate::AGG_REDUCE_ROW_COUNT
          | share::aggregate::AGG_REDUCE_NULL_COUNT;
    } else {
      requested = SCALAR_AGG_MIN == spec.kind_
          ? share::aggregate::AGG_REDUCE_MIN
          : share::aggregate::AGG_REDUCE_MAX;
    }
    share::aggregate::ObAggregateReduction reduction;
    const int reduce_ret = segment.try_reduce(spec.slot_, requested, reduction);
    if (OB_SUCCESS == reduce_ret) {
      if (SCALAR_AGG_COUNT_STAR == spec.kind_
          || SCALAR_AGG_COUNT_NONNULL == spec.kind_) {
        if (OB_FAIL(validate_count_reduction(requested, reduction))) {
        } else {
          can_consume = true;
        }
      } else {
        bool is_exact = false;
        if (OB_FAIL(validate_extreme_reduction(spec, reduction, is_exact))) {
        } else if (is_exact) {
          can_consume = true;
        } else if (OB_FAIL(segment.can_read_values(spec.slot_, can_consume))) {
        }
      }
    } else if (OB_NOT_SUPPORTED == reduce_ret) {
      if (SCALAR_AGG_COUNT_STAR == spec.kind_) {
        can_consume = segment.selection().count_ >= 0;
      } else if (OB_FAIL(segment.can_read_values(spec.slot_, can_consume))) {
      }
    } else {
      ret = reduce_ret;
    }
    return ret;
  }

  int validate_values(
      const share::aggregate::ObIAggregateInputSegment &segment,
      const share::aggregate::ObAggregateValueBatchView &values) const
  {
    return OB_UNLIKELY(values.count_ < 0
                       || values.count_ != segment.selection().count_
                       || (values.count_ > 0 && nullptr == values.datums_))
        ? OB_ERR_UNEXPECTED
        : OB_SUCCESS;
  }

  int read_values(
      const ObScalarPushdownAggregateSpec &spec,
      share::aggregate::ObIAggregateInputSegment &segment,
      share::aggregate::ObAggregateValueBatchView &values) const
  {
    int ret = OB_SUCCESS;
    bool can_read = false;
    if (OB_FAIL(segment.can_read_values(spec.slot_, can_read))) {
    } else if (OB_UNLIKELY(!can_read)) {
      ret = OB_NOT_SUPPORTED;
    } else if (OB_FAIL(segment.read_values(spec.slot_, values))) {
    } else if (OB_FAIL(validate_values(segment, values))) {
    }
    return ret;
  }

  int stage_spec(
      const ObScalarPushdownAggregateSpec &spec,
      share::aggregate::ObIAggregateInputSegment &segment,
      ObScalarPushdownAggregateState &agg_state)
  {
    int ret = OB_SUCCESS;
    if (SCALAR_AGG_COUNT_STAR == spec.kind_
        || SCALAR_AGG_COUNT_NONNULL == spec.kind_) {
      const bool exclude_null = SCALAR_AGG_COUNT_NONNULL == spec.kind_;
      const uint32_t requested = share::aggregate::AGG_REDUCE_ROW_COUNT
          | (exclude_null ? share::aggregate::AGG_REDUCE_NULL_COUNT : 0);
      share::aggregate::ObAggregateReduction reduction;
      const int reduce_ret = segment.try_reduce(spec.slot_, requested, reduction);
      if (OB_SUCCESS == reduce_ret) {
        if (OB_FAIL(validate_count_reduction(requested, reduction))) {
        } else {
          agg_state.delta_ = reduction.row_count_
              - (exclude_null ? reduction.null_count_ : 0);
        }
      } else if (OB_NOT_SUPPORTED != reduce_ret) {
        ret = reduce_ret;
      } else if (!exclude_null) {
        if (OB_UNLIKELY(segment.selection().count_ < 0)) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          agg_state.delta_ = segment.selection().count_;
        }
      } else {
        share::aggregate::ObAggregateValueBatchView values;
        if (OB_FAIL(read_values(spec, segment, values))) {
        } else {
          for (int64_t i = 0; i < values.count_; ++i) {
            agg_state.delta_ += !values.datums_[i].is_null();
          }
        }
      }
    } else {
      const bool is_min = SCALAR_AGG_MIN == spec.kind_;
      const uint32_t requested = is_min
          ? share::aggregate::AGG_REDUCE_MIN
          : share::aggregate::AGG_REDUCE_MAX;
      const common::ObDatum *candidate = nullptr;
      share::aggregate::ObAggregateReduction reduction;
      const int reduce_ret = segment.try_reduce(spec.slot_, requested, reduction);
      bool use_values = OB_NOT_SUPPORTED == reduce_ret;
      if (OB_SUCCESS == reduce_ret) {
        bool is_exact = false;
        if (OB_FAIL(validate_extreme_reduction(spec, reduction, is_exact))) {
        } else if (is_exact) {
          candidate = is_min ? reduction.min_ : reduction.max_;
        } else {
          use_values = true;
        }
      } else if (OB_NOT_SUPPORTED != reduce_ret) {
        ret = reduce_ret;
      }
      if (OB_SUCC(ret) && use_values) {
        share::aggregate::ObAggregateValueBatchView values;
        if (OB_FAIL(read_values(spec, segment, values))) {
        } else {
          for (int64_t i = 0; OB_SUCC(ret) && i < values.count_; ++i) {
            const common::ObDatum &datum = values.datums_[i];
            int cmp = 0;
            if (datum.is_null()) {
            } else if (nullptr == candidate) {
              candidate = &datum;
            } else if (OB_FAIL(spec.cmp_func_(*candidate, datum, cmp, access_ctx_))) {
            } else if ((is_min && cmp > 0) || (!is_min && cmp < 0)) {
              candidate = &datum;
            }
          }
        }
      }
      if (OB_SUCC(ret) && nullptr != candidate && !candidate->is_null()) {
        int cmp = 0;
        bool replace = !agg_state.current_.has_value_;
        if (!replace
            && OB_FAIL(
                spec.cmp_func_(agg_state.current_.datum_, *candidate, cmp, access_ctx_))) {
        } else if (!replace) {
          replace = (is_min && cmp > 0) || (!is_min && cmp < 0);
        }
        if (OB_SUCC(ret) && replace) {
          if (OB_FAIL(agg_state.staging_.assign(*candidate, allocator_))) {
          } else {
            agg_state.staging_ready_ = true;
          }
        }
      }
    }
    return ret;
  }

  int materialize()
  {
    int ret = OB_SUCCESS;
    ObEvalCtx::BatchInfoScopeGuard batch_guard(eval_ctx_);
    if (rich_format_) {
      // Scalar aggregate output is always the first row in the returned
      // batch.  Input projection may leave the shared eval context at a
      // different batch index, so normalize it before initializing vectors.
      batch_guard.set_batch_idx(0);
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < specs_.count(); ++i) {
      const ObScalarPushdownAggregateSpec &spec = specs_.at(i);
      const ObScalarPushdownAggregateState &agg_state = states_.at(i);
      ObExpr *expr = spec.output_;
      if (OB_ISNULL(expr)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("null aggregate output expression", K(ret), K(i));
      }
#if 0
      else if (rich_format_) {
        const int64_t output_idx = 0;
        if (OB_FAIL(expr->init_vector_for_write(
            eval_ctx_, expr->get_default_res_format(), eval_ctx_.max_batch_size_))) {
          LOG_WARN("failed to initialize scalar aggregate output vector", K(ret), K(i), K(output_idx));
        } else if (OB_ISNULL(expr->get_vector(eval_ctx_))) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("null scalar aggregate output vector", K(ret), K(i), K(output_idx));
        } else if (SCALAR_AGG_COUNT_STAR == spec.kind_
                   || SCALAR_AGG_COUNT_NONNULL == spec.kind_) {
          expr->get_vector(eval_ctx_)->set_int(output_idx, agg_state.count_);
          expr->get_eval_info(eval_ctx_).evaluated_ = true;
        } else if (!agg_state.current_.has_value_) {
          expr->get_vector(eval_ctx_)->set_null(output_idx);
          expr->get_eval_info(eval_ctx_).evaluated_ = true;
        } else if (OBJ_DATUM_STRING == expr->obj_datum_map_) {
          const common::ObDatum &datum = agg_state.current_.datum_;
          char *result_buf = expr->get_str_res_mem(eval_ctx_, datum.len_, output_idx);
          if (OB_ISNULL(result_buf)) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
          } else {
            if (datum.len_ > 0) {
              MEMCPY(result_buf, datum.ptr_, datum.len_);
            }
            expr->get_vector(eval_ctx_)->set_payload_shallow(
                output_idx, result_buf, datum.len_);
            expr->get_eval_info(eval_ctx_).evaluated_ = true;
          }
        } else {
          const common::ObDatum &datum = agg_state.current_.datum_;
          expr->get_vector(eval_ctx_)->set_payload(output_idx, datum.ptr_, datum.len_);
          expr->get_eval_info(eval_ctx_).evaluated_ = true;
        }
      }
#endif
      else if (SCALAR_AGG_COUNT_STAR == spec.kind_
                 || SCALAR_AGG_COUNT_NONNULL == spec.kind_) {
        expr->locate_datum_for_write(eval_ctx_).set_int(agg_state.count_);
        expr->get_eval_info(eval_ctx_).evaluated_ = true;
      } else if (!agg_state.current_.has_value_) {
        expr->locate_datum_for_write(eval_ctx_).set_null();
        expr->get_eval_info(eval_ctx_).evaluated_ = true;
      } else if (OB_FAIL(expr->deep_copy_datum(eval_ctx_, agg_state.current_.datum_))) {
        LOG_WARN("failed to copy scalar MIN/MAX output", K(ret), K(i));
      } else {
        expr->get_eval_info(eval_ctx_).evaluated_ = true;
      }
    }
    return ret;
  }

private:
  ObEvalCtx &eval_ctx_;
  const common::ObDatumAccessContext *access_ctx_;
  bool rich_format_;
  common::ObIAllocator &allocator_;
  common::ObSEArray<ObScalarPushdownAggregateSpec, 4> specs_;
  common::ObSEArray<ObScalarPushdownAggregateState, 4> states_;
  share::aggregate::ObPushdownAggregateProgramState state_;
};

#if 0
bool is_supported_processor_sum_pair(
    const common::VecValueTypeClass input_tc,
    const common::VecValueTypeClass output_tc)
{
  bool supported = false;
  switch (input_tc) {
    case common::VEC_TC_INTEGER:
    case common::VEC_TC_UINTEGER:
      supported = common::VEC_TC_NUMBER == output_tc
          || common::VEC_TC_DEC_INT64 == output_tc
          || common::VEC_TC_DEC_INT128 == output_tc
          || common::VEC_TC_DEC_INT256 == output_tc
          || common::VEC_TC_DEC_INT512 == output_tc;
      break;
    case common::VEC_TC_FLOAT:
      supported = common::VEC_TC_FLOAT == output_tc;
      break;
    case common::VEC_TC_FIXED_DOUBLE:
      supported = common::VEC_TC_FIXED_DOUBLE == output_tc;
      break;
    case common::VEC_TC_DOUBLE:
      supported = common::VEC_TC_DOUBLE == output_tc;
      break;
    case common::VEC_TC_NUMBER:
      supported = common::VEC_TC_NUMBER == output_tc;
      break;
    case common::VEC_TC_DEC_INT32:
      supported = common::VEC_TC_NUMBER == output_tc
          || common::VEC_TC_DEC_INT128 == output_tc;
      break;
    case common::VEC_TC_DEC_INT64:
      supported = common::VEC_TC_NUMBER == output_tc
          || common::VEC_TC_DEC_INT128 == output_tc
          || common::VEC_TC_DEC_INT256 == output_tc;
      break;
    case common::VEC_TC_DEC_INT128:
      supported = common::VEC_TC_NUMBER == output_tc
          || common::VEC_TC_DEC_INT128 == output_tc
          || common::VEC_TC_DEC_INT256 == output_tc;
      break;
    case common::VEC_TC_DEC_INT256:
      supported = common::VEC_TC_NUMBER == output_tc
          || common::VEC_TC_DEC_INT256 == output_tc
          || common::VEC_TC_DEC_INT512 == output_tc;
      break;
    case common::VEC_TC_DEC_INT512:
      supported = common::VEC_TC_NUMBER == output_tc
          || common::VEC_TC_DEC_INT512 == output_tc;
      break;
    default:
      // Collection/LOB SUM needs an explicit ownership contract before it can
      // cross the aggregate input seam.
      break;
  }
  return supported;
}

bool is_processor_sum_program(
    const common::ObIArray<ObExpr *> &aggregate_exprs,
    const bool rich_format)
{
  bool supported = rich_format && !aggregate_exprs.empty();
  for (int64_t i = 0; supported && i < aggregate_exprs.count(); ++i) {
    const ObExpr *expr = aggregate_exprs.at(i);
    const ObExpr *input = nullptr;
    if (OB_ISNULL(expr)
        || T_FUN_SUM != expr->type_
        || 1 != expr->arg_cnt_
        || OB_ISNULL(expr->args_)
        || OB_ISNULL(input = expr->args_[0])) {
      supported = false;
    } else {
      const common::VecValueTypeClass input_tc = common::get_vec_value_tc(
          input->datum_meta_.type_, input->datum_meta_.scale_, input->datum_meta_.precision_);
      const common::VecValueTypeClass output_tc = common::get_vec_value_tc(
          expr->datum_meta_.type_, expr->datum_meta_.scale_, expr->datum_meta_.precision_);
      supported = is_supported_processor_sum_pair(input_tc, output_tc);
    }
  }
  return supported;
}

// The first Processor-backed slice deliberately accepts only rich scalar SUM
// programs.  It reuses the regular SQL aggregate implementation while keeping
// the storage seam limited to canonical slots and borrowed datum batches.
class ObProcessorSumPushdownAggregateProgram final
  : public share::aggregate::ObIPushdownAggregateProgram
{
public:
  ObProcessorSumPushdownAggregateProgram(
      ObEvalCtx &eval_ctx,
      common::ObIAllocator &allocator)
    : eval_ctx_(eval_ctx),
      allocator_(allocator),
      monitor_info_(),
      aggr_infos_(allocator),
      processor_(eval_ctx, aggr_infos_, ObModIds::OB_SQL_AGGR_FUNC_ROW, monitor_info_),
      group_row_(nullptr),
      has_rows_(false),
      state_(share::aggregate::AGG_PROGRAM_NEW)
  {}

  ~ObProcessorSumPushdownAggregateProgram() override = default;

  void destroy() override
  {
    common::ObIAllocator *allocator = &allocator_;
    this->~ObProcessorSumPushdownAggregateProgram();
    allocator->free(this);
  }

  int init(const common::ObIArray<ObExpr *> &aggregate_exprs)
  {
    int ret = OB_SUCCESS;
    if (OB_UNLIKELY(!eval_ctx_.is_vectorized()
                    || !is_processor_sum_program(aggregate_exprs, true))) {
      ret = OB_NOT_SUPPORTED;
    } else if (OB_FAIL(aggr_infos_.init(aggregate_exprs.count()))) {
      LOG_WARN("failed to initialize Processor SUM aggregate infos",
               K(ret), K(aggregate_exprs.count()));
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < aggregate_exprs.count(); ++i) {
      ObExpr *expr = aggregate_exprs.at(i);
      ObAggrInfo info(allocator_);
      if (OB_FAIL(info.param_exprs_.init(1))) {
        LOG_WARN("failed to initialize SUM parameter list", K(ret), K(i));
      } else if (OB_FAIL(info.param_exprs_.push_back(expr->args_[0]))) {
        LOG_WARN("failed to append SUM parameter expression", K(ret), K(i));
      } else {
        info.expr_ = expr;
        info.real_aggr_type_ = T_FUN_SUM;
        if (OB_FAIL(aggr_infos_.push_back(info))) {
          LOG_WARN("failed to append Processor SUM aggregate info", K(ret), K(i));
        }
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(processor_.init())) {
      LOG_WARN("failed to initialize Processor-backed SUM", K(ret));
    } else if (OB_FAIL(processor_.init_one_group())) {
      LOG_WARN("failed to initialize Processor SUM row", K(ret));
    } else if (OB_FAIL(processor_.get_group_row(0, group_row_))) {
      LOG_WARN("failed to obtain Processor SUM row", K(ret));
    }
    return ret;
  }

  share::aggregate::ObPushdownAggregateProgramState state() const override
  { return state_; }

  int reset_scan() override
  {
    int ret = OB_SUCCESS;
    clear_expression_flags();
    processor_.reuse();
    group_row_ = nullptr;
    has_rows_ = false;
    if (OB_FAIL(processor_.init())) {
      LOG_WARN("failed to reset Processor-backed SUM", K(ret));
      state_ = share::aggregate::AGG_PROGRAM_FAILED;
    } else if (OB_FAIL(processor_.init_one_group())) {
      LOG_WARN("failed to reset Processor SUM row", K(ret));
      state_ = share::aggregate::AGG_PROGRAM_FAILED;
    } else if (OB_FAIL(processor_.get_group_row(0, group_row_))) {
      LOG_WARN("failed to obtain reset Processor SUM row", K(ret));
      state_ = share::aggregate::AGG_PROGRAM_FAILED;
    } else {
      state_ = share::aggregate::AGG_PROGRAM_NEW;
    }
    return ret;
  }

  int can_consume(
      share::aggregate::ObIAggregateInputSegment &segment,
      bool &can_consume) override
  {
    int ret = OB_SUCCESS;
    can_consume = false;
    if (OB_UNLIKELY(share::aggregate::AGG_PROGRAM_NEW != state_
                    && share::aggregate::AGG_PROGRAM_CONSUMING != state_)) {
      ret = OB_STATE_NOT_MATCH;
    } else if (OB_UNLIKELY(segment.selection().count_ < 0)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      can_consume = true;
    }
    for (int64_t i = 0; OB_SUCC(ret) && can_consume && i < aggr_infos_.count(); ++i) {
      if (OB_FAIL(segment.can_read_values(
              static_cast<share::aggregate::ObAggregateInputSlot>(i), can_consume))) {
        LOG_WARN("failed to probe Processor SUM input", K(ret), K(i));
      }
    }
    return ret;
  }

  int consume(share::aggregate::ObIAggregateInputSegment &segment) override
  {
    int ret = OB_SUCCESS;
    const int64_t selected_count = segment.selection().count_;
    common::ObSEArray<share::aggregate::ObAggregateValueBatchView, 4> values;
    if (OB_UNLIKELY(share::aggregate::AGG_PROGRAM_NEW != state_
                    && share::aggregate::AGG_PROGRAM_CONSUMING != state_)) {
      ret = OB_STATE_NOT_MATCH;
    } else if (OB_UNLIKELY(selected_count < 0)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(values.reserve(aggr_infos_.count()))) {
      LOG_WARN("failed to reserve Processor SUM input views", K(ret));
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < aggr_infos_.count(); ++i) {
      bool can_read = false;
      share::aggregate::ObAggregateValueBatchView view;
      if (OB_FAIL(segment.can_read_values(
              static_cast<share::aggregate::ObAggregateInputSlot>(i), can_read))) {
        LOG_WARN("failed to probe Processor SUM values", K(ret), K(i));
      } else if (OB_UNLIKELY(!can_read)) {
        ret = OB_NOT_SUPPORTED;
      } else if (OB_FAIL(segment.read_values(
                     static_cast<share::aggregate::ObAggregateInputSlot>(i), view))) {
        LOG_WARN("failed to read Processor SUM values", K(ret), K(i));
      } else if (OB_UNLIKELY(view.count_ != selected_count
                             || view.count_ < 0
                             || (view.count_ > 0 && OB_ISNULL(view.datums_)))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid Processor SUM value batch",
                 K(ret), K(i), K(view.count_), K(selected_count), KP(view.datums_));
      } else if (OB_FAIL(values.push_back(view))) {
        LOG_WARN("failed to append Processor SUM input view", K(ret), K(i));
      }
    }
    for (int64_t row_idx = 0; OB_SUCC(ret) && row_idx < selected_count; ++row_idx) {
      clear_expression_flags();
      ObEvalCtx::BatchInfoScopeGuard batch_guard(eval_ctx_);
      batch_guard.set_batch_size(1);
      batch_guard.set_batch_idx(0);
      for (int64_t agg_idx = 0; OB_SUCC(ret) && agg_idx < aggr_infos_.count(); ++agg_idx) {
        ObExpr *param_expr = aggr_infos_.at(agg_idx).param_exprs_.at(0);
        const common::ObDatum &datum = values.at(agg_idx).datums_[row_idx];
        if (OB_ISNULL(param_expr)
            || OB_UNLIKELY(!datum.is_null()
                           && (OB_ISNULL(datum.ptr_) || 0 == datum.len_))) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_FAIL(param_expr->init_vector_for_write(
                       eval_ctx_, param_expr->get_default_res_format(), 1))) {
          LOG_WARN("failed to initialize Processor SUM input vector",
                   K(ret), K(agg_idx));
        } else if (OB_ISNULL(param_expr->get_vector(eval_ctx_))) {
          ret = OB_ERR_UNEXPECTED;
        } else if (datum.is_null()) {
          param_expr->get_vector(eval_ctx_)->set_null(0);
        } else {
          param_expr->get_vector(eval_ctx_)->set_payload_shallow(
              0, datum.ptr_, datum.len_);
        }
      }
      uint64_t skip_word = 0;
      ObBitVector &skip = *to_bit_vector(&skip_word);
      skip.reset(1);
      ObBatchRows batch_rows(skip, 1, true);
      if (OB_SUCC(ret) && OB_FAIL(
              processor_.process_batch(*group_row_, batch_rows, 0, 1))) {
        LOG_WARN("Processor SUM process_batch failed", K(ret), K(row_idx));
      }
    }
    if (OB_SUCC(ret)) {
      has_rows_ = has_rows_ || selected_count > 0;
      state_ = share::aggregate::AGG_PROGRAM_CONSUMING;
    } else {
      clear_expression_flags();
      state_ = share::aggregate::AGG_PROGRAM_FAILED;
    }
    return ret;
  }

  int seal() override
  {
    int ret = OB_SUCCESS;
    if (share::aggregate::AGG_PROGRAM_NEW == state_
        || share::aggregate::AGG_PROGRAM_CONSUMING == state_) {
      state_ = share::aggregate::AGG_PROGRAM_SEALED;
    } else {
      ret = OB_STATE_NOT_MATCH;
      clear_expression_flags();
      state_ = share::aggregate::AGG_PROGRAM_FAILED;
    }
    return ret;
  }

  int emit(
      const int64_t max_rows,
      share::aggregate::ObAggregateEmitResult &result) override
  {
    int ret = OB_SUCCESS;
    result = share::aggregate::ObAggregateEmitResult();
    if (share::aggregate::AGG_PROGRAM_END == state_) {
      result.end_ = true;
    } else if (OB_UNLIKELY(max_rows <= 0
                           || (share::aggregate::AGG_PROGRAM_SEALED != state_
                               && share::aggregate::AGG_PROGRAM_EMITTING != state_))) {
      ret = OB_STATE_NOT_MATCH;
      clear_expression_flags();
      state_ = share::aggregate::AGG_PROGRAM_FAILED;
    } else if (OB_ISNULL(group_row_)) {
      ret = OB_ERR_UNEXPECTED;
      clear_expression_flags();
      state_ = share::aggregate::AGG_PROGRAM_FAILED;
    } else {
      state_ = share::aggregate::AGG_PROGRAM_EMITTING;
      ObEvalCtx::BatchInfoScopeGuard batch_guard(eval_ctx_);
      batch_guard.set_batch_size(1);
      batch_guard.set_batch_idx(0);
      if (!has_rows_) {
        ret = processor_.collect_for_empty_set();
      } else {
        ret = processor_.collect();
      }
      if (OB_FAIL(ret)) {
        LOG_WARN("failed to materialize Processor SUM result", K(ret));
        clear_expression_flags();
        state_ = share::aggregate::AGG_PROGRAM_FAILED;
      } else {
        result.row_count_ = 1;
        result.end_ = true;
        state_ = share::aggregate::AGG_PROGRAM_END;
      }
    }
    return ret;
  }

private:
  void clear_expression_flags()
  {
    for (int64_t i = 0; i < aggr_infos_.count(); ++i) {
      ObAggrInfo &info = aggr_infos_.at(i);
      if (OB_NOT_NULL(info.expr_)) {
        info.expr_->get_eval_info(eval_ctx_).clear_evaluated_flag();
      }
      for (int64_t j = 0; j < info.param_exprs_.count(); ++j) {
        if (OB_NOT_NULL(info.param_exprs_.at(j))) {
          info.param_exprs_.at(j)->get_eval_info(eval_ctx_).clear_evaluated_flag();
        }
      }
    }
  }

private:
  ObEvalCtx &eval_ctx_;
  common::ObIAllocator &allocator_;
  ObMonitorNode monitor_info_;
  common::ObFixedArray<ObAggrInfo, common::ObIAllocator> aggr_infos_;
  ObAggregateProcessor processor_;
  ObAggregateProcessor::GroupRow *group_row_;
  bool has_rows_;
  share::aggregate::ObPushdownAggregateProgramState state_;
};
#endif

} // namespace

namespace
{

int create_pushdown_aggregate_program_instance(
    ObEvalCtx &eval_ctx,
    const common::ObIArray<ObExpr *> &aggregate_exprs,
    const bool rich_format,
    common::ObIAllocator &allocator,
    share::aggregate::ObIPushdownAggregateProgram *&program)
{
  int ret = OB_SUCCESS;
  void *buf = nullptr;
  ObScalarPushdownAggregateProgram *scalar_program = nullptr;
  program = nullptr;
  if (OB_ISNULL(buf = allocator.alloc(sizeof(ObScalarPushdownAggregateProgram)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to allocate pushdown aggregate program", K(ret));
  } else {
    scalar_program = new (buf) ObScalarPushdownAggregateProgram(
        eval_ctx, rich_format, allocator);
    if (OB_FAIL(scalar_program->init(aggregate_exprs))) {
      scalar_program->~ObScalarPushdownAggregateProgram();
      allocator.free(scalar_program);
      scalar_program = nullptr;
    } else {
      program = scalar_program;
    }
  }
  return ret;
}

class ObPushdownAggregatePlan final
  : public share::aggregate::ObIPushdownAggregatePlan
{
public:
  ObPushdownAggregatePlan(
      ObEvalCtx &eval_ctx,
      const common::ObIArray<ObExpr *> &aggregate_exprs,
      const bool rich_format,
      common::ObIAllocator &allocator)
    : eval_ctx_(eval_ctx),
      aggregate_exprs_(aggregate_exprs),
      rich_format_(rich_format),
      allocator_(allocator)
  {}

  ~ObPushdownAggregatePlan() override = default;

  void destroy() override
  {
    common::ObIAllocator *allocator = &allocator_;
    this->~ObPushdownAggregatePlan();
    allocator->free(this);
  }

  int validate()
  {
    int ret = OB_SUCCESS;
    share::aggregate::ObIPushdownAggregateProgram *probe = nullptr;
    if (OB_FAIL(create_program(probe))) {
    } else {
      probe->destroy();
      probe = nullptr;
    }
    return ret;
  }

  int create_program(
      share::aggregate::ObIPushdownAggregateProgram *&program) const override
  {
    return create_pushdown_aggregate_program_instance(
        eval_ctx_, aggregate_exprs_, rich_format_, allocator_, program);
  }

private:
  ObEvalCtx &eval_ctx_;
  const common::ObIArray<ObExpr *> &aggregate_exprs_;
  bool rich_format_;
  common::ObIAllocator &allocator_;
  DISALLOW_COPY_AND_ASSIGN(ObPushdownAggregatePlan);
};

} // namespace

int create_pushdown_aggregate_plan(
    ObEvalCtx &eval_ctx,
    const common::ObIArray<ObExpr *> &aggregate_exprs,
    const bool rich_format,
    common::ObIAllocator &allocator,
    share::aggregate::ObIPushdownAggregatePlan *&plan)
{
  int ret = OB_SUCCESS;
  void *buf = nullptr;
  ObPushdownAggregatePlan *aggregate_plan = nullptr;
  plan = nullptr;
  if (OB_ISNULL(buf = allocator.alloc(sizeof(ObPushdownAggregatePlan)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to allocate pushdown aggregate plan", K(ret));
  } else {
    aggregate_plan = new (buf) ObPushdownAggregatePlan(
        eval_ctx, aggregate_exprs, rich_format, allocator);
    if (OB_FAIL(aggregate_plan->validate())) {
      aggregate_plan->destroy();
      aggregate_plan = nullptr;
    } else {
      plan = aggregate_plan;
    }
  }
  return ret;
}

void destroy_pushdown_aggregate_plan(
    share::aggregate::ObIPushdownAggregatePlan *&plan)
{
  if (nullptr != plan) {
    plan->destroy();
    plan = nullptr;
  }
}

} // namespace sql
} // namespace oceanbase
