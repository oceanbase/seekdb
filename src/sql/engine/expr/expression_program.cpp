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
#include "sql/engine/expr/expression_program.h"
#include "sql/engine/ob_operator.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/ob_physical_plan.h"
#include "sql/engine/expr/ob_expr_is.h"
#include "lib/stat/ob_diagnostic_info_guard.h"
#include <algorithm>
#include <cstring>
#include <limits>

namespace oceanbase
{
using namespace common;
namespace sql
{
extern int eval_question_mark_func(const ObExpr &, ObEvalCtx &, ObDatum &);
namespace
{
constexpr int64_t MAX_PROGRAM_NODES = 256;
constexpr int64_t MAX_PROGRAM_DEPTH = 64;

class ProgramBuilder
{
public:
  explicit ProgramBuilder(const ObOpSpec &spec, ObIAllocator &allocator)
    : instructions_(OB_MALLOC_NORMAL_BLOCK_SIZE, ObWrapperAllocator(allocator)),
      arguments_(OB_MALLOC_NORMAL_BLOCK_SIZE, ObWrapperAllocator(allocator)),
      filters_(OB_MALLOC_NORMAL_BLOCK_SIZE, ObWrapperAllocator(allocator)),
      projections_(OB_MALLOC_NORMAL_BLOCK_SIZE, ObWrapperAllocator(allocator)),
      nodes_(OB_MALLOC_NORMAL_BLOCK_SIZE, ObWrapperAllocator(allocator)),
      inputs_(OB_MALLOC_NORMAL_BLOCK_SIZE, ObWrapperAllocator(allocator)),
      materialized_inputs_(OB_MALLOC_NORMAL_BLOCK_SIZE, ObWrapperAllocator(allocator)),
      depths_(OB_MALLOC_NORMAL_BLOCK_SIZE, ObWrapperAllocator(allocator)),
      spec_(spec), allocator_(allocator), operations_(0)
  {}

