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

#include "sql/engine/expr/native_frame_value_codec.h"
#include "ob_operator.h"
#include "sql/engine/native_param_utils.h"
#include "common/datum/datum_parameter_codec.h"
#include "ob_operator_factory.h"
#include "query/runtime/ob_query_runtime_environment.h"
#include "sql/engine/expr/ob_array_expr_utils.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

OB_SERIALIZE_MEMBER(ObDynamicParamSetter, param_idx_, src_, dst_);
OB_SERIALIZE_MEMBER(ObOpSchemaObj, obj_type_, is_not_null_, order_type_);

int assign_parameter_to_expr(ObEvalCtx &ctx, const ObExpr &expr,
                             const common::ObDatum &source, const SqlTypeMeta &meta)
{
  // This is the construction/compatibility input boundary, not an execution
  // frame alias. Inline scalars are loaded once; same-statement resources keep
  // their existing owner while any wrapper bytes live in the expression arena.
  UNUSED(meta);
  Datum value;
  int ret = expr.get_datum_copy_plan().import_legacy(source, value);
  if (OB_SUCC(ret)) { ret = expr.deep_copy_datum(ctx, value); }
  return ret;
}

int ObDynamicParamSetter::set_dynamic_param(ObEvalCtx &eval_ctx) const
{
  int ret = OB_SUCCESS;
  Datum *res = NULL;
  if (OB_ISNULL(src_)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(src_->eval(eval_ctx, res))) {
  } else if (OB_FAIL(update_dynamic_param(eval_ctx,*res))) {
  }
  return ret;
}

int ObDynamicParamSetter::set_dynamic_param(ObEvalCtx &eval_ctx,
    NativeTypedDatum &value, DatumParameterMetadata &attributes) const
{
  int ret = set_dynamic_param(eval_ctx);
  if (OB_SUCC(ret)) { ret = capture_dynamic_param(eval_ctx, value, attributes); }
  return ret;
}

int ObDynamicParamSetter::capture_dynamic_param(ObEvalCtx &eval_ctx,
    NativeTypedDatum &value, DatumParameterMetadata &attributes) const
{
  const ObPhysicalPlanCtx *plan_ctx = eval_ctx.exec_ctx_.get_physical_plan_ctx();
  return plan_ctx == nullptr || dst_ == nullptr || param_idx_ < 0
      || param_idx_ >= plan_ctx->get_parameter_count() ? OB_INVALID_ARGUMENT
      : plan_ctx->capture_native_parameter(param_idx_, value, attributes);
}

int ObDynamicParamSetter::restore_dynamic_param(ObEvalCtx &eval_ctx,
    const NativeTypedDatum &value, const DatumParameterMetadata &attributes) const
{
  int ret = OB_SUCCESS;
  ObPhysicalPlanCtx *plan_ctx = eval_ctx.exec_ctx_.get_physical_plan_ctx();
  if (plan_ctx == nullptr || dst_ == nullptr || value.type_ == nullptr
      || param_idx_ < 0 || param_idx_ >= plan_ctx->get_parameter_count()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (dst_->native_parameter_alias_
      && OB_FAIL(dst_->prepare_native_parameter_write(eval_ctx, param_idx_))) {
  } else if (OB_FAIL(plan_ctx->set_runtime_parameter(param_idx_, value.datum_, *value.type_, attributes))) {
  } else {
    Datum current;
    if (OB_FAIL(plan_ctx->read_native_parameter(param_idx_, current))) {
    } else {
      // A failed preparation must not invalidate the old expression or parents.
      clear_parent_evaluated_flag(eval_ctx, *dst_);
      dst_->locate_datum_for_write(eval_ctx) = current;
      dst_->get_eval_info(eval_ctx).evaluated_ = true;
    }
  }
  return ret;
}

int ObDynamicParamSetter::update_dynamic_param(ObEvalCtx &eval_ctx, const Datum &datum) const
{
  int ret = OB_SUCCESS;
  ObPhysicalPlanCtx *plan_ctx = eval_ctx.exec_ctx_.get_physical_plan_ctx();
  if (plan_ctx == nullptr || dst_ == nullptr || param_idx_ < 0
      || param_idx_ >= plan_ctx->get_parameter_count()
      || plan_ctx->get_parameter_metadata(param_idx_) == nullptr) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    const Datum candidate = datum;
    // A direct alias writer is an actual generic ABI consumer. Snapshot the
    // input before refreshing a possibly aliased projection, then prepare its
    // stable result before any owner or parent-state publication.
    if (dst_->native_parameter_alias_
        && OB_FAIL(dst_->prepare_native_parameter_write(eval_ctx, param_idx_))) { return ret; }
    const SqlType &target_type = dst_->get_sql_type();
    DatumParameterMetadata attributes = *plan_ctx->get_parameter_metadata(param_idx_);
    if (target_type.legacy_meta().is_pl_extend_type() || target_type.kind() == ObDecimalIntType) {
      // These declarations use their complete prepared accuracy: Decimal needs
      // its target scale/precision, and PL needs the full declaration-id word.
      // Ordinary slot attributes and the restore path retain their producer.
      attributes.accuracy_ = target_type.accuracy();
      attributes.param_meta_ = target_type.legacy_meta();
    }
    if (OB_FAIL(plan_ctx->set_runtime_parameter(param_idx_, candidate, target_type, attributes))) {
    } else {
      Datum current;
      if (OB_FAIL(plan_ctx->read_native_parameter(param_idx_, current))) {
      } else {
        clear_parent_evaluated_flag(eval_ctx, *dst_);
        dst_->locate_datum_for_write(eval_ctx) = current;
        dst_->get_eval_info(eval_ctx).evaluated_ = true;
      }
    }
  }
  return ret;
}

void ObDynamicParamSetter::clear_parent_evaluated_flag(ObEvalCtx &eval_ctx, const ObExpr &expr)
{
  // Marked PX directories retain global parent indices, but unselected Exprs
  // decode as empty placeholders. They have no EvalInfo; their default offset
  // zero may instead address a selected constant's Datum payload.
  if (expr.type_ == T_INVALID && expr.sql_type_ == nullptr) {
    return;
  }
  for (int64_t i = 0; i < expr.parent_cnt_; i++) {
    clear_parent_evaluated_flag(eval_ctx, *expr.parents_[i]);
  }
  expr.get_eval_info(eval_ctx).clear_evaluated_flag();
}

ObOpSpec::ObOpSpec(ObIAllocator &alloc, const ObPhyOperatorType type)
    : type_(type),
    id_(OB_INVALID_ID),
    plan_(NULL),
    parent_(NULL),
    children_(NULL),
    child_cnt_(0),
    left_(NULL),
    right_(NULL),
    output_(&alloc),
    startup_filters_(&alloc),
    filters_(&alloc),
    calc_exprs_(&alloc),
    cost_(0),
    rows_(0),
    width_(0),
    px_est_size_factor_(),
    plan_depth_(0),
    max_batch_size_(0),
    need_check_output_datum_(false),
    compress_type_(NONE_COMPRESSOR)
{
}

ObOpSpec::~ObOpSpec()
{
}

OB_SERIALIZE_MEMBER(ObOpSpec,
                    id_,
                    output_,
                    startup_filters_,
                    filters_,
                    calc_exprs_,
                    cost_,
                    rows_,
                    width_,
                    px_est_size_factor_,
                    plan_depth_,
                    max_batch_size_,
                    need_check_output_datum_,
                    compress_type_);

DEF_TO_STRING(ObOpSpec)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV("name", op_name(),
       K_(type),
       K_(id),
       K_(child_cnt),
       "output_cnt", output_.count(),
       "startup_filters_cnt", startup_filters_.count(),
       "calc_exprs_cnt", calc_exprs_.count(),
       K_(rows),
       K_(max_batch_size),
       K_(filters),
       K_(compress_type));
  J_OBJ_END();
  return pos;
}

int ObOpSpec::set_children_pointer(ObOpSpec **children, const uint32_t child_cnt)
{
  int ret = OB_SUCCESS;
  if (child_cnt > 0 && NULL == children) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    children_ = children;
    child_cnt_ = child_cnt;
    if (child_cnt > 0) {
      child_ = children[0];
    } else {
      child_ = NULL;
    }
    if (child_cnt > 1) {
      right_ = children[1];
    } else {
      right_ = NULL;
    }
  }
  return ret;
}

int ObOpSpec::set_child(const uint32_t idx, ObOpSpec *child)
{
  int ret = OB_SUCCESS;
  if (idx >= child_cnt_ || OB_ISNULL(child)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    children_[idx] = child;
    if (0 == idx) {
      child_ = child;
    }
    if (1 == idx) {
      right_ = child;
    }
    child->parent_ = this;
  }
  return ret;
}

int ObOpSpec::create_op_input(ObExecContext &exec_ctx) const
{
  int ret = OB_SUCCESS;
  // Do some sanity check,
  // we no longer need to check the validity of those pointers in ObOperator.
  if (OB_ISNULL(GET_MY_SESSION(exec_ctx))
      || OB_ISNULL(GET_PHY_PLAN_CTX(exec_ctx))
      || OB_ISNULL(GET_SQL_EXECUTOR_CTX(exec_ctx))) {
  } else if (OB_FAIL(create_op_input_recursive(exec_ctx))) {
  }
  LOG_TRACE("trace create input", K(ret), K(lbt()));
  return ret;
}

