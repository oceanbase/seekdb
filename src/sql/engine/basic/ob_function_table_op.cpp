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

#include "sql/engine/basic/ob_function_table_op.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"
#include "sql/engine/expr/ob_expr_ai/ob_expr_ai_split_document.h"

namespace oceanbase
{
using namespace common;

namespace sql
{

OB_SERIALIZE_MEMBER(
    (ObFunctionTableSpec, ObOpSpec),
    value_expr_,
    column_exprs_,
    has_correlated_expr_);

int ObFunctionTableOp::inner_open()
{
  int ret = OB_SUCCESS;

  node_idx_ = 0;
  already_calc_ = false;
  reset_ai_split_document();

  if (OB_ISNULL(MY_SPEC.value_expr_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("value expr is not initialized", K(ret));
  } else if (T_FUN_SYS_AI_SPLIT_DOCUMENT
             == MY_SPEC.value_expr_->type_) {
    next_row_func_ =
        &ObFunctionTableOp::inner_get_next_row_ai_split_document;
  } else if (ObExtendType
             == MY_SPEC.value_expr_->datum_meta_.type_) {
    next_row_func_ =
        &ObFunctionTableOp::inner_get_next_row_udf;
  } else {
    next_row_func_ =
        &ObFunctionTableOp::inner_get_next_row_sys_func;
  }

  return ret;
}

int ObFunctionTableOp::inner_rescan()
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(ObOperator::inner_rescan())) {
    LOG_WARN("failed to inner rescan", K(ret));
  } else {
    node_idx_ = 0;
    reset_ai_split_document();

    if (MY_SPEC.has_correlated_expr_) {
      row_count_ = 0;
      col_count_ = 0;
      value_table_ = NULL;
      already_calc_ = false;
    }
  }

  return ret;
}

int ObFunctionTableOp::inner_close()
{
  int ret = OB_SUCCESS;

  node_idx_ = 0;
  already_calc_ = false;
  row_count_ = 0;
  col_count_ = 0;
  value_table_ = NULL;
  reset_ai_split_document();

  return ret;
}

// ObFunctionTableOp has its own switch_iterator.
int ObFunctionTableOp::switch_iterator()
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(ObOperator::inner_switch_iterator())) {
    LOG_WARN("failed to switch iterator", K(ret));
  } else if (OB_ISNULL(ctx_.get_my_session())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to get session", K(ret));
  } else if (NULL
                 == ctx_.get_my_session()
                        ->get_pl_implicit_cursor()
             || !ctx_.get_my_session()
                      ->get_pl_implicit_cursor()
                      ->get_in_forall()) {
    ret = OB_ITER_END;
  } else {
    node_idx_ = 0;
    reset_ai_split_document();

    if (MY_SPEC.has_correlated_expr_) {
      row_count_ = 0;
      col_count_ = 0;
      value_table_ = NULL;
      already_calc_ = false;
    }
  }

  return ret;
}

void ObFunctionTableOp::destroy()
{
  reset_ai_split_document();
  ObOperator::destroy();
}

int ObFunctionTableOp::get_current_result(ObObj &result)
{
  int ret = OB_SUCCESS;
  void *data = NULL;

  CK(already_calc_);

  if (node_idx_ < 0
      || node_idx_ >= row_count_ * col_count_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN(
        "failed to get current result in table function",
        K(ret),
        K(node_idx_),
        K(row_count_),
        K(col_count_));
  }

  do {
    CK(node_idx_ >= 0);

    if (OB_SUCC(ret) && node_idx_ >= row_count_) {
      ret = OB_ITER_END;
    }

    CK(OB_NOT_NULL(value_table_));
    OX(data = value_table_->get_data());
    CK(OB_NOT_NULL(data));

    OX(result =
           (static_cast<ObObj *>(data))[node_idx_++]);
  } while (OB_SUCC(ret)
           && result.get_meta().get_type() == ObMaxType);

  return ret;
}

int ObFunctionTableOp::inner_get_next_row()
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(next_row_func_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("next row function is null", K(ret));
  } else {
    ret = (this->*next_row_func_)();
  }

  return ret;
}