  int build()
  {
    int ret = OB_SUCCESS;
    const ObOpSpec &spec = spec_;
    if (spec.output_.empty() || spec.output_.count() > 256 || spec.filters_.count() > 256) {
      ret = OB_NOT_SUPPORTED;
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < spec.filters_.count(); ++i) {
      uint32_t id = 0;
      if (OB_FAIL(lower(spec.filters_.at(i), 0, id))) {
      } else if (OB_FAIL(filters_.push_back(id))) {
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < spec.output_.count(); ++i) {
      uint32_t id = 0;
      if (OB_FAIL(lower(spec.output_.at(i), 0, id))) {
      } else if (OB_FAIL(projections_.push_back(id))) {
      }
    }
    if (OB_SUCC(ret) && operations_ == 0) {
      ret = OB_NOT_SUPPORTED;
    }
    if (OB_SUCC(ret) && (arguments_.count() > 4096 || filters_.count() > 256
                         || projections_.count() > 256)) {
      ret = OB_NOT_SUPPORTED;
    }
    return ret;
  }

  ObSEArray<expression::Instruction, 16, ObWrapperAllocator> instructions_;
  ObSEArray<uint32_t, 32, ObWrapperAllocator> arguments_;
  ObSEArray<uint32_t, 8, ObWrapperAllocator> filters_;
  ObSEArray<uint32_t, 8, ObWrapperAllocator> projections_;
  ObSEArray<const ObExpr *, 16, ObWrapperAllocator> nodes_;
  ObSEArray<const ObExpr *, 16, ObWrapperAllocator> inputs_;
  ObSEArray<uint8_t, 16, ObWrapperAllocator> materialized_inputs_;
  ObSEArray<uint32_t, 16, ObWrapperAllocator> depths_;

private:
  bool is_boundary(const ObExpr *expr) const
  {
    bool boundary = false;
    for (uint32_t i = 0; !boundary && i < spec_.get_child_cnt(); ++i) {
      const ObOpSpec &child_spec = *spec_.get_child(i);
      // Scalar plans can publish lazy outputs. Only eager production routes
      // guarantee that an output remains materialized on subsequent calls.
      const bool eager = child_spec.is_vectorized() || child_spec.get_phy_plan()->is_vectorized()
          || child_spec.get_phy_plan()->is_contains_assignment();
      const auto &outputs = child_spec.output_;
      for (int64_t j = 0; !boundary && j < outputs.count(); ++j) {
        boundary = outputs.at(j) == expr && (eager || expr->eval_func_ == nullptr);
      }
    }
    return boundary;
  }

  int lower(const ObExpr *expr, int64_t depth, uint32_t &id)
  {
    int ret = OB_SUCCESS;
    int64_t existing = -1;
    for (int64_t i = 0; existing < 0 && i < nodes_.count(); ++i) {
      if (nodes_.at(i) == expr) {
        existing = i;
      }
    }
    if (existing >= 0) {
      if (depth + depths_.at(existing) > MAX_PROGRAM_DEPTH) {
        return OB_NOT_SUPPORTED;
      }
      id = static_cast<uint32_t>(existing);
      return ret;
    }
    if (expr == nullptr || depth >= MAX_PROGRAM_DEPTH || nodes_.count() >= MAX_PROGRAM_NODES
        || (!ob_is_int_tc(expr->datum_meta_.type_) && expr->datum_meta_.type_ != ObNullType)) {
      return OB_NOT_SUPPORTED;
    }
    expression::Instruction instruction{};
    uint32_t argument_count = expr->arg_cnt_;
    const bool parameter = expr->type_ == T_QUESTIONMARK
        && (expr->eval_func_ == nullptr || expr->eval_func_ == eval_question_mark_func);
    const bool literal = expr->type_ > T_INVALID && expr->type_ < T_MAX_CONST
        && expr->type_ != T_QUESTIONMARK && expr->eval_func_ == nullptr;
    const bool column = expr->type_ == T_REF_COLUMN && expr->eval_func_ == nullptr;
    if (literal) {
      const auto &frames = spec_.get_phy_plan()->get_expr_frame_info().const_frame_ptrs_;
      if (expr->frame_idx_ >= frames.count()) {
        return OB_NOT_SUPPORTED;
      }
      const ObDatum &datum = *reinterpret_cast<const ObDatum *>(frames.at(expr->frame_idx_) + expr->datum_off_);
      instruction.opcode = 1;
      instruction.is_null = datum.is_null();
      if (!datum.is_null()) {
        if (datum.ptr_ == nullptr) {
          return OB_ERR_UNEXPECTED;
        }
        instruction.value = datum.get_int();
      }
      argument_count = 0;
    } else if (is_boundary(expr) || parameter || column) {
      instruction.opcode = 0;
      instruction.input = static_cast<uint32_t>(inputs_.count());
      argument_count = 0;
      if (OB_FAIL(inputs_.push_back(expr))) {
        return ret;
      } else if (OB_FAIL(materialized_inputs_.push_back(!parameter && expr->eval_func_ != nullptr))) {
        return ret;
      }
    } else {
      switch (expr->type_) {
      case T_OP_ADD: instruction.opcode = 2; break;
      case T_OP_MINUS: instruction.opcode = 3; break;
      case T_OP_MUL: instruction.opcode = 4; break;
      case T_OP_INT_DIV: instruction.opcode = 5; break;
      case T_OP_EQ: instruction.opcode = 6; break;
      case T_OP_NE: instruction.opcode = 7; break;
      case T_OP_LT: instruction.opcode = 8; break;
      case T_OP_LE: instruction.opcode = 9; break;
      case T_OP_GT: instruction.opcode = 10; break;
      case T_OP_GE: instruction.opcode = 11; break;
      case T_OP_NSEQ: instruction.opcode = 12; break;
      case T_OP_AND: instruction.opcode = 13; break;
      case T_OP_OR: instruction.opcode = 14; break;
      case T_OP_NOT: instruction.opcode = 15; break;
      case T_OP_IS:
        if (expr->eval_func_ != ObExprIs::calc_is_null || expr->arg_cnt_ != 2) {
          return OB_NOT_SUPPORTED;
        }
        instruction.opcode = 16;
        argument_count = 1;
        break;
      case T_OP_IS_NOT:
        if (expr->eval_func_ != ObExprIsNot::calc_is_not_null || expr->arg_cnt_ != 2) {
          return OB_NOT_SUPPORTED;
        }
        instruction.opcode = 17;
        argument_count = 1;
        break;
      case T_OP_CASE: instruction.opcode = 18; break;
      case T_FUN_SYS_CAST:
        if (expr->arg_cnt_ != 2 || expr->datum_meta_.type_ != ObIntType
            || !ob_is_int_tc(expr->args_[0]->datum_meta_.type_)) {
          return OB_NOT_SUPPORTED;
        }
        instruction.opcode = 19;
        argument_count = 1;
        break;
      case T_OP_BOOL:
        instruction.opcode = 20;
        break;
      default:
        return OB_NOT_SUPPORTED;
      }
      if ((instruction.opcode >= 2 && instruction.opcode <= 12 && argument_count != 2)
          || ((instruction.opcode == 15 || instruction.opcode == 20) && argument_count != 1)
          || ((instruction.opcode == 13 || instruction.opcode == 14) && argument_count < 2)
          || (instruction.opcode == 18 && argument_count < 2)) {
        return OB_NOT_SUPPORTED;
      }
      if (instruction.opcode >= 2 && instruction.opcode <= 5
          && expr->datum_meta_.type_ != ObIntType) {
        return OB_NOT_SUPPORTED;
      }
      instruction.error_on_zero = expr->is_error_div_by_zero_;
      ++operations_;
    }
    if (argument_count > 256) {
      return OB_NOT_SUPPORTED;
    }
    ObSEArray<uint32_t, 8, ObWrapperAllocator> child_ids(
        OB_MALLOC_NORMAL_BLOCK_SIZE, ObWrapperAllocator(allocator_));
    for (uint32_t i = 0; OB_SUCC(ret) && i < argument_count; ++i) {
      uint32_t child = 0;
      if (OB_FAIL(lower(expr->args_[i], depth + 1, child))) {
      } else if (OB_FAIL(child_ids.push_back(child))) {
      }
    }
    if (OB_SUCC(ret) && nodes_.count() >= MAX_PROGRAM_NODES) {
      ret = OB_NOT_SUPPORTED;
    }
    if (OB_SUCC(ret) && arguments_.count() + child_ids.count() > 4096) {
      ret = OB_NOT_SUPPORTED;
    }
    if (OB_SUCC(ret)) {
      uint32_t node_depth = 1;
      for (int64_t i = 0; i < child_ids.count(); ++i) {
        node_depth = std::max(node_depth, depths_.at(child_ids.at(i)) + 1);
      }
      if (node_depth > MAX_PROGRAM_DEPTH) {
        return OB_NOT_SUPPORTED;
      }
      instruction.argument_start = static_cast<uint32_t>(arguments_.count());
      instruction.argument_count = argument_count;
      id = static_cast<uint32_t>(nodes_.count());
      for (int64_t i = 0; OB_SUCC(ret) && i < child_ids.count(); ++i) {
        ret = arguments_.push_back(child_ids.at(i));
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(instructions_.push_back(instruction))) {
      } else if (OB_FAIL(nodes_.push_back(expr))) {
      } else if (OB_FAIL(depths_.push_back(node_depth))) {
      }
    }
    return ret;
  }

  const ObOpSpec &spec_;
  ObIAllocator &allocator_;
  int64_t operations_;
};
}

CompiledExpressionProgram::CompiledExpressionProgram(ObIAllocator &allocator)
  : allocator_(allocator),
    inputs_(OB_MALLOC_NORMAL_BLOCK_SIZE, ObWrapperAllocator(allocator)),
    materialized_inputs_(OB_MALLOC_NORMAL_BLOCK_SIZE, ObWrapperAllocator(allocator)),
    nodes_(OB_MALLOC_NORMAL_BLOCK_SIZE, ObWrapperAllocator(allocator)),
    roots_(OB_MALLOC_NORMAL_BLOCK_SIZE, ObWrapperAllocator(allocator))
{
}

int CompiledExpressionProgram::create(const ObOpSpec &spec, ObIAllocator &allocator,
                                      CompiledExpressionProgram *&result)
{
  int ret = OB_SUCCESS;
  result = nullptr;
  ProgramBuilder builder(spec, allocator);
  if (OB_FAIL(builder.build())) {
    if (ret == OB_NOT_SUPPORTED) {
      ret = OB_SUCCESS;
    }
  } else {
    void *memory = allocator.alloc(sizeof(CompiledExpressionProgram));
    if (memory == nullptr) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      auto *program = new (memory) CompiledExpressionProgram(allocator);
      if (OB_FAIL(program->inputs_.assign(builder.inputs_))) {
      } else if (OB_FAIL(program->materialized_inputs_.assign(builder.materialized_inputs_))) {
      } else if (OB_FAIL(program->nodes_.assign(builder.nodes_))) {
      } else if (OB_FAIL(program->roots_.assign(builder.filters_))) {
      } else {
        for (int64_t i = 0; OB_SUCC(ret) && i < builder.projections_.count(); ++i) {
          ret = program->roots_.push_back(builder.projections_.at(i));
        }
      }
      if (OB_SUCC(ret)) {
        try {
          program->prepared_.emplace(expression::prepare(
              rust::Slice<const expression::Instruction>(builder.instructions_.get_data(), builder.instructions_.count()),
              rust::Slice<const uint32_t>(builder.arguments_.get_data(), builder.arguments_.count()),
              rust::Slice<const uint32_t>(builder.filters_.get_data(), builder.filters_.count()),
              rust::Slice<const uint32_t>(builder.projections_.get_data(), builder.projections_.count()),
              static_cast<uint32_t>(builder.inputs_.count()), program));
        } catch (const rust::Error &error) {
          ret = program->allocation_failed_ || std::strcmp(error.what(), "expression handle allocation failed") == 0
              ? OB_ALLOCATE_MEMORY_FAILED : OB_ERR_UNEXPECTED;
          SQL_ENG_LOG(WARN, "Rust expression preparation failed", K(ret), "error", error.what());
        }
      }
      if (OB_SUCC(ret)) {
        EVENT_ADD(RUST_EXPRESSION_PREPARES, 1);
        result = program;
      } else {
        program->~CompiledExpressionProgram();
        allocator.free(program);
      }
    }
  }
  return ret;
}

uint64_t *CompiledExpressionProgram::allocate(size_t words)
{
  if (words > static_cast<size_t>(INT64_MAX) / sizeof(uint64_t)) {
    allocation_failed_ = true;
    return nullptr;
  }
  auto *memory = static_cast<uint64_t *>(allocator_.alloc(words * sizeof(uint64_t)));
  allocation_failed_ |= memory == nullptr;
  return memory;
}

void CompiledExpressionProgram::release(uint64_t *data, size_t words)
{
  UNUSED(words);
  allocator_.free(data);
}

ExpressionProgramCatalog::ExpressionProgramCatalog(ObIAllocator &allocator)
  : allocator_(allocator), entries_(OB_MALLOC_NORMAL_BLOCK_SIZE, ObWrapperAllocator(allocator))
{
}

int ExpressionProgramCatalog::create(ObPhysicalPlan &plan, ExpressionProgramCatalog *&result)
{
  int ret = OB_SUCCESS;
  result = nullptr;
  void *memory = plan.get_allocator().alloc(sizeof(ExpressionProgramCatalog));
  if (memory == nullptr) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    auto *catalog = new (memory) ExpressionProgramCatalog(plan.get_allocator());
    if (plan.get_root_op_spec() == nullptr) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      ret = catalog->prepare(*const_cast<ObOpSpec *>(plan.get_root_op_spec()));
    }
    if (OB_SUCC(ret)) {
      result = catalog;
    } else {
      catalog->~ExpressionProgramCatalog();
      plan.get_allocator().free(catalog);
    }
  }
  return ret;
}