int ObOpSpec::create_op_input_recursive(ObExecContext &exec_ctx) const
{
  int ret = OB_SUCCESS;
  ObOperatorKit *kit = exec_ctx.get_operator_kit(id_);
  int64_t create_child_cnt = child_cnt_;
  if (OB_ISNULL(kit) || (child_cnt_ > 0 && NULL == children_)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    kit->spec_ = this;
    LOG_TRACE("trace create input", K(ret), K(id_), K(type_), K(lbt()));
  }

  // create operator input
  if (OB_SUCC(ret)) {
    // Operator input may created in scheduler, no need to create again.
    if (NULL == kit->input_ && ObOperatorFactory::has_op_input(type_)) {
      if (OB_FAIL(ObOperatorFactory::alloc_op_input(
                  exec_ctx.get_allocator(), exec_ctx, *this, kit->input_))) {
      } else if (OB_ISNULL(kit->input_)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        kit->input_->set_deserialize_allocator(&exec_ctx.get_allocator());
      }
    }
  }

  // create child operator
  if (OB_SUCC(ret) && create_child_cnt > 0) {
    for (int64_t i = 0; OB_SUCC(ret) && i < create_child_cnt; i++) {
      if (nullptr == children_[i]) {
        // Here if there is a child but it is nullptr, it means it is a receive operator
        if (!IS_PX_RECEIVE(type_)) {
          ret = OB_ERR_UNEXPECTED;
        }
      } else if (OB_FAIL(children_[i]->create_op_input_recursive(exec_ctx))) {
      }
    }
  }
  return ret;
}

int ObOpSpec::create_operator(ObExecContext &exec_ctx, ObOperator *&op) const
{
  int ret = OB_SUCCESS;
  // Do some sanity check,
  // we no longer need to check the validity of those pointers in ObOperator.
  if (OB_ISNULL(GET_MY_SESSION(exec_ctx))
      || OB_ISNULL(GET_PHY_PLAN_CTX(exec_ctx))
      || OB_ISNULL(GET_SQL_EXECUTOR_CTX(exec_ctx))) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(create_operator_recursive(exec_ctx, op))) {
  } else if (OB_FAIL(create_exec_feedback_node_recursive(exec_ctx))) {
  }
  LOG_DEBUG("trace create operator", K(ret), K(lbt()));
  return ret;
}

int ObOpSpec::create_operator_recursive(ObExecContext &exec_ctx, ObOperator *&op) const
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_stack_overflow())) {
  } else {
    ObOperatorKit *kit = exec_ctx.get_operator_kit(id_);
    int64_t create_child_cnt = child_cnt_;
    if (OB_ISNULL(kit) || (child_cnt_ > 0 && NULL == children_)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      kit->spec_ = this;
      for (int64_t i = 0; OB_SUCC(ret) && i < child_cnt_; i++) {
        if (NULL == children_[i]) {
          // Here if there is a child but it is nullptr, it means it is a receive operator
          if (!IS_PX_RECEIVE(type_)) {
            ret = OB_ERR_UNEXPECTED;
          } else {
            create_child_cnt = 0;
          }
        } else if (IS_TRANSMIT(children_[i]->type_)) {
          OB_ASSERT(1 == child_cnt_);
          // only create context for current DFO
          create_child_cnt = 0;
        }
      }
    }

    // create operator input
    if (OB_SUCC(ret)) {
      // Operator input may created in scheduler, no need to create again.
      if (NULL == kit->input_ && ObOperatorFactory::has_op_input(type_)) {
        if (OB_FAIL(ObOperatorFactory::alloc_op_input(
                    exec_ctx.get_allocator(), exec_ctx, *this, kit->input_))) {
        } else if (OB_ISNULL(kit->input_)) {
          ret = OB_ERR_UNEXPECTED;
        } else {
        }
      }
    }

    // create operator
    if (OB_SUCC(ret)) {
      if (OB_FAIL(ObOperatorFactory::alloc_operator(
                  exec_ctx.get_allocator(), exec_ctx, *this,
                  kit->input_, create_child_cnt, kit->op_))
          || OB_ISNULL(kit->op_)) {
        ret = OB_SUCCESS == ret ? OB_ERR_UNEXPECTED : ret;
      } else {
        op = kit->op_;
        op->get_monitor_info().set_op(op);
        op->get_monitor_info().set_operator_id(id_);
        op->get_monitor_info().set_operator_type(type_);
        op->get_monitor_info().set_plan_depth(plan_depth_);
        
        op->get_monitor_info().open_time_ = oceanbase::common::ObClockGenerator::getClock();
        op->get_monitor_info().set_plan_hash_value(plan_->get_plan_hash_value());
        if (OB_FAIL(op->get_monitor_info().set_sql_id(plan_->get_sql_id_string()))) {
        }
      }
    }

    // create child operator
    if (OB_SUCC(ret)) {
      if (create_child_cnt > 0) {
        for (int64_t i = 0; OB_SUCC(ret) && i < create_child_cnt; i++) {
          ObOperator *child_op = NULL;
          if (nullptr == children_[i]) {
            // Here if there is a child but it is nullptr, it means it is a receive operator
            if (!IS_PX_RECEIVE(type_)) {
              ret = OB_ERR_UNEXPECTED;
            }
          } else if (OB_FAIL(children_[i]->create_operator_recursive(exec_ctx, child_op))) {
          } else if (OB_FAIL(op->set_child(i, child_op))) {
          }
        }
      } else if (child_cnt_ > 0) {
        if (OB_FAIL(assign_spec_ptr_recursive(exec_ctx))) {
        }
      }
    }
  }

  return ret;
}

int ObOpSpec::create_exec_feedback_node_recursive(ObExecContext &exec_ctx) const
{
  int ret = OB_SUCCESS;
  ObOperatorKit *kit = exec_ctx.get_operator_kit(id_);
  ObPhysicalPlanCtx *physical_ctx = exec_ctx.get_physical_plan_ctx();
  if (OB_ISNULL(plan_) || OB_ISNULL(physical_ctx)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (!physical_ctx->get_check_pdml_affected_rows() && !plan_->need_record_plan_info()) {
  } else if (OB_ISNULL(kit)) {
  } else {
    ObExecFeedbackInfo &fb_info = exec_ctx.get_feedback_info();
    ObExecFeedbackNode node(id_);
    if (OB_FAIL(fb_info.add_feedback_node(node))) {
    } else if (OB_NOT_NULL(kit->op_)) {
      common::ObIArray<ObExecFeedbackNode> &nodes = fb_info.get_feedback_nodes();
      kit->op_->set_feedback_node_idx(nodes.count() - 1);
    }
    for (int i = 0; OB_SUCC(ret) && i < child_cnt_; ++i) {
      if (nullptr == children_[i]) {
        continue;
      } else if (OB_FAIL(SMART_CALL(children_[i]->create_exec_feedback_node_recursive(
            exec_ctx)))) {
      }
    }
  }

  return ret;
}

int ObOpSpec::assign_spec_ptr_recursive(ObExecContext &exec_ctx) const
{
  int ret = OB_SUCCESS;
  ObOperatorKit *kit = exec_ctx.get_operator_kit(id_);
  if (NULL == kit) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    kit->spec_ = this;
    for (int64_t i = 0; OB_SUCC(ret) && i < child_cnt_; i++) {
      if (OB_FAIL(children_[i]->assign_spec_ptr_recursive(exec_ctx))) {
      }
    }
  }
  return ret;
}

int ObOpSpec::accept(ObOpSpecVisitor &visitor) const
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(visitor.pre_visit(*this))) {
  }
  if (OB_FAIL(ret)) {
    // do nothing
  } else if (OB_ISNULL(children_)) {
    ret = OB_ERR_UNEXPECTED;
  }
  for (int i = 0; OB_SUCC(ret) && i < child_cnt_; i++) {
    if (OB_ISNULL(children_[i])) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(children_[i]->accept(visitor))) {
    }
  } // end for
  if (OB_SUCC(ret) && OB_FAIL(visitor.post_visit(*this))) {
  }
  return ret;
}

ObOperator::ObOperator(ObExecContext &exec_ctx, const ObOpSpec &spec, ObOpInput *input)
    : spec_(spec),
    ctx_(exec_ctx),
    eval_ctx_(exec_ctx),
    datum_access_ctx_(nullptr),
    eval_infos_(exec_ctx.get_allocator()),
    input_(input),
    parent_(NULL),
    children_(NULL),
    child_cnt_(0),
    left_(NULL),
    right_(NULL),
    try_check_tick_(0),
    opened_(false),
    startup_passed_(spec_.startup_filters_.empty()),
    exch_drained_(false),
    got_first_row_(false),
    need_init_before_get_row_(true),
    fb_node_idx_(OB_INVALID_INDEX),
    io_event_observer_(op_monitor_info_),
    cpu_begin_time_(0),
    cpu_begin_level_(0),
    total_time_(0),
    batch_reach_end_(false),
    row_reach_end_(false),
    output_batches_b4_rescan_(0),
    #ifdef ENABLE_DEBUG_LOG
    dummy_mem_context_(nullptr),
    dummy_ptr_(nullptr),
    #endif
    check_stack_overflow_(false)
{
  eval_ctx_.max_batch_size_ = spec.max_batch_size_;
  eval_ctx_.batch_size_ = spec.max_batch_size_;
}

ObOperator::~ObOperator()
{
  ObOperator::destroy();
}

int ObOperator::set_children_pointer(ObOperator **children, const uint32_t child_cnt)
{
  int ret = OB_SUCCESS;
  if (child_cnt > 0 && NULL == children) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    children_ = children;
    child_cnt_ = child_cnt;
    if (child_cnt > 0) {
      child_ = children[0];
    } else {
      child_ = NULL;
    }
    if (child_cnt > 1) {
      right_ = children[1];
    } else {
      right_ = NULL;
    }
  }
  return ret;
}

