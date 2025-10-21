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
#include "sql/engine/expr/ob_expr_subquery_ref.h"
#include "sql/engine/subquery/ob_subplan_filter_op.h"
#include "sql/ob_spi.h"
#include "sql/engine/subquery/ob_subplan_filter_op.h"

namespace oceanbase
{
using namespace common;
namespace sql
{
OB_SERIALIZE_MEMBER(ObExprSubQueryRef::ExtraInfo,
                    is_cursor_,
                    scalar_result_type_,
                    row_desc_);

int ObExprSubQueryRef::Extra::assign(const Extra &other)
{
  int ret = OB_SUCCESS;
  op_id_ = other.op_id_;
  iter_idx_ = other.iter_idx_;
  is_scalar_ = other.is_scalar_;
  return ret;
}

void ObExprSubQueryRef::ExtraInfo::reset()
{
  is_cursor_ = 0;
  row_desc_.reset();
  scalar_result_type_.reset();
}

int ObExprSubQueryRef::ExtraInfo::assign(const ExtraInfo &other)
{
  int ret = OB_SUCCESS;
  is_cursor_ = other.is_cursor_;
  OZ (scalar_result_type_.assign(other.scalar_result_type_));
  OZ (row_desc_.assign(other.row_desc_));
  return ret;
}

int ObExprSubQueryRef::ExtraInfo::init_cursor_info(ObIAllocator *allocator,
                                                   const ObQueryRefRawExpr &expr,
                                                   const ObExprOperatorType type,
                                                   ObExpr &rt_expr)
{
  int ret = OB_SUCCESS;
  ExtraInfo *cursor_info = NULL;
  void *buf = NULL;
  if (OB_ISNULL(allocator)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("allocator is null", K(ret), K(allocator));
  } else if (OB_ISNULL(buf = allocator->alloc(sizeof(ExtraInfo)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to alloc memory", K(ret));
  } else {
    cursor_info = new(buf) ExtraInfo(*allocator, type);
    bool result_is_scalar = expr.is_scalar();
    if (result_is_scalar) {
      cursor_info->scalar_result_type_ = expr.get_result_type();
    }
    Extra::get_info(rt_expr).is_scalar_ = result_is_scalar;
    Extra::get_info(rt_expr).iter_idx_ = expr.get_ref_id() - 1;
    cursor_info->is_cursor_ = expr.is_cursor();
    if (OB_FAIL(cursor_info->row_desc_.reserve(expr.get_column_types().count()))) {
      LOG_WARN("fail to init row_desc_", K(ret));
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < expr.get_column_types().count(); ++i) {
      ObDataType type;
      type.set_meta_type(expr.get_column_types().at(i));
      type.set_accuracy(expr.get_column_types().at(i).get_accuracy());
      OZ (cursor_info->row_desc_.push_back(type));
    }
    if (OB_SUCC(ret)) {
      rt_expr.extra_info_ = cursor_info;
      LOG_DEBUG("succ init_cursor_info", KPC(cursor_info));
    }
  }
  return ret;
}

int ObExprSubQueryRef::ExtraInfo::deep_copy(common::ObIAllocator &allocator,
                                            const ObExprOperatorType type,
                                            ObIExprExtraInfo *&copied_info) const
{
  int ret = OB_SUCCESS;
  ExtraInfo *copied_cursor_info = NULL;
  if (OB_FAIL(ObExprExtraInfoFactory::alloc(allocator, type, copied_info))) {
    LOG_WARN("failed to alloc expr extra info", K(ret));
  } else if (OB_ISNULL(copied_cursor_info = static_cast<ExtraInfo *>(copied_info))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected error", K(ret));
  } else if (OB_FAIL(copied_cursor_info->row_desc_.prepare_allocate(row_desc_.count()))) {
    LOG_WARN("failed to prepare allocate", K(ret));
  } else {
    copied_cursor_info->is_cursor_ = is_cursor_;
    for (int i = 0; OB_SUCC(ret) && i < row_desc_.count(); i++) {
      copied_cursor_info->row_desc_.at(i) = row_desc_.at(i);
    }
  }
  return ret;
}

OB_DEF_SERIALIZE(ObExprSubQueryRef)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObExprOperator::serialize(buf, buf_len, pos))) {
    LOG_WARN("fail to serialize ObExprOperator", K(buf_len), K(pos), K(ret));
  }
  bool is_scalar = static_cast<bool>(extra_.is_scalar_);
  int64_t iter_idx = static_cast<int64_t>(extra_.iter_idx_);
  LST_DO_CODE(OB_UNIS_ENCODE,
              is_scalar,
              extra_info_.scalar_result_type_,
              iter_idx,
              extra_info_.is_cursor_,
              extra_info_.row_desc_);
  return ret;
}

OB_DEF_DESERIALIZE(ObExprSubQueryRef)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObExprOperator::deserialize(buf, data_len, pos))) {
    LOG_WARN("fail to serialize ObExprOperator", K(data_len), K(pos), K(ret));
  }
  bool is_scalar = false;
  int64_t iter_idx = 0;
  LST_DO_CODE(OB_UNIS_DECODE,
              is_scalar,
              extra_info_.scalar_result_type_,
              iter_idx,
              extra_info_.is_cursor_,
              extra_info_.row_desc_);
  if (OB_SUCC(ret)) {
    extra_.is_scalar_ = static_cast<uint16_t>(is_scalar);
    extra_.iter_idx_ = static_cast<uint16_t>(iter_idx);
  }
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObExprSubQueryRef)
{
  int64_t len = 0;
  bool is_scalar = static_cast<bool>(extra_.is_scalar_);
  int64_t iter_idx = static_cast<int64_t>(extra_.iter_idx_);
  len += ObExprOperator::get_serialize_size();
  LST_DO_CODE(OB_UNIS_ADD_LEN,
              is_scalar,
              extra_info_.scalar_result_type_,
              iter_idx,
              extra_info_.is_cursor_,
              extra_info_.row_desc_);
  return len;
}