int ExpressionProgramCatalog::prepare(ObOpSpec &root)
{
  int ret = OB_SUCCESS;
  ObSEArray<ObOpSpec *, 16, ObWrapperAllocator> pending(
      OB_MALLOC_NORMAL_BLOCK_SIZE, ObWrapperAllocator(allocator_));
  ret = pending.push_back(&root);
  while (OB_SUCC(ret) && !pending.empty()) {
    ObOpSpec *spec = pending.at(pending.count() - 1);
    pending.pop_back();
    CompiledExpressionProgram *program = nullptr;
    if (OB_FAIL(CompiledExpressionProgram::create(*spec, allocator_, program))) {
    } else if (program != nullptr) {
      if (OB_FAIL(entries_.push_back(Entry{spec, program}))) {
        program->~CompiledExpressionProgram();
        allocator_.free(program);
      } else {
        spec->compiled_expression_program_ = program;
      }
    }
    for (uint32_t i = 0; OB_SUCC(ret) && i < spec->get_child_cnt(); ++i) {
      ret = pending.push_back(spec->get_child(i));
    }
  }
  return ret;
}

ExpressionProgramCatalog::~ExpressionProgramCatalog()
{
  for (int64_t i = 0; i < entries_.count(); ++i) {
    Entry &entry = entries_.at(i);
    entry.spec_->compiled_expression_program_ = nullptr;
    entry.program_->~CompiledExpressionProgram();
    allocator_.free(entry.program_);
  }
}

