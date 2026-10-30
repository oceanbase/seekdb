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

#include "sql/engine/basic/ob_values_table_access_op.h"
#include "sql/engine/ob_exec_context.h"
#include "common/datum/datum_parameter_codec.h"

namespace oceanbase
{
using namespace common;
namespace sql
{
// Keep the existing count + bare-value wire; parameter-only accuracy and
// flags were never serialized by the former constant-value array.
OB_DEF_SERIALIZE_SIMPLE(ObValuesTableAccessSpec)
{
  int ret = OB_SUCCESS;
  BASE_SER((ObValuesTableAccessSpec, ObOpSpec));
  OB_UNIS_ENCODE(access_type_);
  OB_UNIS_ENCODE(column_exprs_);
  OB_UNIS_ENCODE(value_exprs_);
  OB_UNIS_ENCODE(start_param_idx_);
  OB_UNIS_ENCODE(end_param_idx_);
  const int64_t count = datum_values_.count();
  OB_UNIS_ENCODE(count);
  for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
    const DatumValue &value = datum_values_.at(i);
    ret = DatumParameterCodec::encode_value(value.datum_, value.meta_, buf, buf_len, pos);
  }
  return ret;
}

OB_DEF_DESERIALIZE_SIMPLE(ObValuesTableAccessSpec)
{
  int ret = OB_SUCCESS;
  BASE_DESER((ObValuesTableAccessSpec, ObOpSpec));
  OB_UNIS_DECODE(access_type_);
  OB_UNIS_DECODE(column_exprs_);
  OB_UNIS_DECODE(value_exprs_);
  OB_UNIS_DECODE(start_param_idx_);
  OB_UNIS_DECODE(end_param_idx_);
  int64_t count = 0;
  OB_UNIS_DECODE(count);
  if (OB_FAIL(ret)) {
  } else if (count < 0 || count > data_len - pos || datum_values_.get_allocator() == nullptr) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    datum_values_.reset();
    if (OB_FAIL(datum_values_.prepare_allocate(count))) {
    }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
    DatumValue &value = datum_values_.at(i);
    SqlTypeMeta meta;
    if (OB_FAIL(DatumParameterCodec::decode_value(buf, data_len, pos,
                                                *datum_values_.get_allocator(), value.datum_, meta))) {
    } else {
      value.meta_ = meta;
    }
  }
  return ret;
}

OB_DEF_SERIALIZE_SIZE_SIMPLE(ObValuesTableAccessSpec)
{
  int64_t len = 0;
  BASE_ADD_LEN((ObValuesTableAccessSpec, ObOpSpec));
  OB_UNIS_ADD_LEN(access_type_);
  OB_UNIS_ADD_LEN(column_exprs_);
  OB_UNIS_ADD_LEN(value_exprs_);
  OB_UNIS_ADD_LEN(start_param_idx_);
  OB_UNIS_ADD_LEN(end_param_idx_);
  const int64_t count = datum_values_.count();
  OB_UNIS_ADD_LEN(count);
  for (int64_t i = 0; i < count; ++i) {
    int64_t size = 0;
    const DatumValue &value = datum_values_.at(i);
    if (DatumParameterCodec::value_size(value.datum_, value.meta_, size) == OB_SUCCESS) {
      len += size;
    }
  }
  return len;
}

ObValuesTableAccessOp::ObValuesTableAccessOp(ObExecContext &exec_ctx,
                                             const ObOpSpec &spec,
                                             ObOpInput *input)
  : ObOperator(exec_ctx, spec, input), datum_caster_(), cm_(CM_NONE),
    row_idx_(0), row_cnt_(0) {}

