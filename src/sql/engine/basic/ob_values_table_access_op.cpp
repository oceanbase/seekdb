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
namespace
{
// The former constants wire starts with a nonnegative count. A negative marker
// makes the new per-value declaration wire explicit without changing OpSpec.
constexpr int64_t NATIVE_CONSTANTS_MARKER = -2;

bool is_legacy_special_state(const Datum &value)
{
  if (value.is_null() || value.is_min() || value.is_max() || value.is_nop()) { return true; }
  if (!value.has_ext_value()) { return false; }
  const int64_t marker = value.get_ext();
  return marker == DATUM_MIN_EXT_VALUE || marker == DATUM_MAX_EXT_VALUE || marker == ObActionFlag::OP_NOP;
}

SqlTypeMeta legacy_constant_type_meta(const DatumValue &value)
{
  SqlTypeMeta meta = value.meta_;
  // MIN/MAX/NOP (and the checked extended NULL wrapper) contain no PL resource
  // declaration. Keep their captured value metadata, but prepare a state domain.
  if (meta.is_ext() && is_legacy_special_state(value.datum_)) { meta.set_extend_type(0); }
  return meta;
}
} // namespace

// Each constant carries its complete declaration beside the explicit legacy
// value wire. Types contain no plan/session addresses.
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
  OB_UNIS_ENCODE(NATIVE_CONSTANTS_MARKER);
  OB_UNIS_ENCODE(count);
  for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
    const DatumValue &value = datum_values_.at(i);
    if (datum_value_types_.count() != count || datum_value_types_.at(i) == nullptr) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(datum_value_types_.at(i)->serialize(buf, buf_len, pos))) {
    } else { ret = DatumParameterCodec::encode_native_value(value.datum_, value.meta_, buf, buf_len, pos); }
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
  int64_t marker_or_count = 0;
  bool native_wire = false;
  if (OB_SUCC(ret)) { ret = serialization::decode(buf, data_len, pos, marker_or_count); }
  if (OB_SUCC(ret)) {
    native_wire = marker_or_count == NATIVE_CONSTANTS_MARKER;
    if (native_wire) { ret = serialization::decode(buf, data_len, pos, count); }
    else { count = marker_or_count; }
  }
  if (OB_FAIL(ret)) {
  } else if (count < 0 || count > data_len - pos || datum_values_.get_allocator() == nullptr) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    datum_values_.reset();
    datum_value_types_.reset();
    if (OB_FAIL(datum_values_.prepare_allocate(count))) {
    } else if (OB_FAIL(datum_value_types_.prepare_allocate(count))) {
    }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
    DatumValue &value = datum_values_.at(i);
    SqlTypeMeta meta;
    const SqlType *type = nullptr;
    if (native_wire) { ret = SqlType::deserialize(buf, data_len, pos, *datum_values_.get_allocator(), type); }
    if (OB_SUCC(ret)) {
      ret = DatumParameterCodec::decode_native_value(buf, data_len, pos,
                                              *datum_values_.get_allocator(), value.datum_, meta);
    }
    if (OB_SUCC(ret)) {
      value.meta_ = meta;
      const SqlTypeMeta expected_meta = legacy_constant_type_meta(value);
      if (!native_wire) {
        // Old bare scalar values contain no Enum labels or PL/SQL-UDT schema
        // declaration. Do not invent those from a target column or subtype ID.
        ret = SqlType::from_legacy(expected_meta, ObAccuracy(), *datum_values_.get_allocator(), type);
      } else if (MEMCMP(&type->legacy_meta(), &meta, sizeof(meta)) != 0
          && !value.is_null()
          && (!is_legacy_special_state(value.datum_)
              || MEMCMP(&type->legacy_meta(), &expected_meta, sizeof(meta)) != 0)) {
        ret = OB_DESERIALIZE_ERROR;
      }
      if (OB_SUCC(ret)) {
        Datum checked;
        alignas(8) char value_scratch[64];
        ObDatum value_view;
        if (OB_FAIL(value.export_legacy(value_view, value_scratch, sizeof(value_scratch)))) {
        } else if (OB_FAIL(DatumCopyPlan(*type).import_legacy(value_view, checked))) {
        } else { datum_value_types_.at(i) = type; }
      }
    }
  }
  if (OB_FAIL(ret)) {
    datum_values_.reset();
    datum_value_types_.reset();
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
  OB_UNIS_ADD_LEN(NATIVE_CONSTANTS_MARKER);
  OB_UNIS_ADD_LEN(count);
  for (int64_t i = 0; i < count; ++i) {
    int64_t size = 0;
    const DatumValue &value = datum_values_.at(i);
    if (datum_value_types_.count() != count || datum_value_types_.at(i) == nullptr
        || DatumParameterCodec::native_value_size(value.datum_, value.meta_, size) != OB_SUCCESS) {
      return -1;
    }
    const int64_t type_size = datum_value_types_.at(i)->get_serialize_size();
    if (type_size < 0 || __builtin_add_overflow(len, size, &len)
        || __builtin_add_overflow(len, type_size, &len)) { return -1; }
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
                        MY_SPEC.end_param_idx_ >= plan_ctx->get_parameter_count())) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          const DatumParameter *group = plan_ctx->get_group_parameter_input(MY_SPEC.start_param_idx_);
          row_cnt_ = 1;
          if (group != nullptr) {
            const DatumParameterArray *array = group->get_array<DatumParameterArray>();
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
    if (OB_UNLIKELY(param_idx < 0 || param_idx >= plan_ctx->get_parameter_count())) {
      ret = OB_INVALID_ARGUMENT;
    } else if (plan_ctx->get_group_parameter_input(param_idx) != nullptr) {
      // If the parameter is is_ext_sql_array
      if (OB_ISNULL(array_obj = plan_ctx->get_group_parameter_input(param_idx)->get_array<DatumParameterArray>())) {
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
      const DatumParameterMetadata *attributes = plan_ctx->get_parameter_metadata(param_idx);
      if (attributes == nullptr) { ret = OB_ERR_UNEXPECTED; }
      else {
        src_obj_meta = attributes->type_meta_;
        update_src_meta(src_obj_meta, attributes->accuracy_, src_meta);
      }
    }
  }
  return ret;
}