ExpressionProgram::ExpressionProgram(ObOperator &owner, const CompiledExpressionProgram &compiled)
  : owner_(owner), compiled_(compiled),
    skip_(OB_MALLOC_NORMAL_BLOCK_SIZE, ObWrapperAllocator(owner.get_exec_ctx().get_allocator())),
    capacity_(std::max<int64_t>(1, owner.get_spec().max_batch_size_))
{
}

int ExpressionProgram::create(ObOperator &owner, ExpressionProgram *&result)
{
  int ret = OB_SUCCESS;
  result = nullptr;
  const auto *compiled = owner.get_spec().compiled_expression_program_;
  if (compiled == nullptr || owner.get_exec_ctx().get_my_session()->is_diagnosis_enabled()
      || owner.get_spec().get_phy_plan()->is_contains_assignment()) {
    return ret;
  }
  void *memory = owner.get_exec_ctx().get_allocator().alloc(sizeof(ExpressionProgram));
  if (memory == nullptr) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    auto *program = new (memory) ExpressionProgram(owner, *compiled);
    try {
      program->runner_.emplace(expression::make_runner(**compiled->prepared_, program, program->capacity_));
    } catch (const rust::Error &error) {
      ret = program->allocation_failed_ || std::strcmp(error.what(), "expression handle allocation failed") == 0
          ? OB_ALLOCATE_MEMORY_FAILED : OB_ERR_UNEXPECTED;
      SQL_ENG_LOG(WARN, "Rust expression execution creation failed", K(ret), "error", error.what());
    }
    if (OB_SUCC(ret)) {
      result = program;
    } else {
      program->~ExpressionProgram();
      owner.get_exec_ctx().get_allocator().free(program);
    }
  }
  return ret;
}