int ObValuesTableAccessOp::inner_open()
{
  int ret = OB_SUCCESS;
  row_idx_ = 0;
  const int32_t result_flag = 0;
  ObPhysicalPlanCtx *plan_ctx = GET_PHY_PLAN_CTX(ctx_);
  if (OB_ISNULL(plan_ctx) || OB_ISNULL(ctx_.get_sql_ctx())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_UNLIKELY(MY_SPEC.column_exprs_.empty())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(datum_caster_.init(eval_ctx_.exec_ctx_))) {
  } else if (OB_FAIL(ObSQLUtils::get_default_cast_mode(false, 0, GET_MY_SESSION(ctx_), cm_))) {
  } else {
    cm_ = cm_ | CM_COLUMN_CONVERT;
    if (OB_SUCC(ret)) {
      if (ObValuesTableDef::FOLD_ACCESS_EXPR == MY_SPEC.access_type_) {
        if (OB_UNLIKELY(MY_SPEC.start_param_idx_ < 0 ||
                        MY_SPEC.start_param_idx_ > MY_SPEC.end_param_idx_ ||
                        MY_SPEC.end_param_idx_ >= plan_ctx->get_datum_param_store().count())) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          const DatumParameter &first = plan_ctx->get_datum_param_store().at(MY_SPEC.start_param_idx_);
          row_cnt_ = 1;
          if (first.get_type() == ObExtendType) {
            const DatumParameterArray *array = first.get_array<DatumParameterArray>();
            if (array == nullptr || array->count_ < 0) {
              ret = OB_ERR_UNEXPECTED;
            } else {
              row_cnt_ = array->count_;
            }
          }
        }
      } else if (ObValuesTableDef::ACCESS_PARAM == MY_SPEC.access_type_) {
        int64_t column_cnt = MY_SPEC.column_exprs_.count();
        row_cnt_ = (MY_SPEC.end_param_idx_ - MY_SPEC.start_param_idx_ + 1) / column_cnt;
      } else if (ObValuesTableDef::ACCESS_VALUE == MY_SPEC.access_type_) {
        int64_t column_cnt = MY_SPEC.column_exprs_.count();
        row_cnt_ = MY_SPEC.datum_values_.count() / column_cnt;
      } else {
        int64_t column_cnt = MY_SPEC.column_exprs_.count();
        row_cnt_ = MY_SPEC.value_exprs_.count() / column_cnt;
      }
    }
  }
  return ret;
}

int ObValuesTableAccessOp::inner_rescan()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObOperator::inner_rescan())) {
  } else {
    row_idx_ = 0;
  }
  return ret;
}

int ObValuesTableAccessOp::switch_iterator()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObOperator::inner_switch_iterator())) {
  } else {
    row_idx_ = 0;
  }
  return ret;
}

int ObValuesTableAccessOp::inner_get_next_row()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(try_check_status())) {
  } else {
    do {
      clear_evaluated_flag();
      if (OB_FAIL(calc_next_row())) {
        if(OB_ITER_END != ret) {
        }
      } else {
        LOG_DEBUG("output row", "row", ROWEXPR2STR(eval_ctx_, MY_SPEC.output_));
      }
      break;
    } while (OB_SUCC(ret));
  }
  return ret;
}

void ObValuesTableAccessOp::update_src_meta(const SqlTypeMeta &src_obj_meta,
                                            const ObAccuracy &src_obj_acc,
                                            ObDatumMeta &src_meta)
{
  src_meta.type_ = src_obj_meta.get_type();
  src_meta.cs_type_ = src_obj_meta.get_collation_type();
  src_meta.scale_ = src_obj_acc.get_scale();
  src_meta.precision_ = src_obj_acc.get_precision();
}