int ObValuesTableAccessOp::calc_datum_from_expr(const int64_t col_idx,
                                                const int64_t row_idx,
                                                ObExpr *src_expr,
                                                ObExpr *dst_expr)
{
  UNUSED(col_idx);
  int ret = OB_SUCCESS;
  ObDatumMeta src_meta = src_expr->datum_meta_;
  SqlTypeMeta src_obj_meta = src_expr->obj_meta_;
  const SqlType *source_type = &src_expr->get_sql_type();
  Datum *datum = nullptr;
  bool need_adjust_decimal_int = false;
  if (src_expr == dst_expr) {
  } else if (T_QUESTIONMARK == src_expr->type_ &&
             OB_FAIL(get_real_src_obj_type(row_idx, *src_expr, src_meta, src_obj_meta))) {
  } else {
    if (T_QUESTIONMARK == src_expr->type_) {
      source_type = GET_PHY_PLAN_CTX(ctx_)->get_native_parameter_type(src_expr->extra_);
      if (source_type == nullptr) { ret = OB_ERR_UNEXPECTED; }
    }
    need_adjust_decimal_int = src_meta.type_ == ObDecimalIntType &&
        dst_expr->datum_meta_.type_ == ObDecimalIntType &&
        ObDatumCast::need_scale_decimalint(src_meta.scale_, src_meta.precision_,
            dst_expr->datum_meta_.scale_, dst_expr->datum_meta_.precision_);
  }
  if (OB_FAIL(ret)) {
  } else if (source_type->equals(dst_expr->get_sql_type()) && !need_adjust_decimal_int) {
    if (OB_FAIL(src_expr->eval(eval_ctx_, datum))) {
    } else {
      dst_expr->locate_datum_for_write(eval_ctx_) = *datum;
      dst_expr->set_evaluated_projected(eval_ctx_);
    }
  } else if (OB_FAIL(ObCharset::check_valid_implicit_convert(src_meta.cs_type_,
                                                            dst_expr->datum_meta_.cs_type_))) {
  } else {
    ObExpr real_src_expr = *src_expr;
    real_src_expr.datum_meta_ = src_meta;
    real_src_expr.obj_meta_ = src_obj_meta;
    real_src_expr.obj_datum_map_ = common::ObDatum::get_sql_value_datum_map_type(src_meta.type_);
    if (OB_FAIL(real_src_expr.bind_sql_type(*source_type, ctx_.get_allocator()))) {
    } else if (OB_FAIL(datum_caster_.to_type(dst_expr->datum_meta_, real_src_expr, cm_, datum))) {
    } else if (OB_FAIL(dst_expr->deep_copy_datum(eval_ctx_, *datum))) {
    } else { dst_expr->set_evaluated_projected(eval_ctx_); }
  }
  return ret;
}