int ObOperator::set_child(const uint32_t idx, ObOperator *child)
{
  int ret = OB_SUCCESS;
  if (idx >= child_cnt_ || OB_ISNULL(child)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    children_[idx] = child;
    if (0 == idx) {
      child_ = child;
    }
    if (1 == idx) {
      right_ = child;
    }
    child_->parent_ = this;
  }
  return ret;
}

int ObOperator::init()
{
  return ctx_.get_datum_access_ctx(datum_access_ctx_);
}

int ObOperator::check_stack_once()
{
  int ret = OB_SUCCESS;
  if (check_stack_overflow_) {
  } else if (OB_FAIL(common::check_stack_overflow())) {
  } else {
    check_stack_overflow_ = true;
  }
  return ret;
}

int ObOperator::output_expr_decint_datum_len_check()
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < spec_.output_.count(); ++i) {
    Datum *datum = NULL;
    const ObExpr *expr = spec_.output_[i];
    if (OB_ISNULL(expr)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (!ob_is_decimal_int(expr->datum_meta_.get_type())) {
      // do nothing
    } else if (OB_FAIL(expr->eval(eval_ctx_, datum))) {
    } else {
      const int16_t precision = expr->datum_meta_.precision_;
      if (OB_UNLIKELY(precision < 0)) {
      } else {
        int64_t bytes = 0;
        ret = expr->get_datum_copy_plan().legacy_payload_size(*datum, bytes);
      }
    }
  }
  return ret;
}

int ObOperator::output_expr_decint_datum_len_check_batch()
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < spec_.output_.count(); ++i) {
    const ObExpr *expr = spec_.output_[i];
    if (OB_ISNULL(expr)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (!ob_is_decimal_int(expr->datum_meta_.get_type())) {
      // do nothing
    } else {
      const int16_t precision = expr->datum_meta_.precision_;
      const int len = wide::ObDecimalIntConstValue::get_int_bytes_by_precision(precision);
      if (OB_UNLIKELY(precision < 0)) {
      } else if (OB_FAIL(expr->eval_batch(eval_ctx_, *brs_.skip_, brs_.size_))) {
      } else if (GET_MY_SESSION(eval_ctx_.exec_ctx_)->is_diagnosis_enabled() &&
                OB_FAIL(do_diagnosis(eval_ctx_.exec_ctx_, *brs_.skip_))) {
      } else if (!expr->is_batch_result()) {
        const Datum &datum = expr->locate_expr_datum(eval_ctx_);
        int64_t bytes = 0;
        ret = expr->get_datum_copy_plan().legacy_payload_size(datum, bytes);
      } else {
        const Datum *datums = expr->locate_batch_datums(eval_ctx_);
        for (int64_t j = 0; j < brs_.size_; j++) {
          if (!brs_.skip_->at(j) && !datums[j].is_null()) {
            int64_t bytes = 0;
            ret = expr->get_datum_copy_plan().legacy_payload_size(datums[j], bytes);
          }
        }
      }
    }
  }
  return ret;
}

// copy from ob_phy_operator.cpp
int ObOperator::open()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_stack_overflow())) {
  } else {
    OperatorOpenOrder open_order = get_operator_open_order();
    if (!spec_.is_vectorized()) {
    /*
        for non-vectorized operator, need set batch size 1;

        case: vectorize.select_basic_vec_mysql
        OceanBase(TEST@TEST)>explain insert into dlt6 select * from dlt4\G
        *************************** 1. row ***************************
        Query Plan: =======================================
        |ID|OPERATOR     |NAME |EST. ROWS|COST|
        ---------------------------------------
        |0 |INSERT       |     |7        |97  |
        |1 | SUBPLAN SCAN|VIEW1|7        |46  |
        |2 |  TABLE SCAN |DLT4 |7        |46  |
        =======================================
        when vectorizition enable,  batch_result_ of column_conv expr in insert_op is true,
        if batch size of insert eval_ctx  is 0, when column_conv eval, reset_ptr will do nothing,
        caused datum ptr is null, and core when execution;
      */
      eval_ctx_.set_batch_size(1);
      eval_ctx_.set_batch_idx(0);
    }
    while (OB_SUCC(ret) && open_order != OPEN_EXIT) {
      switch (open_order) {
      case OPEN_CHILDREN_FIRST:
      case OPEN_CHILDREN_LATER: {
        for (int64_t i = 0; OB_SUCC(ret) && i < child_cnt_; ++i) {
          // children_ pointer is checked before operator open, no need check again.
          if (OB_FAIL(children_[i]->open())) {
            if (OB_TRY_LOCK_ROW_CONFLICT != ret && OB_TRANSACTION_SET_VIOLATION != ret) {
            }
          }
        }
        open_order = (OPEN_CHILDREN_FIRST == open_order) ? OPEN_SELF_LATER : OPEN_EXIT;
        break;
      }
      case OPEN_SELF_FIRST:
      case OPEN_SELF_LATER:
      case OPEN_SELF_ONLY: {
        if (OB_FAIL(init_evaluated_flags())) {
        } else if (OB_FAIL(init_skip_vector())) {
        }
        #ifdef ENABLE_DEBUG_LOG
        else if (OB_FAIL(init_dummy_mem_context())) {
        } else if (OB_LIKELY(nullptr == dummy_ptr_)
                && OB_ISNULL(dummy_ptr_ = static_cast<char *>(dummy_mem_context_->get_malloc_allocator().alloc(sizeof(char))))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
        }
        #endif
        else if (OB_FAIL(inner_open())) {
          if (OB_TRY_LOCK_ROW_CONFLICT != ret && OB_TRANSACTION_SET_VIOLATION != ret) {
          }
        }
        open_order = (OPEN_SELF_FIRST == open_order) ? OPEN_CHILDREN_LATER : OPEN_EXIT;
        break;
      }
      case OPEN_EXIT: {
        open_order = OPEN_EXIT;
        break;
      }
      default:
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected open order type", K(open_order));
        break;
      }
    }
    if (OB_SUCC(ret)) {
      opened_ = true;
      clear_batch_end_flag();
      if (spec_.need_check_output_datum_) {
        void * ptr = ctx_.get_allocator().alloc(sizeof(ObBatchResultHolder));
        if (OB_ISNULL(ptr)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
        } else {
          // replace new the object
          brs_checker_ = new(ptr) ObBatchResultHolder();
          if (OB_FAIL(brs_checker_->init(spec_.output_, eval_ctx_))) {
          }
        }
      }
    }

    LOG_DEBUG("open op", K(ret), "op_type", op_name(), "op_id", spec_.id_, K(open_order));
  }
  return ret;
}

int ObOperator::init_evaluated_flags()
{
  int ret = OB_SUCCESS;
  if (!spec_.calc_exprs_.empty()) {
    if (OB_FAIL(eval_infos_.prepare_allocate(spec_.calc_exprs_.count()))) {
    } else {
      for (int64_t i = 0; i < spec_.calc_exprs_.count(); i++) {
        eval_infos_.at(i) = &spec_.calc_exprs_.at(i)->get_eval_info(eval_ctx_);
      }
    }
  }
  return ret;
}

// default batch_size is 1
int ObOperator::init_skip_vector()
{
  int ret = OB_SUCCESS;
  if (spec_.get_phy_plan()->is_vectorized() && NULL == brs_.skip_) {
    int batch_size = 0;
    if (spec_.max_batch_size_ > 0) {
      batch_size = spec_.max_batch_size_;
    } else {
      batch_size = 1;
    }
    void *mem = ctx_.get_allocator().alloc(ObBitVector::memory_size(batch_size));
    if (OB_ISNULL(mem)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      brs_.skip_ = to_bit_vector(mem);
      brs_.skip_->init(batch_size);
    }
  }
  return ret;
}

// copy from ob_phy_operator.cpp
int ObOperator::rescan()
{
  //rescan must reset the operator context to the state after call operator open()
  //for the general non-terminal operator, function rescan() is to call children rescan()
  //if you want to do more, you must rewrite this function
  //for the general terminal operator, function rescan() does nothing
  //you can rewrite it to complete special function
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < child_cnt_; ++i) {
    if (OB_FAIL(children_[i]->rescan())) {
    }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(inner_rescan())) {
    if (OB_ITER_END != ret) {
    }
  } else {
#ifndef NDEBUG
  OX(OB_ASSERT(false == brs_.end_));
#endif
  }

  return ret;
}

int ObOperator::inner_rescan()
{
  int ret = OB_SUCCESS;
  startup_passed_ = spec_.startup_filters_.empty();
  if (br_it_) {
    br_it_->rescan();
  }
  // If an operator rescan after drained, the exch_drained_ must be reset
  // so it can be drained again.
  exch_drained_ = false;
  batch_reach_end_ = false;
  row_reach_end_ = false;
  clear_batch_end_flag();
  op_monitor_info_.rescan_times_++;
  output_batches_b4_rescan_ = op_monitor_info_.output_batches_;
  if (spec_.need_check_output_datum_ && brs_checker_) {
    brs_checker_->reset();
  }
  if (OB_UNLIKELY(stash_rows_cnt_ > 0)) {
    stash_rows_cnt_ = 0;
    stash_rows_idx_ = 0;
    stash_brs_.size_ = 0;
  }
  return ret;
}

// copy from ob_phy_operator.cpp
int ObOperator::switch_iterator()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(inner_switch_iterator())) {
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < child_cnt_; i++) {
    if (OB_FAIL(children_[i]->switch_iterator())) {
      if (OB_ITER_END != ret) {
      }
    }
  }

#ifndef NDEBUG
  OX(OB_ASSERT(false == brs_.end_));
#endif

  return ret;
}

int ObOperator::inner_switch_iterator()
{
  int ret = OB_ITER_END;
  clear_batch_end_flag();
  return ret;
}

int check_child_closed_helper(ObOperator *child, bool &closed)
{
  closed = true;
  return child->close();
}

