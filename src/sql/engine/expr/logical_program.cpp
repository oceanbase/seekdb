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
#include "sql/engine/expr/logical_program.h"
#include "sql/engine/ob_exec_context.h"
#include "lib/stat/ob_diagnostic_info_guard.h"
#include "lib/utility/ob_common_utility.h"
#include "rust/expression_program.h"
#include "rust/expression-program/include/host.h"
#include <algorithm>

namespace oceanbase { namespace sql {
using namespace common;
int calc_bool_expr_for_integer_type(const ObExpr &, ObEvalCtx &, ObDatum &);
OB_SERIALIZE_MEMBER(LogicalProgramInfo, image_, parents_, ordinals_);

int LogicalProgramInfo::deep_copy(ObIAllocator &allocator, ObExprOperatorType type,
                                 ObIExprExtraInfo *&copy) const
{
  int ret = OB_SUCCESS;
  auto *info = OB_NEWx(LogicalProgramInfo, &allocator, allocator, type);
  copy = nullptr;
  if (info == nullptr) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(info->image_.assign(image_))) {
  } else if (OB_FAIL(info->parents_.assign(parents_))) {
  } else if (OB_FAIL(info->ordinals_.assign(ordinals_))) {
  } else {
    copy = info;
  }
  return ret;
}

namespace {
bool is_logical(const ObExpr &expr)
{
  return expr.type_ == T_OP_AND || expr.type_ == T_OP_OR || expr.type_ == T_OP_NOT;
}

bool is_materialized_integer_normalization(const ObExpr &expr)
{
  if (expr.type_ != T_OP_BOOL || expr.eval_func_ != calc_bool_expr_for_integer_type
      || (expr.eval_batch_func_ != nullptr && expr.eval_batch_func_ != expr_default_eval_batch_func)
      || expr.arg_cnt_ != 1 || expr.args_ == nullptr || expr.args_[0] == nullptr) {
    return false;
  }
  const ObExpr &column = *expr.args_[0];
  return column.type_ == T_REF_COLUMN && column.eval_func_ == nullptr
      && column.eval_batch_func_ == nullptr
      && (ob_is_int_uint_tc(column.datum_meta_.type_) || column.datum_meta_.type_ == ObBitType);
}

int poll_logical_status(ObEvalCtx &ctx, int64_t &visits)
{
  if (++visits < 4096) { return OB_SUCCESS; }
  visits = 0;
  return ctx.exec_ctx_.check_status();
}

struct LogicalBuilder
{
  explicit LogicalBuilder(ObIAllocator &allocator)
    : nodes_(sizeof(const ObExpr *), ObWrapperAllocator(allocator)),
      instructions_(sizeof(expression::LogicalInstruction), ObWrapperAllocator(allocator)),
      arguments_(sizeof(uint32_t), ObWrapperAllocator(allocator)),
      parents_(sizeof(int64_t), ObWrapperAllocator(allocator)),
      ordinals_(sizeof(int64_t), ObWrapperAllocator(allocator)), allocator_(allocator) {}
  int lower(const ObExpr &expr, uint32_t &id)
  {
    int ret = OB_SUCCESS;
    if (OB_FAIL(check_stack_overflow())) {
      return ret;
    }
    for (int64_t i = 0; i < nodes_.count(); ++i) {
      if (nodes_.at(i) == &expr) {
        id = i;
        return OB_SUCCESS;
      }
    }
    ObSEArray<uint32_t, 4, ObWrapperAllocator> children(sizeof(uint32_t), ObWrapperAllocator(allocator_));
    if (is_logical(expr)) {
      if (expr.args_ == nullptr || (expr.type_ == T_OP_NOT ? expr.arg_cnt_ != 1 : expr.arg_cnt_ < 2)) {
        return OB_INVALID_ARGUMENT;
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < expr.arg_cnt_; ++i) {
        uint32_t child = 0;
        if (expr.args_[i] == nullptr) {
          ret = OB_INVALID_ARGUMENT;
        } else if (OB_FAIL(lower(*expr.args_[i], child))) {
        } else if (OB_FAIL(children.push_back(child))) {
        }
      }
    }
    if (OB_SUCC(ret) && (nodes_.count() >= UINT32_MAX || arguments_.count() > UINT32_MAX - children.count())) {
      ret = OB_SIZE_OVERFLOW;
    }
    if (OB_SUCC(ret)) {
      id = nodes_.count();
      expression::LogicalInstruction instruction;
      instruction.opcode = expr.type_ == T_OP_AND ? 13 : expr.type_ == T_OP_OR ? 14
          : expr.type_ == T_OP_NOT ? 15 : 0;
      instruction.argument_start = arguments_.count();
      instruction.argument_count = children.count();
      if (OB_FAIL(nodes_.push_back(&expr))) {
      } else if (OB_FAIL(instructions_.push_back(instruction))) {
      } else if (OB_FAIL(parents_.push_back(-1))) {
      } else if (OB_FAIL(ordinals_.push_back(-1))) {
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < children.count(); ++i) {
        const uint32_t child = children.at(i);
        if (OB_FAIL(arguments_.push_back(child))) {
        } else if (parents_.at(child) < 0) {
          parents_.at(child) = id;
          ordinals_.at(child) = i;
        }
      }
    }
    return ret;
  }
  ObSEArray<const ObExpr *, 16, ObWrapperAllocator> nodes_;
  ObSEArray<expression::LogicalInstruction, 16, ObWrapperAllocator> instructions_;
  ObSEArray<uint32_t, 16, ObWrapperAllocator> arguments_;
  ObSEArray<int64_t, 16, ObWrapperAllocator> parents_;
  ObSEArray<int64_t, 16, ObWrapperAllocator> ordinals_;
  ObIAllocator &allocator_;
};

class LogicalHost final : public expression::ILogicalHost
{
public:
  LogicalHost(ObEvalCtx &ctx, const ObIArray<const ObExpr *> &nodes, bool batch, int64_t rows,
              ObDatum *scalar_result)
    : ctx_(ctx), nodes_(nodes), batch_(batch), rows_(rows), scalar_result_(scalar_result), publication_safe_(true) {}
  int32_t check_cancel() override { return ctx_.exec_ctx_.check_status(); }
  int publish_completed(rust::Slice<const uint64_t> values,
                        rust::Slice<const uint64_t> flags, size_t rows, bool include_root)
  {
    if (!publication_safe_) { return OB_SUCCESS; }
    int64_t visits = 0;
    const int64_t count = nodes_.count();
    const size_t value_count = values.size();
    if (rows != static_cast<size_t>(rows_) || value_count != flags.size()
        || value_count != static_cast<size_t>(count) * rows) {
      return OB_INVALID_ARGUMENT;
    }
    const uint64_t *value_data = values.data();
    const uint64_t *flag_data = flags.data();
    ObEvalCtx::BatchInfoScopeGuard batch_guard(ctx_);
    for (int64_t node = 0; node < count; ++node) {
      const ObExpr &target = *nodes_.at(node);
      if (!is_logical(target) || (node == count - 1 && !include_root)) { continue; }
      ObEvalInfo &published = target.get_eval_info(ctx_);
      bool initialized = published.evaluated_ || published.projected_;
      for (int64_t row = 0; row < rows_; ++row) {
        const int poll_ret = poll_logical_status(ctx_, visits);
        if (poll_ret != OB_SUCCESS) { return poll_ret; }
        const uint64_t state = flag_data[node * rows_ + row];
        if ((state & 1) == 0) { continue; }
        const int64_t index = batch_ ? row : ctx_.get_batch_idx();
        const bool cached = published.projected_ || (published.evaluated_
            && (!target.is_batch_result() || target.get_evaluated_flags(ctx_).at(index)));
        if (cached) { continue; }
        if (!initialized && target.is_batch_result()) {
          target.get_evaluated_flags(ctx_).reset(batch_ ? rows_ : ctx_.get_batch_size());
        }
        initialized = true;
        if (batch_) { batch_guard.set_batch_idx(row); }
        ObDatum &datum = node == count - 1 && !batch_
            ? *scalar_result_ : target.locate_datum_for_write(ctx_);
        if ((state & 2) == 0) { datum.set_null(); }
        else { datum.set_int(value_data[node * rows_ + row]); }
        published.evaluated_ = true;
        published.point_to_frame_ = true;
        published.notnull_ = false;
        published.cnt_ = batch_ ? rows_ : ctx_.get_batch_size();
        if (target.is_batch_result()) { target.get_evaluated_flags(ctx_).set(index); }
      }
    }
    return OB_SUCCESS;
  }
  int32_t demand(size_t node, rust::Slice<const uint64_t> demand,
                 rust::Slice<uint64_t> values, rust::Slice<uint64_t> flags) override
  {
    int ret = OB_SUCCESS;
    int64_t visits = 0;
    const size_t count = nodes_.count();
    const size_t demand_count = demand.size();
    const size_t value_count = values.size();
    if (node >= count || demand_count != static_cast<size_t>(rows_)
        || value_count != count * demand_count || flags.size() != value_count) {
      return OB_INVALID_ARGUMENT;
    }
    const uint64_t *demand_data = demand.data();
    uint64_t *value_data = values.data();
    uint64_t *flag_data = flags.data();
    const ObExpr &input = *nodes_.at(node);
    if (!is_logical(input)) {
      // This normalizer only reads a materialized column and writes its own cache.
      // Every other evaluator may reenter or invalidate shared logical frames.
      const bool handoff = !is_materialized_integer_normalization(input);
      if (handoff) {
        if (OB_FAIL(publish_completed(
            rust::Slice<const uint64_t>(value_data, value_count),
            rust::Slice<const uint64_t>(flag_data, value_count), rows_, false))) {
          return ret;
        }
        publication_safe_ = false;
      }
      if (batch_) {
        ObEvalCtx::TempAllocGuard guard(ctx_);
        const int64_t bytes = ObBitVector::memory_size(rows_);
        auto *memory = guard.get_allocator().alloc(bytes);
        if (memory == nullptr) {
          return OB_ALLOCATE_MEMORY_FAILED;
        }
        auto &skip = *to_bit_vector(memory);
        skip.reset(rows_);
        for (int64_t row = 0; row < rows_; ++row) {
          const int poll_ret = poll_logical_status(ctx_, visits);
          if (poll_ret != OB_SUCCESS) { return poll_ret; }
          if (demand_data[row] == 0) { skip.set(row); }
        }
        ret = input.eval_batch(ctx_, skip, rows_);
      } else {
        ObDatum *datum = nullptr;
        ret = input.eval(ctx_, datum);
      }
      for (size_t cached_node = 0; handoff && cached_node < count; ++cached_node) {
        const ObExpr &cached = *nodes_.at(cached_node);
        const ObEvalInfo &available = cached.get_eval_info(ctx_);
        for (int64_t row = 0; row < rows_; ++row) {
          const int poll_ret = poll_logical_status(ctx_, visits);
          if (poll_ret != OB_SUCCESS) { return ret == OB_SUCCESS ? poll_ret : ret; }
          const size_t slot = cached_node * rows_ + row;
          if ((flag_data[slot] & 8) != 0 || (cached_node == node && demand_data[row] != 0)) {
            continue;
          }
          const int64_t index = batch_ ? row : ctx_.get_batch_idx();
          const bool ready = available.projected_ || cached.eval_func_ == nullptr
              || (available.evaluated_ && (!cached.is_batch_result()
                  || cached.get_evaluated_flags(ctx_).at(index)));
          flag_data[slot] = ready ? 4 : 0;
        }
      }
      publication_safe_ = true;
    }
    for (int64_t row = 0; OB_SUCC(ret) && row < rows_; ++row) {
      const int poll_ret = poll_logical_status(ctx_, visits);
      if (poll_ret != OB_SUCCESS) { return poll_ret; }
      if (demand_data[row] == 0) { continue; }
      const int64_t index = batch_ ? row : ctx_.get_batch_idx();
      if (is_logical(input)) {
        const ObEvalInfo &available = input.get_eval_info(ctx_);
        if (!available.projected_ && !(available.evaluated_
            && (!input.is_batch_result() || input.get_evaluated_flags(ctx_).at(index)))) {
          return OB_ERR_UNEXPECTED;
        }
      }
      const ObDatum &datum = input.locate_expr_datum(ctx_, index);
      const size_t slot = node * rows_ + row;
      flag_data[slot] = datum.is_null() ? 1 : 3;
      value_data[slot] = datum.is_null() ? 0 : static_cast<uint64_t>(datum.get_int());
    }
    return ret;
  }
private:
  ObEvalCtx &ctx_;
  const ObIArray<const ObExpr *> &nodes_;
  bool batch_;
  int64_t rows_;
  ObDatum *scalar_result_;
  // External evaluation can invalidate frames before reconciliation completes.
  bool publication_safe_;
};

int run_logical(const ObExpr &expr, ObEvalCtx &ctx, const ObBitVector *skip,
                int64_t rows, bool batch, ObDatum *scalar_result)
{
  int ret = OB_SUCCESS;
  if (rows < 0 || expr.extra_info_ == nullptr || !is_logical(expr)) {
    return OB_INVALID_ARGUMENT;
  }
  const auto &info = *static_cast<const LogicalProgramInfo *>(expr.extra_info_);
  const int64_t count = info.parents_.count();
  if (count == 0 || info.ordinals_.count() != count || info.image_.empty()
      || info.parents_.at(count - 1) != -1 || info.ordinals_.at(count - 1) != -1) {
    return OB_INVALID_ARGUMENT;
  }
  ObEvalCtx::TempAllocGuard guard(ctx);
  auto &allocator = guard.get_allocator();
  ObSEArray<const ObExpr *, 16, ObWrapperAllocator> nodes(sizeof(const ObExpr *), ObWrapperAllocator(allocator));
  ObSEArray<uint8_t, 16, ObWrapperAllocator> skipped(sizeof(uint8_t), ObWrapperAllocator(allocator));
  int64_t active_rows = 0;
  int64_t visits = 0;
  if (OB_FAIL(nodes.prepare_allocate(count))) {
  } else if (OB_FAIL(skipped.prepare_allocate(rows))) {
  } else {
    nodes.at(count - 1) = &expr;
    for (int64_t i = count - 2; OB_SUCC(ret) && i >= 0; --i) {
      if (OB_FAIL(poll_logical_status(ctx, visits))) { break; }
      const int64_t parent = info.parents_.at(i);
      const int64_t ordinal = info.ordinals_.at(i);
      if (parent <= i || parent >= count || nodes.at(parent) == nullptr
          || ordinal < 0 || ordinal >= nodes.at(parent)->arg_cnt_
          || nodes.at(parent)->args_ == nullptr || nodes.at(parent)->args_[ordinal] == nullptr) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        nodes.at(i) = nodes.at(parent)->args_[ordinal];
      }
    }
    for (int64_t row = 0; OB_SUCC(ret) && row < rows; ++row) {
      if (OB_FAIL(poll_logical_status(ctx, visits))) { break; }
      skipped.at(row) = skip != nullptr && skip->at(row);
      active_rows += skipped.at(row) == 0;
    }
  }
  if (OB_SUCC(ret)) {
    try {
      const auto image = rust::Slice<const uint64_t>(info.image_.get_data(), info.image_.count());
      if (expression::logical_node_count(image) != static_cast<size_t>(count)) {
        return OB_INVALID_ARGUMENT;
      }
      const size_t words = expression::logical_workspace_words(image, rows);
      if (words > static_cast<size_t>(INT64_MAX) / sizeof(uint64_t)) {
        ret = OB_SIZE_OVERFLOW;
      } else {
        auto *storage = static_cast<uint64_t *>(allocator.alloc(words * sizeof(uint64_t)));
        if (storage == nullptr) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
        } else {
          for (size_t offset = 0; offset < words; offset += 4096) {
            if (OB_FAIL(ctx.exec_ctx_.check_status())) { return ret; }
            const size_t chunk = std::min<size_t>(4096, words - offset);
            MEMSET(storage + offset, 0, chunk * sizeof(uint64_t));
          }
          int64_t logical_nodes = 0;
          for (int64_t node = 0; OB_SUCC(ret) && node < count; ++node) {
            const ObExpr &source = *nodes.at(node);
            if (!is_logical(source)) { continue; }
            ++logical_nodes;
            const ObEvalInfo &available = source.get_eval_info(ctx);
            for (int64_t row = 0; OB_SUCC(ret) && row < rows; ++row) {
              if (OB_FAIL(poll_logical_status(ctx, visits))) { break; }
              const int64_t index = batch ? row : ctx.get_batch_idx();
              if (available.projected_ || (available.evaluated_ && (!source.is_batch_result()
                  || source.get_evaluated_flags(ctx).at(index)))) {
                storage[(count + node) * rows + row] = 4;
              }
            }
          }
          if (OB_FAIL(ret)) { return ret; }
          LogicalHost host(ctx, nodes, batch, rows, scalar_result);
          const auto status = expression::execute_logical(image, &host, rows,
              rust::Slice<const uint8_t>(skipped.get_data(), skipped.count()),
              rust::Slice<uint64_t>(storage, words));
          ret = status.code == 0 ? OB_SUCCESS : status.code < 0 ? status.code
              : status.code == 3 ? OB_ALLOCATE_MEMORY_FAILED : OB_ERR_UNEXPECTED;
          const size_t plane_words = static_cast<size_t>(count) * rows;
          const int publish_ret = host.publish_completed(
              rust::Slice<const uint64_t>(storage, plane_words),
              rust::Slice<const uint64_t>(storage + plane_words, plane_words), rows, ret == OB_SUCCESS);
          if (OB_SUCC(ret)) { ret = publish_ret; }
          if (OB_SUCC(ret)) {
            EVENT_ADD(RUST_LOGICAL_PROGRAM_CALLS, 1);
            EVENT_ADD(RUST_LOGICAL_PROGRAM_ROWS, active_rows);
            EVENT_ADD(RUST_LOGICAL_PROGRAM_NODES, logical_nodes);
            if (batch) { EVENT_ADD(RUST_LOGICAL_PROGRAM_BATCHES, 1); }
          }
        }
      }
    } catch (const rust::Error &error) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("native logical program failed", K(ret), "error", error.what());
    }
  }
  return ret;
}
}