ObExprSubQueryRef::ObExprSubQueryRef(ObIAllocator &alloc)
  : ObExprOperator(alloc, T_REF_QUERY,
                   N_REF_QUERY,
                   0,
                   VALID_FOR_GENERATED_COL,
                   NOT_ROW_DIMENSION,
                   INTERNAL_IN_MYSQL_MODE,
                   INTERNAL_IN_ORACLE_MODE),
                   extra_(),
                   extra_info_(alloc, T_REF_QUERY)
{
}

ObExprSubQueryRef::~ObExprSubQueryRef()
{
}

int ObExprSubQueryRef::assign(const ObExprOperator &other)
{
  int ret = OB_SUCCESS;
  const ObExprSubQueryRef *tmp_other = static_cast<const ObExprSubQueryRef *>(&other);
  if (OB_UNLIKELY(NULL == tmp_other)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument. wrong type for other", K(ret), K(other));
  } else if (this != tmp_other) {
    if (OB_FAIL(ObExprOperator::assign(other))) {
      LOG_WARN("copy in Base class ObExprOperator failed", K(ret));
    } else {
      OZ (extra_.assign(tmp_other->extra_));
      OZ (extra_info_.assign(tmp_other->extra_info_));
    }
  }
  return ret;
}

void ObExprSubQueryRef::reset()
{
  extra_.reset();
  extra_info_.reset();
}

int ObExprSubQueryRef::calc_result_type0(ObExprResType &type, ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);
  if (extra_info_.is_cursor_) {
    type.set_ext();
    type.set_extend_type(pl::PL_REF_CURSOR_TYPE);
  } else if (extra_.is_scalar_) {
    // The result of the subquery is a scalar, then the return type is the actual return type of the scalar
    type = extra_info_.scalar_result_type_;
  } else {
    // The result of subquery is a vector or collection, then the return type is the data type of its iterator index
    type.set_int();
    type.set_scale(ObAccuracy::DDL_DEFAULT_ACCURACY[ObIntType].scale_);
    type.set_precision(ObAccuracy::DDL_DEFAULT_ACCURACY[ObIntType].precision_);
  }
  return OB_SUCCESS;
}

void ObExprSubQueryRef::set_scalar_result_type(const ObExprResType &result_type)
{
  extra_info_.scalar_result_type_ = result_type;
}