// copy from ob_phy_operator.cpp
int ObOperator::close()
{
  int ret = OB_SUCCESS;
  OperatorOpenOrder open_order = get_operator_open_order();
  if (OPEN_SELF_ONLY != open_order) {
    //first call close of children
    for (int64_t i = 0; i < child_cnt_; ++i) {
      bool closed = false;
      int tmp_ret = OB_SUCCESS;
      // here retry SMART_CALL a few times, try to avoid child operator not calling close
      // which maybe caused by error injection while allocing new stack in SMART_CALL
      for (int64_t retry_cnt = 0; retry_cnt < SMART_CALL_CLOSE_RETRY_TIMES && !closed; retry_cnt++) {
        // children_ pointer is checked before operator open, no need check again.
        tmp_ret = SMART_CALL(check_child_closed_helper(children_[i], closed));
      }
      if (OB_UNLIKELY(!closed)) {
        // SMART_CALL failed more than SMART_CALL_CLOSE_RETRY_TIMES, cannot alloc more memory for
        // func stack, maybe caused wild ptr because child operator cannot revoke close(),
        // so revoke right_to_die_or_duty_to_live here.
        LOG_ERROR("failed to alloc new stack for child operator to close", K(tmp_ret),
          "op_type", op_name());
        right_to_die_or_duty_to_live();
      } else if (OB_SUCCESS != tmp_ret) {
        ret = OB_SUCCESS == ret ? tmp_ret : ret;
      }
    }
  }

  // no matter what, must call operator's close function
  int tmp_ret = inner_close();
  if (OB_SUCCESS != tmp_ret) {
    ret = tmp_ret; // overwrite child's error code.
  }
  op_monitor_info_.close_time_ = oceanbase::common::ObClockGenerator::getClock();
  IGNORE_RETURN setup_op_feedback_info();
  #ifdef ENABLE_DEBUG_LOG
    if (nullptr != dummy_mem_context_) {
      if (nullptr != dummy_ptr_) {
        dummy_mem_context_->get_malloc_allocator().free(dummy_ptr_);
        dummy_ptr_ = nullptr;
      }
    }
  #endif
  return ret;
}

int ObOperator::setup_op_feedback_info()
{
  int ret = OB_SUCCESS;
  ObPhysicalPlanCtx *phy_ctx = ctx_.get_physical_plan_ctx();
  if (OB_ISNULL(spec_.plan_) || OB_ISNULL(phy_ctx)) {
    ret = OB_ERR_UNEXPECTED;
  } else if ((!spec_.plan_->need_record_plan_info() && !phy_ctx->get_check_pdml_affected_rows()) ||
             OB_INVALID_INDEX == fb_node_idx_) {
  } else {
    ObExecFeedbackInfo &fb_info = ctx_.get_feedback_info();
    common::ObIArray<ObExecFeedbackNode> &nodes = fb_info.get_feedback_nodes();
    int64_t &total_db_time = fb_info.get_total_db_time();
    uint64_t db_time = op_monitor_info_.calc_db_time();
    uint64_t cpu_khz = query::query_cpu_frequency_khz(
        ctx_.get_query_runtime_environment());
    db_time = db_time * 1000 / cpu_khz;
    total_db_time += db_time;
    if (fb_node_idx_ >= 0 && fb_node_idx_ < nodes.count()) {
      ObExecFeedbackNode &node = nodes.at(fb_node_idx_);
      node.block_time_ = op_monitor_info_.block_time_ * 1000 / cpu_khz;;
      node.db_time_ = db_time;
      node.op_close_time_ = op_monitor_info_.close_time_;
      node.op_first_row_time_ = op_monitor_info_.first_row_time_;
      node.op_last_row_time_ = op_monitor_info_.last_row_time_;
      node.op_open_time_ = op_monitor_info_.open_time_;
      node.output_row_count_ = op_monitor_info_.output_row_count_;
      node.worker_count_ = 1;
      if (spec_.is_pdml_operator()) {
        node.pdml_op_write_rows_ = op_monitor_info_.otherstat_5_value_;
      }
    }
  }
  return ret;
}

int ObOperator::get_next_row()
{
  int ret = OB_SUCCESS;
  begin_cpu_time_counting();
  if (OB_FAIL(check_stack_once())) {
  }
  if (OB_FAIL(ret)) {
  } else {
    if (row_reach_end_) {
      ret = OB_ITER_END;
    } else if (OB_UNLIKELY(get_spec().is_vectorized())) {
      // Operator itself supports vectorization, while parent operator does NOT.
      // Use vectorize method to get next row.
      if (OB_FAIL(get_next_row_vectorizely())) {
        // do nothing
      }
    } else {
      if (OB_UNLIKELY(!startup_passed_)) {
        bool filtered = false;
        if (OB_FAIL(startup_filter(filtered))) {
        } else {
          if (filtered) {
            ret = OB_ITER_END;
          } else {
            startup_passed_ = true;
          }
        }
      }

      while (OB_SUCC(ret)) {
        if (OB_FAIL(inner_get_next_row())) {
          if (OB_ITER_END != ret) {
          }
        } else if (OB_FAIL(try_check_status())) {
        } else {
          bool filtered = false;
          if (!spec_.filters_.empty()) {
            if (OB_FAIL(filter_row(filtered))) {
            } else {
              if (filtered) {
                continue;
              }
            }
          }
#ifndef NDEBUG
          if (OB_SUCC(ret) && !filtered) {
            if (OB_FAIL(output_expr_decint_datum_len_check())) {
            }
          }
#endif
        }
        break;
      }

      if (OB_SUCCESS == ret) {
        op_monitor_info_.output_row_count_++;
        if (!got_first_row_) {
          op_monitor_info_.first_row_time_ = oceanbase::common::ObClockGenerator::getClock();
          got_first_row_ = true;
        }
      } else if (OB_ITER_END == ret) {
        row_reach_end_ = true;
        int tmp_ret = do_drain_exch();
        if (OB_SUCCESS != tmp_ret) {
        }
        if (got_first_row_) {
          op_monitor_info_.last_row_time_ = oceanbase::common::ObClockGenerator::getClock();
        }
      }
    }
    if (OB_ITER_END == ret) {
      row_reach_end_ = true;
    } else if (OB_SUCC(ret)) {
      if (spec_.get_phy_plan()->is_vectorized()
          || spec_.get_phy_plan()->is_contains_assignment()) {
        Datum *res = NULL;
        FOREACH_CNT_X(e, spec_.output_, OB_SUCC(ret)) {
          Datum caller_value;
          if ((*e)->native_i64_layout_ != nullptr
              ? OB_FAIL((*e)->eval_value(eval_ctx_, caller_value))
              : OB_FAIL((*e)->eval(eval_ctx_, res))) {
          } else {
            (*e)->get_eval_info(eval_ctx_).projected_ = true;
          }
        }
      }
    }
  }
  end_cpu_time_counting();
  return ret;
}

int ObOperator::pop_stash_rows(const int64_t max_row_cnt)
{
  int ret = OB_SUCCESS;
  brs_.size_ = stash_brs_.size_;
  brs_.skip_->set_all(stash_brs_.size_);
  int64_t cur_row_cnt = 0;
  while (stash_rows_idx_ < stash_brs_.size_ && cur_row_cnt < max_row_cnt) {
    if (!stash_brs_.skip_->at(stash_rows_idx_)) {
      brs_.skip_->unset(stash_rows_idx_);
      ++cur_row_cnt;
    }
    ++stash_rows_idx_;
  }
  stash_rows_cnt_ -= cur_row_cnt;
  if (stash_brs_.end_ && stash_rows_cnt_ == 0) {
    brs_.end_ = true;
  } else {
    brs_.end_ = false;
  }
  brs_.all_rows_active_ = (cur_row_cnt == brs_.size_);
  return ret;
}

int ObOperator::push_stash_rows(const int64_t max_row_cnt, const int64_t output_row_cnt)
{
  int ret = OB_SUCCESS;
  if (nullptr == stash_brs_.skip_) {
    void *mem = ctx_.get_allocator().alloc(ObBitVector::memory_size(get_spec().max_batch_size_));
    if (OB_ISNULL(mem)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      stash_brs_.skip_ = to_bit_vector(mem);
    }
  }
  if (OB_SUCC(ret)) {
    stash_brs_.skip_->deep_copy(*brs_.skip_, brs_.size_);
    stash_brs_.size_ = brs_.size_;
    stash_brs_.end_ = brs_.end_;
    stash_rows_cnt_ = output_row_cnt;
    stash_rows_idx_ = 0;
    if (OB_FAIL(pop_stash_rows(max_row_cnt))) {
    }
  }
  return ret;
}