uint64_t *ExpressionProgram::allocate(size_t words)
{
  if (words > static_cast<size_t>(INT64_MAX) / sizeof(uint64_t)) {
    allocation_failed_ = true;
    return nullptr;
  }
  auto *memory = static_cast<uint64_t *>(owner_.get_exec_ctx().get_allocator().alloc(words * sizeof(uint64_t)));
  allocation_failed_ |= memory == nullptr;
  return memory;
}

void ExpressionProgram::release(uint64_t *data, size_t words)
{
  UNUSED(words);
  owner_.get_exec_ctx().get_allocator().free(data);
}

int32_t ExpressionProgram::check_cancel()
{
  return owner_.get_exec_ctx().check_status();
}

int ExpressionProgram::import_inputs(int64_t rows, bool batch, const ObBitVector *skip)
{
  int ret = OB_SUCCESS;
  auto storage = expression::input_storage(**runner_);
  const size_t plane_size = compiled_.inputs_.count() * capacity_;
  if (storage.size() != plane_size * 2) {
    return OB_ALLOCATE_MEMORY_FAILED;
  }
  uint64_t *values = storage.data();
  uint64_t *validity = values + plane_size;
  ObEvalCtx &ctx = owner_.get_eval_ctx();
  for (int64_t slot = 0; OB_SUCC(ret) && slot < compiled_.inputs_.count(); ++slot) {
    const ObExpr &expr = *compiled_.inputs_.at(slot);
    if (compiled_.materialized_inputs_.at(slot) != 0 && !expr.get_eval_info(ctx).projected_) {
      ret = OB_ERR_UNEXPECTED;
      break;
    }
    const bool parameter = expr.type_ == T_QUESTIONMARK && expr.eval_func_ == eval_question_mark_func;
    const ObObj *parameter_value = nullptr;
    if (parameter) {
      const auto *plan_ctx = ctx.exec_ctx_.get_physical_plan_ctx();
      if (plan_ctx == nullptr || expr.extra_ >= plan_ctx->get_param_store().count()) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        parameter_value = &plan_ctx->get_param_store().at(expr.extra_);
        if (!parameter_value->is_null() && parameter_value->get_type() != expr.datum_meta_.type_) {
          ret = OB_ERR_UNEXPECTED;
        }
      }
    }
    for (int64_t row = 0; OB_SUCC(ret) && row < rows; ++row) {
      const size_t index = slot * capacity_ + row;
      if (skip != nullptr && skip->at(row)) {
        validity[index] = 0;
        values[index] = 0;
      } else if (parameter_value != nullptr) {
        validity[index] = !parameter_value->is_null();
        values[index] = validity[index] ? static_cast<uint64_t>(parameter_value->get_int()) : 0;
      } else {
        const ObDatum &datum = batch ? expr.locate_expr_datum(ctx, row) : expr.locate_expr_datum(ctx);
        validity[index] = !datum.is_null();
        if (!datum.is_null() && datum.ptr_ == nullptr) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          values[index] = validity[index] ? static_cast<uint64_t>(datum.get_int()) : 0;
        }
      }
    }
  }
  return ret;
}