namespace {
OB_NOINLINE int cast_values_parameter(const Datum &source, const SqlType &source_type,
                                      ObExpr &output, ObEvalCtx &ctx,
                                      ObDatumCaster &caster, ObCastMode mode)
{
  Datum &dst = output.locate_expr_datum(ctx);
  dst = source;
  ObExpr input;
  input.frame_idx_ = output.frame_idx_;
  input.datum_off_ = reinterpret_cast<char *>(&dst) - ctx.frames_[input.frame_idx_];
  input.eval_info_off_ = output.eval_info_off_;
  input.obj_meta_ = source_type.legacy_meta();
  input.datum_meta_.type_ = source_type.kind();
  input.datum_meta_.cs_type_ = source_type.legacy_meta().get_collation_type();
  input.datum_meta_.scale_ = source_type.accuracy().get_scale();
  input.datum_meta_.precision_ = source_type.accuracy().get_precision();
  input.obj_datum_map_ = common::ObDatum::get_sql_value_datum_map_type(source_type.kind());
  Datum *converted = nullptr;
  int ret = input.bind_sql_type(source_type, ctx.exec_ctx_.get_allocator());
  if (OB_SUCC(ret)) {
    input.set_evaluated_projected(ctx);
    ret = caster.to_type(output.datum_meta_, input, mode, converted);
  }
  if (OB_SUCC(ret)) { ret = output.deep_copy_datum(ctx, *converted); }
  return ret;
}

OB_NOINLINE int scale_values_decimal(const Datum &source, const SqlType &source_type,
                                      ObExpr &output, ObEvalCtx &ctx, ObCastMode mode)
{
  int ret = OB_SUCCESS;
  alignas(8) char scratch[64];
  common::ObDatum coefficient;
  ObDecimalIntBuilder converted;
  if (OB_FAIL(DatumCopyPlan(source_type).get_decimal(source, coefficient, scratch, sizeof(scratch)))) {
  } else if (OB_FAIL(ObDatumCast::common_scale_decimalint(coefficient.get_decimal_int(),
              coefficient.get_int_bytes(), source_type.accuracy().get_scale(),
              output.datum_meta_.scale_, output.datum_meta_.precision_, mode, converted,
              ctx.exec_ctx_.get_user_logging_ctx()))) {
  } else {
    const DatumCopyPlan plan = output.get_datum_copy_plan();
    char *buffer = output.get_sql_type().canonical_representation() == DatumRepresentation::Reference
        ? output.get_reference_res_mem(ctx, converted.get_int_bytes()) : nullptr;
    if (output.get_sql_type().canonical_representation() == DatumRepresentation::Reference && buffer == nullptr) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      ret = plan.set_decimal(converted.get_decimal_int(), converted.get_int_bytes(),
          output.locate_datum_for_write(ctx), buffer, buffer == nullptr ? 0 : converted.get_int_bytes());
    }
  }
  return ret;
}
} // namespace

int ObValuesTableAccessOp::calc_datum_from_param(const Datum &source,
                                                 const SqlType &source_type,
                                                 ObExpr *dst_expr)
{
  int ret = OB_SUCCESS;
  if (!source.is_null() && source_type.kind() == ObDecimalIntType &&
      dst_expr->datum_meta_.type_ == ObDecimalIntType) {
    ret = scale_values_decimal(source, source_type, *dst_expr, eval_ctx_, cm_);
  } else if (source_type.equals(dst_expr->get_sql_type()) || source.is_null()) {
    dst_expr->locate_expr_datum(eval_ctx_) = source;
  } else {
    ret = cast_values_parameter(source, source_type, *dst_expr, eval_ctx_, datum_caster_, cm_);
  }
  if (OB_SUCC(ret)) { dst_expr->set_evaluated_projected(eval_ctx_); }
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
          Datum value;
          const SqlType *type = plan_ctx->get_native_parameter_type(param_idx);
          if (type == nullptr) { ret = OB_ERR_UNEXPECTED; }
          else if (OB_FAIL(plan_ctx->read_native_parameter(param_idx, value))) {
          } else { ret = calc_datum_from_param(value, *type, col_expr); }
          break;
        }
        case ObValuesTableDef::ACCESS_VALUE: {
          int64_t idx = row_idx_ * col_num + col_idx;
          const DatumValue &param = MY_SPEC.datum_values_.at(idx);
          const SqlType *type = MY_SPEC.datum_value_types_.at(idx);
          Datum value;
          alignas(8) char value_scratch[64];
          ObDatum value_view;
          if (type == nullptr) { ret = OB_ERR_UNEXPECTED; }
          else if (OB_FAIL(param.export_legacy(value_view, value_scratch, sizeof(value_scratch)))) {
          } else if (OB_FAIL(DatumCopyPlan(*type).import_legacy(value_view, value))) {
          } else {
            if (value.is_present() && param.datum_.is_inline_extended()) {
              value = param.datum_; // Keep no Reference to value_scratch in the output frame.
            }
            ret = calc_datum_from_param(value, *type, col_expr);
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
        Datum *dst_datum = NULL;
        ret = MY_SPEC.output_.at(i)->eval(eval_ctx_, dst_datum);
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