int ObValuesTableAccessOp::get_real_src_obj_type(const int64_t row_idx,
                                                 ObExpr &src_expr,
                                                 ObDatumMeta &src_meta,
                                                 SqlTypeMeta &src_obj_meta)
{
  int ret = OB_SUCCESS;
  if (T_QUESTIONMARK == src_expr.type_) {
    int64_t param_idx = src_expr.extra_;
    ObPhysicalPlanCtx *plan_ctx = GET_PHY_PLAN_CTX(ctx_);
    const DatumParameterArray *array_obj = NULL;
    if (OB_UNLIKELY(param_idx < 0 || param_idx >= plan_ctx->get_datum_param_store().count())) {
      ret = OB_INVALID_ARGUMENT;
    } else if (plan_ctx->get_datum_param_store().at(param_idx).is_ext_sql_array()) {
      // If the parameter is is_ext_sql_array
      if (OB_ISNULL(array_obj = plan_ctx->get_datum_param_store().at(param_idx).get_array<DatumParameterArray>())) {
        ret = OB_ERR_UNEXPECTED;
      } else if (array_obj->count_ <= row_idx) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        src_obj_meta = array_obj->data_[row_idx].get_type_meta();
        const ObAccuracy &src_obj_acc = array_obj->data_[row_idx].get_accuracy();
        update_src_meta(src_obj_meta, src_obj_acc, src_meta);
      }
    } else if (src_expr.frame_idx_ < spec_.plan_->get_expr_frame_info().const_frame_.count() +
                                     spec_.plan_->get_expr_frame_info().param_frame_.count()) {
      src_obj_meta = plan_ctx->get_datum_param_store().at(param_idx).get_type_meta();
      const ObAccuracy &src_obj_acc = plan_ctx->get_datum_param_store().at(param_idx).get_accuracy();
      update_src_meta(src_obj_meta, src_obj_acc, src_meta);
    }
  }
  return ret;
}

int ObValuesTableAccessOp::calc_datum_from_expr(const int64_t col_idx,
                                                const int64_t row_idx,
                                                ObExpr *src_expr,
                                                ObExpr *dst_expr)
{
  int ret = OB_SUCCESS;
  ObDatumMeta src_meta = src_expr->datum_meta_;
  SqlTypeMeta src_obj_meta = src_expr->obj_meta_;
  ObDatum *datum = NULL;
  bool need_adjust_decimal_int = false;
  if (src_expr == dst_expr) {
    /* actually should not happened */
  } else if (T_QUESTIONMARK == src_expr->type_ &&
             OB_FAIL(get_real_src_obj_type(row_idx, *src_expr, src_meta, src_obj_meta))) {
    LOG_WARN("failed to get real obj type");
  } else {
    need_adjust_decimal_int = src_meta.type_ == ObDecimalIntType &&
                              dst_expr->datum_meta_.type_ == ObDecimalIntType &&
                              ObDatumCast::need_scale_decimalint(src_meta.scale_,
                                                                 src_meta.precision_,
                                                                 dst_expr->datum_meta_.scale_,
                                                                 dst_expr->datum_meta_.precision_);
  }
  if (OB_FAIL(ret)) {
  } else if (src_meta.type_ == dst_expr->datum_meta_.type_ &&
             src_meta.cs_type_ == dst_expr->datum_meta_.cs_type_ &&
             !need_adjust_decimal_int) {
    // when type and collation_type is same. src has the same type to dst;
    if (OB_FAIL(src_expr->eval(eval_ctx_, datum))) {
    } else {
      dst_expr->locate_datum_for_write(eval_ctx_) = *datum;
      dst_expr->set_evaluated_projected(eval_ctx_);
    }
  } else {
    if (OB_FAIL(ObCharset::check_valid_implicit_convert(src_meta.cs_type_,
                                                        dst_expr->datum_meta_.cs_type_))) {
    } else {
      ObExpr real_src_expr = *src_expr;
      real_src_expr.datum_meta_ = src_meta;
      real_src_expr.obj_meta_ = src_obj_meta;
      real_src_expr.obj_datum_map_ = ObDatum::get_sql_value_datum_map_type(src_meta.type_);
      if (OB_FAIL(datum_caster_.to_type(dst_expr->datum_meta_, real_src_expr, cm_, datum))) {
      } else {
        ObDatum &dst_datum = dst_expr->locate_datum_for_write(eval_ctx_);
        if (SqlValueDatumMapType::OBJ_DATUM_STRING == dst_expr->obj_datum_map_) {
          ObExprStrResAlloc res_alloc(*dst_expr, eval_ctx_);
          if (OB_FAIL(dst_datum.deep_copy(*datum, res_alloc))) {
          }
        } else {
          ObDataBuffer res_alloc(const_cast<char*>(dst_datum.ptr_), dst_expr->res_buf_len_);
          if (OB_FAIL(dst_datum.deep_copy(*datum, res_alloc))) {
          }
        }
        dst_expr->set_evaluated_projected(eval_ctx_);
      }
    }
  }
  return ret;
}