void ExpressionProgram::export_root(const ObExpr &expr, size_t node, int64_t rows, bool batch)
{
  if (expr.eval_func_ == nullptr) {
    return;
  }
  ObEvalCtx &ctx = owner_.get_eval_ctx();
  ObEvalInfo &info = expr.get_eval_info(ctx);
  const auto values = expression::node_values(**runner_, node);
  const auto flags = expression::node_flags(**runner_, node);
  const bool batch_result = batch && expr.is_batch_result();
  if (batch_result) {
    expr.get_evaluated_flags(ctx).reset(rows);
  }
  for (int64_t row = 0; row < rows; ++row) {
    if ((flags[row] & 1) == 0) {
      continue;
    }
    const int64_t index = batch_result ? row : (expr.is_batch_result() ? ctx.get_batch_idx() : 0);
    expr.reset_ptr_in_datum(ctx, index);
    ObDatum &datum = expr.locate_batch_datums(ctx)[index];
    if ((flags[row] & 2) == 0) {
      datum.set_null();
    } else {
      int64_t value = 0;
      const uint64_t bits = values[row];
      MEMCPY(&value, &bits, sizeof(value));
      datum.set_int(value);
    }
    if (batch_result) {
      expr.get_evaluated_flags(ctx).set(row);
    }
  }
  info.evaluated_ = true;
  info.notnull_ = false;
  info.point_to_frame_ = true;
  info.cnt_ = batch_result ? rows : 1;
  info.projected_ = true;
}