int ObExprSubQueryRef::cg_expr(
    ObExprCGCtx &op_cg_ctx, const ObRawExpr &raw_expr, ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  const ObQueryRefRawExpr *expr = static_cast<const ObQueryRefRawExpr *>(&raw_expr);
  CK (OB_NOT_NULL(expr));
  OZ (ExtraInfo::init_cursor_info(op_cg_ctx.allocator_, *expr, type_, rt_expr));
  OX (rt_expr.eval_func_ = &expr_eval);
  return ret;
}


int ObExprSubQueryRef::expr_eval(
    const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum)
{
  int ret = OB_SUCCESS;
  const Extra &extra = Extra::get_info(expr);
  const ExtraInfo *extra_info = static_cast<ExtraInfo *>(expr.extra_info_);
  ObDatum *datum = NULL;
  ObSubQueryIterator *iter = NULL;
  // Reset all iters
  if (OB_ISNULL(extra_info)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("extra info is null", K(ret));
  } else if (OB_FAIL(get_subquery_iter(ctx, extra, iter))) {
    LOG_WARN("get sub query iterator failed", K(ret), K(extra));
  } else if (OB_ISNULL(iter)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("null iter returned", K(ret));
  } else if (OB_FAIL(iter->rewind())) {
    LOG_WARN("filter to rewind subquery iterator", K(ret));
  }
  if (OB_FAIL(ret)) {
  } else if (extra.is_scalar_) {
    if (1 != iter->get_output().count()) {
      //not a scalar obj
      ret = OB_ERR_INVALID_COLUMN_NUM;
      LOG_USER_ERROR(OB_ERR_INVALID_COLUMN_NUM, 1L);
    } else {
      bool iter_end = false;
      bool found_in_hash_map = false;
      bool is_hash_enabled = iter->has_hashmap();
      if (is_hash_enabled) {
        ObDatum out;
        if (OB_FAIL(iter->get_curr_probe_row())) {
          LOG_WARN("failed to get probe row", K(ret));
        } else if (OB_FAIL(iter->get_refactored(out))) {
          if (OB_HASH_NOT_EXIST != ret) {
            LOG_WARN("failed to find in hash map", K(ret));
          } else {
            ret = OB_SUCCESS;
          }
        } else {
          found_in_hash_map = true;
          if (OB_FAIL(expr.deep_copy_datum(ctx, out))) {
            LOG_WARN("failed to deep copy datum", K(ret));
          }
        }
      }
      if (OB_FAIL(ret) || found_in_hash_map) {
      } else if (OB_FAIL(iter->get_next_row())) {
        if (OB_LIKELY(OB_ITER_END == ret)) {
          ret = OB_SUCCESS;
          iter_end = true;
          expr_datum.set_null();
        } else {
          LOG_WARN("get next row from subquery failed", K(ret));
        }
      } else if (OB_FAIL(iter->get_output().at(0)->eval(iter->get_eval_ctx(), datum))) {
        LOG_WARN("expr evaluate failed", K(ret));
        // deep copy datum since the following iter->get_next_row() make datum invalid.
      } else if (OB_FAIL(expr.deep_copy_datum(ctx, *datum))) {
        LOG_WARN("deep copy datum failed", K(ret), K(datum));
      }
      if (OB_SUCC(ret) && is_hash_enabled
                       && iter->probe_row_.cnt_ > 0
                       && !found_in_hash_map
                       && !iter_end) {
        //now we can insert curr row and curr result into hashmap
        //first to get arena allocator from sp_iter to deep copy row
        ObDatum value;
        DatumRow row_key;
        row_key.cnt_ = iter->probe_row_.cnt_;
                //first check memory is enough
        int64_t need_size =  datum->len_ + sizeof(ObDatum);
        for (int64_t i = 0; i < iter->probe_row_.cnt_; ++i) {
          need_size += (iter->probe_row_.elems_[i].len_ + sizeof(ObDatum));
        }
        bool can_insert = iter->check_can_insert(need_size);
        ObIAllocator *alloc = nullptr;
        if (!can_insert) {
          //memory is exceed, do not insert new rows
        } else if (OB_FAIL(iter->get_arena_allocator(alloc)) || OB_ISNULL(alloc)) {
          LOG_WARN("failed to get arena allocator", K(ret));
        } else if (OB_ISNULL(row_key.elems_
                  = static_cast<ObDatum *> (alloc->alloc(sizeof(ObDatum) * row_key.cnt_)))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("failed to alloc memory for row key", K(ret),  K(row_key.cnt_));
        } else if (OB_FAIL(value.deep_copy(*datum, *alloc))) {
          LOG_WARN("deep copy datum failed", K(ret));
        } else {
          for (int64_t i = 0; OB_SUCC(ret) && i < row_key.cnt_; ++i) {
            if (OB_FAIL(row_key.elems_[i].deep_copy(iter->probe_row_.elems_[i], *alloc))) {
              LOG_WARN("failed to copy probe row", K(ret));
            }
          }
          if (OB_SUCC(ret) && OB_FAIL(iter->set_refactored(row_key, value, need_size))) {
            LOG_WARN("failed to insert into hashmap", K(ret));
          }
        }
      }
      if (OB_SUCC(ret) && !found_in_hash_map && !iter_end) {
        if (OB_UNLIKELY(OB_SUCCESS == (ret = iter->get_next_row()))) {
          ret = OB_SUBQUERY_TOO_MANY_ROW;
          LOG_WARN("subquery too many rows", K(ret));
        } else if (OB_ITER_END == ret) {
          ret = OB_SUCCESS;
        } else {
          LOG_WARN("get next row from subquery failed", K(ret));
        }
      }
    }
  } else {
    // result is row or set, return extra info directly
    expr_datum.set_int(static_cast<int64_t>(expr.extra_));
  }
  return ret;
}