int ObOperator::get_next_batch(const int64_t max_row_cnt, const ObBatchRows *&batch_rows)
{
  int ret = OB_SUCCESS;
  begin_cpu_time_counting();

  if (OB_FAIL(check_stack_once())) {
  } else {
    if (OB_UNLIKELY(spec_.need_check_output_datum_
                    && brs_checker_)) {
      if (OB_FAIL(brs_checker_->check_datum_modified())) {
      }
    }
    reset_batchrows();
    int64_t skipped_rows_count = 0;
    batch_rows = &brs_;
    if (batch_reach_end_) {
      // generated all data at last batch, just drain exch in this iterate
      brs_.end_ = true;
      brs_.size_ = 0;
    }
    if (OB_FAIL(ret)) {
      // do nothing
    } else if (OB_LIKELY(get_spec().is_vectorized())) {
      if (OB_UNLIKELY(!startup_passed_) && !brs_.end_) {
        ObEvalCtx::BatchInfoScopeGuard guard(eval_ctx_);
        guard.set_batch_size(1);
        guard.set_batch_idx(0);
        bool filtered = false;
        // TODO bin.lb: vectorized startup filter? no ObExpr::eval() in vectorization?
        if (OB_FAIL(startup_filter(filtered))) {
        } else {
          if (filtered) {
            brs_.end_ = true;
          } else {
            startup_passed_ = true;
          }
        }
      }
      int64_t op_max_row_cnt = min(max_row_cnt, get_spec().max_batch_size_);
      bool all_filtered = false;
      while (OB_SUCC(ret) && !brs_.end_) {
        if (OB_UNLIKELY(stash_rows_cnt_ > 0)) {
          if (OB_FAIL(pop_stash_rows(op_max_row_cnt))) {
          }
        } else {
          if (OB_FAIL(inner_get_next_batch(op_max_row_cnt))) {
          } else {
            LOG_DEBUG("inner get next batch", "id", spec_.get_id(), "op_name", op_name(), K(brs_));
          }
          if (OB_SUCC(ret)) {
            // FIXME bin.lb: accumulate bit count is CPU consuming, disable in perf mode?
            skipped_rows_count = brs_.skip_->accumulate_bit_cnt(brs_.size_);
            if (OB_UNLIKELY(brs_.size_ == skipped_rows_count)) {
              reset_batchrows();
              continue;
            }
          }
          all_filtered = false;
          if (OB_FAIL(ret)) {
          } else if (OB_FAIL(try_check_status_by_rows(brs_.size_))) {
          } else if (!spec_.filters_.empty()) {
            if (OB_FAIL(filter_rows(spec_.filters_,
                                    *brs_.skip_,
                                    brs_.size_,
                                    all_filtered,
                                    brs_.all_rows_active_))) {
            } else if (all_filtered) {
              brs_.skip_->reset(brs_.size_);
              brs_.size_ = 0;
              // keep brs_.end_ unchanged.
              continue;
            }
          }
        }
#ifndef NDEBUG
        if (OB_SUCC(ret) && !all_filtered) {
          if (OB_FAIL(output_expr_decint_datum_len_check_batch())) {
          }
        }
#endif
        break;
      }

      // do project
      if (OB_SUCC(ret) && brs_.size_ > 0) {
        FOREACH_CNT_X(e, spec_.output_, OB_SUCC(ret)) {
          ret = (*e)->eval_batch(eval_ctx_, *brs_.skip_, brs_.size_);
          (*e)->get_eval_info(eval_ctx_).projected_ = true;
        }
      }
      
      if (OB_SUCC(ret) && GET_MY_SESSION(eval_ctx_.exec_ctx_)->is_diagnosis_enabled()) {
        if (OB_FAIL(do_diagnosis(eval_ctx_.exec_ctx_, *brs_.skip_))) {
        }
      }

      if (OB_SUCC(ret) && OB_FAIL(try_push_stash_rows(op_max_row_cnt))) {
      }

      if (OB_SUCC(ret)) {
        if (brs_.end_ && brs_.size_ >= 0) {
          batch_reach_end_ = true; // prepare to be end
          // if no data in batch, end iterate immediately, otherwise wait for next iterate
          brs_.end_ = !brs_.size_;
        }
        skipped_rows_count = brs_.skip_->accumulate_bit_cnt(brs_.size_);
        op_monitor_info_.output_row_count_ += brs_.size_ - skipped_rows_count;
        op_monitor_info_.skipped_rows_count_ += skipped_rows_count; // for batch
        ++op_monitor_info_.output_batches_; // for batch
        if (!got_first_row_ && !brs_.end_) {
          op_monitor_info_.first_row_time_ = ObClockGenerator::getClock();
          got_first_row_ = true;
        }
        if (brs_.end_) {
          int tmp_ret = do_drain_exch();
          if (OB_SUCCESS != tmp_ret) {
          }
          op_monitor_info_.last_row_time_ = ObClockGenerator::getClock();
        }
      }
    } else {
      // Operator does NOT support vectorization, while its parent does. Return
      // the batch with only 1 row
      if (OB_FAIL(get_next_batch_with_onlyone_row())) {
        // do nothing
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_UNLIKELY(spec_.need_check_output_datum_) && brs_checker_ && !brs_.end_ && brs_.size_ > 0) {
        OZ(brs_checker_->save(brs_.size_));
      }
      LOG_DEBUG("get next batch", "id", spec_.get_id(), "op_name", op_name(), K(brs_));
    }
  }

  if (PHY_TABLE_SCAN != spec_.type_) {
    brs_.set_all_rows_active(false);
  }

  end_cpu_time_counting();
  return ret;
}

int ObOperator::filter_row(ObEvalCtx &eval_ctx, const ObIArray<ObExpr *> &exprs, bool &filtered)
{
  Datum *datum = NULL;
  int ret = OB_SUCCESS;
  filtered = false;
  FOREACH_CNT_X(e, exprs, OB_SUCC(ret)) {
    OB_ASSERT(NULL != *e);
    bool native = false;
    if ((*e)->native_i64_layout_ != nullptr && OB_FAIL((*e)->get_native_i64_mode(eval_ctx, native))) {
    } else if (native) {
      int64_t value = 0;
      Datum::State state = Datum::State::Null;
      if (OB_FAIL((*e)->eval_i64(eval_ctx, value, state))) {
      } else if (state == Datum::State::Null || value == 0) {
        filtered = true;
        break;
      }
    } else if (OB_FAIL((*e)->eval(eval_ctx, datum))) {
    } else {
      OB_ASSERT(ob_is_int_tc((*e)->datum_meta_.type_));
      if (datum->is_null() || 0 == datum->get_int()) {
        filtered = true;
        break;
      }
    }
  }

  return ret;
}

int ObOperator::filter(const common::ObIArray<ObExpr *> &exprs, bool &filtered)
{
  return filter_row(eval_ctx_, exprs, filtered);
}

int ObOperator::filter_rows(const ObExprPtrIArray &exprs,
                            ObBitVector &skip,
                            const int64_t bsize,
                            bool &all_filtered,
                            bool &all_active)
{
  return filter_batch_rows(exprs, skip, bsize, all_filtered, all_active);
}

int ObOperator::filter_batch_rows(const ObExprPtrIArray &exprs,
                                  ObBitVector &skip,
                                  const int64_t bsize,
                                  bool &all_filtered,
                                  bool &all_active)
{
  int ret = OB_SUCCESS;
  all_filtered = false;
  bool tmp_all_active = true;
  FOREACH_CNT_X(e, exprs, OB_SUCC(ret) && !all_filtered) {
    OB_ASSERT(ob_is_int_tc((*e)->datum_meta_.type_));
    bool native = false;
    if ((*e)->native_i64_layout_ != nullptr && OB_FAIL((*e)->get_native_i64_mode(eval_ctx_, native))) {
    } else if (OB_FAIL((*e)->eval_batch(eval_ctx_, skip, bsize))) {
    } else if (native) {
      NativeI64Lane lane{};
      if (OB_FAIL((*e)->get_native_i64_lane(eval_ctx_, lane))) {
      } else {
        int64_t output_rows = 0;
        for (int64_t i = 0; OB_SUCC(ret) && i < bsize; ++i) {
          if (!skip.at(i)) {
            const int64_t index = lane.batch ? i : 0;
            if (index >= lane.capacity) { ret = OB_INVALID_ARGUMENT; }
            else if (lane.states[index] != static_cast<uint8_t>(Datum::State::Null)
                     && lane.states[index] != static_cast<uint8_t>(Datum::State::Present)) {
              ret = OB_ERR_UNEXPECTED;
            } else if (lane.states[index] == static_cast<uint8_t>(Datum::State::Null) || lane.values[index] == 0) {
              skip.set(i); tmp_all_active = false;
            } else { ++output_rows; }
          }
        }
        if (OB_SUCC(ret)) { all_filtered = output_rows == 0; }
      }
    } else if (!(*e)->is_batch_result()) {
      const Datum &d = (*e)->locate_expr_datum(eval_ctx_);
      if (d.is_null() || 0 == d.get_int()) {
        all_filtered = true;
        skip.set_all(bsize);
        tmp_all_active = false;
      }
    } else {
      int64_t output_rows = 0;
      const Datum *datums = (*e)->locate_batch_datums(eval_ctx_);
      for (int64_t i = 0; i < bsize; i++) {
        if (!skip.at(i)) {
          if (datums[i].is_null() || 0 == datums[i].get_int()) {
            skip.set(i);
            tmp_all_active = false;
          } else {
            output_rows += 1;
          }
        }
      }
      // FIXME bin.lb: add output_rows to ObBatchRows?
      all_filtered = (0 == output_rows);
    }

    all_active &= tmp_all_active;
  }
  return ret;
}

// copy ObPhyOperator::drain_exch
int ObOperator::drain_exch()
{
  int ret = OB_SUCCESS;
  begin_cpu_time_counting();
  ret = do_drain_exch();
  end_cpu_time_counting();
  return ret;
}

int ObOperator::do_drain_exch()
{
  int ret = OB_SUCCESS;
  /**
   * 1. try to open this operator
   * 2. try to drain all children
   */
  if (OB_FAIL(try_open())) {
  } else if (!exch_drained_) {
    int tmp_ret = inner_drain_exch();
    exch_drained_ = true;
    // If an operator is drained, it means that the parent operator will never call its
    // get_next_batch function again theoretically. However, we cannot guarantee that there won't be
    // any bugs that call get_next_batch again after drain. To prevent this situation, we set the
    // all iter end flags here.
    // For specific case, refer to issue: 
    brs_.end_ = true;
    batch_reach_end_ = true;
    row_reach_end_ = true;
    if (!spec_.is_receive()) {
      for (int64_t i = 0; i < child_cnt_ && OB_SUCC(ret); i++) {
        if (OB_ISNULL(children_[i])) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_FAIL(children_[i]->drain_exch())) {
        }
      }
    }
    if (OB_SUCC(ret)) {
      ret = tmp_ret;
    }
  }
  return ret;
}