int ExpressionProgram::report_error(const expression::RunStatus &status)
{
  int ret = OB_ERR_UNEXPECTED;
  if (status.code < 0) {
    ret = status.code;
  } else if (status.code == 1) {
    ret = OB_OPERATE_OVERFLOW;
    const ObExpr *expr = status.node < static_cast<size_t>(compiled_.nodes_.count()) ? compiled_.nodes_.at(status.node) : nullptr;
    const char *operation = expr == nullptr ? "?" : expr->type_ == T_OP_ADD ? "+"
        : expr->type_ == T_OP_MINUS ? "-" : expr->type_ == T_OP_MUL ? "*" : "div";
    char expression_text[128];
    int64_t pos = 0;
    databuff_printf(expression_text, sizeof(expression_text), pos, "'(%ld %s %ld)'",
                    status.left, operation, status.right);
    LOG_USER_ERROR(OB_OPERATE_OVERFLOW, "BIGINT", expression_text);
  } else if (status.code == 2) {
    ret = OB_DIVISION_BY_ZERO;
  } else if (status.code == 3) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  }
  return ret;
}

int ExpressionProgram::execute(ObBitVector *skip, int64_t rows, bool batch,
                               bool &all_filtered, bool &all_active)
{
  int ret = OB_SUCCESS;
  all_filtered = false;
  if (rows < 0 || static_cast<size_t>(rows) > capacity_) {
    return OB_INVALID_ARGUMENT;
  }
  skip_.reuse();
  int64_t active = 0;
  for (int64_t row = 0; OB_SUCC(ret) && row < rows; ++row) {
    const bool skipped = skip != nullptr && skip->at(row);
    active += !skipped;
    ret = skip_.push_back(skipped);
  }
  if (OB_FAIL(ret)) {
  } else if (active > 0 && OB_FAIL(import_inputs(rows, batch, skip))) {
  } else {
    const auto status = expression::execute(**runner_, **compiled_.prepared_, rows,
        rust::Slice<const uint8_t>(skip_.get_data(), skip_.count()));
    if (status.code != 0) {
      ret = report_error(status);
    } else {
      const auto result_selection = expression::selection(**runner_);
      int64_t survivors = 0;
      for (int64_t row = 0; row < rows; ++row) {
        if (result_selection[row] != 0) {
          if (skip != nullptr) {
            skip->set(row);
          }
        } else {
          ++survivors;
        }
      }
      all_filtered = survivors == 0;
      all_active &= survivors == rows;
      for (int64_t i = 0; i < compiled_.roots_.count(); ++i) {
        const uint32_t node = compiled_.roots_.at(i);
        export_root(*compiled_.nodes_.at(node), node, rows, batch);
      }
      EVENT_ADD(RUST_EXPRESSION_PROGRAMS, 1);
      EVENT_ADD(RUST_EXPRESSION_ROWS, active);
      if (!owner_.get_spec().filters_.empty()) {
        EVENT_ADD(RUST_EXPRESSION_FILTER_ROWS, active);
      }
      if (batch) {
        EVENT_ADD(RUST_EXPRESSION_BATCHES, 1);
      }
    }
  }
  return ret;
}
}
}