namespace {
// Keep the expression adapter used by DECIMAL -> NUMBER off the ordinary
// borrowed-value path. DECIMAL width/scale changes use the numeric kernel
// directly below and never construct an input expression.
OB_NOINLINE int cast_values_decimal_to_number(const ObDatum &source, const SqlTypeMeta &meta,
                                               ObExpr &output, ObEvalCtx &ctx,
                                               ObDatumCaster &caster, ObCastMode mode)
{
  ObDatum &dst = output.locate_expr_datum(ctx);
  dst = source;
  ObExpr input;
  input.frame_idx_ = output.frame_idx_;
  input.datum_off_ = reinterpret_cast<char *>(&dst) - ctx.frames_[input.frame_idx_];
  input.eval_info_off_ = output.eval_info_off_;
  input.obj_meta_ = meta;
  input.datum_meta_.type_ = meta.get_type();
  input.datum_meta_.cs_type_ = meta.get_collation_type();
  input.datum_meta_.scale_ = meta.get_scale();
  input.datum_meta_.precision_ = wide::ObDecimalIntConstValue::get_max_precision_by_int_bytes(
      source.get_int_bytes());
  input.obj_datum_map_ = ObDatum::get_sql_value_datum_map_type(meta.get_type());
  ObDatum *converted = nullptr;
  int ret = caster.to_type(output.datum_meta_, input, mode, converted);
  if (OB_SUCC(ret)) {
    ObDatum &owned = output.locate_datum_for_write(ctx);
    ObDataBuffer allocator(const_cast<char *>(owned.ptr_), output.res_buf_len_);
    ret = owned.deep_copy(*converted, allocator);
  }
  return ret;
}

OB_NOINLINE int scale_values_decimal(const ObDatum &source, const SqlTypeMeta &meta,
                                      ObExpr &output, ObEvalCtx &ctx, ObCastMode mode)
{
  int ret = OB_SUCCESS;
  ObDecimalIntBuilder converted;
  if (OB_FAIL(ObDatumCast::common_scale_decimalint(source.get_decimal_int(), source.get_int_bytes(),
              meta.get_scale(), output.datum_meta_.scale_, output.datum_meta_.precision_,
              mode, converted, ctx.exec_ctx_.get_user_logging_ctx()))) {
  } else {
    ObDatum view;
    view.set_decimal_int_shallow(converted.get_decimal_int(), converted.get_int_bytes());
    ObDatum &owned = output.locate_datum_for_write(ctx);
    ObDataBuffer allocator(const_cast<char *>(owned.ptr_), output.res_buf_len_);
    ret = owned.deep_copy(view, allocator);
  }
  return ret;
}
} // namespace