int ObOperator::check_status()
{
  return ctx_.check_status();
}

void ObOperator::do_clear_datum_eval_flag()
{
  FOREACH_CNT(e, spec_.calc_exprs_) {
    if ((*e)->is_batch_result()) {
      (*e)->get_evaluated_flags(eval_ctx_).unset(eval_ctx_.get_batch_idx());
    } else {
      (*e)->get_eval_info(eval_ctx_).clear_evaluated_flag();
    }
  }
}
void ObOperator::set_pushdown_param_null(const ObIArray<ObDynamicParamSetter> &rescan_params)
{
  ObPhysicalPlanCtx *plan_ctx = GET_PHY_PLAN_CTX(ctx_);
  FOREACH_CNT(param, rescan_params) {
    const int64_t index = param->param_idx_;
    int ret = OB_SUCCESS;
    NativeTypedDatum value;
    DatumParameterMetadata attributes;
    if (OB_FAIL(plan_ctx->capture_native_parameter(index, value, attributes))) {
      LOG_ERROR("invalid pushdown parameter", K(ret), K(index));
    } else {
      Datum null_value;
      null_value.set_null();
      if (OB_FAIL(plan_ctx->set_runtime_parameter(index, null_value, *value.type_, attributes))) {
        LOG_ERROR("failed to clear pushdown parameter", K(ret), K(index));
      } else {
        param->dst_->locate_expr_datum(eval_ctx_, 0).set_null();
      }
    }
  }
}

inline int ObOperator::get_next_row_vectorizely()
{
  int ret = OB_SUCCESS;
  //init br_it_ at the 1st time
  if (br_it_ == nullptr) {
    void * ptr = ctx_.get_allocator().alloc(sizeof(ObBatchRowIter));
    if (OB_ISNULL(ptr)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      SQL_ENG_LOG(WARN, "allocation failed for br_it_", K(ret),
                  K(sizeof(ObBatchRowIter)));
    } else {
      br_it_ = new(ptr) ObBatchRowIter();
      br_it_->set_operator(this);
      if (OB_FAIL(br_it_->brs_holder_.init(spec_.output_, eval_ctx_))){
      }
    }
  }

  if (OB_SUCC(ret) && OB_FAIL(br_it_->get_next_row(eval_ctx_, spec_))) {
    if (ret != OB_ITER_END) {
      SQL_ENG_LOG(WARN, "Failed to get_next_row vectorizedly", K(ret));
    }
  }
  // update eval_ctx_ so that upper operator can find the correct datum when
  // evoking ObExpr::eval_one_datum_of_batch(ctx, datum)
  eval_ctx_.set_batch_size(1);
  eval_ctx_.set_batch_idx(0);
  return ret;
}

#ifdef ENABLE_DEBUG_LOG
inline int ObOperator::init_dummy_mem_context()
{
  int ret = common::OB_SUCCESS;
  if (OB_LIKELY(NULL == dummy_mem_context_)) {
    lib::ContextParam param;
    param.set_properties(lib::USE_TL_PAGE_OPTIONAL)
      .set_mem_attr("ObSqlOp",
                     common::ObCtxIds::DEFAULT_CTX_ID);
    if (OB_FAIL(CURRENT_CONTEXT->CREATE_CONTEXT(dummy_mem_context_, param))) {
      SQL_ENG_LOG(WARN, "create entity failed", K(ret));
    } else if (OB_ISNULL(dummy_mem_context_)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      SQL_ENG_LOG(WARN, "mem entity is null", K(ret));
    }
  }
  return ret;
}
#endif

int ObBatchRowIter::get_next_row()
{
  const int64_t max_row_cnt = INT64_MAX;
  int ret = OB_SUCCESS;
  if (NULL == brs_) {
    if (OB_FAIL(op_->get_next_batch(max_row_cnt, brs_))) {
    }
  }
  while (OB_SUCC(ret)) {
    if (idx_ >= brs_->size_ && brs_->end_) {
      ret = OB_ITER_END;
      break;
    }
    // skip the filtered row
    while (idx_ < brs_->size_ && brs_->skip_->at(idx_)) {
      idx_++;
    }
    if (idx_ >= brs_->size_) {
      if (!brs_->end_) {
        if (OB_FAIL(op_->get_next_batch(max_row_cnt, brs_))) {
        }
        idx_ = 0;
      }
      continue;
    } else {
      // got row, increase the index
      idx_ += 1;
      break;
    }
  }
  return ret;
}

int ObBatchRowIter::get_next_row(ObEvalCtx &eval_ctx, const ObOpSpec &spec)
{
  const int64_t max_row_cnt = INT64_MAX;
  int ret = OB_SUCCESS;
  if (NULL == brs_) {
    if (OB_FAIL(op_->get_next_batch(max_row_cnt, brs_))) {
    } else if (OB_FAIL(brs_holder_.save(1))) {
    }
    // backup datums[0]
  }
  while (OB_SUCC(ret)) {
    if (idx_ >= brs_->size_ && brs_->end_) {
      ret = OB_ITER_END;
      break;
    }
    // skip the filtered row
    while (idx_ < brs_->size_ && brs_->skip_->at(idx_)) {
      idx_++;
    }
    if (idx_ >= brs_->size_) {
      if (!brs_->end_) {
        if (OB_FAIL(brs_holder_.restore())) {
        } else if (OB_FAIL(op_->get_next_batch(max_row_cnt, brs_))) {
        } else {
          idx_ = 0;
          if (0 == brs_->size_ && brs_->end_) {
            ret = OB_ITER_END;
            break;
          } else if (OB_FAIL(brs_holder_.save(1))) {
          }
        }
      }
      continue;
    } else {
      // got row, increase the index to next row
      idx_ += 1;
      break;
    }
  }
  // overwrite datums[0]: shallow copy
  for (auto i = 0; OB_SUCC(ret) && i < spec.output_.count(); i++) {
    ObExpr *expr = spec.output_.at(i);
    if (expr->is_batch_result() && 0 != cur_idx()) {
      bool native = false;
      if (OB_FAIL(expr->get_native_i64_mode(eval_ctx, native))) {
      } else if (native) {
        NativeI64Lane lane;
        if (OB_FAIL(expr->get_native_i64_lane(eval_ctx, lane))) {
        } else if (cur_idx() < 0 || cur_idx() >= lane.capacity) {
          ret = OB_INVALID_ARGUMENT;
        } else {
          const Datum::State state = static_cast<Datum::State>(lane.states[cur_idx()]);
          if (state != Datum::State::Present && state != Datum::State::Null) {
            ret = OB_ERR_UNEXPECTED;
          } else {
            if (state == Datum::State::Present) { lane.values[0] = lane.values[cur_idx()]; }
            lane.states[0] = static_cast<uint8_t>(state);
            LOG_DEBUG("copy native batch value to row zero", K(cur_idx()), K(state), KPC(expr));
          }
        }
      } else {
        Datum *datums = expr->locate_batch_datums(eval_ctx);
        datums[0] = datums[cur_idx()];
        LOG_DEBUG("copy datum to datum[0], details: ", K(cur_idx()), K(datums[0]),
                 KP(datums), KPC(datums), K(expr), KPC(expr));
      }
    } // non batch expr always point to offset 0, do nothing for else brach
  }

  return ret;
}

namespace {
int decode_batch_index_array(const char *buf, int64_t length, int64_t &pos,
                             ObIArray<int64_t> &output, bool one_based)
{
  int ret = OB_SUCCESS;
  int64_t count = 0;
  if (OB_FAIL(serialization::decode(buf, length, pos, count))) {
  } else if (count < 0 || count > length - pos) {
    ret = OB_DESERIALIZE_ERROR;
  } else if (OB_FAIL(output.reserve(count))) {
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
    int64_t index = 0;
    if (OB_FAIL(serialization::decode(buf, length, pos, index))) {
    } else if (index < (one_based ? 1 : 0)) {
      ret = OB_DESERIALIZE_ERROR;
    } else if (OB_FAIL(output.push_back(index))) {
    }
  }
  return ret;
}
}

namespace {
// Stack-only old parameter view for the historical v1 field codec. Its scalar
// scratch is consumed synchronously; neither descriptor nor scratch is stored.
int batch_wire_parameter(const NativeTypedDatum &value, const DatumParameterMetadata &attributes,
                         DatumParameter &output, void *scratch, int64_t capacity)
{
  int ret = OB_SUCCESS;
  DatumParameter candidate;
  if (value.type_ == nullptr) {
    ret = candidate.assign_native_borrowed(value.datum_, attributes.type_meta_);
  } else {
    ObDatum legacy;
    if (OB_FAIL(DatumCopyPlan(*value.type_).legacy_view(value.datum_, legacy, scratch, capacity))) {
    } else { ret = candidate.assign_legacy_borrowed(legacy, attributes.type_meta_); }
  }
  if (OB_SUCC(ret)) { candidate.set_metadata(attributes); output = candidate; }
  return ret;
}
}