int ObFunctionTableOp::inner_get_next_row_udf()
{
  int ret = OB_SUCCESS;
  ObPhysicalPlanCtx *plan_ctx = nullptr;

  clear_evaluated_flag();

  if (OB_ISNULL(plan_ctx =
                    ctx_.get_physical_plan_ctx())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN(
        "failed to get plan ctx",
        K(ret),
        K(plan_ctx));
  } else if (OB_ISNULL(MY_SPEC.value_expr_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("value expr is not initialized", K(ret));
  } else if (ObExtendType
             != MY_SPEC.value_expr_
                    ->datum_meta_.type_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN(
        "unexpected value type",
        K(ret),
        K(MY_SPEC.value_expr_
              ->datum_meta_.type_));
  } else if (FALSE_IT(
                 plan_ctx->set_autoinc_id_tmp(0))) {
  } else if (OB_FAIL(ctx_.check_status())) {
    LOG_WARN("failed to check status", K(ret));
  } else {
    ObDatum *value = nullptr;

    if (!already_calc_) {
      if (OB_FAIL(
              MY_SPEC.value_expr_->eval(
                  eval_ctx_,
                  value))) {
        LOG_WARN(
            "failed to eval value expr",
            K(ret));
      } else if (OB_ISNULL(value)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("value datum is null", K(ret));
      } else if (value->is_null()) {
        // A SQL NULL collection produces no rows.
      } else if (OB_ISNULL(
                     value_table_ =
                         reinterpret_cast<
                             pl::ObPLCollection *>(
                             value->get_ext()))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN(
            "failed to get value table",
            K(ret));
      } else {
        row_count_ =
            value_table_->is_inited()
                ? value_table_->get_count()
                : 0;
        col_count_ =
            value_table_->get_column_count();
      }

      already_calc_ = true;
    }

    if (OB_SUCC(ret)
        && OB_UNLIKELY(node_idx_ >= row_count_)) {
      ret = OB_ITER_END;
    }

    CK(MY_SPEC.column_exprs_.count()
       >= col_count_);

    ObObj obj_stack[col_count_];

    if (OB_SUCC(ret)) {
      if (nullptr != value_table_
          && ObExtendType
                 == value_table_
                        ->get_element_type()
                        .get_obj_type()) {
        pl::ObPLComposite *composite = NULL;
        pl::ObPLRecord *record = NULL;
        ObObj record_obj;

        OZ(get_current_result(record_obj));

        if (OB_FAIL(ret)) {
          // Preserve error.
        } else if (ObUserDefinedSQLType
                   == record_obj.get_type()) {
          obj_stack[0] = record_obj;
        } else if (record_obj.is_pl_extend()) {
          CK(OB_NOT_NULL(
              composite =
                  reinterpret_cast<
                      pl::ObPLComposite *>(
                      record_obj.get_ext())));

          if (OB_SUCC(ret)) {
            if (composite->is_record()) {
              OX(record =
                     static_cast<
                         pl::ObPLRecord *>(
                         composite));

              CK(record->get_count()
                 == col_count_);

              for (int64_t i = 0;
                   OB_SUCC(ret)
                   && i < col_count_;
                   ++i) {
                OZ(record->get_element(
                    i,
                    obj_stack[i]));
              }
            } else if (
                composite->is_collection()) {
              OX(obj_stack[0] = record_obj);
            } else {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN(
                  "unexpected composite type",
                  K(ret),
                  K(composite->get_type()));
            }
          }
        } else {
          ret = OB_ERR_UNEXPECTED;
          LOG_ERROR(
              "unexpected function table value",
              K(ret),
              K(record_obj),
              K(record_obj.meta_));
        }
      } else {
        for (int64_t i = 0;
             OB_SUCC(ret)
             && i < col_count_;
             ++i) {
          if (OB_FAIL(
                  get_current_result(
                      obj_stack[i]))) {
            LOG_WARN(
                "failed to get current result",
                K(ret),
                K(i));
          }
        }
      }
    }

    if (OB_SUCC(ret)) {
      for (int64_t i = 0;
           OB_SUCC(ret)
           && i < col_count_;
           ++i) {
        ObExpr *const &expr =
            MY_SPEC.column_exprs_.at(i);

        if (OB_ISNULL(expr)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN(
              "column expression is null",
              K(ret),
              K(i));
        } else if (obj_stack[i].is_null()) {
          expr->locate_datum_for_write(eval_ctx_)
              .set_null();
        } else {
          const ObObjDatumMapType &datum_map =
              expr->obj_datum_map_;
          ObDatum &datum =
              expr->locate_datum_for_write(
                  eval_ctx_);

          if (OB_FAIL(
                  datum.from_obj(
                      obj_stack[i],
                      datum_map))) {
            LOG_WARN(
                "failed to convert datum",
                K(ret));
          } else if (
              is_lob_storage(
                  obj_stack[i].get_type())
              && OB_FAIL(
                  ob_adjust_lob_datum(
                      obj_stack[i],
                      expr->obj_meta_,
                      datum_map,
                      get_exec_ctx()
                          .get_allocator(),
                      datum))) {
            LOG_WARN(
                "adjust lob datum failed",
                K(ret),
                K(obj_stack[i].get_meta()),
                K(expr->obj_meta_));
          }
        }

        if (OB_SUCC(ret)) {
          expr->set_evaluated_projected(
              eval_ctx_);
        }
      }
    }
  }

  return ret;
}

void ObFunctionTableOp::reset_ai_split_document()
{
  ai_split_inited_ = false;
  ai_split_chunk_idx_ = 0;
  ai_split_chunks_.reset();
}

int ObFunctionTableOp::init_ai_split_document()
{
  int ret = OB_SUCCESS;
  ObExpr *split_expr = MY_SPEC.value_expr_;
  ObDatum *content_datum = NULL;
  ObDatum *parameters_datum = NULL;
  ObString content;
  ObString parameters;

  if (ai_split_inited_) {
    // Already initialized.
  } else if (OB_ISNULL(split_expr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN(
        "ai split document expression is null",
        K(ret));
  } else if (
      T_FUN_SYS_AI_SPLIT_DOCUMENT
      != split_expr->type_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN(
        "unexpected ai split document expression type",
        K(ret),
        K(split_expr->type_));
  } else if (OB_UNLIKELY(
                 split_expr->arg_cnt_ < 1
                 || split_expr->arg_cnt_ > 2)) {
    ret = OB_ERR_PARAM_SIZE;
    LOG_WARN(
        "invalid ai split document argument count",
        K(ret),
        K(split_expr->arg_cnt_));
  } else if (OB_ISNULL(split_expr->args_)
             || OB_ISNULL(
                 split_expr->args_[0])) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN(
        "ai split document content expression is null",
        K(ret));
  } else if (OB_FAIL(
                 ctx_.check_status())) {
    LOG_WARN(
        "failed to check status",
        K(ret));
  } else if (OB_FAIL(
                 split_expr->args_[0]->eval(
                     eval_ctx_,
                     content_datum))) {
    LOG_WARN(
        "failed to evaluate document content",
        K(ret));
  } else if (OB_ISNULL(content_datum)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN(
        "document content datum is null",
        K(ret));
  } else if (content_datum->is_null()) {
    // SQL NULL content produces an empty table.
    ai_split_chunks_.reset();
    ai_split_chunk_idx_ = 0;
    ai_split_inited_ = true;
  } else {
    content = content_datum->get_string();

    if (2 == split_expr->arg_cnt_) {
      if (OB_ISNULL(split_expr->args_[1])) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN(
            "parameters expression is null",
            K(ret));
      } else if (OB_FAIL(
                     split_expr->args_[1]->eval(
                         eval_ctx_,
                         parameters_datum))) {
        LOG_WARN(
            "failed to evaluate split parameters",
            K(ret));
      } else if (OB_ISNULL(parameters_datum)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN(
            "parameters datum is null",
            K(ret));
      } else if (!parameters_datum->is_null()) {
        parameters =
            parameters_datum->get_string();
      }
    }

    if (OB_SUCC(ret)) {
      ai_split_chunks_.reset();
      ai_split_chunk_idx_ = 0;

      if (OB_FAIL(
              ObExprAISplitDocument::
                  split_document(
                      content,
                      parameters,
                      get_exec_ctx()
                          .get_allocator(),
                      ai_split_chunks_))) {
        LOG_WARN(
            "failed to split document",
            K(ret),
            "content_length",
            content.length(),
            "parameters_length",
            parameters.length());
      } else {
        ai_split_inited_ = true;
      }
    }
  }

  return ret;
}