int ObExprSubQueryRef::get_subquery_iter(ObEvalCtx &ctx,
                                         const Extra &extra,
                                         ObSubQueryIterator *&iter)
{
  int ret = OB_SUCCESS;
  if (!extra.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid extra info", K(ret), K(extra));
  } else {
    ObOperatorKit *kit = ctx.exec_ctx_.get_operator_kit(extra.op_id_);
    if (OB_ISNULL(kit) || OB_ISNULL(kit->op_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("operator is NULL", K(ret), K(extra), KP(kit));
    } else if (PHY_SUBPLAN_FILTER != kit->op_->get_spec().type_ && PHY_VEC_SUBPLAN_FILTER != kit->op_->get_spec().type_) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("is not subplan filter operator", K(ret), K(extra),
               "spec", kit->op_->get_spec());
    } else {
      ObSubPlanFilterOp *op = static_cast<ObSubPlanFilterOp *>(kit->op_);
      if (extra.iter_idx_ >= op->get_subplan_iters().count()) {
        ret = OB_ARRAY_OUT_OF_RANGE;
        LOG_WARN("out of range", K(ret), K(extra),
                 "iter_cnt", op->get_subplan_iters().count());
      } else {
        iter = op->get_subplan_iters().at(extra.iter_idx_);
      }
    }
  }
  return ret;
}

int ObExprSubQueryRef::reset_onetime_expr(const ObExpr &expr, ObEvalCtx &ctx)
{
  int ret = OB_SUCCESS;
  const Extra &extra = Extra::get_info(expr);
  const ExtraInfo *extra_info = static_cast<ExtraInfo *>(expr.extra_info_);
  ObSubQueryIterator *iter = NULL;
  bool reset_for_onetime_expr = true;
  if (OB_ISNULL(extra_info)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("extra info is null", K(ret));
  } else if (OB_FAIL(get_subquery_iter(ctx, extra, iter))) {
    LOG_WARN("get sub query iterator failed", K(ret), K(extra));
  } else if (OB_ISNULL(iter)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("null iter returned", K(ret));
  } else if (!iter->is_onetime_plan()) {
    // do nothing
  } else if (OB_FAIL(iter->rewind(reset_for_onetime_expr))) {
    LOG_WARN("filter to rewind subquery iterator", K(ret));
  }
  return ret;
}

}  // namespace sql
}  // namespace oceanbase