int ObBatchRescanParams::validate_shape() const
{
  int ret = OB_SUCCESS;
  if (params_.count() != metadata_rows_.count()
      || (param_expr_idxs_.count() != 0 && param_expr_idxs_.count() != param_idxs_.count())) {
    ret = OB_INVALID_ARGUMENT;
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < param_idxs_.count(); ++i) {
    if (param_idxs_.at(i) < 0) { ret = OB_INVALID_ARGUMENT; }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < param_expr_idxs_.count(); ++i) {
    if (param_expr_idxs_.at(i) <= 0) { ret = OB_INVALID_ARGUMENT; }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < params_.count(); ++i) {
    if (params_.at(i).count() != param_idxs_.count()
        || metadata_rows_.at(i).count() != param_idxs_.count()) { ret = OB_INVALID_ARGUMENT; }
    for (int64_t j = 0; OB_SUCC(ret) && j < params_.at(i).count(); ++j) {
      if (native_ready_ && params_.at(i).at(j).type_ == nullptr) { ret = OB_INVALID_ARGUMENT; }
    }
  }
  return ret;
}

OB_DEF_SERIALIZE(ObBatchRescanParams)
{
  int ret = validate_shape();
  OB_UNIS_ENCODE(params_.count());
  for (int64_t i = 0; OB_SUCC(ret) && i < params_.count(); ++i) {
    const auto &row = params_.at(i);
    OB_UNIS_ENCODE(row.count());
    for (int64_t j = 0; OB_SUCC(ret) && j < row.count(); ++j) {
      alignas(8) char scratch[64];
      DatumParameter parameter;
      if (OB_FAIL(batch_wire_parameter(row.at(j), metadata_rows_.at(i).at(j), parameter, scratch, sizeof(scratch)))) {
      } else { ret = DatumParameterCodec::encode_parameter(parameter, buf, buf_len, pos); }
    }
  }
  OB_UNIS_ENCODE(param_idxs_);
  OB_UNIS_ENCODE(param_expr_idxs_);
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObBatchRescanParams)
{
  int ret = validate_shape();
  int64_t len = 0;
  OB_UNIS_ADD_LEN(params_.count());
  for (int64_t i = 0; OB_SUCC(ret) && i < params_.count(); ++i) {
    const auto &row = params_.at(i);
    OB_UNIS_ADD_LEN(row.count());
    for (int64_t j = 0; OB_SUCC(ret) && j < row.count(); ++j) {
      alignas(8) char scratch[64];
      DatumParameter parameter;
      int64_t bytes = 0;
      if (OB_FAIL(batch_wire_parameter(row.at(j), metadata_rows_.at(i).at(j), parameter, scratch, sizeof(scratch)))) {
      } else if (OB_FAIL(DatumParameterCodec::parameter_size(parameter, bytes))) {
      } else if (__builtin_add_overflow(len, bytes, &len)) { ret = OB_SIZE_OVERFLOW; }
    }
  }
  OB_UNIS_ADD_LEN(param_idxs_);
  OB_UNIS_ADD_LEN(param_expr_idxs_);
  if (OB_FAIL(ret)) { LOG_WARN("batch has no supported v1 wire size", K(ret)); len = 0; }
  return len;
}