int ObFunctionTableOp::
    inner_get_next_row_ai_split_document()
{
  int ret = OB_SUCCESS;

  clear_evaluated_flag();

  if (OB_FAIL(ctx_.check_status())) {
    LOG_WARN(
        "failed to check status",
        K(ret));
  } else if (!ai_split_inited_
             && OB_FAIL(
                 init_ai_split_document())) {
    LOG_WARN(
        "failed to initialize ai split document",
        K(ret));
  }

  if (OB_SUCC(ret)
      && ai_split_chunk_idx_
             >= ai_split_chunks_.count()) {
    ret = OB_ITER_END;
  }

  if (OB_SUCC(ret)) {
    if (OB_UNLIKELY(
            MY_SPEC.column_exprs_.count() < 4)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN(
          "invalid ai split document output column count",
          K(ret),
          K(MY_SPEC.column_exprs_.count()));
    } else {
      const ObAISplitDocumentChunk &chunk =
          ai_split_chunks_.at(
              ai_split_chunk_idx_);

      ObExpr *chunk_id_expr =
          MY_SPEC.column_exprs_.at(0);
      ObExpr *chunk_offset_expr =
          MY_SPEC.column_exprs_.at(1);
      ObExpr *chunk_length_expr =
          MY_SPEC.column_exprs_.at(2);
      ObExpr *chunk_text_expr =
          MY_SPEC.column_exprs_.at(3);

      if (OB_ISNULL(chunk_id_expr)
          || OB_ISNULL(chunk_offset_expr)
          || OB_ISNULL(chunk_length_expr)
          || OB_ISNULL(chunk_text_expr)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN(
            "ai split document output expression is null",
            K(ret),
            KP(chunk_id_expr),
            KP(chunk_offset_expr),
            KP(chunk_length_expr),
            KP(chunk_text_expr));
      } else {
        chunk_id_expr
            ->locate_datum_for_write(eval_ctx_)
            .set_int(chunk.chunk_id_);

        chunk_offset_expr
            ->locate_datum_for_write(eval_ctx_)
            .set_int(chunk.chunk_offset_);

        chunk_length_expr
            ->locate_datum_for_write(eval_ctx_)
            .set_int(chunk.chunk_length_);

        chunk_text_expr
            ->locate_datum_for_write(eval_ctx_)
            .set_string(chunk.chunk_text_);

        chunk_id_expr
            ->set_evaluated_projected(eval_ctx_);
        chunk_offset_expr
            ->set_evaluated_projected(eval_ctx_);
        chunk_length_expr
            ->set_evaluated_projected(eval_ctx_);
        chunk_text_expr
            ->set_evaluated_projected(eval_ctx_);

        ++ai_split_chunk_idx_;
      }
    }
  }

  return ret;
}