int prepare_logical_program(ObExpr &expr, ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  LogicalBuilder builder(allocator);
  uint32_t root = 0;
  if (!is_logical(expr)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(builder.lower(expr, root))) {
  } else {
    auto *info = OB_NEWx(LogicalProgramInfo, &allocator, allocator, expr.type_);
    if (info == nullptr) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (OB_FAIL(info->parents_.assign(builder.parents_))) {
    } else if (OB_FAIL(info->ordinals_.assign(builder.ordinals_))) {
    } else {
      try {
        const auto instructions = rust::Slice<const expression::LogicalInstruction>(
            builder.instructions_.get_data(), builder.instructions_.count());
        const auto arguments = rust::Slice<const uint32_t>(builder.arguments_.get_data(), builder.arguments_.count());
        const size_t words = expression::logical_image_words(instructions, arguments);
        if (OB_FAIL(info->image_.prepare_allocate(words))) {
        } else {
          expression::compile_logical_image(instructions, arguments,
              rust::Slice<uint64_t>(info->image_.get_data(), info->image_.count()));
          expr.extra_info_ = info;
          expr.eval_func_ = eval_logical_program;
          expr.eval_batch_func_ = eval_logical_program_batch;
        }
      } catch (const rust::Error &error) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("native logical program preparation failed", K(ret), "error", error.what());
      }
    }
  }
  return ret;
}

int eval_logical_program(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &result)
{
  return run_logical(expr, ctx, nullptr, 1, false, &result);
}

int eval_logical_program_batch(const ObExpr &expr, ObEvalCtx &ctx,
                               const ObBitVector &skip, int64_t rows)
{
  const int ret = run_logical(expr, ctx, &skip, rows, true, nullptr);
  if (ret != OB_SUCCESS && rows >= 0 && expr.is_batch_result()) {
    expr.get_evaluated_flags(ctx).reset(rows);
  }
  return ret;
}
} }