OB_DEF_DESERIALIZE(ObBatchRescanParams)
{
  int ret = OB_SUCCESS;
  reset();
  native_ready_ = false;
  int64_t row_count = 0;
  if (OB_FAIL(serialization::decode(buf, data_len, pos, row_count))) {
  } else if (row_count < 0 || row_count > data_len - pos) { ret = OB_DESERIALIZE_ERROR; }
  for (int64_t i = 0; OB_SUCC(ret) && i < row_count; ++i) {
    int64_t count = 0;
    Row row(4 * sizeof(NativeTypedDatum));
    MetadataRow attributes(4 * sizeof(DatumParameterMetadata));
    if (OB_FAIL(serialization::decode(buf, data_len, pos, count))) {
    } else if (count < 0 || count > (data_len - pos) / 6) { ret = OB_DESERIALIZE_ERROR; }
    else if (OB_FAIL(row.reserve(count))) {
    } else if (OB_FAIL(attributes.reserve(count))) {
    }
    for (int64_t j = 0; OB_SUCC(ret) && j < count; ++j) {
      DatumParameter physical;
      NativeTypedDatum value;
      if (OB_FAIL(DatumParameterCodec::decode_parameter(buf, data_len, pos, allocator_, physical))) {
      } else {
        value.datum_ = physical.get_datum(); // sole physical value, not a second wrapper row
        if (OB_FAIL(row.push_back(value))) {
        } else { ret = attributes.push_back(physical.get_metadata()); }
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(params_.push_back(row))) {
    } else if (OB_SUCC(ret) && OB_FAIL(metadata_rows_.push_back(attributes))) { params_.pop_back(); }
  }
  if (OB_SUCC(ret)) { ret = decode_batch_index_array(buf, data_len, pos, param_idxs_, false); }
  if (OB_SUCC(ret) && pos < data_len) { ret = decode_batch_index_array(buf, data_len, pos, param_expr_idxs_, true); }
  if (OB_SUCC(ret) && validate_shape() != OB_SUCCESS) { ret = OB_DESERIALIZE_ERROR; }
  if (OB_FAIL(ret)) { reset(); }
  return ret;
}

int ObBatchRescanParams::bind_received_types(const ObIArray<TypeRow> &row_types)
{
  int ret = validate_shape();
  if (native_ready_ || row_types.count() != params_.count()) { return OB_INVALID_ARGUMENT; }
  ObSArray<Row> prepared;
  for (int64_t i = 0; OB_SUCC(ret) && i < params_.count(); ++i) {
    Row row(4 * sizeof(NativeTypedDatum));
    if (row_types.at(i).count() != params_.at(i).count()) { ret = OB_INVALID_ARGUMENT; }
    else { ret = row.reserve(params_.at(i).count()); }
    for (int64_t j = 0; OB_SUCC(ret) && j < params_.at(i).count(); ++j) {
      const SqlType *type = row_types.at(i).at(j);
      alignas(8) char scratch[64];
      ObDatum legacy;
      NativeTypedDatum value, owned;
      if (type == nullptr) { ret = OB_INVALID_ARGUMENT; }
      else if (OB_FAIL(DatumParameterCodec::export_native_value(params_.at(i).at(j).datum_,
          metadata_rows_.at(i).at(j).type_meta_, legacy, scratch, sizeof(scratch)))) {
      } else if (OB_FAIL(DatumCopyPlan(*type).import_legacy(legacy, value.datum_))) {
      } else {
        value.type_ = type;
        if (OB_FAIL(clone_native_parameter_value(value, allocator_, owned))) {
        } else { ret = row.push_back(owned); }
      }
    }
    if (OB_SUCC(ret)) { ret = prepared.push_back(row); }
  }
  // Final publication consists only of trivial descriptors; it cannot allocate
  // or partially replace the old physical matrix on a preparation error.
  if (OB_SUCC(ret)) {
    for (int64_t i = 0; i < params_.count(); ++i) {
      for (int64_t j = 0; j < params_.at(i).count(); ++j) { params_.at(i).at(j) = prepared.at(i).at(j); }
    }
    native_ready_ = true;
  }
  return ret;
}

int ObBatchRescanParams::assign(const ObBatchRescanParams &other)
{
  int ret = OB_SUCCESS;
  if (this != &other) {
    reset();
    if (OB_FAIL(other.validate_shape())) {
    } else if (!other.native_ready_) { ret = OB_NOT_INIT; }
    else if (other.params_.empty()) {
      if (OB_FAIL(param_idxs_.assign(other.param_idxs_))) {
      } else { ret = param_expr_idxs_.assign(other.param_expr_idxs_); }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < other.params_.count(); ++i) {
      ret = append_batch_rescan_param(other.param_idxs_, other.params_.at(i),
          other.metadata_rows_.at(i), other.param_expr_idxs_);
    }
    if (OB_FAIL(ret)) { reset(); }
  }
  return ret;
}

int ObBatchRescanParams::append_batch_rescan_param(const ObIArray<int64_t> &param_idxs,
    const ObIArray<NativeTypedDatum> &values, const ObIArray<DatumParameterMetadata> &attributes)
{
  return append_batch_rescan_param(param_idxs, values, attributes, param_expr_idxs_);
}

int ObBatchRescanParams::append_batch_rescan_param(const ObIArray<int64_t> &param_idxs,
    const ObIArray<NativeTypedDatum> &values, const ObIArray<DatumParameterMetadata> &attributes,
    const ObIArray<int64_t> &param_expr_idxs)
{
  int ret = validate_append(param_idxs, values.count(), param_expr_idxs);
  if (values.count() != attributes.count()) { ret = OB_INVALID_ARGUMENT; }
  RowBuilder builder(*this);
  for (int64_t i = 0; OB_SUCC(ret) && i < values.count(); ++i) { ret = builder.append(values.at(i), attributes.at(i)); }
  if (OB_SUCC(ret)) { ret = builder.finish(param_idxs, param_expr_idxs); }
  return ret;
}

int ObBatchRescanParams::validate_append(const ObIArray<int64_t> &param_idxs, int64_t count,
                                       const ObIArray<int64_t> &param_expr_idxs) const
{
  int ret = OB_SUCCESS;
  if (!native_ready_ || param_idxs.count() != count
      || (param_expr_idxs.count() != 0 && param_expr_idxs.count() != count)) { ret = OB_INVALID_ARGUMENT; }
  for (int64_t i = 0; OB_SUCC(ret) && i < param_idxs.count(); ++i) {
    if (param_idxs.at(i) < 0) { ret = OB_INVALID_ARGUMENT; }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < param_expr_idxs.count(); ++i) {
    if (param_expr_idxs.at(i) <= 0) { ret = OB_INVALID_ARGUMENT; }
  }
  if (OB_SUCC(ret) && !params_.empty()) {
    if (param_idxs.count() != param_idxs_.count() || param_expr_idxs.count() != param_expr_idxs_.count()) { ret = OB_INVALID_ARGUMENT; }
    for (int64_t i = 0; OB_SUCC(ret) && i < param_idxs.count(); ++i) {
      if (param_idxs.at(i) != param_idxs_.at(i)) { ret = OB_INVALID_ARGUMENT; }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < param_expr_idxs.count(); ++i) {
      if (param_expr_idxs.at(i) != param_expr_idxs_.at(i)) { ret = OB_INVALID_ARGUMENT; }
    }
  }
  return ret;
}

int ObBatchRescanParams::append_captured_row(const ObIArray<int64_t> &param_idxs,
    const Row &values, const MetadataRow &attributes, const ObIArray<int64_t> &param_expr_idxs)
{
  int ret = validate_append(param_idxs, values.count(), param_expr_idxs);
  const bool first = params_.empty();
  if (attributes.count() != values.count()) { ret = OB_INVALID_ARGUMENT; }
  if (OB_SUCC(ret) && first) {
    if (&param_idxs != &param_idxs_ && OB_FAIL(param_idxs_.assign(param_idxs))) {
    } else if (&param_expr_idxs != &param_expr_idxs_) { ret = param_expr_idxs_.assign(param_expr_idxs); }
  }
  if (OB_SUCC(ret) && OB_FAIL(params_.push_back(values))) {
  } else if (OB_SUCC(ret) && OB_FAIL(metadata_rows_.push_back(attributes))) { params_.pop_back(); }
  if (OB_FAIL(ret) && first) { param_idxs_.reuse(); param_expr_idxs_.reuse(); }
  return ret;
}

int ObBatchRescanParams::RowBuilder::append(const NativeTypedDatum &value,
                                          const DatumParameterMetadata &attributes)
{
  int ret = OB_SUCCESS;
  NativeTypedDatum owned;
  if (finished_) { ret = OB_STATE_NOT_MATCH; }
  else if (OB_FAIL(clone_native_parameter_value(value, owner_.allocator_, owned))) {
  } else if (OB_FAIL(values_.push_back(owned))) {
  } else if (OB_FAIL(metadata_.push_back(attributes))) { values_.pop_back(); }
  return ret;
}

int ObBatchRescanParams::RowBuilder::finish(const ObIArray<int64_t> &param_idxs,
                                          const ObIArray<int64_t> &param_expr_idxs)
{
  int ret = OB_SUCCESS;
  if (finished_) { ret = OB_STATE_NOT_MATCH; }
  else if (OB_FAIL(owner_.append_captured_row(param_idxs, values_, metadata_, param_expr_idxs))) {
  } else { finished_ = true; }
  return ret;
}

// Explicit typed batch protocol. The enclosing PX transport selects this port;
// historical serialize/deserialize remain the unchanged v1 physical protocol.
namespace {
// Typed-batch headers are varints. Check every byte before reading it; the
// historical generic decoder inspects its next continuation byte too early.
int decode_native_batch_integer(const char *buf, int64_t length, int64_t &pos, int64_t &output)
{
  if (buf == nullptr || pos < 0 || pos > length) { return OB_INVALID_ARGUMENT; }
  int64_t cursor = pos;
  uint64_t bits = 0;
  for (int shift = 0; shift < 70; shift += 7) {
    if (cursor >= length) { return OB_DESERIALIZE_ERROR; }
    const uint8_t byte = static_cast<uint8_t>(buf[cursor++]);
    if (shift == 63 && (byte & 0xfe) != 0) { return OB_DESERIALIZE_ERROR; }
    bits |= uint64_t(byte & 0x7f) << shift;
    if ((byte & 0x80) == 0) {
      output = static_cast<int64_t>(bits); pos = cursor; return OB_SUCCESS;
    }
  }
  return OB_DESERIALIZE_ERROR;
}
int decode_native_batch_indexes(const char *buf, int64_t length, int64_t &pos,
    ObIArray<int64_t> &output, bool one_based)
{
  int64_t count = 0;
  int ret = decode_native_batch_integer(buf, length, pos, count);
  if (ret == OB_SUCCESS && (count < 0 || count > length - pos)) { ret = OB_DESERIALIZE_ERROR; }
  if (ret == OB_SUCCESS) { ret = output.reserve(count); }
  for (int64_t i = 0; ret == OB_SUCCESS && i < count; ++i) {
    int64_t value = 0;
    if (OB_FAIL(decode_native_batch_integer(buf, length, pos, value))) {
    } else if (value < (one_based ? 1 : 0)) { ret = OB_DESERIALIZE_ERROR; }
    else { ret = output.push_back(value); }
  }
  return ret;
}
}

int ObBatchRescanParams::native_size(int64_t &output) const
{
  int ret = validate_shape();
  int64_t size = 0;
  auto add = [&](int64_t bytes) {
    if (OB_SUCC(ret) && (bytes < 0 || __builtin_add_overflow(size, bytes, &size))) { ret = OB_SIZE_OVERFLOW; }
  };
  if (!native_ready_) { ret = OB_NOT_INIT; }
  add(serialization::encoded_length(int64_t(2)));
  add(serialization::encoded_length(params_.count()));
  for (int64_t i = 0; OB_SUCC(ret) && i < params_.count(); ++i) {
    add(serialization::encoded_length(params_.at(i).count()));
    for (int64_t j = 0; OB_SUCC(ret) && j < params_.at(i).count(); ++j) {
      const auto &value = params_.at(i).at(j);
      int64_t bytes = 0;
      if (OB_FAIL(NativeFrameValueCodec::size(*value.type_, value.datum_, bytes))) {
      } else {
        add(bytes);
        add(NativeFrameValueCodec::metadata_size());
      }
    }
  }
  add(serialization::encoded_length(param_idxs_));
  add(serialization::encoded_length(param_expr_idxs_));
  if (OB_SUCC(ret)) { output = size; }
  return ret;
}

int ObBatchRescanParams::native_encode(char *buf, int64_t length, int64_t &pos) const
{
  int64_t bytes = 0;
  int ret = native_size(bytes);
  int64_t cursor = pos;
  if (OB_FAIL(ret)) {
  } else if (buf == nullptr || pos < 0 || pos > length) { ret = OB_INVALID_ARGUMENT; }
  else if (bytes > length - pos) { ret = OB_BUF_NOT_ENOUGH; }
  else if (OB_FAIL(serialization::encode(buf, length, cursor, int64_t(2)))) {
  } else if (OB_FAIL(serialization::encode(buf, length, cursor, params_.count()))) {
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < params_.count(); ++i) {
    if (OB_FAIL(serialization::encode(buf, length, cursor, params_.at(i).count()))) {
    }
    for (int64_t j = 0; OB_SUCC(ret) && j < params_.at(i).count(); ++j) {
      const auto &value = params_.at(i).at(j);
      const auto &attributes = metadata_rows_.at(i).at(j);
      if (OB_FAIL(NativeFrameValueCodec::encode(*value.type_, value.datum_, buf, length, cursor))) {
      } else { ret = NativeFrameValueCodec::encode_metadata(attributes, buf, length, cursor); }
    }
  }
  if (OB_SUCC(ret)) { ret = serialization::encode(buf, length, cursor, param_idxs_); }
  if (OB_SUCC(ret)) { ret = serialization::encode(buf, length, cursor, param_expr_idxs_); }
  if (OB_SUCC(ret)) { pos = cursor; }
  return ret;
}

int ObBatchRescanParams::native_decode(const char *buf, int64_t length, int64_t &pos)
{
  int ret = OB_SUCCESS;
  int64_t cursor = pos, version = 0, rows = 0;
  reset();
  if (buf == nullptr || pos < 0 || pos > length) { ret = OB_INVALID_ARGUMENT; }
  else if (OB_FAIL(decode_native_batch_integer(buf, length, cursor, version))) {
  } else if (version != 2) { ret = OB_DESERIALIZE_ERROR; }
  else if (OB_FAIL(decode_native_batch_integer(buf, length, cursor, rows))) {
  } else if (rows < 0 || rows > length - cursor) { ret = OB_DESERIALIZE_ERROR; }
  for (int64_t i = 0; OB_SUCC(ret) && i < rows; ++i) {
    int64_t columns = 0;
    Row row(4 * sizeof(NativeTypedDatum));
    MetadataRow attributes(4 * sizeof(DatumParameterMetadata));
    if (OB_FAIL(decode_native_batch_integer(buf, length, cursor, columns))) {
    } else if (columns < 0 || columns > length - cursor) { ret = OB_DESERIALIZE_ERROR; }
    else if (OB_FAIL(row.reserve(columns))) {
    } else { ret = attributes.reserve(columns); }
    for (int64_t j = 0; OB_SUCC(ret) && j < columns; ++j) {
      NativeTypedDatum value;
      DatumParameterMetadata metadata;
      if (OB_FAIL(NativeFrameValueCodec::decode(buf, length, cursor, allocator_, value.type_, value.datum_))) {
      } else if (OB_FAIL(NativeFrameValueCodec::decode_metadata(buf, length, cursor, metadata))) {
      } else if (OB_FAIL(row.push_back(value))) {
      } else { ret = attributes.push_back(metadata); }
    }
    if (OB_SUCC(ret) && OB_FAIL(params_.push_back(row))) {
    } else if (OB_SUCC(ret) && OB_FAIL(metadata_rows_.push_back(attributes))) { params_.pop_back(); }
  }
  if (OB_SUCC(ret)) { ret = decode_native_batch_indexes(buf, length, cursor, param_idxs_, false); }
  if (OB_SUCC(ret) && cursor < length) { ret = decode_native_batch_indexes(buf, length, cursor, param_expr_idxs_, true); }
  if (OB_SUCC(ret)) { ret = validate_shape(); }
  if (OB_SUCC(ret)) { pos = cursor; }
  else { reset(); }
  return ret;
}

} // end namespace sql
} // end namespace oceanbase