int ObFunctionTableOp::inner_get_next_row_sys_func()
{
  int ret = OB_SUCCESS;
  ObPhysicalPlanCtx *plan_ctx = nullptr;
  ObDatum *value = nullptr;

  clear_evaluated_flag();

  if (OB_ISNULL(
          plan_ctx =
              ctx_.get_physical_plan_ctx())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN(
        "failed to get plan ctx",
        K(ret),
        K(plan_ctx));
  } else if (OB_ISNULL(
                 MY_SPEC.value_expr_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN(
        "value expression is null",
        K(ret));
  } else if (OB_UNLIKELY(
                 MY_SPEC.column_exprs_
                     .empty())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN(
        "column expression is empty",
        K(ret));
  } else if (OB_FAIL(
                 ctx_.check_status())) {
    LOG_WARN(
        "failed to check status",
        K(ret));
  } else if (OB_FAIL(
                 MY_SPEC.value_expr_->eval(
                     eval_ctx_,
                     value))) {
    if (OB_ITER_END != ret) {
      LOG_WARN(
          "failed to evaluate value expression",
          K(ret));
    }
  } else if (OB_ISNULL(value)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN(
        "evaluated value is null",
        K(ret));
  } else if (OB_ISNULL(
                 MY_SPEC.column_exprs_
                     .at(0))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN(
        "output expression is null",
        K(ret));
  } else {
    MY_SPEC.column_exprs_
        .at(0)
        ->locate_datum_for_write(eval_ctx_)
        .set_datum(*value);

    MY_SPEC.column_exprs_
        .at(0)
        ->set_evaluated_projected(eval_ctx_);
  }

  return ret;
}

} // namespace sql
} // namespace oceanbase