int ObValuesTableAccessOp::calc_datum_from_param(const ObDatum &source, const SqlTypeMeta &meta, ObExpr *dst_expr)
{
  int ret = OB_SUCCESS;
  const SqlValueType dst_type = dst_expr->datum_meta_.type_;
  const bool decimal = !source.is_null() && meta.is_decimal_int();
  if (decimal && dst_type == ObDecimalIntType &&
      ObDatumCast::need_scale_decimalint(meta.get_scale(), source.get_int_bytes(),
          dst_expr->datum_meta_.scale_,
          wide::ObDecimalIntConstValue::get_int_bytes_by_precision(dst_expr->datum_meta_.precision_))) {
    ret = scale_values_decimal(source, meta, *dst_expr, eval_ctx_, cm_);
  } else if (decimal && ob_is_number_tc(dst_type)) {
    ret = cast_values_decimal_to_number(source, meta, *dst_expr, eval_ctx_, datum_caster_, cm_);
  } else {
    // A borrowed native view does not need the reserved result-buffer pointer
    // reset that locate_datum_for_write() performs before scalar construction.
    dst_expr->locate_expr_datum(eval_ctx_) = source;
  }
  if (OB_SUCC(ret)) {
    dst_expr->set_evaluated_projected(eval_ctx_);
  }
  return ret;
}

OB_INLINE int ObValuesTableAccessOp::calc_next_row()
{
  int ret = OB_SUCCESS;
  int64_t col_num = MY_SPEC.column_exprs_.count();
  int64_t col_idx = 0;
  ObPhysicalPlanCtx *plan_ctx = GET_PHY_PLAN_CTX(ctx_);
  if (row_idx_ == row_cnt_) {
    ret = OB_ITER_END;
  } else if (ObValuesTableDef::FOLD_ACCESS_EXPR == MY_SPEC.access_type_ &&
             OB_FAIL(plan_ctx->replace_batch_param_datum(row_idx_,
                                          MY_SPEC.start_param_idx_,
                                          MY_SPEC.end_param_idx_ - MY_SPEC.start_param_idx_ + 1))) {
  } else {
    while (OB_SUCC(ret) && col_idx < col_num) {
      ObExpr *col_expr = MY_SPEC.column_exprs_.at(col_idx);
      switch (MY_SPEC.access_type_)
      {
        case ObValuesTableDef::ACCESS_EXPR : {
          int64_t idx = row_idx_ * col_num + col_idx;
          ObExpr *src_expr = MY_SPEC.value_exprs_.at(idx);
          if (OB_FAIL(calc_datum_from_expr(col_idx, row_idx_, src_expr, col_expr))) {
          }
          break;
        }
        case ObValuesTableDef::FOLD_ACCESS_EXPR : {
          ObExpr *src_expr = MY_SPEC.value_exprs_.at(col_idx);
          if (OB_FAIL(calc_datum_from_expr(col_idx, row_idx_, src_expr, col_expr))) {
          }
          break;
        }
        case ObValuesTableDef::ACCESS_PARAM: {
          int64_t param_idx = MY_SPEC.start_param_idx_ + col_idx + row_idx_ * col_num;
          const DatumParameter &param = plan_ctx->get_datum_param_store().at(param_idx);
          if (OB_FAIL(calc_datum_from_param(param.datum_, param.get_type_meta(), col_expr))) {
          }
          break;
        }
        case ObValuesTableDef::ACCESS_VALUE: {
          int64_t idx = row_idx_ * col_num + col_idx;
          const DatumValue &param = MY_SPEC.datum_values_.at(idx);
          if (OB_FAIL(calc_datum_from_param(param.datum_, param.meta_, col_expr))) {
          }
          break;
        }
      };
      col_idx++;
    } // while end

    for (int64_t i = 0; OB_SUCC(ret) && i < MY_SPEC.output_.count(); i++) {
      ObExpr *output = MY_SPEC.output_.at(i);
      if (i < col_num && output == MY_SPEC.column_exprs_.at(i)) {
        /* need do nothing */
      } else {
        ObDatum *dst_datum = NULL;
        MY_SPEC.output_.at(i)->eval(eval_ctx_, dst_datum);
      }
    }
    if (OB_SUCC(ret)) {
      ++row_idx_;
    }
  }
  return ret;
}

int ObValuesTableAccessOp::inner_close()
{
  int ret = OB_SUCCESS;
  row_idx_ = 0;
  if (OB_FAIL(datum_caster_.destroy())) {
  }
  return ret;
}
} // end namespace sql
} // end namespace oceanbase
